#include "audiocontroller.h"
#include "connectioncontroller.h"
#include "audio/audioengine.h"
#include "audio/txaudiopipeline.h"
#include "logging/logcategories.h"
#include "logging/logsink.h"
#include "audio/opusdecoder.h"
#include "models/radiostate.h"
#include "network/networkmetrics.h"
#include "network/protocol.h"
#include "network/tciserver.h"
#include "network/tcpclient.h"
#include "settings/radiosettings.h"
#include "utils/radioutils.h"
#include "utils/rtscheduling.h"
#include "utils/threadshutdown.h"

#include <QCoreApplication>
#include <QPermissions>
#include <chrono>

namespace {
// 20 Hz: often enough for the buffer sparkline.
constexpr int kMetricsSampleMs = 50;
// No RX packet for this many ticks (250 ms, two SL7 packets) and the stream counts as stopped.
constexpr int kStreamIdleTicks = 5;
// One RX playout line at most this often, and only when something happened.
constexpr int kRxLogEveryTicks = 2000 / kMetricsSampleMs;

// Shutdown deadlines, each covering the worker's stop() and the join.
//
// WHY 2.5 s for TX: stop() closes the microphone, and the TX thread may still be inside opening it
// - 200 ms to 1.5 s measured on PipeWire, CoreAudio and WASAPI, longest for a Bluetooth profile
// switch (see TxAudioPipeline). Past that the thread is wedged and is disowned.
constexpr std::chrono::milliseconds kTxStopTimeout{2500};
// WHY 2 s for RX: stop() destroys the QAudioSink. reset() before stop() means it does not drain,
// but a device going away mid-call can still hold CoreAudio or PipeWire for a while.
constexpr std::chrono::milliseconds kRxStopTimeout{2000};
// The I/O thread never blocks by design, so the decode-handler removal takes milliseconds.
constexpr std::chrono::milliseconds kDecodeDisconnectTimeout{1000};
} // namespace

