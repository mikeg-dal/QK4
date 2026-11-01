#ifndef AUDIOCONTROLLER_H
#define AUDIOCONTROLLER_H

#include <QObject>
#include <QPointer>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>

class AudioEngine;
class TxAudioPipeline;
class TciServer;
class ConnectionController;
class RadioState;

/**
 * @brief Owns the audio thread + AudioEngine + Opus decoder. Task-level API over the RX/TX paths:
 *        startAudio/stopAudio, PTT toggle, atomic volume/mix/balance setters. Connects to
 *        RadioState.streamingLatencyChanged to resize TX Opus frames in step with the K4's SL tier.
 *
 * Threading:
 *   - AudioEngine is moved to `m_audioThread` (single moveToThread at construction).
 *   - OpusDecoder (a plain class) is owned by the RX decode handler connected to
 *     Protocol::audioDataReady, which runs on the I/O thread, and lives exactly as long as that
 *     connection. Nothing else can reach it.
 *   - The TX pipeline (microphone, TCI audio, OpusEncoder, packet framing) is TxAudioPipeline on
 *     `m_txThread` ("AudioTX"), so opening the microphone at PTT never stalls RX playback.
 *     AudioController forwards PTT toggles and SL tier changes; the TX thread emits
 *     txPacketReady straight to TcpClient::sendRaw.
 *   Public AudioController methods are safe to call from the main thread (they dispatch via
 *   QMetaObject::invokeMethod / atomics).
 */
class AudioController : public QObject {
    Q_OBJECT

public:
    AudioController(ConnectionController *connController, RadioState *radioState, QObject *parent = nullptr);
    ~AudioController();

    // Audio lifecycle
    void startAudio(float mainVolume, float subVolume, float micGain);
    void stopAudio();
    void shutdown();

    // PTT control — forwards to AudioEngine (runs on the audio thread).
    void setPttActive(bool active);

    // Which source feeds the transmitter. Mirrors TxAudioPipeline::TxSource so callers need not reach
    // through to the engine (CONVENTIONS.md rule 2).
    //
    // WHY a remote PTT must set this first: setPttActive() opens the microphone, so keying without
    // selecting the source would transmit the room.
    enum class TxSource { Microphone = 0, Tci = 1 };
    void setTxSource(TxSource source);

    // The source to hand the transmitter back to once the PTT change queued just before this one
    // has taken effect. Queued behind it, where setTxSource() lands immediately.
    //
    // WHY BOTH EXIST, and why a caller cannot just pick one: the two edges need opposite ordering.
    // Keying needs the source in place BEFORE the PTT, so setTxSource() is direct. Unkeying needs
    // it changed AFTER, or the microphone is reopened onto a transmitter the radio has not
    // released yet.
    void setTxSourceAfterPtt(TxSource source);

    // Route a TCI server's transmit audio straight to the TX thread, queued, without passing
    // through the main thread. Returns the connection; disconnecting it stops the audio.
    //
    // WHY a direct sender-to-pipeline connection rather than a hop through a controller: the server
    // lives on the TCI thread and the encoder on the TX thread, and a busy GUI (a resize, a
    // full-rate panadapter) used to delay transmit audio that had no business waiting on it. Qt
    // removes the connection itself if either end is destroyed, so no lifetime rule rides on it.
    QMetaObject::Connection routeTciTxAudio(TciServer *server);
    bool isPttActive() const;

    // Volume/mix controls (atomic — safe from any thread)
    void setMainVolume(float vol);
    void setSubVolume(float vol);
    void setRxBoostDb(int db); // 0 to +12 dB above the sliders; see RxOutputStage

    // Read-through to the engine's atomics. READS ONLY - these change nothing. Safe from any
    // thread because the underlying members are std::atomic<float>.
    //
    // WHY read the APPLIED GAIN rather than the slider position: in BAL mode the sub slider drives
    // the L/R balance offset and leaves sub volume alone (mainwindow.cpp), so the slider and the
    // actual gain disagree there. The gain is what the listener hears.
    float mainVolume() const;
    float subVolume() const;
    void setBalanceMode(int mode);
    void setBalanceOffset(int offset);
    void setAudioMix(int left, int right);
    void setSubMuted(bool muted);

    // Device selection + mic gain — used by Options dialog tabs. Each dispatches via
    // QMetaObject::invokeMethod to the audio thread internally. Task-level API only —
    // per CONVENTIONS.md Rule 2, callers do not get direct access to AudioEngine.
    void setMicDevice(const QString &deviceId);
    void setOutputDevice(const QString &deviceId);
    void setMicGain(float gain); // 0.0 to 1.0

