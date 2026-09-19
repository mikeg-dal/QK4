#ifndef AUDIOENGINE_H
#define AUDIOENGINE_H

#include <QObject>
#include <QAudioSink>
#include <QAudioSource>
#include <QAudioFormat>
#include <QIODevice>
#include <QMediaDevices>
#include <QTimer>
#include <QQueue>
#include <QMutex>
#include <atomic>

class OpusEncoder;

/**
 * @brief Qt audio I/O + Opus pipeline for both RX (speaker) and TX (microphone) paths.
 *
 * Lives on @c AudioController::m_audioThread (see `controllers/audiocontroller.cpp`).
 * RX:  decoded 12 kHz stereo Float32 PCM is enqueued from the IO thread via
 *      `enqueueAudio()`, consumed by `feedAudioDevice()` on a 10 ms timer, with MX
 *      routing + volume + balance applied per packet.
 * TX:  48 kHz mono Float32 captured on a 10 ms poll, resampled 4:1 to 12 kHz,
 *      framed to the SL-tier sample count, emitted via `microphoneFrame` for the
 *      Opus encoder.
 *
 * Thread-safety: all public setters mutate @c std::atomic members or take
 * @c m_queueMutex / @c m_mixMutex; no other cross-thread contract.
 */
class AudioEngine : public QObject {
    Q_OBJECT

public:
    enum MixSource { MixA = 0, MixB = 1, MixAB = 2, MixNegA = 3 };

    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine();

    Q_INVOKABLE bool start();
    Q_INVOKABLE void stop();
    void enqueueAudio(const QByteArray &pcmData);
    Q_INVOKABLE void flushQueue();

    // Mic device lifecycle is decoupled from per-PTT send-gating: the QAudioSource is opened
    // once on first PTT (preserving the macOS permission deferral noted in the constructor)
    // and stays open for the lifetime of the K4 connection, so subsequent PTT presses don't
    // pay the OS audio backend renegotiation cost (200 ms – 1.5 s on PipeWire/CoreAudio/WASAPI).
    // openMic() is idempotent. closeMic() is only called from teardown paths (stop(), device
    // swap) — never per-PTT. The TX send-gate is the m_pttActive check in bufferAndEmitTxFrames().
    //
    // Both sentences above were false until AUD-001 was fixed: stop() did not call closeMic(), and
    // the gate it named lived in an AudioController method that no longer exists. They are stated
    // here as a contract, so if stop() stops honouring it again the comment is the thing that is
    // wrong rather than the thing that is trusted.
    Q_INVOKABLE void openMic();
    Q_INVOKABLE void closeMic();
    Q_INVOKABLE void flushMicBuffer(); // Called on PTT-on edge so a partial-frame tail from
                                       // the previous transmission cannot leak into the new one.
    bool isMicOpen() const { return m_micEnabled.load(std::memory_order_relaxed); }

    // Channel volume controls (applied at playback time for instant response)
    void setMainVolume(float volume);
    void setSubVolume(float volume);
    float mainVolume() const { return m_mainVolume.load(std::memory_order_relaxed); }
    float subVolume() const { return m_subVolume.load(std::memory_order_relaxed); }

    // SUB RX mute control (when sub receiver is off, sub channel is silent)
    void setSubMuted(bool muted);

    // Audio mix routing (MX command - how main/sub maps to L/R when SUB is on)
    void setAudioMix(int left, int right); // MixSource values

    // Balance mode control (0=NOR, 1=BAL)
    void setBalanceMode(int mode);
    void setBalanceOffset(int offset); // -50 to +50

    // TX frame size (dynamic, matches SL tier)
    void setFrameSamples(int samples); // 240, 480, 720, or 1440
    int frameSamples() const { return m_frameSamples.load(std::memory_order_relaxed); }

    // TX encode mode (0=RAW S32LE (24-bit), 1=RAW S16LE, 2/3=Opus (same bitstream, int vs float decode)). See
    // audio/rawaudioformat.h for the RAW wire scales. Atomic so the audio-thread encode path reads it lock-free.
    void setEncodeMode(int mode);
    int encodeMode() const { return m_encodeMode.load(std::memory_order_relaxed); }