AudioController::AudioController(ConnectionController *connController, RadioState *radioState, QObject *parent)
    : QObject(parent), m_connectionController(connController), m_radioState(radioState),
      m_audioEngine(new AudioEngine(nullptr)), m_txPipeline(new TxAudioPipeline(nullptr)) {

    // Load saved audio device settings BEFORE moveToThread (only stores strings/floats,
    // no Qt audio objects exist yet, so direct calls are safe)
    QString savedMicDevice = RadioSettings::instance()->micDevice();
    if (!savedMicDevice.isEmpty()) {
        m_txPipeline->setMicDevice(savedMicDevice);
    }
    QString savedSpeakerDevice = RadioSettings::instance()->speakerDevice();
    if (!savedSpeakerDevice.isEmpty()) {
        m_audioEngine->setOutputDevice(savedSpeakerDevice);
        m_txPipeline->setOutputDeviceHint(savedSpeakerDevice);
    }
    m_txPipeline->setMicGain(RadioSettings::instance()->micGain() / 100.0f);
    m_txPipeline->setTciTxGain(RadioSettings::instance()->tciTxGain() / 100.0f);
    // Only what the OS already knows; nothing is asked until the operator first keys the mic.
    updateMicAccess(false);

    // Move AudioEngine to dedicated thread for glitch-free audio playback
    m_audioThread = new QThread(this);
    m_audioThread->setObjectName("AudioEngine");
    Logging::markRealtimeOnStart(m_audioThread);
    RtScheduling::promoteOnStart(m_audioThread, RtScheduling::Threads::kAudioRx);
    m_audioEngine->moveToThread(m_audioThread);
    connect(m_audioThread, &QThread::finished, m_audioEngine, &QObject::deleteLater);
    m_audioThread->start();

    // Transmit audio on a thread of its own. WHY: opening a microphone is synchronous and can take
    // over a second; on the RX thread it stalled the speaker for that long. See TxAudioPipeline.
    m_txThread = new QThread(this);
    m_txThread->setObjectName("AudioTX");
    Logging::markRealtimeOnStart(m_txThread);
    RtScheduling::promoteOnStart(m_txThread, RtScheduling::Threads::kAudioTx);
    m_txPipeline->moveToThread(m_txThread);
    connect(m_txThread, &QThread::finished, m_txPipeline, &QObject::deleteLater);
    m_txThread->start();

    // RX audio path: Protocol → decode → enqueue, entirely on the I/O thread.
    //
    // WHY the decoder is captured by the handler rather than held here: it is used only on the I/O
    // thread, so its lifetime belongs to the handler that runs there. Qt keeps a slot object alive
    // for the duration of a call, so the decoder cannot be freed underneath a decode in progress;
    // it goes when the connection does. (K4 sends 12 kHz stereo: left = Main, right = Sub.)
    //
    // m_audioEngine->enqueueAudio() is the producer side of a single-producer ring: this lambda,
    // on the I/O thread, is its only caller.
    auto decoder = std::make_shared<OpusDecoder>();
    decoder->initialize(12000, 2);
    auto *protocol = m_connectionController->tcpClient()->protocol();
    m_rxDecodeContext = protocol;
    m_rxDecodeConnection =
        connect(protocol, &Protocol::audioDataReady, protocol, [this, decoder](const QByteArray &payload) {
            QByteArray pcmData = decoder->decodeK4Packet(payload);
            m_rxPacketsDecoded.fetch_add(1, std::memory_order_relaxed);
            if (!pcmData.isEmpty()) {
                m_audioEngine->enqueueAudio(pcmData);
                // Fan-out for TCI listeners. Deliberately after enqueueAudio so the speaker path is
                // never delayed by a consumer, and deliberately not downstream of it so a listener does
                // not inherit the jitter buffer's drop-oldest policy.
                //
                // Posts a cross-thread event only while TciController has the fan-out connected (between
                // its start and stop); with no receiver the emit is a no-op.
                emit rxAudioAvailable(pcmData);
            }
        });

    // Audio activity, buffer depth and playout counters → network health metrics, SAMPLED here on
    // the main thread at 20 Hz. WHY not signals: the buffer depth was emitted from the audio thread
    // on every feed tick and the packet sequence from the I/O thread on every packet - together
    // 150-300 queued events a second into the GUI event loop for a sparkline that redraws at 4 Hz.
    m_metricsTimer = new QTimer(this);
    m_metricsTimer->setInterval(kMetricsSampleMs);
    connect(m_metricsTimer, &QTimer::timeout, this, &AudioController::sampleAudioMetrics);
    // Started by startAudio() and stopped by stopAudio(): nothing to sample while disconnected,
    // and an idle QK4 should not wake 20 times a second for it.

    // TX audio path: encode + packetize run on the TX thread; the wire-ready packet goes straight
    // to TcpClient::sendRaw on the I/O thread, queued (one hop).
    connect(m_txPipeline, &TxAudioPipeline::txPacketReady, m_connectionController->tcpClient(), &TcpClient::sendRaw);

    // Straight through to whoever can put it on screen. Queued by default - emitted on the TX
    // thread and consumed by the UI.
    connect(m_txPipeline, &TxAudioPipeline::micUnavailable, this, &AudioController::micUnavailable);

    // Formatted here on the main thread, once per transmission, never on the TX thread.
    connect(m_txPipeline, &TxAudioPipeline::pttLatencyMeasured, this,
            [](qint64 firstPacketUs, qint64 micOpenUs, bool fromMicrophone) {
                if (firstPacketUs < 0) {
                    qCInfo(lcAudioTx, "PTT released before any TX audio was sent (%s; mic open took %.1f ms)",
                           fromMicrophone ? "microphone" : "TCI", micOpenUs / 1000.0);
                    return;
                }
                qCInfo(lcAudioTx, "PTT -> first TX audio packet %.1f ms (%s; mic open %.1f ms)", firstPacketUs / 1000.0,
                       fromMicrophone ? "microphone" : "TCI", micOpenUs / 1000.0);
            });

    // The radio's own TX state pauses RX audio as our PTT does: front-panel PTT, another CAT client,
    // TUNE. The engine tells its jitter buffer, which re-anchors after the pause instead of measuring it.
    connect(m_radioState, &RadioState::transmitStateChanged, this, [this](bool transmitting) {
        if (m_audioEngine)
            m_audioEngine->setRadioTransmitting(transmitting);
    });

    // SL tier changes → TX frame size and RX sink size
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
}

