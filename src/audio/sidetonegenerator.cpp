#include "sidetonegenerator.h"
#include "audio/audiologging.h"
#include "utils/radioutils.h"
#include <QAudioFormat>
#include <QDebug>
#include <QMediaDevices>
#include <QtMath>
#include <algorithm>

SidetoneGenerator::SidetoneGenerator(QObject *parent) : QObject(parent) {
    // Audio init deferred to start() which runs on the sidetone thread.
    // WHY parented here, pre-moveToThread: the monitor rides moveToThread() with the
    // generator, so audioOutputsChanged is delivered on the sidetone thread — the same
    // thread that owns the sink. Same pattern as AudioEngine.
    m_mediaDevices = new QMediaDevices(this);
    connect(m_mediaDevices, &QMediaDevices::audioOutputsChanged, this,
            &SidetoneGenerator::onSystemDefaultOutputChanged);
}

SidetoneGenerator::~SidetoneGenerator() {
    // Audio sink should already be cleaned up by stop() on the correct thread.
    // Guard against missing stop() call, but this runs on the wrong thread.
    if (m_audioSink) {
        qCWarning(qk4Audio) << "SidetoneGenerator: audio sink not cleaned up by stop() — destroying from wrong thread";
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

    // Record the actually-opened device so onSystemDefaultOutputChanged can detect
    // whether the effective default really moved. Set before sink creation so even
    // a failed start() records what was attempted (same ordering as AudioEngine).
    m_activeOutputDeviceId = device.id();

    if (!device.isFormatSupported(format)) {
        qCWarning(qk4Audio) << "SidetoneGenerator: Default format not supported, trying nearest";
        format = device.preferredFormat();
    }

    m_audioSink = new QAudioSink(device, format, this);
    m_audioSink->setBufferSize(131072); // 128KB - handles even 5 WPM dah (720ms = ~69KB)

    // Start audio sink immediately and keep it running
    m_pushDevice = m_audioSink->start();
    if (!m_pushDevice) {
        qCWarning(qk4Audio) << "SidetoneGenerator: Failed to start audio sink:" << m_audioSink->error();
    }
}

void SidetoneGenerator::start() {
    initAudio();
}

void SidetoneGenerator::stop() {
    if (m_audioSink) {
        m_audioSink->stop();
        delete m_audioSink;
        m_audioSink = nullptr;
        m_pushDevice = nullptr;
    }
    m_activeOutputDeviceId.clear();
}

void SidetoneGenerator::onSystemDefaultOutputChanged() {
    // Only follow the OS default when the user hasn't pinned a specific device.
    if (!m_selectedOutputDeviceId.isEmpty())
        return;
    // Nothing built yet (or stop() already ran) — the next initAudio() resolves fresh.
    if (!m_audioSink)
        return;
    const QString newDefault = QMediaDevices::defaultAudioOutput().id();
    if (newDefault.isEmpty() || newDefault == m_activeOutputDeviceId)
        return; // effective default unchanged

    m_audioSink->stop();
    delete m_audioSink;
    m_audioSink = nullptr;
    m_pushDevice = nullptr;
    initAudio();
}

void SidetoneGenerator::setFrequency(int hz) {
    m_frequency.store(hz, std::memory_order_relaxed);
}

void SidetoneGenerator::setVolume(float volume) {
    m_volume.store(volume, std::memory_order_relaxed);
}

void SidetoneGenerator::setKeyerSpeed(int wpm) {
    // Stored unclamped; RadioUtils::ditMsForWpm applies the one clamp both this and the keyer's
    // element clock use. Clamping to a different range here is what made the sidetone drift.
    m_keyerWpm.store(wpm, std::memory_order_relaxed);
}

void SidetoneGenerator::playSingleDit() {
    playElement(ditDurationMs());
}

void SidetoneGenerator::playSingleDah() {
    playElement(dahDurationMs());
}

int SidetoneGenerator::ditDurationMs() const {
    return RadioUtils::ditMsForWpm(m_keyerWpm.load(std::memory_order_relaxed));
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
            qCWarning(qk4Audio) << "SidetoneGenerator: Cannot play - no audio device";
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

    // Reserve once on first call, then reuse for every subsequent element.
    // resize() to a smaller value preserves capacity in Qt 6, so per-element
    // allocations only happen if a slower keyer speed than ever-seen drives a
    // larger buffer.
    const int bufferBytes = totalSamples * static_cast<int>(sizeof(qint16));
    if (m_elementBuffer.capacity() < bufferBytes)
        m_elementBuffer.reserve(bufferBytes);
    m_elementBuffer.resize(bufferBytes);
    qint16 *samples = reinterpret_cast<qint16 *>(m_elementBuffer.data());
    // Clear inter-element silence tail (resize doesn't zero on grow).
    std::fill_n(samples + toneSamples, spaceSamples, qint16{0});

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

    // Inter-element silence was zeroed above before tone generation.

    m_pushDevice->write(m_elementBuffer);
}
