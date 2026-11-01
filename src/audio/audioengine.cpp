#include "audioengine.h"
#include "audio/audiodeviceselection.h" // which device a stream belongs on
#include "logging/logcategories.h"
#include "audio/rxoutputstage.h" // RX boost + soft limiter
#include "audio/rxplayout.h"     // RX sink size and jitter policy
#include <QMediaDevices>
#include <QAudioDevice>
#include <QDebug>
#include <chrono>
#include <cmath>

namespace {
// Written to the sink when the jitter buffer grows (all-bits-zero is 0.0f). Static, so the audio
// thread never allocates for it.
constexpr int kSilenceChunkBytes = 4096;
alignas(float) constexpr char kSilence[kSilenceChunkBytes] = {};

std::int64_t steadyNowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
} // namespace

AudioEngine::AudioEngine(QObject *parent) : QObject(parent), m_audioSink(nullptr), m_audioSinkDevice(nullptr) {

    // Output format: K4 uses 12kHz stereo Float32 PCM (L=Main RX, R=Sub RX)
    m_outputFormat.setSampleRate(12000);
    m_outputFormat.setChannelCount(2);
    m_outputFormat.setSampleFormat(QAudioFormat::Float);

    // WHY PreciseTimer: the sink now holds one packet plus a 30 ms margin instead of 500 ms, and the
    // margin assumes the feed looks every 5 ms. A CoarseTimer may be off by 5% of its interval and
    // coalesced with others; a PreciseTimer "will try to keep millisecond accuracy" (QTimer docs).
    m_feedTimer = new QTimer(this);
    m_feedTimer->setTimerType(Qt::PreciseTimer);
    m_feedTimer->setInterval(RxPlayout::FEED_INTERVAL_MS);
    connect(m_feedTimer, &QTimer::timeout, this, &AudioEngine::feedAudioDevice);

    // Monitor OS device/default changes so a "System Default" selection follows the OS
    // live. Parented to this engine, so moveToThread() carries it to the audio thread and
    // its signals arrive there — the same thread that owns the sink.
    m_mediaDevices = new QMediaDevices(this);
    connect(m_mediaDevices, &QMediaDevices::audioOutputsChanged, this, &AudioEngine::onSystemDefaultOutputChanged);
}

AudioEngine::~AudioEngine() {
    stop();
}

bool AudioEngine::start() {
    m_outputRunning = true;
    rebuildOutput();

    return m_audioSink != nullptr;
}

// Tear the output sink down and build it again for whatever device is currently selected, then
// put the feed timer in the matching state.
//
// WHY every path goes through here rather than calling setupAudioOutput() directly: recovery.
// setOutputDevice() and onSystemDefaultOutputChanged() both used to rebuild only `if (m_audioSink)`,
// so if the FIRST attempt failed there was no sink, and every later attempt was skipped by that
// guard — the audio output was dead for the life of the process and no device change could revive
// it. The feed timer made it worse: it was started only by start(), so even a sink that did get
// built afterwards was never fed.
//
// That is not hypothetical. Launch with a virtual loopback device selected, have it refuse the
// 12 kHz format below, then pick the real speakers in Options: nothing happens, and nothing says
// why. Found on the bench, 2026-09-18.
void AudioEngine::rebuildOutput() {
    if (m_feedTimer) {
        m_feedTimer->stop();
    }
    destroySink();

    // Called during construction too, before the engine is meant to be producing anything.
    if (!m_outputRunning) {
        return;
    }

    if (setupAudioOutput()) { // also flushes the queue + resets the jitter buffer
        m_feedTimer->start();
    }
}

void AudioEngine::stop() {
    m_outputRunning = false;

    // Stop feed timer and clear jitter buffer
    if (m_feedTimer) {
        m_feedTimer->stop();
    }
    flushQueue();

    destroySink();
}

void AudioEngine::destroySink() {
    if (!m_audioSink)
        return;
    // Disconnected first: stop() emits stateChanged, and a StoppedState we caused is not a device
    // error to recover from.
    m_audioSink->disconnect(this);
    // WHY reset() before stop(): "On Linux, and Darwin, [stop()] synchronously drains the underlying
    // audio buffer" (QAudioSink docs). Draining audio we are about to throw away only stalls this
    // thread; reset() discards it at once.
    m_audioSink->reset();
    m_audioSink->stop();
    delete m_audioSink;
    m_audioSink = nullptr;
    m_audioSinkDevice = nullptr;
}

