#include "audiocontroller.h"
#include "connectioncontroller.h"
#include "audio/audioengine.h"
#include "audio/audiologging.h"
#include "audio/opusdecoder.h"
#include "models/radiostate.h"
#include "network/networkmetrics.h"
#include "network/protocol.h"
#include "network/tcpclient.h"
#include "settings/radiosettings.h"
#include "utils/radioutils.h"

Q_LOGGING_CATEGORY(qk4Audio, "qk4.audio")

AudioController::AudioController(ConnectionController *connController, RadioState *radioState, QObject *parent)
    : QObject(parent), m_connectionController(connController), m_radioState(radioState),
      m_audioEngine(new AudioEngine(nullptr)), m_opusDecoder(new OpusDecoder(nullptr)) {
    // Initialize Opus decoder (K4 sends 12kHz stereo: left=Main, right=Sub).
    // The TX-side OpusEncoder is owned by AudioEngine and lives on the audio
    // thread (PR 12); AudioController no longer touches it.
    m_opusDecoder->initialize(12000, 2);

    // Apply initial audio routing BEFORE moveToThread (no Qt audio objects exist yet,
    // so direct calls are safe). Mode is Unknown at startup, so this picks the primary
    // devices/gain. Subsequent changes go through applyAudioRouting() with queued invokes.
    {
        RadioSettings *rs = RadioSettings::instance();
        QString initialMic = rs->micDevice();
        QString initialSpeaker = rs->speakerDevice();
        int initialGain = rs->micGain();
        if (!initialMic.isEmpty()) {
            m_audioEngine->setMicDevice(initialMic);
        }
        if (!initialSpeaker.isEmpty()) {
            m_audioEngine->setOutputDevice(initialSpeaker);
        }
        m_audioEngine->setMicGain(initialGain / 100.0f);
        m_appliedMicDevice = initialMic;
        m_appliedSpeakerDevice = initialSpeaker;
        m_appliedMicGain = initialGain;
    }

    // Move AudioEngine to dedicated thread for glitch-free audio playback
    m_audioThread = new QThread(this);
    m_audioThread->setObjectName("AudioEngine");
    m_audioEngine->moveToThread(m_audioThread);
    m_audioThread->start();

    // RX audio path: Protocol → decode → enqueue (runs entirely on I/O thread)
    // m_opusDecoder is only called from this lambda → single-threaded on I/O thread
    // m_audioEngine->enqueueAudio() is mutex-protected → safe from any thread
    auto *protocol = m_connectionController->tcpClient()->protocol();
    connect(protocol, &Protocol::audioDataReady, protocol, [this](const QByteArray &payload) {
        QByteArray pcmData = m_opusDecoder->decodeK4Packet(payload);
        if (!pcmData.isEmpty()) {
            m_audioEngine->enqueueAudio(pcmData);
        }
    });

    // Audio buffer status → network health metrics
    connect(protocol, &Protocol::audioSequenceReceived, m_connectionController->networkMetrics(),
            &NetworkMetrics::onAudioSequence);
    connect(m_audioEngine, &AudioEngine::bufferStatus, m_connectionController->networkMetrics(),
            &NetworkMetrics::onBufferStatus);

    // TX audio path (PR 12): encode + packetize run on the audio thread; the
    // resulting wire-ready packet goes straight to TcpClient::sendRaw, which
    // auto-marshals from the audio thread to its own IO thread. AutoConnection
    // resolves to QueuedConnection across the audio→IO boundary (one hop).
    connect(m_audioEngine, &AudioEngine::txPacketReady, m_connectionController->tcpClient(), &TcpClient::sendRaw);

    // SL tier changes → update TX frame size
    connect(m_radioState, &RadioState::streamingLatencyChanged, this, &AudioController::onStreamingLatencyChanged);

    // SUB RX mute/unmute
    connect(m_radioState, &RadioState::subRxEnabledChanged, this, [this](bool enabled) {
        if (m_audioEngine) {
            m_audioEngine->setSubMuted(!enabled);
        }
    });

    // Balance and mix from RadioState
    connect(m_radioState, &RadioState::balanceChanged, this, [this](int mode, int offset) {
        if (m_audioEngine) {
            m_audioEngine->setBalanceMode(mode);
            m_audioEngine->setBalanceOffset(offset);
        }
    });
    connect(m_radioState, &RadioState::audioMixChanged, this, [this](int left, int right) {
        if (m_audioEngine) {
            m_audioEngine->setAudioMix(left, right);
        }
    });

    // Audio routing coordinator: re-evaluate device/gain selection whenever the TX VFO
    // mode changes (mode/split) or any audio-routing setting changes.
    connect(m_radioState, &RadioState::modeChanged, this, &AudioController::applyAudioRouting);
    connect(m_radioState, &RadioState::modeBChanged, this, &AudioController::applyAudioRouting);
    connect(m_radioState, &RadioState::splitChanged, this, &AudioController::applyAudioRouting);

    RadioSettings *rs = RadioSettings::instance();
    connect(rs, &RadioSettings::micDeviceChanged, this, &AudioController::applyAudioRouting);
    connect(rs, &RadioSettings::micGainChanged, this, &AudioController::applyAudioRouting);
    connect(rs, &RadioSettings::speakerDeviceChanged, this, &AudioController::applyAudioRouting);
    connect(rs, &RadioSettings::dataModeAudioEnabledChanged, this, &AudioController::applyAudioRouting);
    connect(rs, &RadioSettings::dataMicDeviceChanged, this, &AudioController::applyAudioRouting);
    connect(rs, &RadioSettings::dataMicGainChanged, this, &AudioController::applyAudioRouting);
    connect(rs, &RadioSettings::dataSpeakerDeviceChanged, this, &AudioController::applyAudioRouting);
}