AudioController::~AudioController() {
    // WHY first, and ON the I/O thread: the decode handler runs there and uses this object and the
    // engine. On the normal quit path ConnectionController, and Protocol with it, is already gone,
    // the handler went with it, and this is a no-op. If the I/O link outlives this controller, the
    // disconnect runs on the I/O thread and is waited for: that thread runs one event at a time,
    // so once the call returns no decode is in flight and none can start. Safe from deadlock
    // because the I/O thread never waits on the main thread (only main -> I/O blocking calls
    // exist). With the thread not running, nothing can be in flight and it runs here.
    if (QObject *context = m_rxDecodeContext.data()) {
        const QMetaObject::Connection connection = m_rxDecodeConnection;
        if (!ThreadShutdown::callBlocking(
                context, [connection]() { QObject::disconnect(connection); },
                QDeadlineTimer(kDecodeDisconnectTimeout))) {
            qCWarning(lcAudio) << "I/O thread did not remove the RX decode handler in time; a decode still queued "
                                  "there may run after this controller is gone";
        }
    }

    // Each worker is destroyed on its own thread by the finished -> deleteLater wired at
    // construction, after its stop() has run there. A thread that misses its deadline is left
    // running with its worker rather than having either destroyed underneath it.
    stopThread(m_txThread, m_txPipeline, kTxStopTimeout, m_txStopMissed);
    m_txPipeline = nullptr;
    stopThread(m_audioThread, m_audioEngine, kRxStopTimeout, m_rxStopMissed);
    m_audioEngine = nullptr;
}

template <typename Worker>
void AudioController::stopThread(QThread *&thread, Worker *worker, std::chrono::milliseconds timeout, bool stopMissed) {
    if (!thread) {
        return;
    }
    if (stopMissed) {
        // WHY not again: the thread already failed to run stop() within its deadline, so waiting
        // a second full deadline only doubles the time a quit hangs. Its stop() is still queued
        // ahead of the quit, so a thread that comes free later still stops its device first.
        ThreadShutdown::retire(thread, QDeadlineTimer(0), lcAudio());
        return;
    }
    const QDeadlineTimer deadline(timeout);
    ThreadShutdown::callBlocking(worker, [worker]() { worker->stop(); }, deadline);
    ThreadShutdown::retire(thread, deadline, lcAudio());
}

void AudioController::startAudio(float mainVolume, float subVolume, float micGain) {
    // Queued, not blocking: nothing here needs to wait for the audio thread.
    // The RX sink is sized from the SL tier, so the tier goes ahead of start() in the same queue.
    const int sl = m_connectionController->currentRadio().streamingLatency;
    const int frameSamples = RadioUtils::slTierToFrameSamples(sl);
    QMetaObject::invokeMethod(m_audioEngine, &AudioEngine::setRxPacketFrameSamples, Qt::QueuedConnection, frameSamples);
    QMetaObject::invokeMethod(m_audioEngine, &AudioEngine::start, Qt::QueuedConnection);
    m_metricsTimer->start();

    // Volume setters are atomic — safe as direct calls from any thread
    m_audioEngine->setMainVolume(mainVolume);
    m_audioEngine->setSubVolume(subVolume);
    // Atomic stores in the TX pipeline - safe as direct calls.
    m_txPipeline->setMicGain(micGain);

    // Set initial TX frame size from radio profile's SL tier
    m_txPipeline->setFrameSamples(frameSamples);

    // Set encode mode from radio profile (the TX thread reads this atomic on every frame to pick
    // the wire format).
    m_txPipeline->setEncodeMode(m_connectionController->currentRadio().encodeMode);
}

