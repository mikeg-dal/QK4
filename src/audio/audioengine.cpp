#include "audioengine.h"
#include <QMediaDevices>
#include <QAudioDevice>
#include <QDebug>
#include <cmath>

AudioEngine::AudioEngine(QObject *parent)
    : QObject(parent), m_audioSink(nullptr), m_audioSinkDevice(nullptr), m_audioSource(nullptr),
      m_audioSourceDevice(nullptr), m_micPollTimer(nullptr) {

    // Output format: K4 uses 12kHz stereo Float32 PCM (L=Main RX, R=Sub RX)
    m_outputFormat.setSampleRate(12000);
    m_outputFormat.setChannelCount(2);
    m_outputFormat.setSampleFormat(QAudioFormat::Float);

    // Input format: Use native 48kHz for microphone capture (most hardware supports this)
    // We'll resample to 12kHz before encoding for K4 TX
    m_inputFormat.setSampleRate(48000);
    m_inputFormat.setChannelCount(1);
    m_inputFormat.setSampleFormat(QAudioFormat::Float);

    // Timers are children of AudioEngine so moveToThread() moves them too
    m_micPollTimer = new QTimer(this);
    m_micPollTimer->setInterval(10); // Poll every 10ms for low latency
    connect(m_micPollTimer, &QTimer::timeout, this, &AudioEngine::onMicDataReady);

    m_feedTimer = new QTimer(this);
    m_feedTimer->setInterval(FEED_INTERVAL_MS);
    connect(m_feedTimer, &QTimer::timeout, this, &AudioEngine::feedAudioDevice);

    // Note: setupAudioInput() is deferred to first setMicEnabled(true) call
    // to avoid macOS mic permission deadlock during connection
}

AudioEngine::~AudioEngine() {
    stop();
}

bool AudioEngine::start() {
    bool outputOk = setupAudioOutput();

    if (outputOk) {
        flushQueue();
        m_feedTimer->start();
    }

    // Audio input setup deferred to first setMicEnabled(true) call
    // to avoid triggering macOS mic permission dialog during connection
    // (the permission callback needs the main thread runloop, which would
    // deadlock if start() were called via BlockingQueuedConnection)

    return outputOk;
}

void AudioEngine::stop() {
    // Stop feed timer and clear jitter buffer
    if (m_feedTimer) {
        m_feedTimer->stop();
    }
    {
        QMutexLocker lock(&m_queueMutex);
        m_audioQueue.clear();
        m_queueBytes = 0;
        m_prebuffering = true;
    }
    m_writeBuffer.clear();

    // Stop mic polling timer
    if (m_micPollTimer) {
        m_micPollTimer->stop();
    }

    if (m_audioSink) {
        m_audioSink->stop();
        delete m_audioSink;
        m_audioSink = nullptr;
        m_audioSinkDevice = nullptr;
    }

    if (m_audioSource) {
        m_audioSource->stop();
        delete m_audioSource;
        m_audioSource = nullptr;
        m_audioSourceDevice = nullptr;
    }

    m_micBuffer.clear();
}

bool AudioEngine::setupAudioOutput() {
    // Find the output device - use selected device or fall back to default
    QAudioDevice outputDevice;

    if (!m_selectedOutputDeviceId.isEmpty()) {
        // Try to find the selected device
        for (const QAudioDevice &device : QMediaDevices::audioOutputs()) {
            if (device.id() == m_selectedOutputDeviceId) {
                outputDevice = device;
                break;
            }
        }
    }

    // Fall back to default if selected device not found
    if (outputDevice.isNull()) {
        outputDevice = QMediaDevices::defaultAudioOutput();
    }

    if (outputDevice.isNull()) {
        qWarning() << "AudioEngine: No audio output device available";
        return false;
    }

    if (!outputDevice.isFormatSupported(m_outputFormat)) {
        qWarning() << "AudioEngine: 12kHz output format not supported by device";
        return false;
    }

    m_audioSink = new QAudioSink(outputDevice, m_outputFormat, this);
    m_audioSink->setBufferSize(OUTPUT_BUFFER_SIZE);

    m_audioSinkDevice = m_audioSink->start();
    if (!m_audioSinkDevice) {
        qWarning() << "AudioEngine: Failed to start audio output";
        delete m_audioSink;
        m_audioSink = nullptr;
        return false;
    }

    // Apply current volume setting to the newly created sink
    m_audioSink->setVolume(m_volume.load(std::memory_order_relaxed));

    return true;
}