bool AudioController::isTxVfoDataMode() const {
    if (!m_radioState)
        return false;
    RadioState::Mode m = m_radioState->splitEnabled() ? m_radioState->modeB() : m_radioState->mode();
    return (m == RadioState::DATA || m == RadioState::DATA_R);
}

void AudioController::applyAudioRouting() {
    if (!m_audioEngine)
        return;

    RadioSettings *rs = RadioSettings::instance();
    bool useDataDevices = rs->dataModeAudioEnabled() && isTxVfoDataMode();

    // Pick devices: data devices when in DATA mode AND configured, otherwise primary.
    QString primaryMic = rs->micDevice();
    QString primarySpeaker = rs->speakerDevice();
    QString chosenMic = primaryMic;
    QString chosenSpeaker = primarySpeaker;
    int chosenGain = rs->micGain();

    if (useDataDevices) {
        QString dataMic = rs->dataMicDevice();
        QString dataSpeaker = rs->dataSpeakerDevice();
        if (!dataMic.isEmpty())
            chosenMic = dataMic;
        if (!dataSpeaker.isEmpty())
            chosenSpeaker = dataSpeaker;
        chosenGain = rs->dataMicGain();
    }

    if (chosenMic != m_appliedMicDevice) {
        m_appliedMicDevice = chosenMic;
        QMetaObject::invokeMethod(m_audioEngine, "setMicDevice", Qt::QueuedConnection, Q_ARG(QString, chosenMic));
    }
    if (chosenSpeaker != m_appliedSpeakerDevice) {
        m_appliedSpeakerDevice = chosenSpeaker;
        QMetaObject::invokeMethod(m_audioEngine, "setOutputDevice", Qt::QueuedConnection,
                                  Q_ARG(QString, chosenSpeaker));
    }
    if (chosenGain != m_appliedMicGain) {
        m_appliedMicGain = chosenGain;
        // setMicGain is atomic — safe to call directly from any thread.
        m_audioEngine->setMicGain(chosenGain / 100.0f);
    }
}

AudioController::~AudioController() {
    disconnect(this);
    delete m_opusDecoder;

    if (m_audioThread) {
        QMetaObject::invokeMethod(m_audioEngine, "stop", Qt::BlockingQueuedConnection);
        m_audioThread->quit();
        m_audioThread->wait(2000);
    }
    delete m_audioEngine;
    m_audioEngine = nullptr;
}

