#ifndef AUDIOENGINE_H
#define AUDIOENGINE_H

#include <QObject>
#include <QAudioSink>
#include <QAudioSource>
#include <QAudioFormat>
#include <QIODevice>
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
    // swap) — never per-PTT. The TX send-gate lives in AudioController::onMicrophoneFrame.
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

    // TX encode mode (0=EM0 RAW32, 1=EM1 S16, 2=EM2 Opus int, 3=EM3 Opus float).
    // Atomic so the audio-thread encode path reads it lock-free.
    void setEncodeMode(int mode);
    int encodeMode() const { return m_encodeMode.load(std::memory_order_relaxed); }

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
    // Emitted when mic setup fails so the caller can surface a user-visible warning instead
    // of silently dropping the PTT press (e.g. Bluetooth device in HFP mode at 8/16kHz).
    void micSetupFailed(const QString &reason);

private slots:
    void onMicDataReady();
    void feedAudioDevice();

private:
    bool setupAudioOutput();
    bool setupAudioInput();

    // Resamplers: all take mono Float32 input, produce mono Float32 at 12kHz.
    // 48kHz path writes into pre-allocated m_resampleBuf12k — no heap allocation on the
    // hot 10 ms poll tick. 16kHz and 8kHz paths added for AirPods / Bluetooth HFP mic
    // support on macOS (see setupAudioInput() for the full HFP/A2DP explanation).
    const QByteArray &resample48kTo12k(const QByteArray &input48k); // 4:1 decimation with averaging
    QByteArray resample16kTo12k(const QByteArray &input16k);        // 4:3 linear interpolation
    QByteArray resample8kTo12k(const QByteArray &input8k);          // 2:3 linear interpolation

    // Encode + packetize one captured S16LE mono frame and emit txPacketReady.
    // Runs on the audio thread, called from onMicDataReady when PTT is active.
    void encodeAndSendFrame(const QByteArray &s16leMonoFrame, int frameSamples, int encodeMode);

    // Apply MX routing + volume + balance to a raw [main, sub] interleaved packet
    void applyMixAndVolume(QByteArray &packet);

    // Audio output format: 12kHz stereo Float32 (K4 RX audio, L=Main R=Sub)
    QAudioFormat m_outputFormat;

    // Requested input format (48kHz mono Float32). Bluetooth devices in HFP mode may not
    // support this; setupAudioInput() falls back to the device's preferredFormat() for
    // 8kHz/16kHz cases and records the accepted rate in m_actualInputSampleRate.
    QAudioFormat m_inputFormat;
    int m_actualInputSampleRate = 48000; // Set by setupAudioInput(); drives resampler dispatch

    // Audio output (speaker)
    QAudioSink *m_audioSink;
    QIODevice *m_audioSinkDevice;

    // Audio input (microphone)
    QAudioSource *m_audioSource;
    QIODevice *m_audioSourceDevice;
    std::atomic<bool> m_micEnabled{false};
    QString m_selectedMicDeviceId;    // Empty = use system default
    QString m_selectedOutputDeviceId; // Empty = use system default

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

    // Pre-allocated scratch for resample48kTo12k. Sized to INPUT_BUFFER_SIZE/4
    // bytes (the 4:1 decimation ratio means 12kHz output is 1/4 the 48kHz input
    // size; INPUT_BUFFER_SIZE bytes of 48kHz Float32 = INPUT_BUFFER_SIZE/16
    // samples = INPUT_BUFFER_SIZE/16 * 4 bytes of 12kHz output = INPUT_BUFFER_SIZE/4).
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
    friend class TestAudioEngine; // Allows unit tests to call private resampler methods directly

    static constexpr int PREBUFFER_PACKETS = 1;
    static constexpr int MAX_QUEUE_BYTES = 1000 * BYTES_PER_MS; // 96,000 bytes (1s cap)
    static constexpr int FEED_INTERVAL_MS = 10;
};

#endif // AUDIOENGINE_H
