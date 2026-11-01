#ifndef AUDIOENGINE_H
#define AUDIOENGINE_H

#include <QObject>
#include <QAudioSink>
#include <QAudioFormat>
#include <QIODevice>
#include <QMediaDevices>
#include <QTimer>
#include <array>
#include <atomic>

#include "audio/audiodiagnostics.h"
#include "audio/rxplayout.h"
#include "utils/spscring.h"

/**
 * @brief RX playback: the jitter buffer, MX routing, volume and balance, and the speaker sink.
 *
 * Lives on @c AudioController::m_audioThread (see `controllers/audiocontroller.cpp`).
 * RX:  decoded 12 kHz stereo Float32 PCM is enqueued from the IO thread via
 *      `enqueueAudio()`, consumed by `feedAudioDevice()` on a 5 ms PreciseTimer, with MX
 *      routing + volume + balance applied per packet. The sink buffer is sized from the SL
 *      tier's packet length, and how much audio is held is set by RxPlayout::JitterBuffer from
 *      the jitter it measures (audio/rxplayout.h).
 * TX lives in TxAudioPipeline on its own thread, so opening a microphone never stalls this one.
 *
 * Thread-safety: all public setters mutate @c std::atomic members. RX audio crosses from the
 * I/O thread on a lock-free single-producer/single-consumer ring (utils/spscring.h): exactly one
 * thread may call enqueueAudio(). No mutex is taken on the RX path.
 */
class AudioEngine : public QObject {
    Q_OBJECT

public:
    enum MixSource { MixA = 0, MixB = 1, MixAB = 2, MixNegA = 3 };

    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine();

    Q_INVOKABLE bool start();
    Q_INVOKABLE void stop();
    // RX audio in, 12 kHz stereo Float32 interleaved [main, sub, ...]. Copied into a preallocated
    // ring slot: no allocation, no lock. SINGLE PRODUCER - only the I/O thread may call these.
    // Returns false when the packet was dropped (ring full: the audio thread is not consuming).
    bool enqueueAudio(const float *samples, int floats);
    bool enqueueAudio(const QByteArray &pcmData);
    Q_INVOKABLE void flushQueue();

    // Channel volume controls (applied at playback time for instant response)
    void setMainVolume(float volume);
    void setSubVolume(float volume);
    // Boost above the MAIN/SUB sliders, 0 to +12 dB in 3 dB steps (Options -> Audio Output). Above
    // 0 dB the output also goes through RxOutputStage's soft limiter.
    void setRxBoostDb(int db);
    float mainVolume() const { return m_mainVolume.load(std::memory_order_relaxed); }
    float subVolume() const { return m_subVolume.load(std::memory_order_relaxed); }

    // SUB RX mute control (when sub receiver is off, sub channel is silent)
    void setSubMuted(bool muted);

    // Audio mix routing (MX command - how main/sub maps to L/R when SUB is on)
    void setAudioMix(int left, int right); // MixSource values

    // Balance mode control (0=NOR, 1=BAL)
    void setBalanceMode(int mode);
    void setBalanceOffset(int offset); // -50 to +50

    // RX packet length for the current SL tier, as samples per channel (240/480/720/1440). Resizes
    // the sink buffer to match (audio/rxplayout.h), which needs a sink rebuild: Qt ignores
    // setBufferSize() after start(). Audio thread; call queued.
    Q_INVOKABLE void setRxPacketFrameSamples(int samples);

    // RX playout counters, readable from any thread. Underruns are counted only while receiving,
    // not while the K4 has stopped sending because we are transmitting.
    quint32 rxUnderruns() const { return m_rxUnderruns.load(std::memory_order_relaxed); }
    quint32 rxDroppedPackets() const { return m_rxDroppedPackets.load(std::memory_order_relaxed); }
    // Total RX audio buffered (queue + sink) at the last feed tick, in bytes of decoded audio.
    // Sampled by the UI rather than pushed: the feed runs at 200 Hz.
    int rxBufferedBytes() const { return m_rxBufferedBytes.load(std::memory_order_relaxed); }

    // Consumer side of the qk4.audio.rx diagnostics: ONE thread only (AudioController's main-thread
    // sampler). False when there is nothing to report.
    bool takeRxLevelReport(AudioDiagnostics::RxLevelReport &out);
    bool takeRxPlayoutReport(AudioDiagnostics::RxPlayoutReport &out);

    // Transmitting: the half-duplex K4 stops sending RX audio, so the sink running dry is expected
    // and not counted as an underrun, and the jitter buffer is told RX has paused (it re-anchors on
    // the packets that follow instead of measuring the gap as delay). Two sources, either of which
    // pauses RX: our own PTT (setTransmitting) and the radio's reported TX state
    // (setRadioTransmitting: its front panel, another CAT client, TUNE). Atomics, safe from any
    // thread; the feed picks up the edge on its next tick.
    void setTransmitting(bool transmitting) { m_transmitting.store(transmitting, std::memory_order_relaxed); }
    void setRadioTransmitting(bool transmitting) { m_radioTransmitting.store(transmitting, std::memory_order_relaxed); }

    // Output device settings
    Q_INVOKABLE void setOutputDevice(const QString &deviceId);
    QString outputDeviceId() const;

    // Get list of available output devices (for settings UI)
    static QList<QPair<QString, QString>> availableOutputDevices(); // (id, description)

private slots:
    void feedAudioDevice();
    void onSinkStateChanged(QtAudio::State state);
    // WHY: when the user leaves a device set to "System Default" (empty id), follow
    // the OS default live instead of caching it for the whole session. QMediaDevices
    // fires this when devices or the system default change; we rebuild the sink
    // only if the effective default actually moved. A pinned device is reclaimed when it reappears.
    void onSystemDefaultOutputChanged();

private:
    bool setupAudioOutput();
    // Tear down and rebuild the output sink for the current device, and put the feed timer in
    // the matching state. Every path that changes the output device goes through this.
    void rebuildOutput();
    // Stop and delete the sink without the synchronous drain QAudioSink::stop() does on Linux and
    // Darwin.
    void destroySink();

