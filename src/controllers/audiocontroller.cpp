#include "audiocontroller.h"
#include "connectioncontroller.h"
#include "audio/audioengine.h"
#include "audio/opusdecoder.h"
#include "audio/opusencoder.h"
#include "models/radiostate.h"
#include "network/networkmetrics.h"
#include "network/protocol.h"
#include "settings/radiosettings.h"
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(qk4Audio, "qk4.audio")

AudioController::AudioController(ConnectionController *connController, RadioState *radioState, QObject *parent)
    : QObject(parent), m_connectionController(connController), m_radioState(radioState),
      m_audioEngine(new AudioEngine(nullptr)), m_opusDecoder(new OpusDecoder(nullptr)),
      m_opusEncoder(new OpusEncoder(this)) {
    // Initialize Opus decoder (K4 sends 12kHz stereo: left=Main, right=Sub)
    m_opusDecoder->initialize(12000, 2);

    // Initialize Opus encoder for TX audio (12kHz mono)
    m_opusEncoder->initialize(12000, 1);

    // Load saved audio device settings BEFORE moveToThread (only stores strings/floats,
    // no Qt audio objects exist yet, so direct calls are safe)
    QString savedMicDevice = RadioSettings::instance()->micDevice();
    if (!savedMicDevice.isEmpty()) {
        m_audioEngine->setMicDevice(savedMicDevice);
    }
    QString savedSpeakerDevice = RadioSettings::instance()->speakerDevice();
    if (!savedSpeakerDevice.isEmpty()) {
        m_audioEngine->setOutputDevice(savedSpeakerDevice);
    }
    m_audioEngine->setMicGain(RadioSettings::instance()->micGain() / 100.0f);

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

    // TX audio path: mic frame → encode → send packet
    connect(m_audioEngine, &AudioEngine::microphoneFrame, this, &AudioController::onMicrophoneFrame);

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
}

AudioController::~AudioController() {
    disconnect(this);
    delete m_opusDecoder; // No parent, must delete manually

    if (m_audioThread) {
        QMetaObject::invokeMethod(m_audioEngine, "stop", Qt::BlockingQueuedConnection);
        m_audioThread->quit();
        m_audioThread->wait(2000);
    }
    delete m_audioEngine; // No parent, must delete manually
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
}

void AudioController::stopAudio() {
    if (m_audioEngine) {
        QMetaObject::invokeMethod(m_audioEngine, "stop", Qt::QueuedConnection);
    }
}

void AudioController::setPttActive(bool active) {
    if (!active || m_connectionController->isConnected()) {
        m_pttActive = active;
        if (active) {
            m_txSequence = 0;
        }
        QMetaObject::invokeMethod(m_audioEngine, "setMicEnabled", Qt::QueuedConnection, Q_ARG(bool, active));
        emit pttStateChanged(active);
    }
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

void AudioController::onMicrophoneFrame(const QByteArray &s16leData) {
    // Only transmit when PTT is active and connected
    if (!m_pttActive || !m_connectionController->isConnected()) {
        return;
    }

    QByteArray audioData;

    switch (m_connectionController->currentRadio().encodeMode) {
    case 0: // EM0 - RAW 32-bit float stereo
    {
        // Convert mono S16LE to stereo float32 (K4 expects stereo: L=Main, R=Sub)
        const qint16 *samples = reinterpret_cast<const qint16 *>(s16leData.constData());
        int sampleCount = s16leData.size() / sizeof(qint16);

        audioData.resize(sampleCount * 2 * sizeof(float)); // Stereo output
        float *output = reinterpret_cast<float *>(audioData.data());

        for (int i = 0; i < sampleCount; i++) {
            float normalized = static_cast<float>(samples[i]) / 32768.0f;
            output[i * 2] = normalized;     // Left channel
            output[i * 2 + 1] = normalized; // Right channel (duplicate)
        }
        break;
    }

    case 1: // EM1 - RAW 16-bit S16LE stereo
    {
        // Convert mono S16LE to stereo S16LE (K4 expects stereo: L=Main, R=Sub)
        const qint16 *samples = reinterpret_cast<const qint16 *>(s16leData.constData());
        int sampleCount = s16leData.size() / sizeof(qint16);

        audioData.resize(sampleCount * 2 * sizeof(qint16)); // Stereo output
        qint16 *output = reinterpret_cast<qint16 *>(audioData.data());

        for (int i = 0; i < sampleCount; i++) {
            output[i * 2] = samples[i];     // Left channel
            output[i * 2 + 1] = samples[i]; // Right channel (duplicate)
        }
        break;
    }

    case 2: // EM2 - Opus Int
    case 3: // EM3 - Opus Float
    default:
        // Use Opus encoding (encoder handles mono-to-stereo internally)
        audioData = m_opusEncoder->encode(s16leData);
        break;
    }

    if (audioData.isEmpty()) {
        return;
    }

    // Build and send the audio packet with the selected encode mode
    QByteArray packet =
        Protocol::buildAudioPacket(audioData, m_txSequence++, m_connectionController->currentRadio().encodeMode);
    m_connectionController->sendRawPacket(packet);
}