    // Where TX audio comes from.
    //
    // WHY this must exist alongside any remote PTT: setPttActive() opens the microphone, so a TCI
    // client keying the radio without a source selector would transmit whatever the room hears.
    // The two sources are mutually exclusive, never mixed.
    enum class TxSource { Microphone = 0, Tci = 1 };
    void setTxSource(TxSource source);
    TxSource txSource() const { return static_cast<TxSource>(m_txSource.load(std::memory_order_relaxed)); }

    // Feed 48 kHz mono Float32 from a TCI client. Ignored unless the TX source is Tci.
    //
    // Passes through m_micGain, the same control the sound-card input uses. WSJT-X transmits at or
    // near full scale, so without an operator-facing level this drives the K4 far too hard - which
    // it did on the first on-air test. setMicGain clamps to 0..1 with a cubic curve, so it can only
    // attenuate and can never introduce clipping of its own.
    Q_INVOKABLE void feedTciTxAudio(const QByteArray &f32Mono48k);

    // PTT gate for the TX encode path. Setting to true on PTT-on edge also
    // opens the mic (if needed) and flushes any partial-frame tail from the
    // previous transmission. The TX encode runs on the audio thread; reading
    // m_pttActive there is a lock-free atomic load.
    Q_INVOKABLE void setPttActive(bool active);
    bool isPttActive() const { return m_pttActive.load(std::memory_order_relaxed); }

    // Microphone settings
    Q_INVOKABLE void setMicGain(float gain); // 0.0 to 1.0
    float micGain() const { return m_micGain.load(std::memory_order_relaxed); }

    Q_INVOKABLE void setMicDevice(const QString &deviceId);
    QString micDeviceId() const;

    // Get list of available input devices (for settings UI)
    static QList<QPair<QString, QString>> availableInputDevices(); // (id, description)

    // Output device settings
    Q_INVOKABLE void setOutputDevice(const QString &deviceId);
    QString outputDeviceId() const;

    // Get list of available output devices (for settings UI)
    static QList<QPair<QString, QString>> availableOutputDevices(); // (id, description)

signals:
    // Encoded TX packet ready for the wire. Emitted on the audio thread;
    // TcpClient::sendRaw() auto-marshals to the I/O thread. PR 12 moved the
    // encode pipeline here from AudioController (main thread) so a busy GUI
    // event loop can no longer stall voice TX packet emission.
    void txPacketReady(const QByteArray &packet);
    void bufferStatus(int queueBytes, int maxBytes, bool prebuffering);

private slots:
    void onMicDataReady();
    void feedAudioDevice();
    // WHY: when the user leaves a device set to "System Default" (empty id), follow
    // the OS default live instead of caching it for the whole session. QMediaDevices
    // fires these when devices or the system default change; we rebuild the sink/
    // source only if the effective default actually moved. Pinned devices are ignored.
    void onSystemDefaultInputChanged();
    void onSystemDefaultOutputChanged();

private:
    bool setupAudioOutput();
    // Tear down and rebuild the output sink for the current device, and put the feed timer in
    // the matching state. Every path that changes the output device goes through this.
    void rebuildOutput();
    bool setupAudioInput();

    // Shared tail of both TX sources: take 12 kHz Float32, apply `gain`, buffer as S16, and emit
    // whole SL-tier frames while PTT is asserted. Audio thread only.
    void bufferAndEmitTxFrames(const QByteArray &pcm12k, float gain);

    // Resample 48kHz Float32 samples to 12kHz (4:1 decimation with averaging).
    // Reads from input48k, writes into the pre-allocated m_resampleBuf12k
    // member and returns a const reference to it. Avoids per-poll allocation.
    // Decimate a captured frame to the K4's 12 kHz, using whatever rate the device is actually
    // running at. See audio/audiodecimator.h for why the rate is not a constant any more.
    const QByteArray &resampleTo12k(const QByteArray &input);

    // The microphone's ACTUAL capture rate, taken from the device rather than demanded of it.
    // 0 until a device has been opened. See setupAudioInput().
    int m_micSampleRate = 0;
    int m_micDecimationFactor = 0;