void AudioEngine::setRxPacketFrameSamples(int samples) {
    const int packetMs = RxPlayout::packetMsForFrameSamples(samples);
    if (packetMs == m_rxPacketMs)
        return;
    m_rxPacketMs = packetMs;
    // The sink has to be rebuilt to take a new size - Qt ignores setBufferSize() after start(). The
    // queue is flushed with it; an SL change is an operator action, and the gap is one packet.
    if (m_outputRunning)
        rebuildOutput();
}

void AudioEngine::onSinkStateChanged(QtAudio::State state) {
    if (!m_audioSink)
        return;
    const QtAudio::Error error = m_audioSink->error();
    if (state == QtAudio::IdleState && error == QtAudio::UnderrunError) {
        // In push mode the sink goes Idle when it runs dry and back to Active by itself on the next
        // write, so there is nothing to restart - only to count. Not counted while transmitting
        // (ours or the radio's): the K4 stops sending RX then, and the sink running dry is expected.
        if (!rxPaused())
            m_rxUnderruns.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (state == QtAudio::ActiveState) {
        m_sinkErrorRebuilds = 0;
        return;
    }
    if (state == QtAudio::StoppedState && error != QtAudio::NoError && m_outputRunning) {
        // Any error other than an underrun stops the sink: the device went away or failed. Rebuild
        // it, on the next turn of the loop rather than inside the sink's own signal. Bounded, so a
        // device that fails every time does not spin; the next device-list change retries anyway.
        constexpr int kMaxErrorRebuilds = 3;
        qCWarning(lcAudio, "AudioEngine: output stopped with error %d%s", static_cast<int>(error),
                  m_sinkErrorRebuilds < kMaxErrorRebuilds ? " - rebuilding it" : " - giving up until devices change");
        if (m_sinkErrorRebuilds++ < kMaxErrorRebuilds)
            QMetaObject::invokeMethod(this, &AudioEngine::rebuildOutput, Qt::QueuedConnection);
    }
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
        qCWarning(lcAudio) << "AudioEngine: No audio output device available";
        return false;
    }

    if (!outputDevice.isFormatSupported(m_outputFormat)) {
        // No fallback yet: QK4 asks for the K4's native 12 kHz and takes no for an answer. Virtual
        // loopback drivers are commonly fixed at 44.1/48 kHz and land here, which means NO RX AUDIO
        // AT ALL from that device - not quiet audio, none. Say which device and what was asked for,
        // because the operator's next move is to pick a different one in Options and nothing else
        // in the UI tells them that.
        // One formatted string rather than a chain: Homebrew's clang-format 18 and CI's apt
        // clang-format-18 disagree about where to wrap a chain that reaches the 120-column limit,
        // so a chain here passes locally and fails CI. See CLAUDE.md -> Lint.
        qCWarning(lcAudio,
                  "AudioEngine: output device %s does not support %d Hz %d ch float - NO RX AUDIO from it. "
                  "Choose a different output device in Options.",
                  qUtf8Printable(outputDevice.description()), m_outputFormat.sampleRate(),
                  m_outputFormat.channelCount());
        return false;
    }

    // Logged on success for the same reason as the input side: which device we actually opened is
    // not recoverable from anywhere else, and on Bluetooth it decides whether the microphone can
    // work at all. A2DP (stereo playback) and HFP (microphone live) are mutually exclusive on one
    // Bluetooth device, so a sink held open on a headset can keep the OS from completing the
    // profile switch its own microphone needs.
    const QAudioFormat outPreferred = outputDevice.preferredFormat();
    qCInfo(lcAudio, "AudioEngine: output device \"%s\" prefers %d Hz %d ch; we request %d Hz %d ch Float",
           qUtf8Printable(outputDevice.description()), outPreferred.sampleRate(), outPreferred.channelCount(),
           m_outputFormat.sampleRate(), m_outputFormat.channelCount());

    m_activeOutputDeviceId = outputDevice.id();
    m_audioSink = new QAudioSink(outputDevice, m_outputFormat, this);
    const int requested = RxPlayout::sinkBufferBytes(m_rxPacketMs);
    m_audioSink->setBufferSize(requested);

    m_audioSinkDevice = m_audioSink->start();
    if (!m_audioSinkDevice) {
        qCWarning(lcAudio) << "AudioEngine: Failed to start audio output";
        delete m_audioSink;
        m_audioSink = nullptr;
        return false;
    }
    // Connected after start(): its own Idle-on-start transition carries no error and is not news.
    connect(m_audioSink, &QAudioSink::stateChanged, this, &AudioEngine::onSinkStateChanged);

    // What the backend actually granted. Qt: "It should not be assumed that the buffer size set is
    // the actual buffer size used". Larger means more latency than designed; smaller is handled by
    // the partial-write path in feedAudioDevice. Either way it belongs in a bench log.
    qCInfo(lcAudio, "AudioEngine: RX sink buffer %lld bytes (%lld ms) for %d ms packets; requested %d ms",
           static_cast<long long>(m_audioSink->bufferSize()),
           static_cast<long long>(m_audioSink->bufferSize() / RxPlayout::BYTES_PER_MS), m_rxPacketMs,
           requested / RxPlayout::BYTES_PER_MS);

    // Always 1.0: QK4's volume is applied per sample in applyMixAndVolume (the MAIN/SUB sliders).
    // The K4's AG is the radio's own speaker and does not reach this stream.
    m_audioSink->setVolume(1.0f);

    // WHY: every sink (re)build must restart the jitter buffer and drop any backlog. setupAudioOutput()
    // is called from start() AND the mid-stream rebuild paths (onSystemDefaultOutputChanged,
    // setOutputDevice); without this, a rebuild inherits the queue that piled up while the sink was
    // down (e.g. on the PTT-edge device churn) and that latency becomes permanent. Single source of
    // truth here so no caller can forget. (flushQueue empties the ring and resets the jitter buffer.)
    flushQueue();

    return true;
}

