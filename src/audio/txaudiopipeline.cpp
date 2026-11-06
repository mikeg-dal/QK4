#include "audio/txaudiopipeline.h"
#include "audio/audiodecimator.h"       // capture-rate decimation
#include "audio/audiodeviceselection.h" // which device a stream belongs on
#include "audio/audiodiagnostics.h"     // qk4.audio.tx records, formatted off this thread
#include "audio/micrate.h"              // what the operator is told when a rate is unusable
#include "audio/rawaudioformat.h"       // EM0/EM1 wire format
#include "logging/logcategories.h"
#include "network/protocol.h" // buildAudioPacket
#include <QAudioDevice>
#include <chrono>
#include <cmath>

namespace {
qint64 steadyNowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
} // namespace

TxAudioPipeline::TxAudioPipeline(QObject *parent) : QObject(parent) {
    // Opus encoder for TX. 12 kHz mono - K4 expects mono frames (mono -> stereo duplication for
    // EM0/EM1 happens in RawAudioFormat; the Opus encoder handles EM2/EM3).
    m_opusEncoder.initialize(12000, 1);

    // Input format TEMPLATE. Only the channel count and sample format are ours to choose; the
    // SAMPLE RATE here is a placeholder that setupAudioInput() overwrites with whatever the chosen
    // device actually runs at. Demanding a fixed rate is what broke Bluetooth microphones - see
    // audio/audiodecimator.h for the measurement.
    m_inputFormat.setSampleRate(48000);
    m_inputFormat.setChannelCount(1);
    m_inputFormat.setSampleFormat(QAudioFormat::Float);

    // Children, so moveToThread() carries them to the TX thread.
    m_micPollTimer = new QTimer(this);
    m_micPollTimer->setInterval(10);
    connect(m_micPollTimer, &QTimer::timeout, this, &TxAudioPipeline::onMicDataReady);

    // m_micBuffer holds 12 kHz Float32 samples queued up to one max frame (SL7 = 1440 samples);
    // 2x that gives headroom for partial frames plus the next poll's data before we compact.
    // m_resampleBuf12k holds one poll of converter output, sized for the worst case: the whole
    // input buffer (a 12 kHz device passes through 1:1).
    m_micBuffer.reserve(2 * 1440 * static_cast<int>(sizeof(float)));
    m_resampleBuf12k.reserve(INPUT_BUFFER_SIZE);

    m_mediaDevices = new QMediaDevices(this);
    connect(m_mediaDevices, &QMediaDevices::audioInputsChanged, this, &TxAudioPipeline::onSystemDefaultInputChanged);
}

TxAudioPipeline::~TxAudioPipeline() {
    stop();
}

void TxAudioPipeline::stop() {
    // AUD-001. Transmit state belongs to the connection and must not outlive it: a PTT left set
    // would come back up on reconnect with the gate open, and a source left on Tci would make the
    // operator's next PTT transmit silence.
    m_awaitingFirstPacket = false;
    m_pttActive.store(false, std::memory_order_release);
    m_txSource.store(static_cast<int>(TxSource::Microphone), std::memory_order_release);
    closeMic();
    delete m_audioSource;
    m_audioSource = nullptr;
    m_audioSourceDevice = nullptr;
    m_micBuffer.clear();
    m_micReadOffset = 0;
}

void TxAudioPipeline::setOutputDeviceHint(const QString &deviceId) {
    m_outputDeviceHint = deviceId;
}

