#ifndef SIDETONEGENERATOR_H
#define SIDETONEGENERATOR_H

#include <QObject>
#include <QAudioSink>
#include <QIODevice>
#include <QByteArray>
#include <QMediaDevices>
#include <QtMath>
#include <atomic>

class LiveSidetoneSource;

/**
 * @brief Local CW sidetone synth. Lives on its own thread (HardwareController's m_sidetoneThread)
 *        so audio output never blocks the keyer or UI. IambicKeyer drives `playSingleDit/Dah` per
 *        element for both HaliKey transports (V1.4 serial and MIDI); when the KPOD+ owns the CW
 *        path, CwController suppresses this local sidetone entirely. Each element is written as a
 *        complete PCM block (tone + inter-element space) and always plays to completion.
 *        Optional Vail computer feedback follows already-keyed edges through setKeyDownAt(),
 *        using a small pull buffer and timestamped edge queue rather than another keyer.
 *        All public slots are Q_INVOKABLE and expected to be posted via QueuedConnection.
 */
class SidetoneGenerator : public QObject {
    Q_OBJECT
public:
    explicit SidetoneGenerator(QObject *parent = nullptr);
    ~SidetoneGenerator();

    Q_INVOKABLE void setFrequency(int hz);
    Q_INVOKABLE void setVolume(float volume);
    Q_INVOKABLE void setKeyerSpeed(int wpm);

    // Initialize/shutdown audio (called on sidetone thread via invokeMethod)
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void setOutputDevice(const QString &deviceId);

    // Play a single element (one per IambicKeyer::elementStarted)
    Q_INVOKABLE void playSingleDit();
    Q_INVOKABLE void playSingleDah();

    // Follow already-keyed hardware edges for optional computer feedback. Calling false while
    // started primes the live pull stream, so the first press need not reopen the sound device.
    Q_INVOKABLE void setKeyDown(bool down);
    // Hardware routing passes its callback ingress timestamp so delayed Qt delivery cannot
    // collapse a complete short element into two adjacent processing-time edges.
    Q_INVOKABLE void setKeyDownAt(bool down, qint64 timestampNs);
    // Lifecycle/source changes must discard pending edge history and already-buffered PCM.
    Q_INVOKABLE void resetKeyedTone();

signals:

private:
    void initAudio();
    void clearAudio();
    enum class AudioMode { Elements, Live };
    void setAudioMode(AudioMode mode);
    void playElement(int durationMs);
    int ditDurationMs() const;
    int dahDurationMs() const;
    // Follow the effective selected/default output and recover when a missing output returns.
    // Every replacement starts silent so held live tone cannot survive a device change.
    void onSystemDefaultOutputChanged();

    QAudioSink *m_audioSink = nullptr;
    QIODevice *m_pushDevice = nullptr;
    LiveSidetoneSource *m_liveSource = nullptr;
    AudioMode m_audioMode = AudioMode::Elements;
    bool m_started = false;
    QString m_selectedOutputDeviceId;
    QString m_activeOutputDeviceId;          // id of the device the sink was actually opened on
    QMediaDevices *m_mediaDevices = nullptr; // OS device/default-change monitor
    std::atomic<int> m_frequency{600};
    std::atomic<float> m_volume{0.3f};
    std::atomic<int> m_keyerWpm{20};
    double m_phase = 0.0;

    // Pre-allocated PCM scratch buffer. Worst case is 5 WPM dah (720 ms tone +
    // 240 ms inter-element space) at 48 kHz × 2 bytes = ~92 kB. Sized to
    // 128 kB so a future slower-keyer mode doesn't realloc. Previously
    // playElement() allocated a fresh QByteArray per CW element (7+ allocs/sec
    // during keying). resize() against the pre-sized capacity is alloc-free.
    QByteArray m_elementBuffer;
};

#endif // SIDETONEGENERATOR_H
