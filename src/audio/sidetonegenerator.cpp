#include "sidetonegenerator.h"
#include "audio/audiodeviceselection.h"
#include "logging/logcategories.h"
#include "audio/sidetonescheduler.h"
#include "utils/rtscheduling.h"
#include <QAudioFormat>
#include <QMediaDevices>
#include <QTimer>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <numeric>

namespace {
constexpr double kPi = 3.14159265358979323846; // not M_PI: MSVC hides it behind _USE_MATH_DEFINES
} // namespace

SidetoneGenerator::SidetoneGenerator(SidetoneScheduler &scheduler, CwKeying::Clock &clock, QObject *parent)
    : QObject(parent), m_scheduler(scheduler), m_clock(clock) {
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
    if (m_audioSink) {
        qCWarning(lcAudio) << "SidetoneGenerator: audio sink not cleaned up by stop() — destroying from wrong thread";
        m_streamActive = false;
        delete m_audioSink;
        m_audioSink = nullptr;
    }
}

std::optional<double> SidetoneGenerator::streamTime() const {
    if (!m_streamActive.load(std::memory_order_acquire))
        return std::nullopt;
    return m_clock.now();
}

void SidetoneGenerator::setOutputDevice(const QString &deviceId) {
    if (m_selectedOutputDeviceId == deviceId)
        return;
    m_selectedOutputDeviceId = deviceId;
    if (m_started) {
        closeAudio();
        initAudio();
    }
}

void SidetoneGenerator::initAudio() {
    if (!m_started || !m_streamWanted || m_audioSink)
        return;
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
        qCWarning(lcAudio) << "SidetoneGenerator: No audio output device";
        return;
    }

    QAudioFormat format;
    format.setSampleRate(48000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Float);
    if (!device.isFormatSupported(format)) {
        format.setSampleFormat(QAudioFormat::Int16);
        if (!device.isFormatSupported(format)) {
            // WHY the device's own format, rendered as it asks: the old generator switched to
            // preferredFormat() but kept writing 48 kHz mono Int16 into it. Every rate, channel
            // count and sample format below is rendered to what was actually opened.
            format = device.preferredFormat();
        }
    }
    m_format = format;
    m_rate = format.sampleRate();
    if (m_rate <= 0 || format.bytesPerFrame() <= 0) {
        qCWarning(lcAudio) << "SidetoneGenerator: unusable output format" << format;
        return;
    }
    m_blockFrames = std::max(1, static_cast<int>(std::lround(m_rate * kBlockSeconds)));
    m_riseStep = 1.0 / std::max(1L, std::lround(m_rate * kRiseSeconds));
    m_fallStep = 1.0 / std::max(1L, std::lround(m_rate * kFallSeconds));
    m_scheduler.setMinimumSidetoneMs(static_cast<int>(std::lround((kRiseSeconds * .5 + kFallSeconds * .5) * 1000)));
    m_keyed = false;
    m_envelope = 0.0;
    m_framesWritten = 0;
    m_targetQueue = kTargetQueueSeconds;
    m_toneFrequency = 0;
    m_liveTone.reset();
    rebuildTone();

    m_audioSink = new QAudioSink(device, format, this);
    m_audioSink->setBufferSize(
        static_cast<qsizetype>(format.bytesForDuration(static_cast<qint64>(kBufferSeconds * 1e6))));
    // A new stream: every timing estimate from the old one is void.
    m_scheduler.clear();
    m_pushDevice = m_audioSink->start();
    if (!m_pushDevice || m_audioSink->error() != QAudio::NoError) {
        qCWarning(lcAudio) << "SidetoneGenerator: Failed to start audio sink:" << m_audioSink->error();
        m_streamActive.store(false, std::memory_order_release);
        m_pushDevice = nullptr;
        closeAudio();
    } else {
        m_streamActive.store(true, std::memory_order_release);
        if (!m_pumpTimer) {
            m_pumpTimer = new QTimer(this);
            m_pumpTimer->setTimerType(Qt::PreciseTimer);
            m_pumpTimer->setInterval(1);
            connect(m_pumpTimer, &QTimer::timeout, this, &SidetoneGenerator::pump);
        }
        m_pumpTimer->start();
        pump();
        qCInfo(lcAudio) << "SidetoneGenerator: sidetone on" << device.description() << format << "buffer"
                        << m_audioSink->bufferSize()
                        << "bytes =" << format.durationForBytes(m_audioSink->bufferSize()) / 1000.0 << "ms";
    }
}

void SidetoneGenerator::closeAudio() {
    m_streamActive.store(false, std::memory_order_release);
    if (m_pumpTimer)
        m_pumpTimer->stop();
    m_pushDevice = nullptr;
    if (m_audioSink) {
        m_audioSink->reset();
        delete m_audioSink;
        m_audioSink = nullptr;
    }
    m_scheduler.clear();
    m_liveTone.reset();
}

void SidetoneGenerator::start() {
    RtScheduling::promoteCurrentThread(RtScheduling::Threads::kSidetone);
    m_started = true;
    initAudio();
}