bool TxAudioPipeline::setupAudioInput() {
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
        qCWarning(lcAudio) << "TxAudioPipeline: No audio input device available";
        reportMicUnavailable(MicRate::noMicrophone());
        return false;
    }

    // Capture at the rate the DEVICE reports, not one of our choosing.
    //
    // WHY (INT-005): asking for a fixed 48 kHz is what broke Bluetooth microphones. An AirPods Pro
    // runs at 24 kHz in HFP; Qt's isFormatSupported() says 48 kHz is fine for it, the stream opens
    // without error, and then delivers NOTHING - measured, twice, alternating formats to rule out a
    // warm-up effect (tools/audio_device_probe.cpp). The device's own preferred rate is the only
    // one it can be trusted to actually produce.
    //
    // Channels and sample format are still ours to pick: mono Float captured cleanly on every
    // device probed, including one whose preferredFormat() was stereo Int16.
    const QAudioFormat preferred = inputDevice.preferredFormat();
    QAudioFormat format = m_inputFormat;
    format.setSampleRate(preferred.sampleRate());

    // A rate that is not an exact multiple of 12 kHz goes through the rational resampler instead of
    // being refused. It used to be refused outright, which meant no capture, no TX audio, and -
    // because a local PTT press keys the K4 by streaming audio - no transmission whatever. 44.1 kHz
    // is the default for most consumer audio, so that was a dead PTT for a great many operators.
    //
    // The integer path is kept for the rates that already worked. See audioresampler.h for why
    // 48 kHz and 24 kHz deliberately do not change.
    if (!AudioDecimator::isSupportedRate(format.sampleRate())) {
        if (!m_micResampler.configure(format.sampleRate(), AudioDecimator::OUTPUT_RATE)) {
            qCWarning(lcAudio,
                      "TxAudioPipeline: input device \"%s\" reported %d Hz, which cannot be resampled to "
                      "%d Hz - NO TX AUDIO from it. Choose a different microphone in Options.",
                      qUtf8Printable(inputDevice.description()), format.sampleRate(), AudioDecimator::OUTPUT_RATE);
            // ...and to the operator, not only to the log. This refusal is total - PTT appears dead -
            // so it is the one audio fault that has to reach the screen.
            reportMicUnavailable(MicRate::problem(format.sampleRate()));
            return false;
        }
        qCInfo(lcAudio, "TxAudioPipeline: input device \"%s\" runs at %d Hz; resampling %d/%d to %d Hz",
               qUtf8Printable(inputDevice.description()), format.sampleRate(), m_micResampler.interpolation(),
               m_micResampler.decimation(), AudioDecimator::OUTPUT_RATE);
    }

    if (!inputDevice.isFormatSupported(format)) {
        qCWarning(lcAudio,
                  "TxAudioPipeline: input device \"%s\" refused %d Hz %d ch float - NO TX AUDIO from it. "
                  "It reports %d..%d Hz. Choose a different microphone in Options.",
                  qUtf8Printable(inputDevice.description()), format.sampleRate(), format.channelCount(),
                  inputDevice.minimumSampleRate(), inputDevice.maximumSampleRate());
        reportMicUnavailable(MicRate::micWouldNotOpen(inputDevice.description()));
        return false;
    }

    m_micSampleRate = format.sampleRate();
    m_micDecimationFactor = AudioDecimator::factorFor(m_micSampleRate);
    // The conversion named as whichever one is actually running. This printed "%d:1" with
    // m_micDecimationFactor unconditionally, which read as "0:1" on the resampler path - the factor
    // is legitimately zero there, but a log line saying a decimation factor of zero is how someone
    // loses an afternoon to a conversion that was working.
    const QString conversion =
        (m_micDecimationFactor > 0)
            ? QStringLiteral("%1:1").arg(m_micDecimationFactor)
            : QStringLiteral("resampled %1/%2").arg(m_micResampler.interpolation()).arg(m_micResampler.decimation());
    qCInfo(lcAudio, "TxAudioPipeline: mic device \"%s\" prefers %d Hz %d ch; capturing at %d Hz mono Float, %s to %d",
           qUtf8Printable(inputDevice.description()), preferred.sampleRate(), preferred.channelCount(), m_micSampleRate,
           qUtf8Printable(conversion), AudioDecimator::OUTPUT_RATE);

    m_activeMicDeviceId = inputDevice.id();
    m_audioSource = new QAudioSource(inputDevice, format, this);
    m_audioSource->setBufferSize(INPUT_BUFFER_SIZE);

    // Don't start mic by default - user must enable
    return true;
}