    // Apply MX routing + volume + balance in place to raw [main, sub] interleaved frames
    void applyMixAndVolume(float *samples, int frames);

    // qk4.audio.rx accumulator, reported and reset once a second of audio. Audio-thread only.
    struct RxLevelStats {
        int frames = 0;
        double sumSqMain = 0.0;
        double sumSqSub = 0.0;
        float peakMain = 0.0f;
        float peakSub = 0.0f;
        float peakOut = 0.0f; // before the clamp or limiter, so it can read above 0 dBFS
        int clipped = 0;      // samples over full scale before that stage: clipped at 0 dB, rounded off when boosted
    };
    RxLevelStats m_rxStats;
    // Turn a second of m_rxStats into a record for the main thread to format (Rule 16), and reset.
    void reportRxLevels(float mainVol, float subVol, float boost);
    SpscRing<AudioDiagnostics::RxLevelReport, 8> m_rxDiagnostics;

    // Audio output format: 12kHz stereo Float32 (K4 RX audio, L=Main R=Sub)
    QAudioFormat m_outputFormat;

    // True between start() and stop(): the engine is meant to be producing RX audio. Distinct from
    // "a sink exists", because the sink can fail to open and must still be retried on the next
    // device change. rebuildOutput() is the only thing that acts on it.
    bool m_outputRunning = false;

    // Audio output (speaker)
    QAudioSink *m_audioSink;
    QIODevice *m_audioSinkDevice;

    QString m_selectedOutputDeviceId;        // Empty = use system default
    QString m_activeOutputDeviceId;          // id of the device the sink was actually opened on
    QMediaDevices *m_mediaDevices = nullptr; // OS device/default-change monitor

    // Channel volume controls (0.0 to 1.0)
    std::atomic<float> m_mainVolume{1.0f};
    std::atomic<float> m_subVolume{1.0f};
    std::atomic<float> m_rxBoostGain{1.0f};

    // SUB RX mute state (true = sub muted, sub channel is silent)
    std::atomic<bool> m_subMuted{true}; // Starts muted (SUB RX is off at startup)

    // Audio mix routing (MX command) - default A.B (main left, sub right). Both sides packed in one
    // atomic, left in the low byte, so the audio thread can never read a left from one MX command
    // and a right from the next - the reason this used to be a mutex.
    static constexpr int packMix(MixSource left, MixSource right) { return int(left) | (int(right) << 8); }
    std::atomic<int> m_mix{packMix(MixA, MixB)};

    // Balance mode (0=NOR: independent volume, 1=BAL: L/R balance)
    std::atomic<int> m_balanceMode{0};
    std::atomic<int> m_balanceOffset{0}; // -50 to +50

    // RX packet length in ms for the current SL tier. Sizes the sink. Audio thread only.
    int m_rxPacketMs = 20;
    // Device-error rebuilds since the last good start; bounds a device that fails every time.
    int m_sinkErrorRebuilds = 0;
    std::atomic<quint32> m_rxUnderruns{0};
    std::atomic<quint32> m_rxDroppedPackets{0};
    std::atomic<bool> m_transmitting{false};
    std::atomic<bool> m_radioTransmitting{false};
    bool rxPaused() const {
        return m_transmitting.load(std::memory_order_relaxed) || m_radioTransmitting.load(std::memory_order_relaxed);
    }
    std::atomic<int> m_rxBufferedBytes{0};

    // Jitter buffer for RX audio playback: one decoded K4 packet per slot, filled in place by the
    // I/O thread and mixed and written in place by the audio thread.
    //
    // WHY 64 slots: the ring is the backstop the 1 s queue cap used to be. 64 packets is 1.28 s at
    // SL0's 20 ms and more at every other tier; the jitter policy trims far below that, so the
    // ring only fills when the audio thread has stopped consuming, and then the producer drops.
    struct RxPacket {
        static constexpr int MAX_FLOATS = 1440 * 2; // 120 ms (SL6-7) of 12 kHz stereo
        int floats = 0;
        std::array<float, MAX_FLOATS> samples;
    };
    SpscRing<RxPacket, 64> m_rxRing;
    // Consumer state, audio thread only. A packet written to the sink in part stays at the ring's
    // front with m_rxHeadOffset floats already written; m_rxHeadMixed says its mix is applied.
    int m_rxHeadOffset = 0;
    bool m_rxHeadMixed = false;
    // How much RX audio to hold and when to correct it (audio/rxplayout.h). Audio thread only; reset
    // by flushQueue(), so every sink rebuild, SL change, stop and start begins a fresh estimate.
    RxPlayout::JitterBuffer m_jitter;
    // Ring packets, counted from the front, that m_jitter has already observed.
    std::size_t m_rxSeen = 0;
    // The RX-paused state m_jitter was last told. Cleared with it by flushQueue(), so a reset while
    // transmitting is told again on the next tick.
    bool m_rxPauseTold = false;
    // qk4.audio.rx playout record, one per second of feed ticks while that category is enabled.
    int m_playoutReportTicks = 0;
    void reportRxPlayout(int bufferedBytes);
    SpscRing<AudioDiagnostics::RxPlayoutReport, 8> m_playoutDiagnostics;
    QTimer *m_feedTimer;

    // Release the ring's front slot and reset the partial-write state.
    void popRxHead();
};

#endif // AUDIOENGINE_H