    // Transmit level for audio from a TCI client. Separate from setMicGain because the two sources
    // sit in completely different parts of the same cubic curve — see TxAudioPipeline::feedTciTxAudio.
    void setTciTxGain(float gain); // 0.0 to 1.0

signals:
    // PTT changed by SOMEBODY - this says nothing about who. Every local unkey in QK4 (the Esc
    // shortcut, the PTT button, the HaliKey PTT line, the side panel, CatServer) funnels through
    // setPttActive, so one signal here reaches all of them; the TCI side needs to know because it
    // otherwise goes on believing a client still holds the transmitter.
    void pttActiveChanged(bool active);

    // Decoded K4 receive audio: 12 kHz stereo Float32, L = Main, R = Sub.
    //
    // WHY here and not downstream of AudioEngine::enqueueAudio: this is the raw per-receiver audio,
    // before the jitter buffer and before MX routing, volume and balance. A TCI listener must get
    // what the radio sent, not what the operator chose to hear - and must not inherit the speaker
    // path's policy of dropping the oldest audio to claw back latency.
    //
    // Emitted on the I/O thread. Consumers on another thread must connect with
    // Qt::QueuedConnection; nothing may block here, because this thread also carries the K4
    // control stream.
    void rxAudioAvailable(const QByteArray &pcm12kStereo);

    // Microphone trouble the OPERATOR has to see, relayed from AudioEngine on the audio thread.
    //
    // WHY IT HAS TO REACH THE SCREEN: a local PTT press keys the K4 by streaming audio, so a
    // microphone QK4 cannot use is a PTT button that does nothing whatever. Every cause already
    // writes a qCWarning and has for some time, and the field reports still arrive as "PTT does not
    // work" - because nobody runs QK4 from a terminal.
    void micUnavailable(const QString &reason);

private slots:
    void onStreamingLatencyChanged(int tier);
    // Main thread, on m_metricsTimer: reads the audio thread's counters and hands them to
    // NetworkMetrics. Sampling replaces events pushed from the audio thread.
    void sampleAudioMetrics();

private:
    // stop() on the worker's own thread, then the thread retired, within one deadline. With
    // `stopMissed` (shutdown() already waited out this worker's stop) neither is waited for again:
    // the stop is still queued there, and the thread is disowned at once if it is still busy.
    template <typename Worker>
    void stopThread(QThread *&thread, Worker *worker, std::chrono::milliseconds timeout, bool stopMissed);

    // Set by shutdown() when a worker's stop() missed its deadline, so quitting waits for a stuck
    // thread once, not again in the destructor.
    bool m_txStopMissed = false;
    bool m_rxStopMissed = false;

    // Hands the OS microphone permission to the TX pipeline. With `askIfUndetermined`, also asks
    // the OS - on the main thread, the only one Qt allows, and asynchronously, so the prompt never
    // blocks the TX thread. Asked on the first microphone PTT, not at startup (Qt's guidance:
    // "Request permissions in response to specific actions by the user").
    void updateMicAccess(bool askIfUndetermined);
    bool m_micPermissionRequestPending = false;

    ConnectionController *m_connectionController;
    RadioState *m_radioState;

    AudioEngine *m_audioEngine;
    QThread *m_audioThread = nullptr;
    TxAudioPipeline *m_txPipeline;
    QThread *m_txThread = nullptr;

    QTimer *m_metricsTimer = nullptr;
    // RX packets decoded, counted on the I/O thread; the sampler turns an increase into "audio is
    // arriving" for NetworkMetrics instead of an event per packet.
    std::atomic<quint32> m_rxPacketsDecoded{0};
    quint32 m_sampledRxPackets = 0;
    int m_ticksSinceRxPacket = 1000;
    // Counter values at the last RX playout log line, and ticks since it.
    quint32 m_loggedUnderruns = 0;
    quint32 m_loggedDrops = 0;
    int m_ticksSinceRxLog = 0;
    // The RX decode handler, which owns the decoder. Its context is Protocol, not this, so Qt
    // never drops it when this object is destroyed; the destructor has to, on the I/O thread.
    QMetaObject::Connection m_rxDecodeConnection;
    QPointer<QObject> m_rxDecodeContext;

    // Orders the two ways the TX source is written. Every write takes the next generation; a
    // setTxSourceAfterPtt() switch still in the queue applies only if no write has happened since
    // it was posted. Without this, unkey-then-rekey inside one audio-thread turn let the unkey's
    // queued "back to Microphone" land after the rekey had selected Tci - TCI holding the
    // transmitter, gate open, room microphone on the air.
    //
    // The mutex makes check-and-write atomic: the queued switch runs on the audio thread while a
    // direct write can come from the main thread at the same moment. Shared so a queued switch
    // that outlives this controller still has something valid to lock.
    struct TxSourceOrder {
        std::mutex mutex;
        quint64 generation = 0;
    };
    std::shared_ptr<TxSourceOrder> m_txSourceOrder = std::make_shared<TxSourceOrder>();
};

#endif // AUDIOCONTROLLER_H