    // Encode + packetize one captured S16LE mono frame and emit txPacketReady.
    // Runs on the audio thread, called from onMicDataReady when PTT is active.
    void encodeAndSendFrame(const QByteArray &f32MonoFrame, int frameSamples, int encodeMode);

    // Report what one TX frame actually put on the wire, under qk4.audio.tx.
    // See the WHY at the call site in encodeAndSendFrame.
    void logTxFrameDiagnostic(const QByteArray &f32MonoFrame, const QByteArray &wireData, int frameSamples,
                              int encodeMode) const;

    // Frames between qk4.audio.tx reports during a transmission. The first frame of
    // every transmission is always reported; this throttles the rest.
    static constexpr int TX_DIAG_FRAME_INTERVAL = 50;

    // Mic-silence watchdog. A microphone that opens successfully and then delivers nothing looks
    // exactly like a quiet room from everywhere downstream, so PTT lights the indicator, the gate
    // opens, and the operator transmits nothing while believing they are on the air. That is
    // INT-005, and it stayed undiagnosed because the read path treats an empty read as normal -
    // which it is, most of the time. These make the difference between "no data this poll" and
    // "no data at all since the operator keyed" observable. Audio thread only.
    // How long a keyed transmitter may produce no microphone data before we say so. Long enough
    // that ordinary buffer starvation never trips it, short enough that the operator learns within
    // one over rather than after the contact.
    static constexpr qint64 MIC_SILENCE_WARN_MS = 500;
    qint64 m_firstEmptyPollMs = 0;
    bool m_micSilenceReported = false;

    // Loudest mic sample since PTT engaged, tracked across EVERY frame while qk4.audio.tx is on.
    // WHY: reporting only the throttled frames' own peaks made short transmissions unreadable -
    // a brief over keys, logs frame 0 (still silence, before the operator speaks) and ends before
    // frame 50, so the log showed a near-zero peak next to a high ALC reading. The running peak
    // is what a level measurement actually needs. Audio-thread only, like m_txSequence.
    float m_txPeakSinceKey = 0.0f;

    // Apply MX routing + volume + balance to a raw [main, sub] interleaved packet
    void applyMixAndVolume(QByteArray &packet);

    // Audio output format: 12kHz stereo Float32 (K4 RX audio, L=Main R=Sub)
    QAudioFormat m_outputFormat;

    // True between start() and stop(): the engine is meant to be producing RX audio. Distinct from
    // "a sink exists", because the sink can fail to open and must still be retried on the next
    // device change. rebuildOutput() is the only thing that acts on it.
    bool m_outputRunning = false;

    // Audio input format: 48kHz mono Float32 (native macOS rate, resampled to 12kHz)
    QAudioFormat m_inputFormat;

    // Audio output (speaker)
    QAudioSink *m_audioSink;
    QIODevice *m_audioSinkDevice;

    // Audio input (microphone)
    QAudioSource *m_audioSource;
    QIODevice *m_audioSourceDevice;
    std::atomic<bool> m_micEnabled{false};
    QString m_selectedMicDeviceId;           // Empty = use system default
    QString m_selectedOutputDeviceId;        // Empty = use system default
    QString m_activeMicDeviceId;             // id of the device the source was actually opened on
    QString m_activeOutputDeviceId;          // id of the device the sink was actually opened on
    QMediaDevices *m_mediaDevices = nullptr; // OS device/default-change monitor

    // Channel volume controls (0.0 to 1.0)
    std::atomic<float> m_mainVolume{1.0f};
    std::atomic<float> m_subVolume{1.0f};

    // SUB RX mute state (true = sub muted, sub channel is silent)
    std::atomic<bool> m_subMuted{true}; // Starts muted (SUB RX is off at startup)

    // Audio mix routing (MX command) - default A.B (main left, sub right)
    MixSource m_mixLeft = MixA;
    MixSource m_mixRight = MixB;
    QMutex m_mixMutex; // Protects m_mixLeft and m_mixRight (always set together)

    // Balance mode (0=NOR: independent volume, 1=BAL: L/R balance)
    std::atomic<int> m_balanceMode{0};
    std::atomic<int> m_balanceOffset{0}; // -50 to +50