bool AudioEngine::setupAudioInput() {
    // Find the input device - use selected device or fall back to default
    QAudioDevice inputDevice;

    if (!m_selectedMicDeviceId.isEmpty()) {
        // Try to find the selected device
        for (const QAudioDevice &device : QMediaDevices::audioInputs()) {
            if (device.id() == m_selectedMicDeviceId) {
                inputDevice = device;
                break;
            }
        }
    }

    // Fall back to default if selected device not found
    if (inputDevice.isNull()) {
        inputDevice = QMediaDevices::defaultAudioInput();
    }

    if (inputDevice.isNull()) {
        qWarning() << "AudioEngine: No audio input device available";
        return false;
    }

    if (!inputDevice.isFormatSupported(m_inputFormat)) {
        qWarning() << "AudioEngine: 48kHz input format not supported by device";
        return false;
    }

    m_audioSource = new QAudioSource(inputDevice, m_inputFormat, this);
    m_audioSource->setBufferSize(INPUT_BUFFER_SIZE);

    // Don't start mic by default - user must enable
    return true;
}

void AudioEngine::enqueueAudio(const QByteArray &pcmData) {
    if (pcmData.isEmpty())
        return;

    QMutexLocker lock(&m_queueMutex);

    // Overflow protection: drop oldest packets if queue exceeds 1s of audio
    while (m_queueBytes + pcmData.size() > MAX_QUEUE_BYTES && !m_audioQueue.isEmpty()) {
        m_queueBytes -= m_audioQueue.dequeue().size();
    }

    m_audioQueue.enqueue(pcmData);
    m_queueBytes += pcmData.size();
}

void AudioEngine::flushQueue() {
    QMutexLocker lock(&m_queueMutex);
    m_audioQueue.clear();
    m_queueBytes = 0;
    m_prebuffering = true;
    m_writeBuffer.clear();
}

void AudioEngine::feedAudioDevice() {
    if (!m_audioSinkDevice)
        return;

    // Drain any leftover write buffer from a previous partial write
    if (!m_writeBuffer.isEmpty()) {
        int bytesFree = m_audioSink->bytesFree();
        if (bytesFree > 0) {
            qint64 toWrite = qMin(static_cast<qint64>(m_writeBuffer.size()), static_cast<qint64>(bytesFree));
            qint64 written = m_audioSinkDevice->write(m_writeBuffer.constData(), toWrite);
            if (written > 0)
                m_writeBuffer.remove(0, static_cast<int>(written));
        }
        if (!m_writeBuffer.isEmpty())
            return; // Still have leftover — don't pull more from queue yet
    }

    // Query sink capacity (audio-thread-only, no mutex needed)
    int bytesFree = m_audioSink->bytesFree();

    // Drain queue under a short lock, then write outside the lock
    QList<QByteArray> localPackets;
    int preDrainQueueBytes;
    bool snapshotPrebuffering;
    {
        QMutexLocker lock(&m_queueMutex);

        if (m_audioQueue.isEmpty())
            return;

        // Wait for at least one packet before starting playback
        if (m_prebuffering) {
            if (m_audioQueue.size() < PREBUFFER_PACKETS)
                return;
            m_prebuffering = false;
        }

        // Snapshot queue depth BEFORE draining (steady-state depth)
        preDrainQueueBytes = m_queueBytes;
        snapshotPrebuffering = m_prebuffering;

        // Drain packets that fit in the sink's free space
        while (!m_audioQueue.isEmpty()) {
            int headSize = m_audioQueue.head().size();
            if (bytesFree < headSize)
                break;

            QByteArray pkt = m_audioQueue.dequeue();
            m_queueBytes -= pkt.size();
            bytesFree -= headSize;
            localPackets.append(std::move(pkt));
        }
    }

    emit bufferStatus(preDrainQueueBytes, MAX_QUEUE_BYTES, snapshotPrebuffering);

    // Apply mix/volume and write to audio sink without holding the lock
    for (QByteArray &packet : localPackets) {
        applyMixAndVolume(packet);
        qint64 written = m_audioSinkDevice->write(packet.constData(), packet.size());
        if (written < packet.size()) {
            // Partial write — save remainder for next feed cycle
            m_writeBuffer.append(packet.constData() + written, packet.size() - static_cast<int>(written));
        }
    }
}

