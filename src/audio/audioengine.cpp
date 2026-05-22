#include "audioengine.h"
#include "audio/audiologging.h"
#include "audio/opusdecoder.h" // NORMALIZE_16BIT constant
#include "audio/opusencoder.h"
#include "network/protocol.h" // buildAudioPacket
#include <QMediaDevices>
#include <QAudioDevice>
#include <QDebug>
#include <cmath>

// Shared logging category for the audio subsystem — declared in audiologging.h,
// defined here because AudioEngine is the primary producer of audio log messages.
Q_LOGGING_CATEGORY(qk4Audio, "qk4.audio")

AudioEngine::AudioEngine(QObject *parent)
    : QObject(parent), m_audioSink(nullptr), m_audioSinkDevice(nullptr), m_audioSource(nullptr),
      m_audioSourceDevice(nullptr), m_opusEncoder(new OpusEncoder(nullptr)), m_micPollTimer(nullptr) {

    // Opus encoder for TX. 12kHz mono — K4 expects mono frames (mono → stereo
    // duplication for EM0/EM1 happens inline below; Opus encoder handles its
    // own mono-to-stereo encoding internally for EM2/EM3). Initialized here so
    // the audio thread (where encode runs) has it ready before the first
    // setPttActive(true) call.
    m_opusEncoder->initialize(12000, 1);

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

    // Pre-size hot-path buffers so the per-poll / per-frame paths reuse capacity.
    // m_micBuffer holds 12kHz S16LE samples queued up to one max frame (SL7 = 1440
    // samples = 2880 bytes); 2× that gives headroom for partial frames + the next
    // poll's data before we compact. m_resampleBuf12k holds 48kHz→12kHz output
    // for one poll cycle (INPUT_BUFFER_SIZE = 19200 bytes of 48kHz Float32 → 4800
    // bytes of 12kHz Float32). m_feedBatch is dimensioned for ~4 packets per cycle.
    m_micBuffer.reserve(2 * 1440 * sizeof(qint16));
    m_resampleBuf12k.reserve(INPUT_BUFFER_SIZE / 4);
    m_feedBatch.reserve(4);

    // WHY setupAudioInput() is deferred until the first openMic() call:
    // Qt's mic-permission callback on macOS runs on the main-thread runloop. During connection
    // startup, AudioController calls into the AudioEngine from the IO thread via
    // BlockingQueuedConnection; if we opened the input here we would block the IO thread waiting
    // for the main thread to deliver the permission result, while the main thread would be
    // blocked on the `RDY;` round-trip waiting on the same IO thread. Deferring to the first PTT
    // press breaks the cycle: by that point the connection is fully up and the main thread is
    // free to process the permission dialog. Once opened, the mic stays open for the remainder
    // of the connection so subsequent PTT presses are instant — see openMic() for details.
}

AudioEngine::~AudioEngine() {
    stop();
    delete m_opusEncoder;
    m_opusEncoder = nullptr;
}

