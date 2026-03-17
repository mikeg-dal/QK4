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

    Q_INVOKABLE void setMicEnabled(bool enabled);
    bool isMicEnabled() const { return m_micEnabled.load(std::memory_order_relaxed); }

    void setVolume(float volume); // 0.0 to 1.0
    float volume() const { return m_volume.load(std::memory_order_relaxed); }

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

    // Microphone settings
    void setMicGain(float gain); // 0.0 to 1.0
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
    void microphoneData(const QByteArray &pcmData);    // Raw Float32 mic data (variable size)
    void microphoneFrame(const QByteArray &s16leData); // Complete frame (240 samples, S16LE @ 12kHz)
    void micLevelChanged(float level);                 // RMS level 0.0-1.0 for meter display
    void bufferStatus(int queueBytes, int maxBytes, bool prebuffering);

private slots:
    void onMicDataReady();
    void feedAudioDevice();

private:
    bool setupAudioOutput();
    bool setupAudioInput();

    // Resample 48kHz Float32 samples to 12kHz (4:1 decimation with averaging)
    QByteArray resample48kTo12k(const QByteArray &input48k);

    // Apply MX routing + volume + balance to a raw [main, sub] interleaved packet
    void applyMixAndVolume(QByteArray &packet);

    // Audio output format: 12kHz stereo Float32 (K4 RX audio, L=Main R=Sub)
    QAudioFormat m_outputFormat;

    // Audio input format: 48kHz mono Float32 (native macOS rate, resampled to 12kHz)
    QAudioFormat m_inputFormat;

    // Audio output (speaker)
    QAudioSink *m_audioSink;
    QIODevice *m_audioSinkDevice;

    // Audio input (microphone)
    QAudioSource *m_audioSource;
    QIODevice *m_audioSourceDevice;
    std::atomic<bool> m_micEnabled{false};
    QString m_selectedMicDeviceId;    // Empty = use system default
    QString m_selectedOutputDeviceId; // Empty = use system default

    // Volume control (QAudioSink system volume)
    std::atomic<float> m_volume{1.0f};

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

    // Microphone gain scaling factor (gain slider 0-1 maps to 0-2x, so 0.5 = unity)
    static constexpr float MIC_GAIN_SCALE = 2.0f;

    // Microphone frame buffering for Opus encoding
    // Buffer accumulates S16LE samples at 12kHz until we have a complete frame
    static constexpr int FRAME_SAMPLES = 240;                                // 20ms at 12kHz
    static constexpr int FRAME_BYTES_S16LE = FRAME_SAMPLES * sizeof(qint16); // 480 bytes
    QByteArray m_micBuffer;

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

    // Jitter buffer constants (adapt to any SL level automatically)
    // Prebuffer: start playback as soon as the first packet arrives.
    // The SL level already provides jitter tolerance (larger packets = more runway),
    // so additional prebuffering just adds latency without benefit.
    static constexpr int PREBUFFER_PACKETS = 1;
    static constexpr int MAX_QUEUE_BYTES = 1000 * BYTES_PER_MS; // 96,000 bytes (1s cap)
    static constexpr int FEED_INTERVAL_MS = 10;
};

#endif // AUDIOENGINE_H