void AudioController::stopAudio() {
    if (m_metricsTimer)
        m_metricsTimer->stop();
    if (m_txPipeline) {
        QMetaObject::invokeMethod(m_txPipeline, &TxAudioPipeline::stop, Qt::QueuedConnection);
    }
    if (m_audioEngine) {
        m_audioEngine->setTransmitting(false);
        m_audioEngine->setRadioTransmitting(false);
        QMetaObject::invokeMethod(m_audioEngine, &AudioEngine::stop, Qt::QueuedConnection);
    }
}

void AudioController::shutdown() {
    // Synchronous audio teardown — required from MainWindow::closeEvent so
    // QAudioSink/QAudioSource are destroyed while the event loop is still
    // alive, not during libc atexit (which races PipeWire's RT worker on Linux).
    //
    // Bounded: a TX thread still inside opening the microphone must not hold the window open.
    if (m_txPipeline) {
        TxAudioPipeline *pipeline = m_txPipeline;
        if (!ThreadShutdown::callBlocking(
                pipeline, [pipeline]() { pipeline->stop(); }, QDeadlineTimer(kTxStopTimeout))) {
            qCWarning(lcAudio) << "TX audio thread did not stop the microphone in time";
            m_txStopMissed = true;
        }
    }
    if (m_audioEngine) {
        AudioEngine *engine = m_audioEngine;
        if (!ThreadShutdown::callBlocking(engine, [engine]() { engine->stop(); }, QDeadlineTimer(kRxStopTimeout))) {
            qCWarning(lcAudio) << "RX audio thread did not close the speaker in time";
            m_rxStopMissed = true;
        }
    }
}

void AudioController::setPttActive(bool active) {
    // Ignore PTT-on when disconnected; always honor PTT-off so a stuck-active
    // state can be cleared regardless of connection state.
    if (active && !m_connectionController->isConnected())
        return;

    // Before the PTT is queued, so the pipeline has the answer when it decides to open the mic. A
    // TCI transmission never opens the microphone and so never asks for it.
    if (active && m_txPipeline->txSource() == TxAudioPipeline::TxSource::Microphone) {
        updateMicAccess(true);
    }

    // Forward to the TX pipeline (TX thread). setPttActive there gates the encode pipeline, opens
    // or closes the microphone, and resets the sequence counter and partial-frame tail. The press
    // time travels with it so the delay to the first packet is measured from here.
    const qint64 nowNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    QMetaObject::invokeMethod(m_txPipeline, &TxAudioPipeline::setPttActive, Qt::QueuedConnection, active, nowNs);
    // The K4 stops sending RX audio while transmitting; the RX side must not count that as underruns.
    m_audioEngine->setTransmitting(active);

    // Emitted AFTER the early return above, so a PTT-on that was refused for being disconnected
    // does not announce a transmit that is not happening.
    emit pttActiveChanged(active);
}

void AudioController::updateMicAccess(bool askIfUndetermined) {
    QCoreApplication *app = QCoreApplication::instance();
    if (!app || !m_txPipeline) {
        return;
    }
    // Granted on platforms where the microphone needs no authorization (QCoreApplication::
    // requestPermission), so this gates nothing outside macOS.
    const QMicrophonePermission microphone;
    switch (app->checkPermission(microphone)) {
    case Qt::PermissionStatus::Granted:
        m_txPipeline->setMicAccess(TxAudioPipeline::MicAccess::Granted);
        return;
    case Qt::PermissionStatus::Denied:
        m_txPipeline->setMicAccess(TxAudioPipeline::MicAccess::Denied);
        return;
    case Qt::PermissionStatus::Undetermined:
        break;
    }
    m_txPipeline->setMicAccess(TxAudioPipeline::MicAccess::Undetermined);
    if (!askIfUndetermined || m_micPermissionRequestPending || QThread::currentThread() != app->thread()) {
        return;
    }
    m_micPermissionRequestPending = true;
    // Context `this`: "If context is destroyed before the request completes, the functor will not
    // be called." The result is never Undetermined.
    app->requestPermission(microphone, this, [this](const QPermission &result) {
        m_micPermissionRequestPending = false;
        if (!m_txPipeline) {
            return;
        }
        const bool granted = result.status() == Qt::PermissionStatus::Granted;
        qCInfo(lcAudio) << "Microphone permission" << (granted ? "granted" : "denied");
        m_txPipeline->setMicAccess(granted ? TxAudioPipeline::MicAccess::Granted : TxAudioPipeline::MicAccess::Denied);
    });
}