void TxAudioPipeline::openMic() {
    if (m_micOpen && m_audioSourceDevice)
        return;

    // The QAudioSource object is built once per device and kept; only start()/stop() happen per
    // transmission. That keeps the device enumeration and format checks off every PTT press.
    if (!m_audioSource) {
        if (!setupAudioInput()) {
            qCWarning(lcAudio) << "TxAudioPipeline: Failed to setup audio input";
            return;
        }
    }

    m_audioSourceDevice = m_audioSource->start();
    if (!m_audioSourceDevice) {
        qCWarning(lcAudio, "TxAudioPipeline: failed to start microphone device (QAudioSource error %d) - NO TX AUDIO",
                  static_cast<int>(m_audioSource->error()));
        QString name;
        for (const QAudioDevice &device : QMediaDevices::audioInputs()) {
            if (device.id() == m_activeMicDeviceId)
                name = device.description();
        }
        reportMicUnavailable(MicRate::micWouldNotOpen(name));
        return;
    }

    // The format the source actually runs at, which need not be the one we asked for: Qt's FFmpeg
    // backend resamples, so a request can succeed against a device whose hardware rate is quite
    // different. Logging it distinguishes "we are getting what we asked for" from "the backend is
    // quietly converting", and the frame-level qk4.audio.tx peaks then say whether any audio is
    // actually arriving.
    // Debug, not info: this now happens on every PTT press. setupAudioInput() already logged the
    // device and the rate once at info.
    const QAudioFormat actual = m_audioSource->format();
    qCDebug(lcAudio, "TxAudioPipeline: mic started at %d Hz %d ch, buffer %lld bytes", actual.sampleRate(),
            actual.channelCount(), static_cast<long long>(m_audioSource->bufferSize()));

    m_micOpen = true;
    m_micSilenceTimer.invalidate();
    m_micSilenceReported = false;
    // Use timer-based polling instead of readyRead signal
    // (readyRead doesn't fire reliably on all platforms).
    m_micPollTimer->start();
}

void TxAudioPipeline::closeMic() {
    if (!m_micOpen)
        return;

    // stop() "detach[es] from the system resource" (QAudioSource docs), which is what turns the
    // OS microphone indicator off.
    m_micOpen = false;
    m_micPollTimer->stop();
    if (m_audioSource) {
        m_audioSource->stop();
    }
    m_audioSourceDevice = nullptr;
    m_micBuffer.clear();
    m_micReadOffset = 0;
}

void TxAudioPipeline::flushMicBuffer() {
    m_micBuffer.clear();
    m_micReadOffset = 0;
    // The resampler's filter history is part of the half-buffered audio being discarded here. Left
    // in place it would carry the tail of the departing source into the first samples of the next.
    m_micResampler.reset();
}

const QByteArray &TxAudioPipeline::resampleTo12k(const QByteArray &input, int factor) {
    // Writes into the pre-allocated m_resampleBuf12k member; resize() at the pre-reserved
    // capacity is alloc-free.
    const float *inputSamples = reinterpret_cast<const float *>(input.constData());
    const int inputCount = static_cast<int>(input.size() / sizeof(float));
    const int outputCount = AudioDecimator::outputSampleCount(inputCount, factor);

    m_resampleBuf12k.resize(outputCount * static_cast<int>(sizeof(float)));
    AudioDecimator::decimate(inputSamples, inputCount, factor, reinterpret_cast<float *>(m_resampleBuf12k.data()));
    return m_resampleBuf12k;
}

