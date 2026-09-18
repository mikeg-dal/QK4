#ifndef AUDIOCONTROLLER_H
#define AUDIOCONTROLLER_H

#include <QObject>
#include <QThread>

class AudioEngine;
class OpusDecoder;
class ConnectionController;
class RadioState;

/**
 * @brief Owns the audio thread + AudioEngine + Opus decoder. Task-level API over the RX/TX paths:
 *        startAudio/stopAudio, PTT toggle, atomic volume/mix/balance setters. Connects to
 *        RadioState.streamingLatencyChanged to resize TX Opus frames in step with the K4's SL tier.
 *
 * Threading:
 *   - AudioEngine is moved to `m_audioThread` (single moveToThread at construction).
 *   - OpusDecoder keeps main-thread affinity but is only called from the IO-thread lambda
 *     wired to Protocol::audioDataReady — effectively single-threaded on the IO thread.
 *   - The TX encode pipeline (OpusEncoder + packet framing) lives on AudioEngine and runs
 *     fully on the audio thread (PR 12). AudioController only forwards PTT toggles and SL
 *     tier changes; the audio thread emits txPacketReady straight to TcpClient::sendRaw.
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

    // Which source feeds the transmitter. Mirrors AudioEngine::TxSource so callers need not reach
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

    // One block of TCI transmit audio, 48 kHz mono Float32. Ignored unless the source is Tci.
    void feedTciTxAudio(const QByteArray &f32Mono48k);
    bool isPttActive() const;

    // Volume/mix controls (atomic — safe from any thread)
    void setMainVolume(float vol);
    void setSubVolume(float vol);

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

private slots:
    void onStreamingLatencyChanged(int tier);

private:
    ConnectionController *m_connectionController;
    RadioState *m_radioState;

    AudioEngine *m_audioEngine;
    QThread *m_audioThread = nullptr;
    OpusDecoder *m_opusDecoder;
};

#endif // AUDIOCONTROLLER_H