void AudioController::setTxSource(TxSource source) {
    if (!m_txPipeline)
        return;
    // Direct, and deliberately so: the source must be in effect BEFORE the PTT that follows, and
    // setPttActive is queued. A queued pair would arrive in order, but a direct source write
    // followed by a queued PTT cannot be reordered at all.
    //
    // Safe from any thread because setTxSource on the pipeline writes only an atomic; it queues its
    // own buffer flush to the TX thread rather than touching TX-thread state here.
    //
    // Taking a new generation invalidates any setTxSourceAfterPtt() switch still queued, so an
    // older "back to Microphone" cannot land on top of this one.
    std::lock_guard<std::mutex> lock(m_txSourceOrder->mutex);
    ++m_txSourceOrder->generation;
    m_txPipeline->setTxSource(static_cast<TxAudioPipeline::TxSource>(source));
}

void AudioController::setTxSourceAfterPtt(TxSource source) {
    if (!m_txPipeline)
        return;
    // Queued, and that is the whole point of this method existing next to setTxSource().
    //
    // Qt delivers queued calls to one thread in posting order, so this lands after the
    // setPttActive() posted immediately before it. Writing the source directly on an unkey instead
    // opened a window - one audio block wide - in which the TX thread saw PTT still asserted
    // and the source already back to Microphone, and duly encoded the room and sent it while the
    // radio was still transmitting the TCI transmission.
    //
    // STALE SWITCHES ARE DROPPED. If a newer source write happens before this one is delivered - an
    // immediate re-key selecting Tci directly - this switch no longer describes the transmission
    // it was queued for, and applying it would put the microphone under the new one.
    quint64 generation;
    {
        std::lock_guard<std::mutex> lock(m_txSourceOrder->mutex);
        generation = ++m_txSourceOrder->generation;
    }
    QMetaObject::invokeMethod(
        m_txPipeline,
        [pipeline = m_txPipeline, order = m_txSourceOrder, generation, source]() {
            std::lock_guard<std::mutex> lock(order->mutex);
            if (order->generation != generation)
                return; // superseded by a later source write
            pipeline->setTxSource(static_cast<TxAudioPipeline::TxSource>(source));
        },
        Qt::QueuedConnection);
}

QMetaObject::Connection AudioController::routeTciTxAudio(TciServer *server) {
    if (!m_txPipeline || !server)
        return {};
    return connect(server, &TciServer::txAudioReceived, m_txPipeline, &TxAudioPipeline::feedTciTxAudio,
                   Qt::QueuedConnection);
}

bool AudioController::isPttActive() const {
    // Lock-free atomic read from the TX pipeline — safe from any thread.
    return m_txPipeline ? m_txPipeline->isPttActive() : false;
}

float AudioController::mainVolume() const {
    return m_audioEngine ? m_audioEngine->mainVolume() : 1.0f;
}

float AudioController::subVolume() const {
    return m_audioEngine ? m_audioEngine->subVolume() : 1.0f;
}

void AudioController::setMainVolume(float vol) {
    if (m_audioEngine)
        m_audioEngine->setMainVolume(vol);
}

void AudioController::setSubVolume(float vol) {
    if (m_audioEngine)
        m_audioEngine->setSubVolume(vol);
}

void AudioController::setRxBoostDb(int db) {
    if (m_audioEngine)
        m_audioEngine->setRxBoostDb(db);
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
    if (m_txPipeline)
        QMetaObject::invokeMethod(m_txPipeline, &TxAudioPipeline::setMicDevice, Qt::QueuedConnection, deviceId);
}

void AudioController::setOutputDevice(const QString &deviceId) {
    if (m_audioEngine)
        QMetaObject::invokeMethod(m_audioEngine, &AudioEngine::setOutputDevice, Qt::QueuedConnection, deviceId);
    if (m_txPipeline)
        QMetaObject::invokeMethod(m_txPipeline, &TxAudioPipeline::setOutputDeviceHint, Qt::QueuedConnection, deviceId);
}