void SidetoneGenerator::stop() {
    m_streamWanted = false;
    m_started = false;
    closeAudio();
    m_liveMode = false;
    m_activeOutputDeviceId.clear();
}

void SidetoneGenerator::setStreamWanted(bool wanted) {
    if (wanted == m_streamWanted)
        return;
    m_streamWanted = wanted;
    if (wanted) {
        initAudio();
    } else {
        closeAudio();
        qCInfo(lcAudio) << "SidetoneGenerator: sidetone off until CW keying is possible again";
    }
}

void SidetoneGenerator::onSystemDefaultOutputChanged() {
    bool pinnedPresent = false;
    if (!m_selectedOutputDeviceId.isEmpty()) {
        for (const QAudioDevice &d : QMediaDevices::audioOutputs()) {
            if (d.id() == m_selectedOutputDeviceId) {
                pinnedPresent = true;
                break;
            }
        }
    }

    // Same decision as AudioEngine's output: follow the OS default when nothing is pinned, and
    // reclaim a pinned device the moment it reappears. See audio/audiodeviceselection.h.
    const auto decision = AudioDeviceSelection::onDeviceListChanged(
        m_selectedOutputDeviceId.toStdString(), m_activeOutputDeviceId.toStdString(),
        QMediaDevices::defaultAudioOutput().id().toStdString(), pinnedPresent, m_audioSink != nullptr);

    qCDebug(lcAudio, "SidetoneGenerator: output device list changed (selection=\"%s\", active=\"%s\") -> %s",
            m_selectedOutputDeviceId.isEmpty() ? "system default" : qUtf8Printable(m_selectedOutputDeviceId),
            qUtf8Printable(m_activeOutputDeviceId), decision.reason);

    if (decision.action != AudioDeviceSelection::Action::SwitchTo)
        return;
    closeAudio();
    initAudio();
}

void SidetoneGenerator::setKeyDown(bool down) {
    const auto timestampNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    setKeyDownAt(down, timestampNs);
}

void SidetoneGenerator::setKeyDownAt(bool down, qint64 timestampNs) {
    if (!m_started || !m_streamWanted)
        return;
    if (!m_liveMode) {
        closeAudio();
        m_liveMode = true;
        initAudio();
    } else if (down && (!m_audioSink || m_audioSink->error() != QAudio::NoError)) {
        closeAudio();
        initAudio();
    }
    if (!m_liveTone.setKeyDown(down, timestampNs))
        qCWarning(lcAudio) << "SidetoneGenerator: Live edge queue overflowed; silencing output";
}

void SidetoneGenerator::resetKeyedTone() {
    m_liveTone.reset();
    if (!m_liveMode)
        return;
    closeAudio();
    m_liveMode = false;
    // WHY defer: a source reset is often followed immediately by setKeyDown(false) to prime
    // Vail feedback. Let that open the stream once; otherwise resume scheduled paddle audio.
    if (m_started)
        QMetaObject::invokeMethod(this, [this]() { initAudio(); }, Qt::QueuedConnection);
}

void SidetoneGenerator::setFrequency(int hz) {
    m_frequency.store(hz, std::memory_order_relaxed);
    m_liveTone.setFrequency(hz);
}

void SidetoneGenerator::setVolume(float volume) {
    m_volume.store(volume, std::memory_order_relaxed);
    m_liveTone.setVolume(volume);
}

void SidetoneGenerator::rebuildTone() {
    const int pitch = std::max(1, m_frequency.load(std::memory_order_relaxed));
    if (pitch == m_toneFrequency && !m_cycle.empty())
        return;
    m_toneFrequency = pitch;
    // One exact period: rate / gcd(rate, pitch) samples, so the table wraps with no phase error.
    const int period = m_rate / std::gcd(m_rate, pitch);
    m_cycle.resize(static_cast<size_t>(period));
    for (int i = 0; i < period; ++i)
        m_cycle[static_cast<size_t>(i)] =
            static_cast<float>(std::sin(2 * kPi * pitch * static_cast<double>(i) / m_rate));
    m_cycleIndex %= m_cycle.size();
}

void SidetoneGenerator::renderBlock(float *out, int frames, double dacTime, double now) {
    if (m_liveMode) {
        m_liveTone.renderFloat(out, frames, m_rate);
        return;
    }
    m_scheduler.noteBlockTiming(dacTime, now, static_cast<double>(frames) / m_rate);
    const bool idle = !m_keyed && m_envelope <= 0 && !m_scheduler.pendingEdgeHeld();
    if (idle)
        m_scheduler.easeLag(static_cast<double>(frames) / m_rate);
    if (idle && m_scheduler.queueEmpty()) {
        std::fill(out, out + frames, 0.0f);
        return;
    }
    m_positions.resize(static_cast<size_t>(frames));
    int start = 0;
    while (start < frames) {
        const auto edge = m_scheduler.nextKeyEdge(start, frames, dacTime, m_rate);
        const int end = edge ? edge->offset : frames;
        const double step = m_keyed ? m_riseStep : -m_fallStep;
        for (int i = start; i < end; ++i) {
            m_envelope = std::clamp(m_envelope + step, 0.0, 1.0);
            m_positions[static_cast<size_t>(i)] = static_cast<float>(m_envelope);
        }
        if (!edge)
            break;
        m_keyed = edge->down;
        start = end;
    }
    rebuildTone();
    const float volume = m_volume.load(std::memory_order_relaxed);
    for (int i = 0; i < frames; ++i) {
        const double env = .5 * (1 - std::cos(kPi * m_positions[static_cast<size_t>(i)]));
        out[i] = m_cycle[m_cycleIndex] * static_cast<float>(env) * volume;
        m_cycleIndex = (m_cycleIndex + 1) % m_cycle.size();
    }
}

