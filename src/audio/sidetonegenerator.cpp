#include "sidetonegenerator.h"
#include "audio/audiologging.h"
#include "audio/livesidetone.h"
#include "utils/radioutils.h"
#include <QAudioFormat>
#include <QDebug>
#include <QMediaDevices>
#include <QtMath>
#include <algorithm>
#include <chrono>

class LiveSidetoneSource : public QIODevice {
public:
    explicit LiveSidetoneSource(QObject *parent) : QIODevice(parent) {
        // QIODevice's default read-ahead buffer could otherwise pre-render another 170 ms.
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return 1920 + QIODevice::bytesAvailable(); }
    LiveSidetone tone;

protected:
    qint64 readData(char *data, qint64 size) override { return tone.render(data, size); }
    qint64 writeData(const char *, qint64) override { return -1; }
};

SidetoneGenerator::SidetoneGenerator(QObject *parent) : QObject(parent) {
    m_liveSource = new LiveSidetoneSource(this);
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
    m_liveSource->tone.reset();
    if (m_started) {
        clearAudio();
        initAudio();
    }
}

void SidetoneGenerator::initAudio() {
    if (m_audioSink || !m_started) {
        return;
    }
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

    if (device.isNull()) {
        qCWarning(qk4Audio) << "SidetoneGenerator: No audio output device";
        return;
    }
    if (!device.isFormatSupported(format)) {
        // Both renderers produce this exact PCM format. Falling back to a device's preferred
        // float/stereo format while still writing mono Int16 corrupts audio and its duration.
        qCWarning(qk4Audio) << "SidetoneGenerator: Output does not support 48 kHz mono Int16 audio";
        return;
    }

    m_audioSink = new QAudioSink(device, format, this);
    if (m_audioMode == AudioMode::Live) {
        // Request 20 ms, not the complete-element path's 1.36 s capacity. The backend may use a
        // larger hardware minimum; the adapter buzzer remains the path for zero USB/audio delay.
        m_audioSink->setBufferSize(48000 * 2 * 20 / 1000);
        m_audioSink->start(m_liveSource);
        if (m_audioSink->error() != QAudio::NoError) {
            qCWarning(qk4Audio) << "SidetoneGenerator: Failed to start live audio:" << m_audioSink->error();
            clearAudio();
        }
        return;
    }
    m_audioSink->setBufferSize(131072); // Complete dit/dah plus inter-element space.

    // Start audio sink immediately and keep it running
    m_pushDevice = m_audioSink->start();
    if (!m_pushDevice) {
        qCWarning(qk4Audio) << "SidetoneGenerator: Failed to start audio sink:" << m_audioSink->error();
        clearAudio();
    }
}

void SidetoneGenerator::start() {
    m_started = true;
    initAudio();
}

void SidetoneGenerator::stop() {
    m_started = false;
    clearAudio();
    m_liveSource->tone.reset();
    m_activeOutputDeviceId.clear();
}

void SidetoneGenerator::clearAudio() {
    if (m_audioSink) {
        m_audioSink->reset(); // Discard queued samples as well as stopping output.
        delete m_audioSink;
        m_audioSink = nullptr;
        m_pushDevice = nullptr;
    }
}

void SidetoneGenerator::setAudioMode(AudioMode mode) {
    if (mode == m_audioMode) {
        return;
    }
    clearAudio();
    m_liveSource->tone.reset();
    m_audioMode = mode;
    initAudio();
}

void SidetoneGenerator::setKeyDown(bool down) {
    const auto timestampNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    setKeyDownAt(down, timestampNs);
}

void SidetoneGenerator::setKeyDownAt(bool down, qint64 timestampNs) {
    if (!m_started) {
        return;
    }
    setAudioMode(AudioMode::Live);
    if (down && (!m_audioSink || m_audioSink->error() != QAudio::NoError)) {
        clearAudio();
        m_liveSource->tone.reset();
        initAudio();
    }
    if (!m_liveSource->tone.setKeyDown(down, timestampNs)) {
        qCWarning(qk4Audio) << "SidetoneGenerator: Live edge queue overflowed; silencing output";
    }
}

void SidetoneGenerator::resetKeyedTone() {
    m_liveSource->tone.reset();
    if (m_audioMode == AudioMode::Live && m_started) {
        clearAudio();
        initAudio();
    }
}

void SidetoneGenerator::onSystemDefaultOutputChanged() {
    if (!m_started)
        return;
    QAudioDevice effectiveDevice;
    if (!m_selectedOutputDeviceId.isEmpty()) {
        for (const QAudioDevice &device : QMediaDevices::audioOutputs()) {
            if (device.id() == m_selectedOutputDeviceId) {
                effectiveDevice = device;
                break;
            }
        }
    }
    if (effectiveDevice.isNull()) {
        effectiveDevice = QMediaDevices::defaultAudioOutput();
    }
    if (effectiveDevice.id() == m_activeOutputDeviceId && m_audioSink && m_audioSink->error() == QAudio::NoError) {
        return;
    }
    // Retry if a formerly missing/unsupported output became usable, including a pinned device
    // returning. Every replacement starts silent; never replay held or queued tone on a new sink.
    clearAudio();
    m_liveSource->tone.reset();
    initAudio();
}

void SidetoneGenerator::setFrequency(int hz) {
    const int frequency = std::clamp(hz, 100, 2000);
    m_frequency.store(frequency, std::memory_order_relaxed);
    m_liveSource->tone.setFrequency(frequency);
}

void SidetoneGenerator::setVolume(float volume) {
    const float bounded = std::isfinite(volume) ? std::clamp(volume, 0.0f, 1.0f) : 0.0f;
    m_volume.store(bounded, std::memory_order_relaxed);
    m_liveSource->tone.setVolume(bounded);
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
    if (!m_started) {
        return;
    }
    setAudioMode(AudioMode::Elements);
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
