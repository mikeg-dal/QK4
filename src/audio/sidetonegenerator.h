#ifndef SIDETONEGENERATOR_H
#define SIDETONEGENERATOR_H

#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>
#include <QObject>
#include <atomic>
#include <optional>
#include <vector>

#include "hardware/keyertiming.h"
#include "audio/livesidetone.h"

class SidetoneScheduler;

/**
 * @brief Local CW sidetone output for the HaliKey keyer. Lives on its own real-time thread
 *        (HardwareController's m_sidetoneThread).
 *
 * A push-mode QAudioSink topped up on a 1 ms timer, one 4 ms block at a time, only while less than
 * kTargetQueueSeconds is waiting in it, so the sidetone itself decides how far ahead it renders.
 * SidetoneScheduler says where in each block a key edge falls, against the block's play-out time
 * (time of render plus the audio already queued ahead of it). That places every edge on an
 * exact frame, so each tone sounds its true length with a fixed delay. Envelope and tone follow
 * K4-Companion's mix_sidetone (Dale Farnsworth, GPL-3.0): 4 ms raised-cosine rise and fall, an
 * exact-period sine table, continuous phase.
 * Vail's optional computer sidetone renders already-keyed timestamped edges through the same
 * paced output and format conversion, independently of the paddle keyer's scheduler.
 *
 * WHY its own sink for now rather than mixed into the RX output: the RX path is being reworked.
 * Phase 2 of docs/cw-keyer-port-plan.md moves this render into AudioEngine's output, so the tone
 * shares the RX device and clock. The scheduler is already the seam that move needs.
 *
 * All public slots are Q_INVOKABLE and expected to be posted via QueuedConnection.
 */
class SidetoneGenerator : public QObject {
    Q_OBJECT

public:
    SidetoneGenerator(SidetoneScheduler &scheduler, CwKeying::Clock &clock, QObject *parent = nullptr);
    ~SidetoneGenerator() override;

    Q_INVOKABLE void setFrequency(int hz);
    Q_INVOKABLE void setVolume(float volume);

    // start() promotes the sidetone thread; stop() closes the stream. Both on the sidetone thread
    // via invokeMethod.
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    // Open the output stream and run the pump only while CW keying can happen (CwGate). While it
    // is closed nothing runs on this thread and the scheduler refuses sidetone, as it does with no
    // output device.
    Q_INVOKABLE void setStreamWanted(bool wanted);
    Q_INVOKABLE void setOutputDevice(const QString &deviceId);

    Q_INVOKABLE void setKeyDown(bool down);
    Q_INVOKABLE void setKeyDownAt(bool down, qint64 timestampNs);
    Q_INVOKABLE void resetKeyedTone();

    // The sidetone's own clock, as the scheduler sees it: the keyer clock while a stream is open,
    // nullopt otherwise. Thread-safe.
    std::optional<double> streamTime() const;

private:
    void initAudio();
    void closeAudio();
    void onSystemDefaultOutputChanged();
    void rebuildTone();
    void renderBlock(float *out, int frames, double dacTime, double now);
    void convert(const float *in, int frames, char *dst) const;
    void pump();

    SidetoneScheduler &m_scheduler;
    CwKeying::Clock &m_clock;

    QAudioSink *m_audioSink = nullptr;
    QIODevice *m_pushDevice = nullptr;
    class QTimer *m_pumpTimer = nullptr;
    QString m_selectedOutputDeviceId;
    QString m_activeOutputDeviceId;          // id of the device the sink was actually opened on
    QMediaDevices *m_mediaDevices = nullptr; // OS device/default-change monitor
    QAudioFormat m_format;
    bool m_streamWanted = false; // sidetone thread only

    std::atomic<bool> m_streamActive{false};
    std::atomic<int> m_frequency{600};
    std::atomic<float> m_volume{0.3f};
    bool m_started = false;
    bool m_liveMode = false;
    LiveSidetone m_liveTone;

    // Render state (sidetone thread only).
    int m_rate = 0;
    int m_blockFrames = 0;
    double m_riseStep = 0;
    double m_fallStep = 0;
    bool m_keyed = false;
    double m_envelope = 0.0;
    std::vector<float> m_cycle; // one exact period of the tone at unit amplitude
    size_t m_cycleIndex = 0;
    int m_toneFrequency = 0;
    qint64 m_framesWritten = 0;
    double m_targetQueue = 0; // adapts upward from kTargetQueueSeconds; see pump()
    QByteArray m_bytes;
    std::vector<float> m_scratch;
    std::vector<float> m_positions;

    static constexpr double kRiseSeconds = .004;
    static constexpr double kFallSeconds = .004;
    static constexpr double kBlockSeconds = .004;
    // The sink's capacity, and the queue the pump starts from. The queue is the local delay between
    // "keyed" and "heard" on top of the device's own latency, so it starts low (8 ms) and rises one
    // block at a time only when the device actually runs dry (pump()). Pull mode, letting Qt
    // decide, came to ~100 ms on a Mac mini. The capacity bounds how far it can rise.
    static constexpr double kBufferSeconds = .040;
    static constexpr double kTargetQueueSeconds = .008;
};

#endif // SIDETONEGENERATOR_H