// Compute one output channel's mix from main/sub sources
static inline float mixChannel(float mainSample, float subSample, AudioEngine::MixSource src, float mainVol,
                               float subVol) {
    switch (src) {
    case AudioEngine::MixA:
        return mainSample * mainVol;
    case AudioEngine::MixB:
        return subSample * subVol;
    case AudioEngine::MixAB:
        return mainSample * mainVol + subSample * subVol;
    case AudioEngine::MixNegA:
        return -mainSample * mainVol;
    }
    return 0.0f;
}

void AudioEngine::applyMixAndVolume(QByteArray &packet) {
    float *samples = reinterpret_cast<float *>(packet.data());
    int totalFloats = packet.size() / sizeof(float);
    int sampleCount = totalFloats / 2;

    // Load atomic/guarded values once per packet (not per sample)
    const float mainVol = m_mainVolume.load(std::memory_order_relaxed);
    const float subVol = m_subVolume.load(std::memory_order_relaxed);
    const bool subMuted = m_subMuted.load(std::memory_order_relaxed);
    const int balMode = m_balanceMode.load(std::memory_order_relaxed);
    const int balOffset = m_balanceOffset.load(std::memory_order_relaxed);

    MixSource mixL, mixR;
    {
        QMutexLocker lock(&m_mixMutex);
        mixL = m_mixLeft;
        mixR = m_mixRight;
    }

    // Pre-compute BL balance gains (BAL mode only, applied after MX routing)
    float balLeftGain = 1.0f, balRightGain = 1.0f;
    if (balMode == 1) {
        balLeftGain = qBound(0.0f, (50.0f - balOffset) / 50.0f, 1.0f);
        balRightGain = qBound(0.0f, (50.0f + balOffset) / 50.0f, 1.0f);
    }

    for (int i = 0; i < sampleCount; i++) {
        float mainSample = samples[i * 2];    // Left channel (Main RX / VFO A)
        float subSample = samples[i * 2 + 1]; // Right channel (Sub RX / VFO B)

        // Step 1: SUB RX off — both channels get main audio only, sub slider has no effect
        // BL balance still applies (L/R gain is independent of SUB RX state)
        if (subMuted) {
            float s = mainSample * mainVol;
            samples[i * 2] = qBound(-1.0f, s * balLeftGain, 1.0f);
            samples[i * 2 + 1] = qBound(-1.0f, s * balRightGain, 1.0f);
            continue;
        }

        // Step 2: SUB RX on — apply MX routing
        float left, right;
        if (balMode == 0) {
            // NOR mode: main slider controls main, sub slider controls sub
            left = mixChannel(mainSample, subSample, mixL, mainVol, subVol);
            right = mixChannel(mainSample, subSample, mixR, mainVol, subVol);
        } else {
            // BAL mode: mainVolume controls both receivers (sub slider repurposed as balance)
            left = mixChannel(mainSample, subSample, mixL, mainVol, mainVol);
            right = mixChannel(mainSample, subSample, mixR, mainVol, mainVol);

            // Step 3: Apply BL balance (L/R gain adjustment after MX routing)
            left *= balLeftGain;
            right *= balRightGain;
        }

        // Step 4: Clamp
        samples[i * 2] = qBound(-1.0f, left, 1.0f);
        samples[i * 2 + 1] = qBound(-1.0f, right, 1.0f);
    }
}