void SidetoneGenerator::convert(const float *in, int frames, char *dst) const {
    const int channels = m_format.channelCount();
    const auto sampleFormat = m_format.sampleFormat();
    const int bytesPerSample = m_format.bytesPerSample();
    for (int i = 0; i < frames; ++i) {
        const float v = in[i];
        for (int c = 0; c < channels; ++c) {
            char *p = dst + (static_cast<qint64>(i) * channels + c) * bytesPerSample;
            switch (sampleFormat) {
            case QAudioFormat::Float:
                std::memcpy(p, &v, sizeof v);
                break;
            case QAudioFormat::Int16: {
                const auto s = static_cast<qint16>(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f));
                std::memcpy(p, &s, sizeof s);
                break;
            }
            case QAudioFormat::Int32: {
                const auto s = static_cast<qint32>(std::llround(std::clamp(v, -1.0f, 1.0f) * 2147483647.0));
                std::memcpy(p, &s, sizeof s);
                break;
            }
            case QAudioFormat::UInt8: {
                const auto s = static_cast<quint8>(std::lround(std::clamp(v, -1.0f, 1.0f) * 127.0f + 128.0f));
                std::memcpy(p, &s, sizeof s);
                break;
            }
            default:
                std::memset(p, 0, static_cast<size_t>(bytesPerSample));
                break;
            }
        }
    }
}

// WHY push, a block at a time, instead of letting the sink pull: in pull mode Qt on macOS asked for
// up to ~85 ms per read even with a 20 ms buffer, and all of it was queued to play. The scheduler
// rightly held the delay above the worst of that, so every tone sounded ~100 ms after the paddle.
// Pushing lets the sidetone decide how far ahead it is: on a 1 ms timer, top the sink up only
// while less than kTargetQueueSeconds is waiting in it, one 4 ms block per write. The lead then
// stays between the target and the target plus a block, whatever the backend would have preferred.
void SidetoneGenerator::pump() {
    if (!m_audioSink || !m_pushDevice || m_rate <= 0)
        return;
    const int bytesPerFrame = m_format.bytesPerFrame();
    if (bytesPerFrame <= 0)
        return;
    const double bytesPerSecond = static_cast<double>(m_format.bytesForDuration(1000000));
    const qsizetype blockBytes = static_cast<qsizetype>(m_blockFrames) * bytesPerFrame;
    m_scratch.resize(static_cast<size_t>(m_blockFrames));
    m_bytes.resize(blockBytes);
    while (true) {
        const qsizetype freeBytes = m_audioSink->bytesFree();
        const double queued = std::max<qsizetype>(0, m_audioSink->bufferSize() - freeBytes) / bytesPerSecond;
        if (queued >= m_targetQueue || freeBytes < blockBytes)
            return;
        // WHY adapt: the device takes audio in its own period (about 11 ms on a Mac mini's
        // speakers), and a queue shorter than that period runs dry every cycle, which is an
        // audible dropout. Each time it runs dry, keep one more block queued, up to the sink's
        // capacity. It settles at the shortest queue this device sustains.
        if (queued == 0 && m_framesWritten > 0) {
            const double blockSeconds = static_cast<double>(m_blockFrames) / m_rate;
            const double cap = m_audioSink->bufferSize() / bytesPerSecond - blockSeconds;
            if (m_targetQueue + blockSeconds <= cap + 1e-9) {
                m_targetQueue += blockSeconds;
                qCInfo(lcAudio).nospace()
                    << "SidetoneGenerator: output ran dry, now keeping " << m_targetQueue * 1000 << " ms queued";
            } else {
                qCDebug(lcAudioSidetone) << "output ran dry at the sink's capacity";
            }
        }
        const double now = m_clock.now();
        // Play-out time of this block's first frame: everything still queued plays first. The
        // device's own fixed latency beyond that is the same for every block, which is all the
        // scheduler needs.
        renderBlock(m_scratch.data(), m_blockFrames, now + queued, now);
        convert(m_scratch.data(), m_blockFrames, m_bytes.data());
        const qint64 written = m_pushDevice->write(m_bytes.constData(), blockBytes);
        if (written < blockBytes) {
            qCDebug(lcAudioSidetone) << "short write" << written << "of" << blockBytes;
            return;
        }
        m_framesWritten += m_blockFrames;
    }
}