const QByteArray &TxAudioPipeline::rationalResampleTo12k(const QByteArray &input) {
    const float *inputSamples = reinterpret_cast<const float *>(input.constData());
    const int inputCount = static_cast<int>(input.size() / sizeof(float));

    // maxOutputFor is deliberately generous: the exact count depends on the phase carried in from
    // the previous block, so the buffer is sized to the bound and then trimmed to what was written.
    const int capacity = m_micResampler.maxOutputFor(inputCount);
    m_resampleBuf12k.resize(capacity * static_cast<int>(sizeof(float)));
    const int produced =
        m_micResampler.process(inputSamples, inputCount, reinterpret_cast<float *>(m_resampleBuf12k.data()));
    m_resampleBuf12k.resize(produced * static_cast<int>(sizeof(float)));
    return m_resampleBuf12k;
}

void TxAudioPipeline::reportMicUnavailable(const QString &reason) {
    if (txSource() != TxSource::Microphone)
        return;
    emit micUnavailable(reason);
}

void TxAudioPipeline::setTxSource(TxSource source) {
    const int previous = m_txSource.exchange(static_cast<int>(source), std::memory_order_release);
    if (previous == static_cast<int>(source)) {
        return;
    }
    // Whatever is half-buffered belongs to the old source; carrying it across would splice two
    // unrelated streams into one transmission.
    //
    // WHY queued rather than a direct call: m_micBuffer and m_micReadOffset are plain members owned
    // by the TX thread, and this setter is called from whichever thread owns the PTT decision.
    // Only the atomic above may be written from here. The microphone follows the source on the same
    // turn: an operator taking over from a TCI client mid-transmission needs it opened, a handback
    // to TCI needs it closed.
    QMetaObject::invokeMethod(
        this,
        [this]() {
            flushMicBuffer();
            reconcileMic();
        },
        Qt::QueuedConnection);
}

void TxAudioPipeline::setMicAccess(MicAccess access) {
    const int previous = m_micAccess.exchange(static_cast<int>(access), std::memory_order_acq_rel);
    if (previous == static_cast<int>(access)) {
        return;
    }
    // Queued for the same reason as setTxSource(): the microphone state is the TX thread's.
    QMetaObject::invokeMethod(this, [this]() { reconcileMic(); }, Qt::QueuedConnection);
}

void TxAudioPipeline::reconcileMic() {
    const bool wanted = m_pttActive.load(std::memory_order_relaxed) && txSource() == TxSource::Microphone;
    if (wanted && !m_micOpen && micAccess() != MicAccess::Granted) {
        // Undetermined: the operator is looking at the OS prompt; the answer arrives through
        // setMicAccess() and reconciles again. Denied: say so, because PTT now does nothing.
        if (micAccess() == MicAccess::Denied) {
            qCWarning(lcAudio) << "TxAudioPipeline: microphone access denied by the OS - NO TX AUDIO";
            reportMicUnavailable(MicRate::micAccessDenied());
        }
        return;
    }
    if (wanted && !m_micOpen) {
        QElapsedTimer opening;
        opening.start();
        openMic();
        m_micOpenUs += opening.nsecsElapsed() / 1000;
    } else if (!wanted && m_micOpen) {
        closeMic();
    }
}

void TxAudioPipeline::feedTciTxAudio(const QByteArray &f32Mono48k) {
    // Ignored unless TCI owns the transmitter. A stale frame arriving after the operator took the
    // microphone back must not reach the radio.
    if (txSource() != TxSource::Tci || f32Mono48k.isEmpty()) {
        return;
    }
    // WHY a fixed factor, not the microphone's: TCI audio is 48 kHz whatever device the mic opened
    // on. The mic's factor is 2 for a 24 kHz headset (twice the samples: wrong pitch and timing)
    // and 0 if the mic failed to open (silence).
    static constexpr int TCI_DECIMATION_FACTOR = AudioDecimator::factorFor(48000);
    const QByteArray &data12k = resampleTo12k(f32Mono48k, TCI_DECIMATION_FACTOR);
    // The TCI level, NOT the microphone's. An operator-facing level is required here rather than
    // optional - WSJT-X sends at or near full scale and drove the K4 far too hard without one -
    // but it has to be its own, because the cubic curve puts a microphone and a line-level digital
    // source in completely different parts of the slider.
    bufferAndEmitTxFrames(data12k, m_tciTxGain.load(std::memory_order_relaxed));
}