void AudioController::setMicGain(float gain) {
    // Logged here, not on the TX thread. Recorded so a bench log shows slider moves between
    // transmissions, not only during them.
    qCDebug(lcAudioTx, "mic slider %.0f%% -> gain %.4f", static_cast<double>(gain) * 100.0,
            static_cast<double>(gain * gain * gain));
    if (m_txPipeline)
        QMetaObject::invokeMethod(m_txPipeline, &TxAudioPipeline::setMicGain, Qt::QueuedConnection, gain);
}

void AudioController::setTciTxGain(float gain) {
    qCDebug(lcAudioTx, "TCI TX slider %.0f%% -> gain %.4f", static_cast<double>(gain) * 100.0,
            static_cast<double>(gain * gain * gain));
    if (m_txPipeline)
        QMetaObject::invokeMethod(m_txPipeline, &TxAudioPipeline::setTciTxGain, Qt::QueuedConnection, gain);
}

void AudioController::onStreamingLatencyChanged(int tier) {
    const int frameSamples = RadioUtils::slTierToFrameSamples(tier);
    m_txPipeline->setFrameSamples(frameSamples);
    // The RX sink is resized too: the tier sets how much audio each received packet carries.
    QMetaObject::invokeMethod(m_audioEngine, &AudioEngine::setRxPacketFrameSamples, Qt::QueuedConnection, frameSamples);
}

void AudioController::sampleAudioMetrics() {
    if (!m_audioEngine)
        return;
    NetworkMetrics *metrics = m_connectionController->networkMetrics();
    const quint32 packets = m_rxPacketsDecoded.load(std::memory_order_relaxed);
    if (packets != m_sampledRxPackets) {
        m_sampledRxPackets = packets;
        m_ticksSinceRxPacket = 0;
        metrics->onAudioSequence();
    } else if (m_ticksSinceRxPacket < kStreamIdleTicks) {
        ++m_ticksSinceRxPacket;
    }
    // Depth only while audio is flowing: the engine's last value goes stale when the
    // stream stops, and an idle connection should not draw a flat line at it. "Flowing" spans a
    // few ticks so SL6-7's 120 ms packets still give a sample every tick.
    if (m_ticksSinceRxPacket < kStreamIdleTicks)
        metrics->onBufferStatus(m_audioEngine->rxBufferedBytes());
    // Level diagnostics measured on the audio threads, formatted here (audio/audiodiagnostics.h).
    AudioDiagnostics::RxLevelReport rxLevels;
    while (m_audioEngine->takeRxLevelReport(rxLevels))
        AudioDiagnostics::logRxLevels(rxLevels);
    AudioDiagnostics::RxPlayoutReport rxPlayout;
    while (m_audioEngine->takeRxPlayoutReport(rxPlayout))
        AudioDiagnostics::logRxPlayout(rxPlayout);
    AudioDiagnostics::TxFrameReport txFrame;
    while (m_txPipeline->takeTxFrameReport(txFrame))
        AudioDiagnostics::logTxFrame(txFrame);

    const quint32 underruns = m_audioEngine->rxUnderruns();
    const quint32 drops = m_audioEngine->rxDroppedPackets();
    metrics->onRxPlayoutCounters(underruns, drops);

    // Formatted here, on the main thread, never on the audio thread (CONVENTIONS.md Rule 16).
    if (++m_ticksSinceRxLog < kRxLogEveryTicks)
        return;
    m_ticksSinceRxLog = 0;
    if (underruns != m_loggedUnderruns || drops != m_loggedDrops) {
        qCInfo(lcAudioRx, "RX playout: %u underrun(s), %u packet(s) dropped to cut latency in the last %d s",
               underruns - m_loggedUnderruns, drops - m_loggedDrops, kRxLogEveryTicks * kMetricsSampleMs / 1000);
        m_loggedUnderruns = underruns;
        m_loggedDrops = drops;
    }
}