    // Microphone gain control
    std::atomic<float> m_micGain{0.25f}; // Default 25% (macOS mic input is typically hot)

    // Audio throughput: 12kHz × 2ch × sizeof(float) = 96,000 bytes/sec = 96 bytes/ms
    static constexpr int BYTES_PER_MS = 96;

    // Audio buffer sizes
    // QAudioSink buffer: 500ms — large enough for 4+ max-size packets (SL7 = 11,520 bytes = 120ms)
    // Ensures bytesFree() always exceeds one max packet, preventing partial writes and data loss
    static constexpr int OUTPUT_BUFFER_SIZE = 500 * BYTES_PER_MS; // 48,000 bytes
    // Input: 48kHz * 4 bytes/sample * 0.1 sec = 19200 bytes
    static constexpr int INPUT_BUFFER_SIZE = 19200;

    // Microphone gain uses cubic curve for fine control at low levels

    // TX encode pipeline state. Lives here (audio thread) instead of in
    // AudioController (main thread) so a busy GUI doesn't stall voice TX.
    OpusEncoder *m_opusEncoder = nullptr; // Owned, deleted in destructor
    quint8 m_txSequence = 0;              // Audio-thread-only — no atomic needed
    std::atomic<int> m_encodeMode{3};     // EM3 (Opus float) default
    std::atomic<bool> m_pttActive{false}; // TX gate; read on every mic frame
    // TxSource as an int so it is lock-free from the audio thread.
    std::atomic<int> m_txSource{static_cast<int>(TxSource::Microphone)};

    // Microphone frame buffering for Opus encoding
    // Buffer accumulates S16LE samples at 12kHz until we have a complete frame.
    // Frame size is dynamic, matching the SL tier (240/480/720/1440 samples).
    std::atomic<int> m_frameSamples{240}; // Default 20ms, updated on SL change
    QByteArray m_micBuffer;
    // Offset into m_micBuffer of the next byte to emit. Eliminates the
    // per-poll O(N) memmove from QByteArray::remove(0, n) — instead we just
    // bump this and compact only when the offset exceeds half the buffer's
    // capacity.
    int m_micReadOffset = 0;

    // Pre-allocated scratch for resampleTo12k. Sized to the full INPUT_BUFFER_SIZE because the
    // decimation factor depends on the capture device: a 48 kHz mic decimates 4:1, an AirPods Pro
    // at 24 kHz decimates 2:1, and a 12 kHz device would pass through unchanged.
    QByteArray m_resampleBuf12k;

    // Timer for polling microphone data (more reliable than readyRead signal)
    QTimer *m_micPollTimer;

    // Jitter buffer for RX audio playback
    QQueue<QByteArray> m_audioQueue;
    int m_queueBytes = 0; // Total decoded bytes in m_audioQueue (tracked for time-based thresholds)
    QMutex m_queueMutex;  // Protects m_audioQueue, m_queueBytes, m_prebuffering
    QTimer *m_feedTimer;
    bool m_prebuffering = true;

    // Write staging buffer: holds processed PCM that couldn't be written in one feed cycle
    // Audio-thread-only (no mutex needed) — safety net for partial QIODevice::write()
    QByteArray m_writeBuffer;

    // Reused per feedAudioDevice() cycle. Hoisted from a per-call stack QList
    // so the 100 Hz feed timer doesn't construct a fresh list each tick.
    QList<QByteArray> m_feedBatch;

    // Jitter buffer constants (adapt to any SL level automatically).
    // WHY PREBUFFER_PACKETS = 1: SL-tier packets already encode the jitter runway (SL7 carries
    // ~120 ms of audio per packet). Waiting for a second packet would double the startup latency
    // without improving tolerance — the runway is already inside the single packet. See
    // `memory/k4-streaming-latency.md` for the verified SL0–7 frame-bundling map.
    static constexpr int PREBUFFER_PACKETS = 1;
    static constexpr int MAX_QUEUE_BYTES = 1000 * BYTES_PER_MS; // 96,000 bytes (1s cap)
    static constexpr int FEED_INTERVAL_MS = 10;
};

#endif // AUDIOENGINE_H
