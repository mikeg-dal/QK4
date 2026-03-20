#include "sidetonegenerator.h"
#include <QAudioFormat>
#include <QMediaDevices>
#include <QDebug>
#include <QTimer>
#include <QtMath>

SidetoneGenerator::SidetoneGenerator(QObject *parent) : QObject(parent) {
    // Repeat timer created here (moves with parent via moveToThread)
    // Audio init deferred to start() which runs on the sidetone thread
    m_repeatTimer = new QTimer(this);
    m_repeatTimer->setTimerType(Qt::PreciseTimer);
}

SidetoneGenerator::~SidetoneGenerator() {
    // Audio sink should already be cleaned up by stop() on the correct thread.
    // Guard against missing stop() call, but this runs on the wrong thread.
    if (m_audioSink) {
        qWarning() << "SidetoneGenerator: audio sink not cleaned up by stop() — destroying from wrong thread";
        delete m_audioSink;
        m_audioSink = nullptr;
    }
}

void SidetoneGenerator::setOutputDevice(const QString &deviceId) {
    if (m_selectedOutputDeviceId == deviceId)
        return;
    m_selectedOutputDeviceId = deviceId;

    if (m_audioSink) {
        m_audioSink->stop();
        delete m_audioSink;
        m_audioSink = nullptr;
        m_pushDevice = nullptr;
        initAudio();
    }
}

void SidetoneGenerator::initAudio() {
    QAudioFormat format;
    format.setSampleRate(48000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    QAudioDevice device;
    if (!m_selectedOutputDeviceId.isEmpty()) {
        for (const QAudioDevice &d : QMediaDevices::audioOutputs()) {
            if (d.id() == m_selectedOutputDeviceId) {
                device = d;
                break;
            }
        }
    }
    if (device.isNull())
        device = QMediaDevices::defaultAudioOutput();

    if (!device.isFormatSupported(format)) {
        qWarning() << "SidetoneGenerator: Default format not supported, trying nearest";
        format = device.preferredFormat();
    }

    m_audioSink = new QAudioSink(device, format, this);
    m_audioSink->setBufferSize(131072); // 128KB - handles even 5 WPM dah (720ms = ~69KB)

    // Start audio sink immediately and keep it running
    m_pushDevice = m_audioSink->start();
    if (!m_pushDevice) {
        qWarning() << "SidetoneGenerator: Failed to start audio sink:" << m_audioSink->error();
    }
}

void SidetoneGenerator::start() {
    initAudio();
}

void SidetoneGenerator::stop() {
    m_repeatTimer->stop();
    if (m_audioSink) {
        m_audioSink->stop();
        delete m_audioSink;
        m_audioSink = nullptr;
        m_pushDevice = nullptr;
    }
}

void SidetoneGenerator::setFrequency(int hz) {
    m_frequency.store(hz, std::memory_order_relaxed);
}

void SidetoneGenerator::setVolume(float volume) {
    m_volume.store(volume, std::memory_order_relaxed);
}

void SidetoneGenerator::setKeyerSpeed(int wpm) {
    m_keyerWpm.store(qBound(5, wpm, 60), std::memory_order_relaxed);
}

void SidetoneGenerator::stopElement() {
    m_currentElement = ElementNone;
    m_repeatTimer->stop();
}

void SidetoneGenerator::playSingleDit() {
    m_currentElement = ElementNone; // No repeat
    m_repeatTimer->stop();
    playElement(ditDurationMs());
}

void SidetoneGenerator::playSingleDah() {
    m_currentElement = ElementNone; // No repeat
    m_repeatTimer->stop();
    playElement(dahDurationMs());
}

int SidetoneGenerator::ditDurationMs() const {
    return 1200 / m_keyerWpm.load(std::memory_order_relaxed);
}

int SidetoneGenerator::dahDurationMs() const {
    return ditDurationMs() * 3;
}

void SidetoneGenerator::playElement(int durationMs) {
    if (!m_audioSink)
        return;
    if (!m_pushDevice) {
        // Try to restart audio sink if it stopped
        m_pushDevice = m_audioSink->start();
        if (!m_pushDevice) {
            qWarning() << "SidetoneGenerator: Cannot play - no audio device";
            return;
        }
    }

    const int sampleRate = 48000;
    int toneSamples = (sampleRate * durationMs) / 1000;
    int spaceSamples = (sampleRate * ditDurationMs()) / 1000; // Inter-element space = 1 dit
    int totalSamples = toneSamples + spaceSamples;

    // Add short rise/fall time to avoid clicks (3ms each)
    const int riseTimeSamples = (sampleRate * 3) / 1000;
    const int fallTimeSamples = riseTimeSamples;

    QByteArray buffer(totalSamples * sizeof(qint16), 0);
    qint16 *samples = reinterpret_cast<qint16 *>(buffer.data());

    int freq = m_frequency.load(std::memory_order_relaxed);
    float vol = m_volume.load(std::memory_order_relaxed);
    double phaseIncrement = 2.0 * M_PI * freq / sampleRate;

    // Generate tone samples
    for (int i = 0; i < toneSamples; ++i) {
        float envelope = 1.0f;
        if (i < riseTimeSamples) {
            envelope = 0.5f * (1.0f - qCos(M_PI * i / riseTimeSamples));
        } else if (i >= toneSamples - fallTimeSamples) {
            int fallIndex = i - (toneSamples - fallTimeSamples);
            envelope = 0.5f * (1.0f + qCos(M_PI * fallIndex / fallTimeSamples));
        }

        double sample = qSin(m_phase) * vol * envelope * 32767.0;
        samples[i] = static_cast<qint16>(sample);
        m_phase += phaseIncrement;
        if (m_phase >= 2.0 * M_PI) {
            m_phase -= 2.0 * M_PI;
        }
    }

    // Silence samples are already zero from QByteArray initialization

    m_pushDevice->write(buffer);
}