bool AudioEngine::enqueueAudio(const QByteArray &pcmData) {
    return enqueueAudio(reinterpret_cast<const float *>(pcmData.constData()),
                        static_cast<int>(pcmData.size() / sizeof(float)));
}

bool AudioEngine::enqueueAudio(const float *samples, int floats) {
    // I/O thread. Copy into the next free slot and publish it; nothing else. The jitter policy runs
    // on the consumer, in feedAudioDevice, where the sink's fill is known.
    floats -= floats % 2; // whole stereo frames
    if (floats <= 0)
        return true;
    RxPacket *slot = m_rxRing.beginWrite();
    if (!slot || floats > RxPacket::MAX_FLOATS) {
        // Full: the audio thread is not consuming (no sink, or stalled). Dropping the newest keeps
        // this thread wait-free, and whatever is queued is far older than the jitter target anyway.
        m_rxDroppedPackets.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    std::copy(samples, samples + floats, slot->samples.begin());
    slot->floats = floats;
    m_rxRing.commitWrite();
    return true;
}

void AudioEngine::popRxHead() {
    m_rxRing.pop();
    if (m_rxSeen > 0)
        --m_rxSeen;
    m_rxHeadOffset = 0;
    m_rxHeadMixed = false;
}

void AudioEngine::flushQueue() {
    // Audio thread: the ring is drained from the consumer side only.
    while (m_rxRing.front())
        popRxHead();
    m_rxSeen = 0;
    m_rxPauseTold = false;
    m_jitter.reset();
}

void AudioEngine::feedAudioDevice() {
    if (!m_audioSinkDevice)
        return;

    // The jitter buffer (audio/rxplayout.h) is driven in a fixed order, the same order the
    // simulation in tests/test_rxplayout.cpp drives it: sink first, then each new packet, then the
    // decision, then the corrections, then the writes.
    const std::int64_t nowUs = steadyNowUs();
    const int sinkSize = static_cast<int>(m_audioSink->bufferSize());
    int bytesFree = static_cast<int>(m_audioSink->bytesFree());
    int sinkFill = qMax(0, sinkSize - bytesFree);
    // A transmit edge, ours or the radio's: tell the jitter buffer before it sees this tick's packets.
    if (const bool paused = rxPaused(); paused != m_rxPauseTold) {
        if (paused)
            m_jitter.receivePaused();
        else
            m_jitter.receiveResumed();
        m_rxPauseTold = paused;
    }
    m_jitter.observeSink(sinkFill, nowUs);

    int queued = 0;
    if (!m_rxRing.empty()) {
        // Every packet the feed has not seen before, in order, with everything ahead of it.
        int through = sinkFill - m_rxHeadOffset * static_cast<int>(sizeof(float));
        for (std::size_t i = 0; const RxPacket *p = m_rxRing.peek(i); ++i) {
            const int bytes = p->floats * static_cast<int>(sizeof(float));
            through += bytes;
            if (i >= m_rxSeen)
                m_jitter.observePacket(bytes, through, nowUs);
        }
        m_rxSeen = m_rxRing.size();
        queued = through - sinkFill;

        const int pktBytes = m_rxRing.front()->floats * static_cast<int>(sizeof(float));
        m_jitter.decide(queued + sinkFill, pktBytes, nowUs);

        // Shrink or backlog trim: skip from the head of the queue, never from the sink (it plays).
        // Whole packets go as drops; a part of one moves the head's offset, like a partial write.
        int skip = m_jitter.takeSkip(queued);
        while (skip > 0) {
            const RxPacket *head = m_rxRing.front();
            const int remaining = (head->floats - m_rxHeadOffset) * static_cast<int>(sizeof(float));
            if (skip >= remaining) {
                skip -= remaining;
                queued -= remaining;
                popRxHead();
                m_rxDroppedPackets.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            m_rxHeadOffset += skip / static_cast<int>(sizeof(float));
            queued -= skip;
            skip = 0;
        }
    }

    // Grow: silence ahead of the next packet. Packets wait until all of it is in.
    while (m_jitter.pendingSilenceBytes() > 0 && bytesFree >= RxPlayout::FRAME_BYTES) {
        int n = qMin(qMin(m_jitter.pendingSilenceBytes(), bytesFree), kSilenceChunkBytes);
        n -= n % RxPlayout::FRAME_BYTES;
        const int written = static_cast<int>(qMax<qint64>(0, m_audioSinkDevice->write(kSilence, n)));
        if (written <= 0)
            break;
        m_jitter.silenceWritten(written);
        bytesFree -= written;
        sinkFill += written;
    }

    m_rxBufferedBytes.store(queued + sinkFill, std::memory_order_relaxed);

    // Write whole packets while they fit; a packet larger than the room left goes in part only when
    // the sink is about to starve (RxPlayout::bytesToWrite), and the rest waits at the ring's front.
    while (m_jitter.pendingSilenceBytes() == 0) {
        RxPacket *head = m_rxRing.front();
        if (!head)
            break;
        if (!m_rxHeadMixed) {
            applyMixAndVolume(head->samples.data(), head->floats / 2);
            m_rxHeadMixed = true;
        }
        const int remainingBytes = (head->floats - m_rxHeadOffset) * static_cast<int>(sizeof(float));
        const int toWrite = RxPlayout::bytesToWrite(remainingBytes, bytesFree, sinkFill);
        if (toWrite <= 0)
            break;
        const char *from = reinterpret_cast<const char *>(head->samples.data() + m_rxHeadOffset);
        const int written = static_cast<int>(qMax<qint64>(0, m_audioSinkDevice->write(from, toWrite)));
        bytesFree -= written;
        sinkFill += written;
        if (written < remainingBytes) {
            m_rxHeadOffset += written / static_cast<int>(sizeof(float));
            break;
        }
        popRxHead();
    }
    m_jitter.noteWritten(sinkFill);

    if (lcAudioRx().isDebugEnabled() &&
        ++m_playoutReportTicks >= 1000 / RxPlayout::FEED_INTERVAL_MS) { // once a second of ticks
        m_playoutReportTicks = 0;
        reportRxPlayout(queued + sinkFill);
    }
}

void AudioEngine::reportRxPlayout(int bufferedBytes) {
    if (AudioDiagnostics::RxPlayoutReport *r = m_playoutDiagnostics.beginWrite()) {
        r->jitterMs = m_jitter.jitterMs();
        r->targetMs = m_jitter.targetBytes() / RxPlayout::BYTES_PER_MS;
        r->devicePeriodMs = m_jitter.devicePeriodBytes() / RxPlayout::BYTES_PER_MS;
        r->devicePeriodMeasured = m_jitter.devicePeriodMeasured();
        r->bufferedMs = bufferedBytes / RxPlayout::BYTES_PER_MS;
        r->packetMs = m_rxPacketMs;
        r->silenceInserts = m_jitter.silenceInserts();
        r->shrinkSkips = m_jitter.shrinkSkips();
        r->backlogTrims = m_jitter.backlogTrims();
        r->stalls = m_jitter.stalls();
        r->inStall = m_jitter.inStall();
        m_playoutDiagnostics.commitWrite();
    }
}

bool AudioEngine::takeRxPlayoutReport(AudioDiagnostics::RxPlayoutReport &out) {
    const AudioDiagnostics::RxPlayoutReport *r = m_playoutDiagnostics.front();
    if (!r)
        return false;
    out = *r;
    m_playoutDiagnostics.pop();
    return true;
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

void AudioEngine::applyMixAndVolume(float *samples, int sampleCount) {
    // Load atomic/guarded values once per packet (not per sample)
    // The boost multiplies the sliders, so a saved slider position means what it always did.
    const float boost = m_rxBoostGain.load(std::memory_order_relaxed);
    const float mainVol = m_mainVolume.load(std::memory_order_relaxed) * boost;
    const float subVol = m_subVolume.load(std::memory_order_relaxed) * boost;
    // WHY only when boosted: at 0 dB the output is exactly what it was before the boost existed
    // (hard clamp), and the radio's stream - scaled to full scale by the decoder - does not exceed
    // it on its own. The boost is what pushes peaks over the top, so the boost brings the limiter.
    const bool limit = boost > 1.0f;
    auto finish = [limit](float v) { return limit ? RxOutputStage::softLimit(v) : qBound(-1.0f, v, 1.0f); };
    const bool subMuted = m_subMuted.load(std::memory_order_relaxed);
    const int balMode = m_balanceMode.load(std::memory_order_relaxed);
    const int balOffset = m_balanceOffset.load(std::memory_order_relaxed);

    const int mix = m_mix.load(std::memory_order_relaxed);
    const MixSource mixL = static_cast<MixSource>(mix & 0xff);
    const MixSource mixR = static_cast<MixSource>((mix >> 8) & 0xff);

    // Pre-compute BL balance gains (BAL mode only, applied after MX routing)
    float balLeftGain = 1.0f, balRightGain = 1.0f;
    if (balMode == 1) {
        balLeftGain = qBound(0.0f, (50.0f - balOffset) / 50.0f, 1.0f);
        balRightGain = qBound(0.0f, (50.0f + balOffset) / 50.0f, 1.0f);
    }

    const bool measure = lcAudioRx().isDebugEnabled();
    auto measureIn = [&](float mainSample, float subSample) {
        m_rxStats.sumSqMain += static_cast<double>(mainSample) * mainSample;
        m_rxStats.sumSqSub += static_cast<double>(subSample) * subSample;
        m_rxStats.peakMain = qMax(m_rxStats.peakMain, std::fabs(mainSample));
        m_rxStats.peakSub = qMax(m_rxStats.peakSub, std::fabs(subSample));
        ++m_rxStats.frames;
    };
    auto measureOut = [&](float left, float right) {
        const float peak = qMax(std::fabs(left), std::fabs(right));
        m_rxStats.peakOut = qMax(m_rxStats.peakOut, peak);
        m_rxStats.clipped += (std::fabs(left) > 1.0f) + (std::fabs(right) > 1.0f);
    };

    for (int i = 0; i < sampleCount; i++) {
        float mainSample = samples[i * 2];    // Left channel (Main RX / VFO A)
        float subSample = samples[i * 2 + 1]; // Right channel (Sub RX / VFO B)
        if (measure)
            measureIn(mainSample, subSample);

        // Step 1: SUB RX off — both channels get main audio only, sub slider has no effect
        // BL balance still applies (L/R gain is independent of SUB RX state)
        if (subMuted) {
            float s = mainSample * mainVol;
            if (measure)
                measureOut(s * balLeftGain, s * balRightGain);
            samples[i * 2] = finish(s * balLeftGain);
            samples[i * 2 + 1] = finish(s * balRightGain);
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

        // Step 4: Clamp, or soft-limit when boosted
        if (measure)
            measureOut(left, right);
        samples[i * 2] = finish(left);
        samples[i * 2 + 1] = finish(right);
    }

    if (measure && m_rxStats.frames >= m_outputFormat.sampleRate())
        reportRxLevels(mainVol / boost, subVol / boost, boost);
}

void AudioEngine::reportRxLevels(float mainVol, float subVol, float boost) {
    if (AudioDiagnostics::RxLevelReport *r = m_rxDiagnostics.beginWrite()) {
        const double n = static_cast<double>(m_rxStats.frames);
        r->peakMain = m_rxStats.peakMain;
        r->rmsMain = static_cast<float>(std::sqrt(m_rxStats.sumSqMain / n));
        r->peakSub = m_rxStats.peakSub;
        r->rmsSub = static_cast<float>(std::sqrt(m_rxStats.sumSqSub / n));
        r->peakOut = m_rxStats.peakOut;
        r->clipped = m_rxStats.clipped;
        r->mainSlider = mainVol;
        r->subSlider = subVol;
        r->boost = boost;
        m_rxDiagnostics.commitWrite();
    }
    m_rxStats = RxLevelStats{};
}

bool AudioEngine::takeRxLevelReport(AudioDiagnostics::RxLevelReport &out) {
    const AudioDiagnostics::RxLevelReport *r = m_rxDiagnostics.front();
    if (!r)
        return false;
    out = *r;
    m_rxDiagnostics.pop();
    return true;
}

void AudioEngine::setMainVolume(float volume) {
    m_mainVolume.store(qBound(0.0f, volume, 1.0f), std::memory_order_relaxed);
}

void AudioEngine::setRxBoostDb(int db) {
    m_rxBoostGain.store(RxOutputStage::boostGain(db), std::memory_order_relaxed);
}

void AudioEngine::setSubVolume(float volume) {
    m_subVolume.store(qBound(0.0f, volume, 1.0f), std::memory_order_relaxed);
}

void AudioEngine::setSubMuted(bool muted) {
    m_subMuted.store(muted, std::memory_order_relaxed);
}

void AudioEngine::setAudioMix(int left, int right) {
    m_mix.store(packMix(static_cast<MixSource>(qBound(0, left, 3)), static_cast<MixSource>(qBound(0, right, 3))),
                std::memory_order_relaxed);
}

void AudioEngine::setBalanceMode(int mode) {
    m_balanceMode.store(qBound(0, mode, 1), std::memory_order_relaxed);
}

void AudioEngine::setBalanceOffset(int offset) {
    m_balanceOffset.store(qBound(-50, offset, 50), std::memory_order_relaxed);
}

void AudioEngine::setOutputDevice(const QString &deviceId) {
    if (m_selectedOutputDeviceId == deviceId) {
        return;
    }
    m_selectedOutputDeviceId = deviceId;

    // Unconditionally, NOT `if (m_audioSink)`. Picking a working device in Options is exactly how
    // an operator recovers from a device that could not be opened, so the one case that most needs
    // to rebuild is the one where there is nothing to tear down.
    rebuildOutput();
}

void AudioEngine::onSystemDefaultOutputChanged() {
    bool pinnedPresent = false;
    if (!m_selectedOutputDeviceId.isEmpty()) {
        for (const QAudioDevice &d : QMediaDevices::audioOutputs()) {
            if (d.id() == m_selectedOutputDeviceId) {
                pinnedPresent = true;
                break;
            }
        }
    }

    // With no sink we are in the failed state, and any list change is a chance to get out of it -
    // so `haveStream` is reported as true only when one exists, and the no-sink case is handled
    // below by rebuilding regardless of the decision.
    const auto decision = AudioDeviceSelection::onDeviceListChanged(
        m_selectedOutputDeviceId.toStdString(), m_activeOutputDeviceId.toStdString(),
        QMediaDevices::defaultAudioOutput().id().toStdString(), pinnedPresent, m_audioSink != nullptr);

    qCInfo(lcAudio, "AudioEngine: output device list changed (selection=\"%s\", active=\"%s\") -> %s",
           m_selectedOutputDeviceId.isEmpty() ? "system default" : qUtf8Printable(m_selectedOutputDeviceId),
           qUtf8Printable(m_activeOutputDeviceId), decision.reason);

    if (!m_outputRunning)
        return; // not producing output yet - start() will resolve the current default

    if (m_audioSink && decision.action != AudioDeviceSelection::Action::SwitchTo)
        return;

    rebuildOutput();
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
