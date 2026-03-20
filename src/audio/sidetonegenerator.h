#ifndef SIDETONEGENERATOR_H
#define SIDETONEGENERATOR_H

#include <QObject>
#include <QAudioSink>
#include <QIODevice>
#include <QByteArray>
#include <QtMath>
#include <atomic>

class QTimer;

class SidetoneGenerator : public QObject {
    Q_OBJECT
public:
    explicit SidetoneGenerator(QObject *parent = nullptr);
    ~SidetoneGenerator();

    void setFrequency(int hz);
    void setVolume(float volume);
    void setKeyerSpeed(int wpm);

    // Initialize/shutdown audio (called on sidetone thread via invokeMethod)
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void setOutputDevice(const QString &deviceId);

    Q_INVOKABLE void stopElement(); // Call when paddle is released

    // Play a single element without repeat (MIDI interface — K4 keyer handles repeat)
    Q_INVOKABLE void playSingleDit();
    Q_INVOKABLE void playSingleDah();

signals:

private:
    void initAudio();
    void playElement(int durationMs);
    int ditDurationMs() const;
    int dahDurationMs() const;

    enum Element { ElementNone, ElementDit, ElementDah };

    QAudioSink *m_audioSink = nullptr;
    QIODevice *m_pushDevice = nullptr;
    QTimer *m_repeatTimer = nullptr;
    QString m_selectedOutputDeviceId;
    std::atomic<int> m_frequency{600};
    std::atomic<float> m_volume{0.3f};
    std::atomic<int> m_keyerWpm{20};
    double m_phase = 0.0;
    Element m_currentElement = ElementNone;
};

#endif // SIDETONEGENERATOR_H