void AudioController::startAudio(float mainVolume, float subVolume, float micGain) {
    // Must NOT use BlockingQueuedConnection — setupAudioInput() (now deferred to
    // first mic use) can trigger macOS permission dialogs that need the main thread
    // runloop, which would deadlock if the main thread were blocked here.
    QMetaObject::invokeMethod(m_audioEngine, "start", Qt::QueuedConnection);

    // Volume setters are atomic — safe as direct calls from any thread
    m_audioEngine->setMainVolume(mainVolume);
    m_audioEngine->setSubVolume(subVolume);
    m_audioEngine->setMicGain(micGain);

    // Set initial TX frame size from radio profile's SL tier
    const int sl = m_connectionController->currentRadio().streamingLatency;
    m_audioEngine->setFrameSamples(RadioUtils::slTierToFrameSamples(sl));

    // Set encode mode from radio profile (audio thread reads this atomic on
    // every captured frame to pick the wire format).
    m_audioEngine->setEncodeMode(m_connectionController->currentRadio().encodeMode);
}

void AudioController::stopAudio() {
    if (m_audioEngine) {
        QMetaObject::invokeMethod(m_audioEngine, "stop", Qt::QueuedConnection);
    }
}

void AudioController::shutdown() {
    // Synchronous audio teardown — required from MainWindow::closeEvent so
    // QAudioSink/QAudioSource are destroyed while the event loop is still
    // alive, not during libc atexit (which races PipeWire's RT worker on Linux).
    if (m_audioEngine && m_audioThread && m_audioThread->isRunning()) {
        QMetaObject::invokeMethod(m_audioEngine, "stop", Qt::BlockingQueuedConnection);
    }
}

void AudioController::setPttActive(bool active) {
    // Ignore PTT-on when disconnected; always honor PTT-off so a stuck-active
    // state can be cleared regardless of connection state.
    if (active && !m_connectionController->isConnected())
        return;

    // Forward to AudioEngine (audio thread). setPttActive there atomically
    // gates the encode pipeline, opens the mic on rising edge if needed, and
    // resets the txSequence counter + flushes the partial-frame tail.
    QMetaObject::invokeMethod(m_audioEngine, "setPttActive", Qt::QueuedConnection, Q_ARG(bool, active));
}

bool AudioController::isPttActive() const {
    // Lock-free atomic read from AudioEngine — safe from any thread.
    return m_audioEngine ? m_audioEngine->isPttActive() : false;
}

void AudioController::setMainVolume(float vol) {
    if (m_audioEngine)
        m_audioEngine->setMainVolume(vol);
}

void AudioController::setSubVolume(float vol) {
    if (m_audioEngine)
        m_audioEngine->setSubVolume(vol);
}

void AudioController::setBalanceMode(int mode) {
    if (m_audioEngine)
        m_audioEngine->setBalanceMode(mode);
}

void AudioController::setBalanceOffset(int offset) {
    if (m_audioEngine)
        m_audioEngine->setBalanceOffset(offset);
}

void AudioController::setAudioMix(int left, int right) {
    if (m_audioEngine)
        m_audioEngine->setAudioMix(left, right);
}

void AudioController::setSubMuted(bool muted) {
    if (m_audioEngine)
        m_audioEngine->setSubMuted(muted);
}

void AudioController::setMicDevice(const QString &deviceId) {
    if (m_audioEngine)
        QMetaObject::invokeMethod(m_audioEngine, "setMicDevice", Qt::QueuedConnection, Q_ARG(QString, deviceId));
}

void AudioController::setOutputDevice(const QString &deviceId) {
    if (m_audioEngine)
        QMetaObject::invokeMethod(m_audioEngine, "setOutputDevice", Qt::QueuedConnection, Q_ARG(QString, deviceId));
}

void AudioController::setMicGain(float gain) {
    if (m_audioEngine)
        QMetaObject::invokeMethod(m_audioEngine, "setMicGain", Qt::QueuedConnection, Q_ARG(float, gain));
}

void AudioController::onStreamingLatencyChanged(int tier) {
    m_audioEngine->setFrameSamples(RadioUtils::slTierToFrameSamples(tier));
}