void TxAudioPipeline::onMicDataReady() {
    if (!m_audioSourceDevice || !m_micOpen)
        return;

    QByteArray data48k = m_audioSourceDevice->readAll();
    if (data48k.isEmpty()) {
        // An empty read is normal between polls. Silence that PERSISTS while the operator is
        // keying is not - see the watchdog members for why this is worth distinguishing.
        if (m_pttActive.load(std::memory_order_acquire) && txSource() == TxSource::Microphone) {
            if (!m_micSilenceTimer.isValid()) {
                m_micSilenceTimer.start();
            } else if (!m_micSilenceReported && m_micSilenceTimer.elapsed() > MIC_SILENCE_WARN_MS) {
                m_micSilenceReported = true;

                // ON SCREEN, not only here. A local PTT press keys the K4 by streaming audio, so a
                // microphone delivering nothing is a PTT button that does nothing at all - and an
                // operator with no explanation reports a dead radio rather than acting on it.
                //
                // QK4 CANNOT RECOVER FROM THE LIKELIEST CAUSE, which is why the message names a
                // restart. Qt snapshots a device's preferredFormat() when it first enumerates and
                // never refreshes it for a format-only change; macOS fires no device-list
                // notification for one, so nothing invalidates the cache. Measured 2026-10-18: a
                // process running while a device moved from 16 kHz to 8 kHz reported 16 kHz for its
                // whole life, while a process started afterwards read 8 kHz at once. Re-opening the
                // device here would re-open it at the stale rate, so there is nothing to attempt.
                emit micUnavailable(MicRate::micDeliveringNothing());

                const QAudioFormat actual = m_audioSource ? m_audioSource->format() : QAudioFormat();
                qCWarning(lcAudio,
                          "TxAudioPipeline: microphone has delivered NO audio for %lld ms while transmitting - "
                          "the device opened but is producing nothing, so nothing is going on the air. "
                          "Device \"%s\" running at %d Hz %d ch, QAudioSource state %d error %d. "
                          "Bluetooth headsets are the usual cause: they must switch to HFP for the "
                          "microphone, which cannot happen while a stereo (A2DP) playback stream is open "
                          "on the same device - ours is on \"%s\".",
                          static_cast<long long>(m_micSilenceTimer.elapsed()),
                          qUtf8Printable(m_activeMicDeviceId.isEmpty() ? QByteArray("(default)") : m_activeMicDeviceId),
                          actual.sampleRate(), actual.channelCount(),
                          m_audioSource ? static_cast<int>(m_audioSource->state()) : -1,
                          m_audioSource ? static_cast<int>(m_audioSource->error()) : -1,
                          qUtf8Printable(m_outputDeviceHint.isEmpty() ? QByteArray("(default)") : m_outputDeviceHint));
            }
        }
        return;
    }
    m_micSilenceTimer.invalidate();
    m_micSilenceReported = false;

    // WHY drain but discard when TCI owns TX: the source may still be open for the turn it takes
    // reconcileMic() to close it, and what must not happen is the room being transmitted while a
    // TCI client holds the transmitter.
    if (txSource() != TxSource::Microphone) {
        return;
    }

    // Down to the K4's 12 kHz, by whichever path this device's rate needs. The factor is the rate
    // the microphone actually opened at, NOT assumed - see audio/audiodecimator.h - and is zero
    // exactly when the resampler is carrying the stream instead.
    const QByteArray &data12k =
        (m_micDecimationFactor > 0) ? resampleTo12k(data48k, m_micDecimationFactor) : rationalResampleTo12k(data48k);
    bufferAndEmitTxFrames(data12k, m_micGain.load(std::memory_order_relaxed));
}