void AudioEngine::setMicEnabled(bool enabled) {
    if (m_micEnabled.load(std::memory_order_relaxed) == enabled)
        return;

    m_micEnabled.store(enabled, std::memory_order_relaxed);

    if (enabled) {
        // Lazy mic initialization — deferred from start() to avoid triggering
        // macOS mic permission dialog during connection (which would deadlock)
        if (!m_audioSource) {
            if (!setupAudioInput()) {
                qWarning() << "AudioEngine: Failed to setup audio input";
                m_micEnabled.store(false, std::memory_order_relaxed);
                return;
            }
        }
        m_audioSourceDevice = m_audioSource->start();
        if (m_audioSourceDevice) {
            // Use timer-based polling instead of readyRead signal
            // (readyRead doesn't fire reliably on all platforms)
            m_micPollTimer->start();
        } else {
            qWarning() << "AudioEngine: Failed to start microphone device";
        }
    } else {
        m_micPollTimer->stop();
        if (m_audioSource) {
            m_audioSource->stop();
        }
        m_audioSourceDevice = nullptr;
        m_micBuffer.clear(); // Clear any buffered data
    }
}

QByteArray AudioEngine::resample48kTo12k(const QByteArray &input48k) {
    // Simple 4:1 decimation with averaging filter
    // 48kHz / 4 = 12kHz
    const float *inputSamples = reinterpret_cast<const float *>(input48k.constData());
    int inputCount = input48k.size() / sizeof(float);
    int outputCount = inputCount / 4;

    QByteArray output12k;
    output12k.reserve(outputCount * sizeof(float));

    for (int i = 0; i < outputCount; i++) {
        // Average 4 samples for simple low-pass filtering
        int srcIdx = i * 4;
        float sum = 0.0f;
        int count = 0;
        for (int j = 0; j < 4 && (srcIdx + j) < inputCount; j++) {
            sum += inputSamples[srcIdx + j];
            count++;
        }
        float avg = (count > 0) ? (sum / count) : 0.0f;
        output12k.append(reinterpret_cast<const char *>(&avg), sizeof(float));
    }

    return output12k;
}

void AudioEngine::onMicDataReady() {
    if (!m_audioSourceDevice || !m_micEnabled.load(std::memory_order_relaxed))
        return;

    QByteArray data48k = m_audioSourceDevice->readAll();
    if (data48k.isEmpty()) {
        // No data available yet - this is normal, just wait for next poll
        return;
    }

    // Resample from 48kHz to 12kHz
    QByteArray data12k = resample48kTo12k(data48k);

    // Emit raw resampled data for any listeners that want it
    emit microphoneData(data12k);

    // Convert Float32 to S16LE, apply gain, and buffer for frame-based emission
    const float *floatData = reinterpret_cast<const float *>(data12k.constData());
    int floatSamples = data12k.size() / sizeof(float);

    // Calculate RMS level AFTER gain for meter display (shows what will be transmitted)
    float sumSquares = 0.0f;
    const float gain = m_micGain.load(std::memory_order_relaxed);

    // Convert and append to buffer with gain applied
    for (int i = 0; i < floatSamples; i++) {
        // Apply mic gain and clamp (MIC_GAIN_SCALE makes 50% slider = unity gain)
        float sample = qBound(-1.0f, floatData[i] * gain * MIC_GAIN_SCALE, 1.0f);
        qint16 s16Sample = static_cast<qint16>(sample * 32767.0f);

        // Accumulate for RMS calculation (after gain)
        sumSquares += sample * sample;

        // Append as little-endian bytes
        m_micBuffer.append(reinterpret_cast<const char *>(&s16Sample), sizeof(qint16));
    }

    // Emit RMS level for meter display
    float rmsLevel = (floatSamples > 0) ? std::sqrt(sumSquares / floatSamples) : 0.0f;
    emit micLevelChanged(rmsLevel);

    // Emit complete frames (240 samples = 480 bytes S16LE each)
    while (m_micBuffer.size() >= FRAME_BYTES_S16LE) {
        QByteArray frame = m_micBuffer.left(FRAME_BYTES_S16LE);
        m_micBuffer.remove(0, FRAME_BYTES_S16LE);
        emit microphoneFrame(frame);
    }
}