bool AudioEngine::start() {
    bool outputOk = setupAudioOutput();

    if (outputOk) {
        flushQueue();
        m_feedTimer->start();
    }

    // Audio input setup deferred to the first openMic() call (first PTT press) to avoid
    // triggering the macOS mic permission dialog during connection — see ctor comment.

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
    m_micReadOffset = 0;
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
        qCWarning(qk4Audio) << "AudioEngine: No audio output device available";
        return false;
    }

    if (!outputDevice.isFormatSupported(m_outputFormat)) {
        qCWarning(qk4Audio) << "AudioEngine: 12kHz output format not supported by device";
        return false;
    }

    m_audioSink = new QAudioSink(outputDevice, m_outputFormat, this);
    m_audioSink->setBufferSize(OUTPUT_BUFFER_SIZE);

    m_audioSinkDevice = m_audioSink->start();
    if (!m_audioSinkDevice) {
        qCWarning(qk4Audio) << "AudioEngine: Failed to start audio output";
        delete m_audioSink;
        m_audioSink = nullptr;
        return false;
    }

    // Volume is always 1.0 — actual volume control is in the K4's AG command
    m_audioSink->setVolume(1.0f);

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
        const QString reason = QStringLiteral("No audio input device available");
        qCWarning(qk4Audio) << "AudioEngine:" << reason;
        emit micSetupFailed(reason);
        return false;
    }

    // macOS AirPods (and Bluetooth headsets generally) mic support — why this fallback exists:
    //
    // When a Bluetooth headset is used as an audio output device it operates in A2DP mode,
    // which is high-quality stereo but provides NO microphone input to the OS at all. The
    // moment any app opens the device as an input, macOS switches the headset to HFP/HSP
    // (Hands-Free / Headset Profile). In HFP mode the headset exposes a microphone, but the
    // sample rate drops to 8kHz (NBS – narrowband) or 16kHz (WBS – wideband, used by AirPods).
    //
    // The previous code called isFormatSupported(48kHz) and returned false silently when it
    // failed, so every PTT press was a no-op with AirPods. The fix: if 48kHz is unsupported,
    // ask the device for its preferredFormat(), accept it if the rate is 8kHz or 16kHz, and
    // let the matching resampler (resample16kTo12k / resample8kTo12k) convert to the 12kHz
    // mono stream that the Opus encoder expects. Voice quality at 8–16kHz is already limited
    // by the Bluetooth codec, so the additional resampling step has no audible impact.
    QAudioFormat formatToUse = m_inputFormat;
    if (!inputDevice.isFormatSupported(m_inputFormat)) {
        QAudioFormat preferred = inputDevice.preferredFormat();
        preferred.setChannelCount(1);
        preferred.setSampleFormat(QAudioFormat::Float);
        int rate = preferred.sampleRate();
        if ((rate == 8000 || rate == 16000) && inputDevice.isFormatSupported(preferred)) {
            qCWarning(qk4Audio) << "AudioEngine: 48kHz not supported by mic device, falling back to" << rate << "Hz";
            formatToUse = preferred;
        } else {
            const QString reason = QString("Mic device does not support 48kHz and preferred rate "
                                           "%1Hz has no supported resampler path")
                                       .arg(rate);
            qCWarning(qk4Audio) << "AudioEngine:" << reason;
            emit micSetupFailed(reason);
            return false;
        }
    }
    m_actualInputSampleRate = formatToUse.sampleRate();

    m_audioSource = new QAudioSource(inputDevice, formatToUse, this);
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

    // Drain queue under a short lock, then write outside the lock. m_feedBatch
    // is a member to avoid constructing a fresh QList on every 10 ms tick.
    m_feedBatch.clear();
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
            m_feedBatch.append(std::move(pkt));
        }
    }

    emit bufferStatus(preDrainQueueBytes, MAX_QUEUE_BYTES, snapshotPrebuffering);

    // Apply mix/volume and write to audio sink without holding the lock
    for (QByteArray &packet : m_feedBatch) {
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

void AudioEngine::openMic() {
    // Idempotent: already open → return immediately so subsequent PTT presses don't pay
    // the OS audio backend renegotiation cost.
    if (m_micEnabled.load(std::memory_order_relaxed) && m_audioSourceDevice)
        return;

    // Lazy mic initialization — deferred from start() to avoid triggering the macOS mic
    // permission dialog during connection (which would deadlock; see ctor comment).
    if (!m_audioSource) {
        if (!setupAudioInput()) {
            qCWarning(qk4Audio) << "AudioEngine: Failed to setup audio input";
            return;
        }
    }

    m_audioSourceDevice = m_audioSource->start();
    if (!m_audioSourceDevice) {
        qCWarning(qk4Audio) << "AudioEngine: Failed to start microphone device";
        return;
    }

    m_micEnabled.store(true, std::memory_order_relaxed);
    // Use timer-based polling instead of readyRead signal
    // (readyRead doesn't fire reliably on all platforms).
    m_micPollTimer->start();
}

void AudioEngine::closeMic() {
    if (!m_micEnabled.load(std::memory_order_relaxed))
        return;

    m_micEnabled.store(false, std::memory_order_relaxed);
    m_micPollTimer->stop();
    if (m_audioSource) {
        m_audioSource->stop();
    }
    m_audioSourceDevice = nullptr;
    m_micBuffer.clear();
    m_micReadOffset = 0;
}

void AudioEngine::flushMicBuffer() {
    m_micBuffer.clear();
    m_micReadOffset = 0;
}

const QByteArray &AudioEngine::resample48kTo12k(const QByteArray &input48k) {
    // Simple 4:1 decimation with averaging filter (48kHz / 4 = 12kHz).
    // Writes into the pre-allocated m_resampleBuf12k member; resize() at the
    // pre-reserved capacity is alloc-free.
    const float *inputSamples = reinterpret_cast<const float *>(input48k.constData());
    int inputCount = input48k.size() / sizeof(float);
    int outputCount = inputCount / 4;
    const int outputBytes = outputCount * static_cast<int>(sizeof(float));

    m_resampleBuf12k.resize(outputBytes);
    float *output = reinterpret_cast<float *>(m_resampleBuf12k.data());

    for (int i = 0; i < outputCount; i++) {
        // Average 4 samples for simple low-pass filtering
        int srcIdx = i * 4;
        float sum = 0.0f;
        int count = 0;
        for (int j = 0; j < 4 && (srcIdx + j) < inputCount; j++) {
            sum += inputSamples[srcIdx + j];
            count++;
        }
        output[i] = (count > 0) ? (sum / count) : 0.0f;
    }

    return m_resampleBuf12k;
}

QByteArray AudioEngine::resample16kTo12k(const QByteArray &input16k) {
    // Added for AirPods / Bluetooth WBS (Wideband Speech) mic support on macOS.
    // AirPods in HFP mode expose a 16kHz microphone; this converts it to the 12kHz
    // mono stream expected by the Opus encoder.
    // 16kHz → 12kHz: 4:3 ratio. Output sample k sits at input position k * (4/3).
    // Linear interpolation between adjacent input samples avoids aliasing on voice.
    const float *in = reinterpret_cast<const float *>(input16k.constData());
    int inCount = input16k.size() / static_cast<int>(sizeof(float));
    int outCount = (inCount * 3) / 4;

    QByteArray out;
    out.reserve(outCount * static_cast<int>(sizeof(float)));

    for (int k = 0; k < outCount; k++) {
        float pos = k * (4.0f / 3.0f);
        int idx = static_cast<int>(pos);
        float frac = pos - idx;
        float s0 = in[idx];
        float s1 = (idx + 1 < inCount) ? in[idx + 1] : s0;
        float sample = s0 + frac * (s1 - s0);
        out.append(reinterpret_cast<const char *>(&sample), sizeof(float));
    }
    return out;
}

QByteArray AudioEngine::resample8kTo12k(const QByteArray &input8k) {
    // Added for Bluetooth NBS (Narrowband Speech) mic support on macOS.
    // Older Bluetooth headsets in HFP mode expose an 8kHz microphone; this upsamples
    // it to the 12kHz mono stream expected by the Opus encoder.
    // 8kHz → 12kHz: 2:3 ratio (upsampling). Output sample k sits at input position k * (2/3).
    // Linear interpolation fills the inserted samples.
    const float *in = reinterpret_cast<const float *>(input8k.constData());
    int inCount = input8k.size() / static_cast<int>(sizeof(float));
    int outCount = (inCount * 3) / 2;

    QByteArray out;
    out.reserve(outCount * static_cast<int>(sizeof(float)));

    for (int k = 0; k < outCount; k++) {
        float pos = k * (2.0f / 3.0f);
        int idx = static_cast<int>(pos);
        float frac = pos - idx;
        float s0 = in[idx];
        float s1 = (idx + 1 < inCount) ? in[idx + 1] : s0;
        float sample = s0 + frac * (s1 - s0);
        out.append(reinterpret_cast<const char *>(&sample), sizeof(float));
    }
    return out;
}

void AudioEngine::onMicDataReady() {
    if (!m_audioSourceDevice || !m_micEnabled.load(std::memory_order_relaxed))
        return;

    QByteArray data48k = m_audioSourceDevice->readAll();
    if (data48k.isEmpty()) {
        // No data available yet - this is normal, just wait for next poll
        return;
    }

    // Dispatch to the resampler matching the accepted input rate.
    // 48kHz is the normal path for wired/USB mics — writes into the pre-allocated
    // m_resampleBuf12k member (no heap allocation on the hot 10 ms poll tick).
    // 16kHz and 8kHz cover AirPods / Bluetooth HFP mics on macOS; they allocate a
    // small temporary buffer (infrequent, Bluetooth-only path).
    QByteArray btBuf12k;
    const QByteArray *p12k;
    switch (m_actualInputSampleRate) {
    case 16000:
        btBuf12k = resample16kTo12k(data48k);
        p12k = &btBuf12k;
        break;
    case 8000:
        btBuf12k = resample8kTo12k(data48k);
        p12k = &btBuf12k;
        break;
    default:
        p12k = &resample48kTo12k(data48k);
        break;
    }
    const QByteArray &data12k = *p12k;

    // Convert Float32 to S16LE, apply gain, and buffer for frame-based emission
    const float *floatData = reinterpret_cast<const float *>(data12k.constData());
    int floatSamples = data12k.size() / sizeof(float);

    const float gain = m_micGain.load(std::memory_order_relaxed);

    // Convert Float32 to S16LE with gain applied (cubic curve already baked into m_micGain)
    for (int i = 0; i < floatSamples; i++) {
        float sample = qBound(-1.0f, floatData[i] * gain, 1.0f);
        qint16 s16Sample = static_cast<qint16>(sample * 32767.0f);
        m_micBuffer.append(reinterpret_cast<const char *>(&s16Sample), sizeof(qint16));
    }

    // Emit complete frames (size matches SL tier: 240/480/720/1440 samples).
    // m_micReadOffset advances per emitted frame instead of remove(0, n)'s O(N)
    // memmove on every poll. We compact only when the offset has grown past
    // half the buffer's size — keeps amortized work O(1) per frame.
    const int frameBytes = m_frameSamples.load(std::memory_order_relaxed) * static_cast<int>(sizeof(qint16));
    const int frameSamples = m_frameSamples.load(std::memory_order_relaxed);
    const bool pttActive = m_pttActive.load(std::memory_order_acquire);
    const int encodeMode = m_encodeMode.load(std::memory_order_relaxed);

    while (m_micBuffer.size() - m_micReadOffset >= frameBytes) {
        if (pttActive) {
            // Use fromRawData to avoid a copy; immediately consumed inside this
            // tick on the audio thread — the underlying buffer doesn't move.
            const QByteArray frame = QByteArray::fromRawData(m_micBuffer.constData() + m_micReadOffset, frameBytes);
            encodeAndSendFrame(frame, frameSamples, encodeMode);
        }
        m_micReadOffset += frameBytes;
    }
    // Compact lazily: only when the consumed prefix is at least half the buffer.
    if (m_micReadOffset > 0 && m_micReadOffset * 2 >= m_micBuffer.size()) {
        m_micBuffer.remove(0, m_micReadOffset);
        m_micReadOffset = 0;
    }
}

void AudioEngine::encodeAndSendFrame(const QByteArray &s16leMonoFrame, int frameSamples, int encodeMode) {
    // Runs on the audio thread. Translates the captured S16LE mono frame into
    // the K4 wire format and emits txPacketReady. PR 12 moved this logic out
    // of AudioController::onMicrophoneFrame (which ran on the main thread)
    // so a busy GUI event loop no longer stalls voice TX packet emission.
    QByteArray audioData;

    switch (encodeMode) {
    case 0: // EM0 — RAW 32-bit float stereo
    {
        const qint16 *samples = reinterpret_cast<const qint16 *>(s16leMonoFrame.constData());
        const int sampleCount = s16leMonoFrame.size() / static_cast<int>(sizeof(qint16));
        audioData.resize(sampleCount * 2 * static_cast<int>(sizeof(float))); // Stereo output
        float *output = reinterpret_cast<float *>(audioData.data());
        for (int i = 0; i < sampleCount; i++) {
            const float normalized = static_cast<float>(samples[i]) * OpusDecoder::NORMALIZE_16BIT;
            output[i * 2] = normalized;     // Left = Main
            output[i * 2 + 1] = normalized; // Right = Sub (duplicate)
        }
        break;
    }

    case 1: // EM1 — RAW 16-bit S16LE stereo
    {
        const qint16 *samples = reinterpret_cast<const qint16 *>(s16leMonoFrame.constData());
        const int sampleCount = s16leMonoFrame.size() / static_cast<int>(sizeof(qint16));
        audioData.resize(sampleCount * 2 * static_cast<int>(sizeof(qint16))); // Stereo output
        qint16 *output = reinterpret_cast<qint16 *>(audioData.data());
        for (int i = 0; i < sampleCount; i++) {
            output[i * 2] = samples[i];     // Left = Main
            output[i * 2 + 1] = samples[i]; // Right = Sub (duplicate)
        }
        break;
    }

    case 2: // EM2 — Opus int
    case 3: // EM3 — Opus float
    default:
        if (m_opusEncoder)
            audioData = m_opusEncoder->encode(s16leMonoFrame, frameSamples);
        break;
    }

    if (audioData.isEmpty())
        return;

    QByteArray packet = Protocol::buildAudioPacket(audioData, m_txSequence++, encodeMode, frameSamples);
    emit txPacketReady(packet);
}

void AudioEngine::setEncodeMode(int mode) {
    m_encodeMode.store(mode, std::memory_order_relaxed);
}

void AudioEngine::setPttActive(bool active) {
    // Q_INVOKABLE — invoked via QueuedConnection from AudioController on the
    // main thread, so this method body runs on the audio thread.
    m_pttActive.store(active, std::memory_order_release);
    if (active) {
        m_txSequence = 0; // Restart sequence counter for each transmission
        openMic();        // Idempotent — see openMic() WHY comment
        // Flush partial-frame tail from previous transmission so it can't leak
        // into this one's first frame.
        m_micBuffer.clear();
        m_micReadOffset = 0;
    }
    // PTT release: leave mic open. Next frames will be dropped by the
    // pttActive check at the top of onMicDataReady.
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
    // Cubic curve: slider 0-1 maps to gain 0-1 with fine control at low levels
    // e.g., 40% slider → 0.064x gain, 70% → 0.343x, 100% → 1.0x (unity)
    float cubic = gain * gain * gain;
    m_micGain.store(qBound(0.0f, cubic, 1.0f), std::memory_order_relaxed);
}

void AudioEngine::setFrameSamples(int samples) {
    m_frameSamples.store(samples, std::memory_order_relaxed);
}

void AudioEngine::setMicDevice(const QString &deviceId) {
    if (m_selectedMicDeviceId != deviceId) {
        m_selectedMicDeviceId = deviceId;

        // If mic is currently open, restart it with the new device.
        bool wasOpen = m_micEnabled.load(std::memory_order_relaxed);
        if (wasOpen) {
            closeMic();
        }

        // Tear down the existing audio source — it will be recreated lazily by the next
        // openMic() call with the new device ID.
        if (m_audioSource) {
            delete m_audioSource;
            m_audioSource = nullptr;
        }

        if (wasOpen) {
            openMic();
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