void TxAudioPipeline::bufferAndEmitTxFrames(const QByteArray &pcm12k, float gain) {
    // Apply mic gain and buffer as Float32 for frame-based emission.
    //
    // WHY the buffer stays float. This used to quantise to S16 here, before the wire format was
    // even chosen, which discarded bits in proportion to the gain: at a 30% mic slider only ~10.8
    // bits survived, and quiet audio landed on a handful of codes. Undithered error at that depth
    // correlates with the signal, so it is harmonic distortion, not noise — on the bench it was a
    // tonal buzz on dead air that disappeared entirely once the gain was raised. Quantising once,
    // at the wire format, is what encodeAndSendFrame now does.
    const float *floatData = reinterpret_cast<const float *>(pcm12k.constData());
    int floatSamples = pcm12k.size() / sizeof(float);

    for (int i = 0; i < floatSamples; i++) {
        const float sample = qBound(-1.0f, floatData[i] * gain, 1.0f);
        m_micBuffer.append(reinterpret_cast<const char *>(&sample), sizeof(float));
    }

    // Emit complete frames (size matches SL tier: 240/480/720/1440 samples).
    // m_micReadOffset advances per emitted frame instead of remove(0, n)'s O(N)
    // memmove on every poll. We compact only when the offset has grown past
    // half the buffer's size — keeps amortized work O(1) per frame.
    const int frameSamples = m_frameSamples.load(std::memory_order_relaxed);
    const int frameBytes = frameSamples * static_cast<int>(sizeof(float));
    const bool pttActive = m_pttActive.load(std::memory_order_acquire);
    const int encodeMode = m_encodeMode.load(std::memory_order_relaxed);

    while (m_micBuffer.size() - m_micReadOffset >= frameBytes) {
        if (pttActive) {
            // Use fromRawData to avoid a copy; immediately consumed inside this
            // tick on the TX thread — the underlying buffer doesn't move.
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

void TxAudioPipeline::encodeAndSendFrame(const QByteArray &f32MonoFrame, int frameSamples, int encodeMode) {
    // Runs on the TX thread. Translates the captured Float32 mono frame into the K4 wire format
    // and emits txPacketReady.
    //
    // Quantisation happens HERE and nowhere earlier, so each mode gets the full depth its wire
    // format allows: 24 bits for EM0, 16 for EM1, and none at all for the Opus modes.
    QByteArray audioData;
    const float *samples = reinterpret_cast<const float *>(f32MonoFrame.constData());
    const int sampleCount = static_cast<int>(f32MonoFrame.size() / sizeof(float));

    switch (encodeMode) {
    case 0: // EM0 — RAW S32LE stereo, 24-bit. See audio/rawaudioformat.h.
        audioData.resize(RawAudioFormat::em0BytesFor(sampleCount));
        RawAudioFormat::encodeEm0(samples, sampleCount, reinterpret_cast<unsigned char *>(audioData.data()));
        break;

    case 1: // EM1 — RAW 16-bit S16LE stereo
        audioData.resize(RawAudioFormat::em1BytesFor(sampleCount));
        RawAudioFormat::encodeEm1(samples, sampleCount, reinterpret_cast<unsigned char *>(audioData.data()));
        break;

    case 2: // EM2 — Opus int
    case 3: // EM3 — Opus float
    default:
        // Opus takes float natively, so these never quantise.
        audioData = m_opusEncoder.encodeFloat(f32MonoFrame, frameSamples);
        break;
    }

    if (audioData.isEmpty())
        return;

    // WHY: EM0 and EM1 put raw samples on the wire, so a wrong container format is silent
    // here and only wrong at the radio - nothing QK4 can hear distinguishes it from a dead
    // mic. This records what actually left the machine. The per-sample scan is why the
    // category is off by default and why every frame after a transmission's first is
    // throttled; on the TX thread this is not free.
    if (lcAudioTx().isDebugEnabled()) {
        // Scan every frame so the running peak is a true maximum, but only emit on the throttle.
        for (int i = 0; i < sampleCount; i++)
            m_txPeakSinceKey = qMax(m_txPeakSinceKey, std::fabs(samples[i]));

        if (m_txSequence == 0 || m_txSequence % TX_DIAG_FRAME_INTERVAL == 0)
            recordTxFrameDiagnostic(f32MonoFrame, audioData, frameSamples, encodeMode);
    }

    QByteArray packet = Protocol::buildAudioPacket(audioData, m_txSequence++, encodeMode, frameSamples);
    emit txPacketReady(packet);

    if (m_awaitingFirstPacket) {
        m_awaitingFirstPacket = false;
        const qint64 firstUs = m_pttRequestedAtNs > 0 ? (steadyNowNs() - m_pttRequestedAtNs) / 1000 : 0;
        emit pttLatencyMeasured(firstUs, m_micOpenUs, txSource() == TxSource::Microphone);
    }
}

void TxAudioPipeline::recordTxFrameDiagnostic(const QByteArray &f32MonoFrame, const QByteArray &wireData,
                                              int frameSamples, int encodeMode) {
    AudioDiagnostics::TxFrameReport *r = m_txDiagnostics.beginWrite();
    if (!r)
        return; // nobody is draining (no sampler yet); a diagnostic is never worth blocking for
    const float *in = reinterpret_cast<const float *>(f32MonoFrame.constData());
    const int inCount = static_cast<int>(f32MonoFrame.size() / sizeof(float));
    float framePeak = 0.0f;
    for (int i = 0; i < inCount; i++)
        framePeak = qMax(framePeak, std::fabs(in[i]));

    const bool fromTci = txSource() == TxSource::Tci;
    r->encodeMode = encodeMode;
    r->sequence = m_txSequence;
    r->frameSamples = frameSamples;
    r->framePeak = framePeak;
    r->peakSinceKey = m_txPeakSinceKey;
    r->fromTci = fromTci;
    r->gain = (fromTci ? m_tciTxGain : m_micGain).load(std::memory_order_relaxed);
    r->wirePayloadBytes = static_cast<int>(wireData.size());
    r->em0WirePeak = -1;
    if (encodeMode == 0) {
        const unsigned char *bytes = reinterpret_cast<const unsigned char *>(wireData.constData());
        const int words = static_cast<int>(wireData.size() / 4);
        qint64 wirePeak = 0;
        for (int i = 0; i < words; i++) {
            quint32 raw = static_cast<quint32>(bytes[i * 4]);
            raw |= static_cast<quint32>(bytes[i * 4 + 1]) << 8;
            raw |= static_cast<quint32>(bytes[i * 4 + 2]) << 16;
            raw |= static_cast<quint32>(bytes[i * 4 + 3]) << 24;
            wirePeak = qMax(wirePeak, qAbs(static_cast<qint64>(static_cast<qint32>(raw))));
        }
        r->em0WirePeak = wirePeak;
    }
    m_txDiagnostics.commitWrite();
}

bool TxAudioPipeline::takeTxFrameReport(AudioDiagnostics::TxFrameReport &out) {
    const AudioDiagnostics::TxFrameReport *r = m_txDiagnostics.front();
    if (!r)
        return false;
    out = *r;
    m_txDiagnostics.pop();
    return true;
}

void TxAudioPipeline::setEncodeMode(int mode) {
    m_encodeMode.store(mode, std::memory_order_relaxed);
}

void TxAudioPipeline::setPttActive(bool active, qint64 requestedAtNs) {
    // Q_INVOKABLE, queued from AudioController, so this runs on the TX thread.
    const bool was = m_pttActive.exchange(active, std::memory_order_release);
    if (active && !was) {
        m_txSequence = 0;     // Restart sequence counter for each transmission
        m_txPeakSinceKey = 0; // qk4.audio.tx running peak is per transmission
        m_pttRequestedAtNs = requestedAtNs;
        m_micOpenUs = 0;
        m_awaitingFirstPacket = true;
        // Flush the partial-frame tail from the previous transmission so it cannot leak into this
        // one's first frame. The resampler's filter history is the same hazard one stage earlier.
        flushMicBuffer();
    }
    if (!active && was && m_awaitingFirstPacket) {
        m_awaitingFirstPacket = false;
        emit pttLatencyMeasured(-1, m_micOpenUs, txSource() == TxSource::Microphone);
    }
    // Opens the microphone on a press when it is the source, closes it on a release. Here, on the
    // TX thread: however long the OS takes, the RX audio thread is not the one waiting.
    reconcileMic();
}

void TxAudioPipeline::setMicGain(float gain) {
    // Cubic curve: slider 0-1 maps to gain 0-1 with fine control at low levels
    // e.g., 40% slider → 0.064x gain, 70% → 0.343x, 100% → 1.0x (unity)
    float cubic = gain * gain * gain;
    m_micGain.store(qBound(0.0f, cubic, 1.0f), std::memory_order_relaxed);
}

void TxAudioPipeline::setTciTxGain(float gain) {
    // Identical curve to setMicGain, deliberately: the two controls behave the same way so a
    // position means the same thing on both, even though the useful range differs by source.
    const float cubic = gain * gain * gain;
    m_tciTxGain.store(qBound(0.0f, cubic, 1.0f), std::memory_order_relaxed);
}

void TxAudioPipeline::setFrameSamples(int samples) {
    m_frameSamples.store(samples, std::memory_order_relaxed);
}

void TxAudioPipeline::setMicDevice(const QString &deviceId) {
    if (m_selectedMicDeviceId != deviceId) {
        m_selectedMicDeviceId = deviceId;

        // If mic is currently open, restart it with the new device.
        const bool wasOpen = m_micOpen;
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

QList<QPair<QString, QString>> TxAudioPipeline::availableInputDevices() {
    QList<QPair<QString, QString>> devices;

    // Add "System Default" as the first option
    devices.append(qMakePair(QString(""), QString("System Default")));

    // Add all available input devices
    for (const QAudioDevice &device : QMediaDevices::audioInputs()) {
        devices.append(qMakePair(QString(device.id()), device.description()));
    }

    return devices;
}

void TxAudioPipeline::onSystemDefaultInputChanged() {
    // Fires when the device LIST changes, not only when the default moves - which is what lets a
    // pinned device be reclaimed the moment it reappears. See audio/audiodeviceselection.h.
    bool pinnedPresent = false;
    if (!m_selectedMicDeviceId.isEmpty()) {
        for (const QAudioDevice &d : QMediaDevices::audioInputs()) {
            if (d.id() == m_selectedMicDeviceId) {
                pinnedPresent = true;
                break;
            }
        }
    }

    const auto decision = AudioDeviceSelection::onDeviceListChanged(
        m_selectedMicDeviceId.toStdString(), m_activeMicDeviceId.toStdString(),
        QMediaDevices::defaultAudioInput().id().toStdString(), pinnedPresent, m_audioSource != nullptr);

    // WHY logged either way: a handler that never runs and a handler that runs and declines to act
    // are indistinguishable from outside, which cost a bench session on 2026-09-19.
    qCInfo(lcAudio, "TxAudioPipeline: input device list changed (selection=\"%s\", active=\"%s\") -> %s",
           m_selectedMicDeviceId.isEmpty() ? "system default" : qUtf8Printable(m_selectedMicDeviceId),
           qUtf8Printable(m_activeMicDeviceId), decision.reason);

    if (decision.action != AudioDeviceSelection::Action::SwitchTo)
        return;

    // Rebuild the source on the chosen device, preserving the open/closed state.
    const bool wasOpen = m_micOpen;
    if (wasOpen)
        closeMic();
    delete m_audioSource;
    m_audioSource = nullptr;
    if (wasOpen)
        openMic();
}