void AudioEngine::setVolume(float volume) {
    m_volume.store(qBound(0.0f, volume, 1.0f), std::memory_order_relaxed);
    // Note: QAudioSink::setVolume() must be called on the audio thread.
    // Since setVolume() is rarely used (system volume), we skip the cross-thread
    // call here — the stored value will be applied on next start().
}

void AudioEngine::setMainVolume(float volume) {
    m_mainVolume.store(qBound(0.0f, volume, 1.0f), std::memory_order_relaxed);
}

void AudioEngine::setSubVolume(float volume) {
    m_subVolume.store(qBound(0.0f, volume, 1.0f), std::memory_order_relaxed);
}

void AudioEngine::setSubMuted(bool muted) {
    m_subMuted.store(muted, std::memory_order_relaxed);
}

void AudioEngine::setAudioMix(int left, int right) {
    QMutexLocker lock(&m_mixMutex);
    m_mixLeft = static_cast<MixSource>(qBound(0, left, 3));
    m_mixRight = static_cast<MixSource>(qBound(0, right, 3));
}

void AudioEngine::setBalanceMode(int mode) {
    m_balanceMode.store(qBound(0, mode, 1), std::memory_order_relaxed);
}

void AudioEngine::setBalanceOffset(int offset) {
    m_balanceOffset.store(qBound(-50, offset, 50), std::memory_order_relaxed);
}

void AudioEngine::setMicGain(float gain) {
    m_micGain.store(qBound(0.0f, gain, 1.0f), std::memory_order_relaxed);
}

void AudioEngine::setMicDevice(const QString &deviceId) {
    if (m_selectedMicDeviceId != deviceId) {
        m_selectedMicDeviceId = deviceId;

        // If mic is currently enabled, we need to restart it with the new device
        bool wasEnabled = m_micEnabled.load(std::memory_order_relaxed);
        if (wasEnabled) {
            setMicEnabled(false);
        }

        // Tear down existing audio source — it will be recreated lazily
        // by the next setMicEnabled(true) call with the new device ID
        if (m_audioSource) {
            delete m_audioSource;
            m_audioSource = nullptr;
        }

        if (wasEnabled) {
            setMicEnabled(true);
        }
    }
}

QString AudioEngine::micDeviceId() const {
    return m_selectedMicDeviceId;
}

QList<QPair<QString, QString>> AudioEngine::availableInputDevices() {
    QList<QPair<QString, QString>> devices;

    // Add "System Default" as the first option
    devices.append(qMakePair(QString(""), QString("System Default")));

    // Add all available input devices
    for (const QAudioDevice &device : QMediaDevices::audioInputs()) {
        devices.append(qMakePair(QString(device.id()), device.description()));
    }

    return devices;
}

void AudioEngine::setOutputDevice(const QString &deviceId) {
    if (m_selectedOutputDeviceId != deviceId) {
        m_selectedOutputDeviceId = deviceId;

        // Restart audio output with the new device if currently running
        if (m_audioSink) {
            m_audioSink->stop();
            delete m_audioSink;
            m_audioSink = nullptr;
            m_audioSinkDevice = nullptr;

            setupAudioOutput();
        }
    }
}

QString AudioEngine::outputDeviceId() const {
    return m_selectedOutputDeviceId;
}

QList<QPair<QString, QString>> AudioEngine::availableOutputDevices() {
    QList<QPair<QString, QString>> devices;

    // Add "System Default" as the first option
    devices.append(qMakePair(QString(""), QString("System Default")));

    // Add all available output devices
    for (const QAudioDevice &device : QMediaDevices::audioOutputs()) {
        devices.append(qMakePair(QString(device.id()), device.description()));
    }

    return devices;
}
