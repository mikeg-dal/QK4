#include "sstvdecoder.h"

#include <QDebug>
#include <QHash>
#include <QRegularExpression>
#include <QtMath>
#include <algorithm>
#include <iterator>

namespace {
constexpr double Pi = 3.14159265358979323846;
constexpr double CenterHz = 1900.0;
constexpr double TwoPi = 2.0 * Pi;

int clampedByte(double value) {
    return qBound(0, qRound(value), 255);
}

bool isScottie(const SstvModeSpec *mode) {
    return mode && mode->lineLayout == SstvLineLayout::Scottie;
}

QChar morseCharacter(const QString &pattern) {
    static const QHash<QString, QChar> table = {
        {QStringLiteral(".-"), 'A'},    {QStringLiteral("-..."), 'B'},  {QStringLiteral("-.-."), 'C'},
        {QStringLiteral("-.."), 'D'},   {QStringLiteral("."), 'E'},     {QStringLiteral("..-."), 'F'},
        {QStringLiteral("--."), 'G'},   {QStringLiteral("...."), 'H'},  {QStringLiteral(".."), 'I'},
        {QStringLiteral(".---"), 'J'},  {QStringLiteral("-.-"), 'K'},   {QStringLiteral(".-.."), 'L'},
        {QStringLiteral("--"), 'M'},    {QStringLiteral("-."), 'N'},    {QStringLiteral("---"), 'O'},
        {QStringLiteral(".--."), 'P'},  {QStringLiteral("--.-"), 'Q'},  {QStringLiteral(".-."), 'R'},
        {QStringLiteral("..."), 'S'},   {QStringLiteral("-"), 'T'},     {QStringLiteral("..-"), 'U'},
        {QStringLiteral("...-"), 'V'},  {QStringLiteral(".--"), 'W'},   {QStringLiteral("-..-"), 'X'},
        {QStringLiteral("-.--"), 'Y'},  {QStringLiteral("--.."), 'Z'},  {QStringLiteral("-----"), '0'},
        {QStringLiteral(".----"), '1'}, {QStringLiteral("..---"), '2'}, {QStringLiteral("...--"), '3'},
        {QStringLiteral("....-"), '4'}, {QStringLiteral("....."), '5'}, {QStringLiteral("-...."), '6'},
        {QStringLiteral("--..."), '7'}, {QStringLiteral("---.."), '8'}, {QStringLiteral("----."), '9'},
        {QStringLiteral("-..-."), '/'}};
    return table.value(pattern);
}

bool plausibleCallsign(const QString &text) {
    static const QRegularExpression syntax(QStringLiteral("^[A-Z0-9]+(?:/[A-Z0-9]+)*$"));
    return text.size() >= 3 && text.size() <= 16 && text.contains(QRegularExpression(QStringLiteral("[A-Z]"))) &&
           text.contains(QRegularExpression(QStringLiteral("[0-9]"))) && syntax.match(text).hasMatch();
}
} // namespace

SstvDecoder::SstvDecoder(QObject *parent) : QObject(parent) {
    initializeBandpass();
    initializeIqLowpass();
    m_syncMedianSorted.reserve(128);
    connect(this, &SstvDecoder::statusChanged, this,
            [](const QString &status) { qInfo().noquote() << "SSTV RX STATE:" << status; });
    m_streamWatchdog = new QTimer(this);
    m_streamWatchdog->setSingleShot(true);
    m_streamWatchdog->setInterval(1500);
    connect(m_streamWatchdog, &QTimer::timeout, this, [this]() {
        if (!m_streamActive)
            return;
        m_streamActive = false;
        qInfo() << "SSTV RX PCM stream stopped";
        emit inputLevelChanged(0);
        emit inputStreamChanged(false);
    });
    resetDsp();
}

void SstvDecoder::resetAuto() {
    m_postIdActive = false;
    m_postIdAudio.clear();
    resetFskIdSearch();
    resetDsp();
    if (!m_streamActive)
        emit inputLevelChanged(0);
    emit inputStreamChanged(m_streamActive);
    emit statusChanged(QStringLiteral("AUTO RX ON • MODE AUTO • SYNC WAITING • SLANT AUTO"));
}

void SstvDecoder::resetDsp() {
    m_state = State::SearchingVis;
    m_mode = nullptr;
    m_frequency.clear();
    m_frequency.reserve(SampleRate * 140);
    std::fill(m_bandpassHistory.begin(), m_bandpassHistory.end(), 0.0);
    m_bandpassIndex = 0;
    m_ncoPhase = 0.0;
    std::fill(m_iHistory.begin(), m_iHistory.end(), 0.0);
    std::fill(m_qHistory.begin(), m_qHistory.end(), 0.0);
    m_iqIndex = 0;
    m_haveBasebandSample = false;
    m_previousI = 0.0;
    m_previousQ = 0.0;
    m_smoothedFrequency = CenterHz;
    m_levelEnergy = 0.0;
    m_levelSamples = 0;
    m_toneRun = 0;
    m_toneRunStart = 0;
    m_pendingTone = -1;
    m_pendingToneStart = 0;
    m_haveTuningOffset = false;
    m_tuningOffsetHz = 0.0;
    m_seenFirstLeader = false;
    m_seenBreak = false;
    m_firstLeaderEnd = -1;
    m_visSequenceDeadline = -1;
    m_ignoreLeaderUntilTransition = false;
    m_visStart = -1;
    m_lastCorrelatedVisScan = -1;
    m_acquisitionKind = AcquisitionKind::FullPreamble;
    m_modeAnnounced = false;
    m_acquisitionConfirmed = false;
    m_acquisitionDeadline = -1;
    m_confirmationSyncSample = -1;
    m_confirmationSyncCount = 0;
    m_inSync = false;
    m_syncStart = 0;
    m_syncMedianWindow.clear();
    m_syncMedianSorted.clear();
    m_syncMinimums.clear();
    m_syncMaximums.clear();
    m_syncs.clear();
    m_imageAfc.clear();
    m_renderTuningOffsetHz = 0.0;
    m_firstLineStart = -1;
    m_imageStart = -1;
    m_linePeriod = 0.0;
    m_slantLocked = false;
    m_slantStatus = QStringLiteral("SLANT: AUTO • WAITING");
    m_renderedScanLines = 0;
    m_image = QImage();
    m_robotRy.clear();
}

void SstvDecoder::initializeIqLowpass() {
    // 1.5 kHz Hamming-windowed low-pass at 12 kHz. The usable SSTV baseband
    // extends from about -1150 Hz (1100 Hz VIS at maximum negative AFC) to
    // +750 Hz. The first real-mixer image begins near -2900 Hz, leaving enough
    // transition width for a short streaming FIR without passband droop at the
    // low VIS and sync tones.
    constexpr double cutoffHz = 1500.0;
    constexpr int middle = (IqTapCount - 1) / 2;
    double gain = 0.0;
    for (int n = 0; n < IqTapCount; ++n) {
        const int k = n - middle;
        double ideal = 2.0 * cutoffHz / SampleRate;
        if (k != 0)
            ideal = qSin(TwoPi * cutoffHz * k / SampleRate) / (Pi * k);
        const double window = 0.54 - 0.46 * qCos(TwoPi * n / (IqTapCount - 1));
        m_iqTaps[n] = ideal * window;
        gain += m_iqTaps[n];
    }
    if (qAbs(gain) > 1.0e-9) {
        for (double &tap : m_iqTaps)
            tap /= gain;
    }
}

void SstvDecoder::initializeBandpass() {
    // Windowed-sinc 650-2750 Hz bandpass at 12 kHz. The margin is deliberate:
    // a receiver offset near the supported +/-350 Hz AFC limit can move the
    // 1100 Hz VIS tone down to 750 Hz and the 2300 Hz image tone up to 2650 Hz.
    // A 65-tap Hamming window still rejects low-frequency receiver rumble and
    // high-frequency noise while adding only 2.67 ms of fixed latency.
    constexpr double lowHz = 650.0;
    constexpr double highHz = 2750.0;
    constexpr int middle = (BandpassTapCount - 1) / 2;
    for (int n = 0; n < BandpassTapCount; ++n) {
        const int k = n - middle;
        double ideal = 2.0 * (highHz - lowHz) / SampleRate;
        if (k != 0) {
            ideal = (qSin(TwoPi * highHz * k / SampleRate) - qSin(TwoPi * lowHz * k / SampleRate)) / (Pi * k);
        }
        const double window = 0.54 - 0.46 * qCos(TwoPi * n / (BandpassTapCount - 1));
        m_bandpassTaps[n] = ideal * window;
    }

    double responseReal = 0.0;
    double responseImag = 0.0;
    for (int n = 0; n < BandpassTapCount; ++n) {
        const double phase = -TwoPi * CenterHz * n / SampleRate;
        responseReal += m_bandpassTaps[n] * qCos(phase);
        responseImag += m_bandpassTaps[n] * qSin(phase);
    }
    const double gain = qSqrt(responseReal * responseReal + responseImag * responseImag);
    if (gain > 1.0e-9) {
        for (double &tap : m_bandpassTaps)
            tap /= gain;
    }
}

double SstvDecoder::filterBandpass(double sample) {
    m_bandpassHistory[m_bandpassIndex] = sample;
    double filtered = 0.0;
    int historyIndex = m_bandpassIndex;
    for (double tap : m_bandpassTaps) {
        filtered += tap * m_bandpassHistory[historyIndex];
        if (--historyIndex < 0)
            historyIndex = BandpassTapCount - 1;
    }
    m_bandpassIndex = (m_bandpassIndex + 1) % BandpassTapCount;
    return filtered;
}

void SstvDecoder::consumeStereoFloat(const QByteArray &stereoFloat) {
    const int frames = stereoFloat.size() / (2 * static_cast<int>(sizeof(float)));
    if (frames <= 0)
        return;
    if (!m_streamActive) {
        m_streamActive = true;
        qInfo() << "SSTV RX PCM stream started";
        emit inputStreamChanged(true);
    }
    m_streamWatchdog->start();
    const float *input = reinterpret_cast<const float *>(stereoFloat.constData());
    for (int i = 0; i < frames; ++i)
        demodulate(input[i * 2]); // Main RX only; Sub RX remains untouched.
    if (m_state == State::Receiving && m_modeAnnounced)
        renderAvailableLines();
    if (m_postIdActive && !m_postCwEmitted)
        tryDecodeCwId();
}

void SstvDecoder::consumeMono(const QVector<float> &samples) {
    for (float sample : samples)
        demodulate(sample);
    if (m_state == State::Receiving && m_modeAnnounced)
        renderAvailableLines();
    if (m_postIdActive && !m_postCwEmitted)
        tryDecodeCwId();
}

void SstvDecoder::demodulate(float sample) {
    m_levelEnergy += static_cast<double>(sample) * sample;
    if (++m_levelSamples >= SampleRate / 4) {
        const double rms = qSqrt(m_levelEnergy / m_levelSamples);
        const double db = 20.0 * qLn(qMax(1.0e-6, rms)) / qLn(10.0);
        // Keep a small visible floor while PCM packets are arriving. Zero is
        // reserved for a stopped/missing stream so field diagnosis can tell a
        // quiet receiver from an audio path that never reached the decoder.
        emit inputLevelChanged(qMax(3, qBound(0, qRound((db + 60.0) * (100.0 / 60.0)), 100)));
        m_levelEnergy = 0.0;
        m_levelSamples = 0;
    }
    const double filteredSample = filterBandpass(sample);
    const double i = filteredSample * qCos(m_ncoPhase);
    const double q = -filteredSample * qSin(m_ncoPhase);
    m_ncoPhase += TwoPi * CenterHz / SampleRate;
    if (m_ncoPhase >= TwoPi)
        m_ncoPhase -= TwoPi;

    m_iHistory[m_iqIndex] = i;
    m_qHistory[m_iqIndex] = q;
    double filteredI = 0.0;
    double filteredQ = 0.0;
    int historyIndex = m_iqIndex;
    for (double tap : m_iqTaps) {
        filteredI += tap * m_iHistory[historyIndex];
        filteredQ += tap * m_qHistory[historyIndex];
        if (--historyIndex < 0)
            historyIndex = IqTapCount - 1;
    }
    m_iqIndex = (m_iqIndex + 1) % IqTapCount;

    double frequency = m_smoothedFrequency;
    if (m_haveBasebandSample) {
        // arg(z[n] * conj(z[n-1])) avoids separately wrapping two phases and
        // remains stable across the +/-pi branch cut.
        const double delta = qAtan2(filteredQ * m_previousI - filteredI * m_previousQ,
                                    filteredI * m_previousI + filteredQ * m_previousQ);
        const double raw = CenterHz + delta * SampleRate / TwoPi;
        if (raw >= 500.0 && raw <= 2900.0)
            frequency = 0.72 * m_smoothedFrequency + 0.28 * raw;
    }
    m_haveBasebandSample = true;
    m_previousI = filteredI;
    m_previousQ = filteredQ;
    m_smoothedFrequency = frequency;
    m_frequency.append(static_cast<float>(frequency));

    if (m_postIdActive)
        consumePostImageId(sample, frequency - m_postIdOffsetHz);

    const qint64 position = m_frequency.size() - 1;
    if (m_state == State::SearchingVis) {
        if (boundIdleSearchHistory(position))
            return;
        // The permissive path may need the complete 300 ms VIS word before it
        // can validate a damaged second leader. Do not abandon a first-leader
        // candidate until that guarded fallback has had time to run.
        if (m_visSequenceDeadline >= 0 && m_visStart < 0 && position > m_visSequenceDeadline) {
            m_seenFirstLeader = false;
            m_seenBreak = false;
            m_firstLeaderEnd = -1;
            m_visSequenceDeadline = -1;
            m_haveTuningOffset = false;
            m_tuningOffsetHz = 0.0;
            m_ignoreLeaderUntilTransition = true;
            m_toneRun = 0;
            m_toneRunStart = position;
            m_pendingTone = -1;
            emit statusChanged(QStringLiteral("AUTO RX ON • MODE AUTO • SYNC WAITING • SLANT AUTO"));
        }
        int tone = classifyTone(frequency);
        if (m_ignoreLeaderUntilTransition) {
            if (tone == 19) {
                tone = 0;
            } else {
                m_ignoreLeaderUntilTransition = false;
            }
        }
        // The 10 ms break is too short to demand a stable per-sample choice
        // among the closely spaced 1100/1200/1300 Hz classes. While leaving a
        // leader, treat the complete low-tone family as one transition; VIS
        // decoding below still determines the exact tones from window means.
        if (m_toneRun == 19 && (tone == 11 || tone == 13))
            tone = 12;
        // Confirm transitions over a short window. Requiring every individual
        // sample to remain classified split real over-air leaders whenever a
        // noise spike crossed a tone boundary.
        constexpr qint64 transitionSamples = qRound64(0.003 * SampleRate);
        if (tone == m_toneRun) {
            m_pendingTone = -1;
        } else if (tone != m_pendingTone) {
            m_pendingTone = tone;
            m_pendingToneStart = position;
        } else if (position - m_pendingToneStart >= transitionSamples) {
            completeToneRun(m_toneRun, m_toneRunStart, m_pendingToneStart, tone);
            m_toneRun = tone;
            m_toneRunStart = m_pendingToneStart;
            m_pendingTone = -1;
        }

        // If the discriminator crosses the unclassified band slowly enough,
        // the entire 10 ms break can otherwise be hidden inside the debounced
        // 1900 Hz run. Recover it directly from a short settled-frequency
        // window and split the run at the protocol edge.
        if (!m_seenFirstLeader && m_haveTuningOffset && m_toneRun == 19 &&
            position - m_toneRunStart >= qRound64(0.24 * SampleRate) &&
            position - m_toneRunStart <= qRound64(0.50 * SampleRate)) {
            const qint64 breakWindow = qRound64(0.004 * SampleRate);
            const double recentHz = meanFrequency(position - breakWindow, position) - m_tuningOffsetHz;
            if (qAbs(recentHz - 1200.0) <= 220.0) {
                const qint64 leaderEnd = position - breakWindow;
                const qint64 edge = qRound64(0.020 * SampleRate);
                const double leaderHz = meanFrequency(m_toneRunStart + edge, leaderEnd - edge) - m_tuningOffsetHz;
                if (qAbs(leaderHz - 1900.0) <= 120.0) {
                    m_seenFirstLeader = true;
                    m_seenBreak = true;
                    m_firstLeaderEnd = leaderEnd;
                    m_visSequenceDeadline = leaderEnd + qRound64(0.70 * SampleRate);
                    m_toneRun = 12;
                    m_toneRunStart = leaderEnd;
                    m_pendingTone = -1;
                    emit statusChanged(QStringLiteral("AUTO RX • VIS BREAK • AFC %1 Hz").arg(qRound(m_tuningOffsetHz)));
                }
            }
        }

        // The real 10 ms VIS break can spend much of its duration crossing
        // discriminator/classifier boundaries. Validate its settled center
        // directly instead of requiring a five-millisecond discrete tone run.
        if (m_seenFirstLeader && !m_seenBreak && m_firstLeaderEnd >= 0 &&
            position >= m_firstLeaderEnd + qRound64(0.012 * SampleRate)) {
            const double breakHz = meanFrequency(m_firstLeaderEnd + qRound64(0.004 * SampleRate),
                                                 m_firstLeaderEnd + qRound64(0.009 * SampleRate)) -
                                   m_tuningOffsetHz;
            if (qAbs(breakHz - 1200.0) <= 220.0) {
                m_seenBreak = true;
                emit statusChanged(QStringLiteral("AUTO RX • VIS BREAK • AFC %1 Hz").arg(qRound(m_tuningOffsetHz)));
            }
        }

        // A weak over-air second leader can be broken into short classified
        // runs even though its energy remains centered on 1900 Hz. Once the
        // first leader and 1200 Hz break are established, validate the second
        // leader over its protocol-timed interior instead of requiring one
        // uninterrupted 240 ms run. VIS start/stop bits and parity still guard
        // against promoting image-body coincidences into a decoded mode.
        if (m_seenFirstLeader && m_seenBreak && m_visStart < 0 && m_firstLeaderEnd >= 0 &&
            position >= m_firstLeaderEnd + qRound64(0.315 * SampleRate)) {
            const qint64 leaderWindowStart = m_firstLeaderEnd + qRound64(0.025 * SampleRate);
            const qint64 leaderWindowEnd = m_firstLeaderEnd + qRound64(0.285 * SampleRate);
            const double secondLeaderHz = meanFrequency(leaderWindowStart, leaderWindowEnd) - m_tuningOffsetHz;
            int matchingSamples = 0;
            for (qint64 i = leaderWindowStart; i < leaderWindowEnd; ++i) {
                if (qAbs(m_frequency.at(i) - m_tuningOffsetHz - 1900.0) <= 320.0)
                    ++matchingSamples;
            }
            const double matchingFraction =
                static_cast<double>(matchingSamples) / (leaderWindowEnd - leaderWindowStart);
            if (position == m_firstLeaderEnd + qRound64(0.315 * SampleRate)) {
                qInfo().nospace() << "SSTV RX integrated second leader check: mean=" << qRound(secondLeaderHz)
                                  << " Hz match=" << qRound(matchingFraction * 100.0) << '%';
            }
            if (qAbs(secondLeaderHz - 1900.0) <= 200.0 && matchingFraction >= 0.60) {
                m_visStart = m_firstLeaderEnd + qRound64(0.310 * SampleRate);
                m_acquisitionKind = AcquisitionKind::FullPreamble;
                m_firstLeaderEnd = -1;
                m_visSequenceDeadline = -1;
                qInfo().nospace() << "SSTV RX integrated second leader accepted: mean=" << qRound(secondLeaderHz)
                                  << " Hz match=" << qRound(matchingFraction * 100.0) << '%';
                emit statusChanged(QStringLiteral("AUTO RX • VIS HEADER DETECTED • WEAK LEADER"));
            }
        }

        // Learn tuning offset from the stable first leader before its short
        // break arrives, so the break and VIS bits share the same AFC offset.
        if (!m_haveTuningOffset && m_toneRun == 19 && position - m_toneRunStart >= qRound64(0.050 * SampleRate)) {
            const qint64 window = qRound64(0.040 * SampleRate);
            m_tuningOffsetHz = qBound(-300.0, meanFrequency(position - window, position) - 1900.0, 300.0);
            m_haveTuningOffset = true;
            emit statusChanged(QStringLiteral("AUTO RX • LEADER CANDIDATE • AFC %1 Hz").arg(qRound(m_tuningOffsetHz)));
        }
        // The run-based detector gives fast status feedback, but strong
        // selective fading or rapid image-tone transitions can churn that
        // state machine and hide the one real break. Independently correlate
        // the complete leader/break/leader/VIS pattern as a guarded fallback.
        // Framing, parity and the registered mode table remain mandatory.
        tryCorrelatedVisRecovery(position);
        tryDecodeVis();
    } else {
        processSyncSample(frequency, position);
        if (m_state == State::Receiving && !m_acquisitionConfirmed && m_acquisitionDeadline >= 0 &&
            position > m_acquisitionDeadline) {
            const QString modeName = m_mode ? m_mode->displayName : QStringLiteral("SSTV");
            qInfo().noquote() << "SSTV RX abandoned unconfirmed acquisition:" << modeName;
            resetDsp();
            emit statusChanged(QStringLiteral("AUTO RX • VIS VALID • NO LINE SYNC • SEARCHING"));
        }
    }
}

void SstvDecoder::tryCorrelatedVisRecovery(qint64 position) {
    if (m_visStart >= 0)
        return;

    constexpr qint64 millisecond = SampleRate / 1000;
    constexpr qint64 scanCadence = 5 * millisecond;
    const qint64 bitSamples = 30 * millisecond;
    const qint64 wordSamples = 10 * bitSamples;
    const qint64 requiredLeaderHistory = 220 * millisecond;
    if (position + 1 < requiredLeaderHistory + wordSamples)
        return;
    if (m_lastCorrelatedVisScan >= 0 && position - m_lastCorrelatedVisScan < scanCadence)
        return;
    m_lastCorrelatedVisScan = position;

    const qint64 latestStart = m_frequency.size() - wordSamples;
    const qint64 earliestStart = qMax(requiredLeaderHistory, latestStart - scanCadence);
    qint64 bestStart = -1;
    double bestOffsetHz = 0.0;
    double bestScore = 1.0e30;
    bool bestFullPreamble = false;
    bool bestStrongLeader = false;

    // Independently scan each newly exposed millisecond for a clipped-header
    // recovery. Unlike a complete-preamble acquisition, this path is only a
    // provisional candidate until several line syncs confirm the mode. Keep
    // every tone window unambiguous: the VIS data tones are only 200 Hz apart,
    // so an error allowance at or above 100 Hz can classify arbitrary energy
    // near their midpoint as either bit value.
    for (qint64 candidateStart = earliestStart; candidateStart <= latestStart; candidateStart += millisecond) {
        auto rawBitTone = [this, bitSamples, candidateStart](int index) {
            const qint64 start = candidateStart + index * bitSamples + bitSamples / 5;
            const qint64 end = candidateStart + (index + 1) * bitSamples - bitSamples / 5;
            // A lower percentile keeps a faded 1100 Hz bit from being pulled
            // upward by transition smear. Framing tones use the median because
            // they establish AFC rather than binary polarity.
            const double percentile = (index >= 1 && index <= 8) ? 0.30 : 0.50;
            return percentileFrequency(start, end, percentile);
        };

        const double startRawHz = rawBitTone(0);
        const double stopRawHz = rawBitTone(9);
        const double framingOffsetHz = (startRawHz + stopRawHz) * 0.5 - 1200.0;
        if (qAbs(framingOffsetHz) > 350.0 || qAbs(startRawHz - stopRawHz) > 200.0)
            continue;

        const qint64 secondLeaderStart = candidateStart - 220 * millisecond;
        const qint64 secondLeaderEnd = candidateStart - 20 * millisecond;
        const double leaderOffsetHz = percentileFrequency(secondLeaderStart, secondLeaderEnd, 0.50) - 1900.0;
        if (qAbs(leaderOffsetHz) > 350.0 || qAbs(leaderOffsetHz - framingOffsetHz) > 90.0)
            continue;
        const double offsetHz = 0.75 * leaderOffsetHz + 0.25 * framingOffsetHz;
        const double secondMatch = tonePresenceFraction(secondLeaderStart, secondLeaderEnd, 1900.0, offsetHz, 140.0);
        const qint64 leaderRun =
            longestToneRun(secondLeaderStart, secondLeaderEnd, 1900.0, offsetHz, 140.0, 5 * millisecond);
        const qint64 mergeGap = 5 * millisecond;
        const qint64 startRun =
            longestToneRun(candidateStart, candidateStart + bitSamples, 1200.0, offsetHz, 100.0, mergeGap);
        const qint64 stopRun = longestToneRun(candidateStart + 9 * bitSamples, candidateStart + 10 * bitSamples, 1200.0,
                                              offsetHz, 100.0, mergeGap);
        const bool credibleLeader = secondMatch >= 0.50 && leaderRun >= 80 * millisecond &&
                                    startRun >= 18 * millisecond && stopRun >= 18 * millisecond;
        if (!credibleLeader)
            continue;

        const double startError = qAbs(startRawHz - offsetHz - 1200.0);
        const double stopError = qAbs(stopRawHz - offsetHz - 1200.0);
        if (startError > 100.0 || stopError > 100.0)
            continue;

        int code = 0;
        int parity = 0;
        double score = startError + stopError + 400.0 * (1.0 - secondMatch);
        bool tonesValid = true;
        constexpr double dataLimit = 90.0;
        for (int bit = 0; bit < 7; ++bit) {
            const int index = bit + 1;
            const double hz = rawBitTone(index) - offsetHz;
            const double zeroError = qAbs(hz - 1300.0);
            const double oneError = qAbs(hz - 1100.0);
            const bool one = oneError < zeroError;
            const double error = qMin(zeroError, oneError);
            const qint64 bitStart = candidateStart + index * bitSamples + bitSamples / 5;
            const qint64 bitEnd = candidateStart + (index + 1) * bitSamples - bitSamples / 5;
            const double expectedHz = one ? 1100.0 : 1300.0;
            if (error > dataLimit || tonePresenceFraction(bitStart, bitEnd, expectedHz, offsetHz, 100.0) < 0.50) {
                tonesValid = false;
                break;
            }
            if (one)
                code |= (1 << bit);
            parity ^= one ? 1 : 0;
            score += error;
        }
        if (!tonesValid)
            continue;

        const double parityHz = rawBitTone(8) - offsetHz;
        const double parityZeroError = qAbs(parityHz - 1300.0);
        const double parityOneError = qAbs(parityHz - 1100.0);
        const bool parityOne = parityOneError < parityZeroError;
        const double parityError = qMin(parityZeroError, parityOneError);
        const qint64 parityStart = candidateStart + 8 * bitSamples + bitSamples / 5;
        const qint64 parityEnd = candidateStart + 9 * bitSamples - bitSamples / 5;
        if (parityError > dataLimit ||
            tonePresenceFraction(parityStart, parityEnd, parityOne ? 1100.0 : 1300.0, offsetHz, 100.0) < 0.50 ||
            (parity ^ (parityOne ? 1 : 0)) != 0)
            continue;

        const SstvModeSpec *candidateMode = SstvModeRegistry::findByVis(code);
        if (!candidateMode || !candidateMode->decoderImplemented)
            continue;
        bool fullPreamble = false;
        if (candidateStart >= 590 * millisecond) {
            const qint64 firstLeaderStart = candidateStart - 600 * millisecond;
            const qint64 firstLeaderEnd = candidateStart - 350 * millisecond;
            const qint64 breakSearchStart = candidateStart - 335 * millisecond;
            const qint64 breakSearchEnd = candidateStart - 285 * millisecond;
            const double firstMatch = tonePresenceFraction(firstLeaderStart, firstLeaderEnd, 1900.0, offsetHz, 140.0);
            const qint64 breakRun =
                longestToneRun(breakSearchStart, breakSearchEnd, 1200.0, offsetHz, 180.0, 2 * millisecond);
            fullPreamble = firstMatch >= 0.55 && secondMatch >= 0.55 && breakRun >= 4 * millisecond;
            if (fullPreamble)
                score -= 100.0;
        }
        const bool strongLeader = secondMatch >= 0.70 && leaderRun >= 140 * millisecond;
        score += parityError + (strongLeader ? 0.0 : 80.0);
        if (score < bestScore) {
            bestStart = candidateStart;
            bestOffsetHz = offsetHz;
            bestScore = score;
            bestFullPreamble = fullPreamble;
            bestStrongLeader = strongLeader;
        }
    }

    if (bestStart < 0)
        return;

    m_visStart = bestStart;
    m_acquisitionKind = bestFullPreamble ? AcquisitionKind::FullPreamble : AcquisitionKind::SecondLeaderRecovery;
    m_tuningOffsetHz = bestOffsetHz;
    m_haveTuningOffset = true;
    m_seenFirstLeader = true;
    m_seenBreak = true;
    m_firstLeaderEnd = -1;
    m_visSequenceDeadline = -1;
    qInfo().nospace() << "SSTV RX confidence-tiered VIS recovery: AFC=" << qRound(m_tuningOffsetHz)
                      << " Hz fullPreamble=" << bestFullPreamble << " strongLeader=" << bestStrongLeader;
    if (bestFullPreamble) {
        emit statusChanged(QStringLiteral("AUTO RX • VIS HEADER DETECTED • CORRELATION RECOVERY"));
    } else {
        emit statusChanged(QStringLiteral("AUTO RX • VIS CANDIDATE • VERIFYING LINE SYNC"));
    }
}

bool SstvDecoder::boundIdleSearchHistory(qint64 position) {
    // VIS acquisition needs less than one second of history. Keeping hours of
    // idle discriminator output serves no decoding purpose and can exhaust the
    // Android process heap when AUTO RX remains armed overnight.
    constexpr qint64 softLimit = 8LL * SampleRate;
    constexpr qint64 hardLimit = 10LL * SampleRate;
    if (position < softLimit)
        return false;

    const bool recentLeader =
        m_toneRun == 19 && m_haveTuningOffset && position - m_toneRunStart <= qRound64(0.75 * SampleRate);
    const bool headerInProgress = m_visStart >= 0 || m_seenFirstLeader || m_seenBreak || recentLeader;
    if (headerInProgress && position < hardLimit)
        return false;

    qInfo() << "SSTV RX bounded idle search history at" << m_frequency.size() << "samples";
    resetDsp();
    return true;
}

int SstvDecoder::classifyTone(double frequency) const {
    struct Candidate {
        int id;
        double hz;
    };
    static const Candidate tones[] = {{12, 1200.0}, {11, 1100.0}, {13, 1300.0}, {19, 1900.0}};

    // Accept a wide initial leader window, then make every header tone follow
    // the acquired offset. The complete leader/break/leader/VIS sequence still
    // guards against treating an ordinary image tone as a header.
    if (!m_haveTuningOffset && qAbs(frequency - 1900.0) <= 320.0)
        return 19;

    int best = 0;
    double error = 150.0;
    for (const Candidate &tone : tones) {
        const double candidateError = qAbs(frequency - (tone.hz + m_tuningOffsetHz));
        if (candidateError < error) {
            error = candidateError;
            best = tone.id;
        }
    }
    return best;
}

void SstvDecoder::completeToneRun(int tone, qint64 start, qint64 end, int nextTone) {
    Q_UNUSED(nextTone)
    const qint64 length = end - start;
    // Tone transitions pass briefly through an unclassified band. Treat that
    // as transition dead time instead of breaking an otherwise valid header.
    if (tone == 0)
        return;
    if (length < qRound64(0.005 * SampleRate))
        return;
    if (!m_seenFirstLeader) {
        // On some real and synthetic streams the debounced classifier bridges
        // the very short break and reports both 300 ms leaders as one run.
        // Recognize that composite form by validating the hidden break at its
        // protocol position, then anchor VIS to the run's trailing edge.
        if (tone == 19 && length >= qRound64(0.52 * SampleRate) && length <= qRound64(0.72 * SampleRate)) {
            const qint64 firstEdge = qRound64(0.020 * SampleRate);
            const qint64 firstEnd = start + qRound64(0.280 * SampleRate);
            const qint64 breakStart = start + qRound64(0.303 * SampleRate);
            const qint64 breakEnd = start + qRound64(0.308 * SampleRate);
            const qint64 secondStart = start + qRound64(0.330 * SampleRate);
            const qint64 secondEnd = end - qRound64(0.020 * SampleRate);
            const double firstHz = meanFrequency(start + firstEdge, firstEnd) - m_tuningOffsetHz;
            const double breakHz = meanFrequency(breakStart, breakEnd) - m_tuningOffsetHz;
            const double secondHz = meanFrequency(secondStart, secondEnd) - m_tuningOffsetHz;
            if (qAbs(firstHz - 1900.0) <= 120.0 && qAbs(breakHz - 1200.0) <= 260.0 &&
                qAbs(secondHz - 1900.0) <= 120.0) {
                m_seenFirstLeader = true;
                m_seenBreak = true;
                m_firstLeaderEnd = -1;
                m_visSequenceDeadline = -1;
                m_visStart = end;
                m_acquisitionKind = AcquisitionKind::FullPreamble;
                emit statusChanged(QStringLiteral("AUTO RX • VIS HEADER DETECTED"));
                return;
            }
        }
        if (tone == 19 && length >= qRound64(0.24 * SampleRate) && length <= qRound64(5.0 * SampleRate)) {
            const qint64 edge = qRound64(0.020 * SampleRate);
            m_tuningOffsetHz = qBound(-300.0, meanFrequency(start + edge, end - edge) - 1900.0, 300.0);
            m_haveTuningOffset = true;
            m_seenFirstLeader = true;
            m_firstLeaderEnd = end;
            m_visSequenceDeadline = end + qRound64(0.70 * SampleRate);
            emit statusChanged(QStringLiteral("AUTO RX • VIS LEADER • AFC %1 Hz").arg(qRound(m_tuningOffsetHz)));
        } else if (tone == 19) {
            // A short image-tone match is not a VIS leader. Release its AFC
            // estimate so the next genuine header starts from the wide search
            // window instead of inheriting a stale offset.
            m_haveTuningOffset = false;
            m_tuningOffsetHz = 0.0;
            emit statusChanged(QStringLiteral("AUTO RX ON • MODE AUTO • SYNC WAITING • SLANT AUTO"));
        }
        return;
    }
    if (!m_seenBreak) {
        if (tone == 12 && length >= qRound64(0.005 * SampleRate) && length <= qRound64(0.060 * SampleRate)) {
            m_seenBreak = true;
            emit statusChanged(QStringLiteral("AUTO RX • VIS BREAK • AFC %1 Hz").arg(qRound(m_tuningOffsetHz)));
        } else if (tone != 19) {
            m_seenFirstLeader = false;
            m_firstLeaderEnd = -1;
            m_visSequenceDeadline = -1;
            m_haveTuningOffset = false;
            m_tuningOffsetHz = 0.0;
            emit statusChanged(QStringLiteral("AUTO RX ON • MODE AUTO • SYNC WAITING • SLANT AUTO"));
        }
        return;
    }
    if (tone == 19 && length >= qRound64(0.24 * SampleRate) && length <= qRound64(0.60 * SampleRate)) {
        m_visStart = end;
        m_acquisitionKind = AcquisitionKind::FullPreamble;
        m_firstLeaderEnd = -1;
        m_visSequenceDeadline = -1;
        emit statusChanged(QStringLiteral("AUTO RX • VIS HEADER DETECTED"));
    }
}

void SstvDecoder::tryDecodeVis() {
    if (m_visStart < 0)
        return;
    const bool recovery = m_acquisitionKind == AcquisitionKind::SecondLeaderRecovery;
    const int bitSamples = qRound(0.030 * SampleRate);
    const qint64 predictedVisStart = m_visStart;
    const qint64 searchStart = qMax<qint64>(0, predictedVisStart - qRound64(0.060 * SampleRate));
    const qint64 searchEnd = predictedVisStart + qRound64(0.090 * SampleRate);
    constexpr qint64 alignmentStep = SampleRate / 1000; // 1 ms

    // Weak or distorted leader edges make the protocol-derived VIS timestamp
    // imprecise. Search a bounded neighborhood and accept only an alignment
    // whose entire VIS word has framing tones, even parity, and a supported
    // mode. Those checks prevent image-body 1200/1900 Hz structures from
    // being promoted into false headers.
    // demodulate() calls this once per sample; a one-millisecond cadence is
    // sufficient for VIS alignment and avoids rescoring the same windows.
    if ((m_frequency.size() % alignmentStep) != 0)
        return;
    const qint64 availableSearchEnd = qMin<qint64>(searchEnd, m_frequency.size() - 10LL * bitSamples);
    if (availableSearchEnd < searchStart)
        return;

    qint64 bestStart = -1;
    double bestOffsetHz = m_tuningOffsetHz;
    double bestScore = 1.0e30;
    for (qint64 candidateStart = searchStart; candidateStart <= availableSearchEnd; candidateStart += alignmentStep) {
        auto rawBitTone = [this, bitSamples, candidateStart](int index) {
            const qint64 start = candidateStart + index * bitSamples + bitSamples / 5;
            const qint64 end = candidateStart + (index + 1) * bitSamples - bitSamples / 5;
            return percentileFrequency(start, end, (index >= 1 && index <= 8) ? 0.30 : 0.50);
        };
        const double startRawHz = rawBitTone(0);
        const double stopRawHz = rawBitTone(9);
        const double offsetHz = (startRawHz + stopRawHz) * 0.5 - 1200.0;
        if (qAbs(offsetHz) > 350.0 || qAbs(offsetHz - m_tuningOffsetHz) > 260.0)
            continue;

        const double startError = qAbs(startRawHz - offsetHz - 1200.0);
        const double stopError = qAbs(stopRawHz - offsetHz - 1200.0);
        const double framingLimit = recovery ? 100.0 : 140.0;
        if (startError > framingLimit || stopError > framingLimit)
            continue;

        int code = 0;
        int parity = 0;
        double score = startError + stopError + 2.0 * qAbs(offsetHz - m_tuningOffsetHz);
        bool tonesValid = true;
        for (int bit = 0; bit < 7; ++bit) {
            const double hz = rawBitTone(bit + 1) - offsetHz;
            const double zeroError = qAbs(hz - 1300.0);
            const double oneError = qAbs(hz - 1100.0);
            const bool one = oneError < zeroError;
            const double error = qMin(zeroError, oneError);
            const int index = bit + 1;
            const qint64 bitStart = candidateStart + index * bitSamples + bitSamples / 5;
            const qint64 bitEnd = candidateStart + (index + 1) * bitSamples - bitSamples / 5;
            const double expectedHz = one ? 1100.0 : 1300.0;
            if (error > (recovery ? 90.0 : 170.0) ||
                (recovery && tonePresenceFraction(bitStart, bitEnd, expectedHz, offsetHz, 100.0) < 0.50)) {
                tonesValid = false;
                break;
            }
            if (one)
                code |= (1 << bit);
            parity ^= one ? 1 : 0;
            score += error;
        }
        if (!tonesValid)
            continue;

        const double parityHz = rawBitTone(8) - offsetHz;
        const double parityZeroError = qAbs(parityHz - 1300.0);
        const double parityOneError = qAbs(parityHz - 1100.0);
        const bool parityOne = parityOneError < parityZeroError;
        const double parityError = qMin(parityZeroError, parityOneError);
        const qint64 parityStart = candidateStart + 8 * bitSamples + bitSamples / 5;
        const qint64 parityEnd = candidateStart + 9 * bitSamples - bitSamples / 5;
        if (parityError > (recovery ? 90.0 : 170.0) ||
            (recovery &&
             tonePresenceFraction(parityStart, parityEnd, parityOne ? 1100.0 : 1300.0, offsetHz, 100.0) < 0.50) ||
            (parity ^ (parityOne ? 1 : 0)) != 0)
            continue;

        const SstvModeSpec *candidateMode = SstvModeRegistry::findByVis(code);
        if (!candidateMode || !candidateMode->decoderImplemented)
            continue;
        score += parityError;
        if (score < bestScore) {
            bestStart = candidateStart;
            bestOffsetHz = offsetHz;
            bestScore = score;
        }
    }

    // A framing-valid alignment can appear slightly before the real tone
    // edge because each bit mean trims its transitions. Keep a short amount
    // of look-ahead so scoring can compare both sides of that edge.
    if (bestStart >= 0 && availableSearchEnd < qMin(searchEnd, bestStart + qRound64(0.015 * SampleRate))) {
        return;
    }

    if (bestStart >= 0) {
        m_visStart = bestStart;
        // The long leader is a more stable AFC reference than two short VIS
        // framing bits. Blend the VIS estimate instead of allowing an early
        // alignment to manufacture an offset that shifts the image colors.
        m_tuningOffsetHz = 0.75 * m_tuningOffsetHz + 0.25 * bestOffsetHz;
        qInfo().nospace() << "SSTV RX VIS alignment recovered: "
                          << qRound(1000.0 * (bestStart - predictedVisStart) / SampleRate)
                          << " ms AFC=" << qRound(m_tuningOffsetHz) << " Hz";
    } else if (availableSearchEnd < searchEnd) {
        return;
    }
    auto bitTone = [this, bitSamples](int index) {
        return percentileFrequency(m_visStart + index * bitSamples + bitSamples / 5,
                                   m_visStart + (index + 1) * bitSamples - bitSamples / 5,
                                   (index >= 1 && index <= 8) ? 0.30 : 0.50) -
               m_tuningOffsetHz;
    };
    const double startToneHz = bitTone(0);
    const double stopToneHz = bitTone(9);
    if (qAbs(startToneHz - 1200.0) > 140.0 || qAbs(stopToneHz - 1200.0) > 140.0) {
        qInfo().nospace() << "SSTV RX VIS rejected: start=" << qRound(startToneHz) << " Hz stop=" << qRound(stopToneHz)
                          << " Hz AFC=" << qRound(m_tuningOffsetHz) << " Hz";
        m_visStart = -1;
        m_seenFirstLeader = m_seenBreak = false;
        m_firstLeaderEnd = -1;
        m_visSequenceDeadline = -1;
        m_haveTuningOffset = false;
        m_tuningOffsetHz = 0.0;
        // The rejected VIS window may still be inside a long 1900 Hz run.
        // Require a real transition before learning another leader so the
        // tail cannot poison acquisition of the next genuine header.
        m_ignoreLeaderUntilTransition = true;
        m_toneRun = 0;
        m_toneRunStart = m_frequency.size() - 1;
        m_pendingTone = -1;
        return;
    }
    int code = 0;
    int parity = 0;
    for (int bit = 0; bit < 7; ++bit) {
        const double frequency = bitTone(bit + 1);
        const bool one = qAbs(frequency - 1100.0) < qAbs(frequency - 1300.0);
        if (one)
            code |= (1 << bit);
        parity ^= one ? 1 : 0;
    }
    const bool parityOne = qAbs(bitTone(8) - 1100.0) < qAbs(bitTone(8) - 1300.0);
    if ((parity ^ (parityOne ? 1 : 0)) != 0) {
        qInfo().nospace() << "SSTV RX VIS parity rejected: code=0x"
                          << QString::number(code, 16).rightJustified(2, QLatin1Char('0'))
                          << " AFC=" << qRound(m_tuningOffsetHz) << " Hz";
        emit statusChanged(QStringLiteral("AUTO RX • VIS PARITY ERROR • SEARCHING"));
        m_visStart = -1;
        m_seenFirstLeader = m_seenBreak = false;
        m_firstLeaderEnd = -1;
        m_visSequenceDeadline = -1;
        m_haveTuningOffset = false;
        m_tuningOffsetHz = 0.0;
        return;
    }
    m_mode = SstvModeRegistry::findByVis(code);
    if (!m_mode || !m_mode->decoderImplemented) {
        qInfo().nospace() << "SSTV RX unsupported VIS: code=0x"
                          << QString::number(code, 16).rightJustified(2, QLatin1Char('0'))
                          << " AFC=" << qRound(m_tuningOffsetHz) << " Hz";
        emit statusChanged(QStringLiteral("AUTO RX • UNSUPPORTED VIS 0x%1").arg(code, 2, 16, QChar('0')));
        m_visStart = -1;
        m_seenFirstLeader = m_seenBreak = false;
        m_firstLeaderEnd = -1;
        m_visSequenceDeadline = -1;
        m_haveTuningOffset = false;
        m_tuningOffsetHz = 0.0;
        return;
    }

    // A new valid VIS header owns the receiver and supersedes any late ID
    // tail from the previously completed image.
    m_postIdActive = false;
    m_postIdAudio.clear();
    resetFskIdSearch();
    m_state = State::Receiving;
    m_linePeriod = nominalLineSamples();
    m_imageStart = m_visStart + 10LL * bitSamples;
    m_acquisitionConfirmed = false;
    m_acquisitionDeadline = m_imageStart + qRound64((recovery ? 5.0 : 3.0) * m_linePeriod);
    m_confirmationSyncSample = -1;
    m_confirmationSyncCount = 0;
    m_imageAfc.append({0, m_tuningOffsetHz});
    m_renderTuningOffsetHz = m_tuningOffsetHz;
    // The VIS stop bit and the first scan-line sync are both 1200 Hz and are
    // therefore one continuous run. Anchor row zero to the protocol timing;
    // subsequent independently visible syncs refine it for auto slant.
    if (!isScottie(m_mode)) {
        m_firstLineStart = m_imageStart;
        m_syncs.append({0, m_firstLineStart});
        m_slantStatus = QStringLiteral("SLANT: AUTO • CALIBRATING");
    }
    m_image = QImage(m_mode->width, m_mode->height, QImage::Format_RGB32);
    m_image.fill(Qt::black);
    if (m_mode->id == SstvModeId::Robot36)
        m_robotRy.fill(128, m_mode->width);
    if (recovery) {
        emit statusChanged(QStringLiteral("AUTO RX • %1 CANDIDATE • VERIFYING LINE SYNC").arg(m_mode->displayName));
    } else {
        announceModeIfNeeded();
        emit statusChanged(QStringLiteral("AUTO RX • %1 • SYNC ACQUIRING • SLANT: AUTO").arg(m_mode->displayName));
    }
}

void SstvDecoder::announceModeIfNeeded() {
    if (m_modeAnnounced || !m_mode)
        return;
    m_modeAnnounced = true;
    emit modeDetected(static_cast<int>(m_mode->id), m_mode->displayName);
}

double SstvDecoder::meanFrequency(qint64 start, qint64 end) const {
    start = qBound<qint64>(0, start, m_frequency.size());
    end = qBound<qint64>(start, end, m_frequency.size());
    if (end <= start)
        return CenterHz;
    double sum = 0.0;
    for (qint64 i = start; i < end; ++i)
        sum += m_frequency.at(i);
    return sum / static_cast<double>(end - start);
}

double SstvDecoder::percentileFrequency(qint64 start, qint64 end, double percentile) const {
    start = qBound<qint64>(0, start, m_frequency.size());
    end = qBound<qint64>(start, end, m_frequency.size());
    if (end <= start)
        return CenterHz;

    QVector<float> values;
    values.reserve(end - start);
    for (qint64 i = start; i < end; ++i)
        values.append(m_frequency.at(i));
    const qsizetype index =
        qBound<qsizetype>(0, qRound64(qBound(0.0, percentile, 1.0) * (values.size() - 1)), values.size() - 1);
    std::nth_element(values.begin(), values.begin() + index, values.end());
    return values.at(index);
}

double SstvDecoder::tonePresenceFraction(qint64 start, qint64 end, double targetHz, double offsetHz,
                                         double toleranceHz) const {
    start = qBound<qint64>(0, start, m_frequency.size());
    end = qBound<qint64>(start, end, m_frequency.size());
    if (end <= start)
        return 0.0;
    qint64 matching = 0;
    for (qint64 i = start; i < end; ++i) {
        if (qAbs(m_frequency.at(i) - offsetHz - targetHz) <= toleranceHz)
            ++matching;
    }
    return static_cast<double>(matching) / static_cast<double>(end - start);
}

qint64 SstvDecoder::longestToneRun(qint64 start, qint64 end, double targetHz, double offsetHz, double toleranceHz,
                                   qint64 mergeGapSamples) const {
    start = qBound<qint64>(0, start, m_frequency.size());
    end = qBound<qint64>(start, end, m_frequency.size());
    qint64 runStart = -1;
    qint64 lastMatch = -1;
    qint64 longest = 0;
    for (qint64 i = start; i < end; ++i) {
        const bool matches = qAbs(m_frequency.at(i) - offsetHz - targetHz) <= toleranceHz;
        if (matches) {
            if (runStart < 0 || (lastMatch >= 0 && i - lastMatch > mergeGapSamples + 1))
                runStart = i;
            lastMatch = i;
            longest = qMax(longest, lastMatch - runStart + 1);
        } else if (lastMatch >= 0 && i - lastMatch > mergeGapSamples) {
            runStart = -1;
            lastMatch = -1;
        }
    }
    return longest;
}

double SstvDecoder::medianFrequency(qint64 start, qint64 end) const {
    start = qBound<qint64>(0, start, m_frequency.size());
    end = qBound<qint64>(start, end, m_frequency.size());
    if (end <= start)
        return CenterHz;

    QVector<float> values;
    values.reserve(end - start);
    for (qint64 i = start; i < end; ++i)
        values.append(m_frequency.at(i));
    const qsizetype middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + middle, values.end());
    const double upper = values.at(middle);
    if ((values.size() & 1) != 0)
        return upper;
    const double lower = *std::max_element(values.begin(), values.begin() + middle);
    return 0.5 * (lower + upper);
}

void SstvDecoder::processSyncSample(double frequency, qint64 position) {
    // Phase slips create narrow IF spikes that can split or manufacture a
    // horizontal-sync run. A trailing median covering half the mode's nominal
    // sync width removes those spikes while retaining a real pulse. Associate
    // its result with the window center to cancel the detector delay.
    const int nominalSyncSamples = qMax(3, qRound(m_mode->lineSyncMs * SampleRate / 1000.0));
    int medianSamples = qMax(3, qRound(nominalSyncSamples * 0.5));
    if ((medianSamples & 1) == 0)
        ++medianSamples;
    const std::pair<double, qint64> newest{frequency, position};
    m_syncMedianWindow.push_back(newest);
    m_syncMedianSorted.insert(std::lower_bound(m_syncMedianSorted.begin(), m_syncMedianSorted.end(), newest), newest);
    while (static_cast<int>(m_syncMedianWindow.size()) > medianSamples) {
        const auto oldestSample = m_syncMedianWindow.front();
        m_syncMedianWindow.pop_front();
        const auto remove = std::lower_bound(m_syncMedianSorted.begin(), m_syncMedianSorted.end(), oldestSample);
        if (remove != m_syncMedianSorted.end() && *remove == oldestSample)
            m_syncMedianSorted.erase(remove);
    }
    if (static_cast<int>(m_syncMedianWindow.size()) < medianSamples)
        return;
    const int middle = medianSamples / 2;
    const double medianFrequencyHz = m_syncMedianSorted.at(middle).first;
    const qint64 filteredPosition = position - middle;

    // Track the local frequency floor and range over two lines. Monotonic
    // queues keep this O(1) per sample even for the longest SSTV modes. Values
    // remain in raw Hz so a slow AFC update does not invalidate the queues.
    while (!m_syncMinimums.empty() && m_syncMinimums.back().second >= medianFrequencyHz)
        m_syncMinimums.pop_back();
    while (!m_syncMaximums.empty() && m_syncMaximums.back().second <= medianFrequencyHz)
        m_syncMaximums.pop_back();
    m_syncMinimums.emplace_back(filteredPosition, medianFrequencyHz);
    m_syncMaximums.emplace_back(filteredPosition, medianFrequencyHz);
    const qint64 adaptiveSamples = qMax<qint64>(medianSamples, qRound64(2.0 * nominalLineSamples()));
    const qint64 oldest = filteredPosition - adaptiveSamples + 1;
    while (!m_syncMinimums.empty() && m_syncMinimums.front().first < oldest)
        m_syncMinimums.pop_front();
    while (!m_syncMaximums.empty() && m_syncMaximums.front().first < oldest)
        m_syncMaximums.pop_front();

    const double localMinimum = m_syncMinimums.front().second - m_tuningOffsetHz;
    const double localMaximum = m_syncMaximums.front().second - m_tuningOffsetHz;
    const double adaptiveThreshold = qBound(1300.0, localMinimum + 0.10 * (localMaximum - localMinimum), 1450.0);
    const double correctedFrequency = medianFrequencyHz - m_tuningOffsetHz;
    const double minimumSyncHz = m_acquisitionKind == AcquisitionKind::SecondLeaderRecovery ? 950.0 : 850.0;
    const double maximumSyncHz = m_acquisitionKind == AcquisitionKind::SecondLeaderRecovery
                                     ? qMax(1450.0, adaptiveThreshold)
                                     : adaptiveThreshold;
    const bool syncTone = correctedFrequency >= minimumSyncHz && correctedFrequency < maximumSyncHz;
    if (syncTone && !m_inSync) {
        m_inSync = true;
        m_syncStart = filteredPosition;
    } else if (!syncTone && m_inSync) {
        m_inSync = false;
        acceptSync(m_syncStart, filteredPosition);
    }
}

void SstvDecoder::acceptSync(qint64 start, qint64 end) {
    const double nominalWidth = m_mode->lineSyncMs * SampleRate / 1000.0;
    const double width = static_cast<double>(end - start);
    const bool recovery = m_acquisitionKind == AcquisitionKind::SecondLeaderRecovery;
    const double minimumWidth = recovery ? 0.70 : 0.48;
    const double maximumWidth = recovery ? 1.35 : 1.75;
    if (width < nominalWidth * minimumWidth || width > nominalWidth * maximumWidth)
        return;
    // Use the pulse center and nominal width to estimate its leading edge.
    // This cancels most threshold-dependent width variation while preserving
    // the mode registry's line-start convention for rendering.
    const qint64 normalizedStart = qRound64(0.5 * (start + end) - 0.5 * nominalWidth);
    if (!m_acquisitionConfirmed) {
        if (m_confirmationSyncSample < 0) {
            m_confirmationSyncSample = normalizedStart;
            m_confirmationSyncCount = 1;
            qInfo().nospace() << "SSTV RX line sync candidate " << qRound(1000.0 * width / SampleRate) << " ms";
        } else {
            const double separation = normalizedStart - m_confirmationSyncSample;
            const int periods = qRound(separation / m_linePeriod);
            const bool periodic = periods >= 1 && periods <= (recovery ? 1 : 3) &&
                                  qAbs(separation - periods * m_linePeriod) <= (recovery ? 0.08 : 0.18) * m_linePeriod;
            if (periodic) {
                ++m_confirmationSyncCount;
                m_confirmationSyncSample = normalizedStart;
            } else if (separation > 0.35 * m_linePeriod) {
                m_confirmationSyncCount = 1;
                m_confirmationSyncSample = normalizedStart;
            }
            const int requiredPulses = recovery ? 3 : 2;
            if (m_confirmationSyncCount >= requiredPulses) {
                m_acquisitionConfirmed = true;
                qInfo().nospace() << "SSTV RX line sync confirmed by " << m_confirmationSyncCount << " periodic pulses";
                if (recovery) {
                    announceModeIfNeeded();
                    emit statusChanged(
                        QStringLiteral("AUTO RX • %1 • SYNC CONFIRMED • SLANT: AUTO").arg(m_mode->displayName));
                }
            }
        }
    }
    if (m_firstLineStart < 0) {
        m_firstLineStart = normalizedStart;
        m_syncs.append({0, normalizedStart});
        updateImageAfc(0, start, end);
        m_slantStatus = QStringLiteral("SLANT: AUTO • CALIBRATING");
        return;
    }
    const int line = qRound((normalizedStart - m_firstLineStart) / nominalLineSamples());
    if (line <= 0 || line >= scanLineCount() + 4)
        return;
    if (!m_syncs.isEmpty() && line <= m_syncs.last().line)
        return;
    const double predicted = m_firstLineStart + line * m_linePeriod;
    if (qAbs(normalizedStart - predicted) > nominalLineSamples() * 0.18)
        return;
    m_syncs.append({line, normalizedStart});
    updateImageAfc(line, start, end);
    updateSlantEstimate();
}

void SstvDecoder::updateImageAfc(int line, qint64 start, qint64 end) {
    const qint64 edge = qMax<qint64>(1, (end - start) / 5);
    if (end - start <= 2 * edge)
        return;
    const double measuredOffset = medianFrequency(start + edge, end - edge) - 1200.0;
    if (!qIsFinite(measuredOffset) || qAbs(measuredOffset - m_tuningOffsetHz) > 220.0)
        return;

    // Follow real oscillator/tuning drift without allowing one marginal sync
    // to recolor the image. Each accepted line can move AFC by at most 6 Hz.
    const double limitedTarget = m_tuningOffsetHz + qBound(-40.0, measuredOffset - m_tuningOffsetHz, 40.0);
    const double updated = qBound(-350.0, 0.85 * m_tuningOffsetHz + 0.15 * limitedTarget, 350.0);
    m_tuningOffsetHz = updated;
    if (!m_imageAfc.isEmpty() && line <= m_imageAfc.last().line) {
        m_imageAfc.last().offsetHz = updated;
    } else {
        m_imageAfc.append({line, updated});
    }
}

double SstvDecoder::offsetForLine(int line) const {
    if (m_imageAfc.isEmpty())
        return m_tuningOffsetHz;
    if (line <= m_imageAfc.first().line)
        return m_imageAfc.first().offsetHz;
    for (int i = 1; i < m_imageAfc.size(); ++i) {
        if (line > m_imageAfc.at(i).line)
            continue;
        const AfcObservation &left = m_imageAfc.at(i - 1);
        const AfcObservation &right = m_imageAfc.at(i);
        if (right.line == left.line)
            return right.offsetHz;
        const double fraction = static_cast<double>(line - left.line) / static_cast<double>(right.line - left.line);
        return left.offsetHz + fraction * (right.offsetHz - left.offsetHz);
    }
    return m_imageAfc.last().offsetHz;
}

void SstvDecoder::updateSlantEstimate() {
    if (m_slantLocked || m_syncs.size() < 4)
        return;
    auto fit = [](const QVector<SyncObservation> &points, double *intercept, double *slope) {
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (const auto &point : points) {
            sx += point.line;
            sy += point.sample;
            sxx += point.line * point.line;
            sxy += point.line * point.sample;
        }
        const double n = points.size();
        const double denominator = n * sxx - sx * sx;
        if (qAbs(denominator) < 1e-9)
            return false;
        *slope = (n * sxy - sx * sy) / denominator;
        *intercept = (sy - *slope * sx) / n;
        return true;
    };

    double intercept = 0.0, slope = nominalLineSamples();
    if (!fit(m_syncs, &intercept, &slope))
        return;
    QVector<double> residuals;
    residuals.reserve(m_syncs.size());
    for (const auto &point : m_syncs)
        residuals.append(qAbs(point.sample - (intercept + slope * point.line)));
    std::sort(residuals.begin(), residuals.end());
    const double median = residuals.at(residuals.size() / 2);
    const double tolerance = qMax(6.0, median * 3.5);
    QVector<SyncObservation> inliers;
    for (const auto &point : m_syncs) {
        if (qAbs(point.sample - (intercept + slope * point.line)) <= tolerance)
            inliers.append(point);
    }
    if (inliers.size() >= 4)
        fit(inliers, &intercept, &slope);
    if (slope < nominalLineSamples() * 0.97 || slope > nominalLineSamples() * 1.03)
        return;

    const bool changed = qAbs(m_linePeriod - slope) > 0.02;
    m_linePeriod = slope;
    m_firstLineStart = qRound64(intercept);
    double rms = 0.0;
    for (const auto &point : inliers) {
        const double error = point.sample - (intercept + slope * point.line);
        rms += error * error;
    }
    rms = inliers.isEmpty() ? 999.0 : qSqrt(rms / inliers.size());
    if (inliers.size() >= 12 && rms <= 6.0) {
        m_slantLocked = true;
        m_slantStatus =
            QStringLiteral("SLANT: AUTO • LOCKED • %1 ppm").arg(qRound(1.0e6 * (slope / nominalLineSamples() - 1.0)));
    } else {
        m_slantStatus =
            QStringLiteral("SLANT: AUTO • CALIBRATING • %1/%2 SYNC").arg(inliers.size()).arg(m_syncs.size());
    }
    if (changed)
        renderAvailableLines(true);
}

void SstvDecoder::renderAvailableLines(bool forceAll) {
    if (!m_mode || m_firstLineStart < 0 || m_linePeriod <= 0.0)
        return;
    int available = 0;
    if (isScottie(m_mode)) {
        const double requiredAfterSync =
            (m_mode->lineSyncMs + 2.0 * m_mode->porchMs + m_mode->componentMs) * SampleRate / 1000.0;
        if (m_frequency.size() >= m_firstLineStart + requiredAfterSync) {
            available =
                1 + static_cast<int>((m_frequency.size() - m_firstLineStart - requiredAfterSync) / m_linePeriod);
        }
    } else {
        available = static_cast<int>((m_frequency.size() - m_firstLineStart) / m_linePeriod);
    }
    available = qBound(0, available, scanLineCount());
    const int previous = m_renderedScanLines;
    if (forceAll)
        m_renderedScanLines = 0;
    while (m_renderedScanLines < available) {
        decodeScanLine(m_renderedScanLines, m_firstLineStart + m_renderedScanLines * m_linePeriod);
        ++m_renderedScanLines;
    }
    if (m_renderedScanLines != previous && (m_renderedScanLines % 4 == 0 || m_renderedScanLines == scanLineCount())) {
        const int rows = m_mode->colorFamily == SstvColorFamily::PdYuv ? qMin(m_mode->height, m_renderedScanLines * 2)
                                                                       : m_renderedScanLines;
        emit imageUpdated(m_image, rows, m_mode->height, m_slantStatus);
    }
    if (m_renderedScanLines >= scanLineCount()) {
        // The complete set of sync-derived AFC observations is now available.
        // Re-render once so early and late rows use the same interpolated AFC
        // model in the saved/shared image without O(N^2) progressive work.
        if (m_imageAfc.size() > 1) {
            for (int line = 0; line < scanLineCount(); ++line)
                decodeScanLine(line, m_firstLineStart + line * m_linePeriod);
        }
        beginPostImageIds();
        emit imageCompleted(m_image, static_cast<int>(m_mode->id), m_slantStatus);
        resetDsp();
        emit statusChanged(QStringLiteral("AUTO RX ON • IMAGE COMPLETE • SEARCHING NEXT VIS"));
    }
}

void SstvDecoder::beginPostImageIds() {
    m_postIdActive = true;
    m_postFskEmitted = false;
    m_postCwEmitted = false;
    m_postIdSamples = 0;
    m_postIdOffsetHz = m_haveTuningOffset ? m_tuningOffsetHz : 0.0;
    m_postIdAudio.clear();
    m_postIdAudio.reserve(SampleRate * 20);
    resetFskIdSearch();
}

void SstvDecoder::resetFskIdSearch() {
    m_fskIdState = FskIdState::Leader1500;
    m_fskToneSamples = 0;
    m_fskNextBitSample = 0;
    m_fskSymbol = 0;
    m_fskBit = 0;
    m_fskHaveHeader = false;
    m_fskExpectChecksum = false;
    m_fskText.clear();
    m_fskChecksum = 0;
}

void SstvDecoder::consumePostImageId(float sample, double correctedFrequency) {
    if (!m_postIdActive)
        return;
    ++m_postIdSamples;
    if (m_postIdAudio.size() < SampleRate * 20)
        m_postIdAudio.append(sample);
    consumeFskId(correctedFrequency);
    if (m_postIdSamples >= SampleRate * 20)
        m_postIdActive = false;
}

void SstvDecoder::consumeFskId(double frequency) {
    const auto near = [frequency](double target, double tolerance) { return qAbs(frequency - target) <= tolerance; };
    switch (m_fskIdState) {
    case FskIdState::Leader1500:
        m_fskToneSamples = near(1500.0, 170.0) ? m_fskToneSamples + 1 : 0;
        if (m_fskToneSamples >= qRound(0.10 * SampleRate)) {
            m_fskIdState = FskIdState::Mark2100;
            m_fskToneSamples = 0;
        }
        break;
    case FskIdState::Mark2100:
        m_fskToneSamples = near(2100.0, 170.0) ? m_fskToneSamples + 1 : 0;
        if (m_fskToneSamples >= qRound(0.07 * SampleRate)) {
            m_fskIdState = FskIdState::Start1900;
            m_fskToneSamples = 0;
        }
        break;
    case FskIdState::Start1900:
        m_fskToneSamples = near(1900.0, 150.0) ? m_fskToneSamples + 1 : 0;
        if (m_fskToneSamples >= qRound(0.018 * SampleRate)) {
            m_fskIdState = FskIdState::Bits;
            // Detection occurs near the end of the 22 ms alignment mark. The
            // header's first bit center is about 15 ms beyond this point.
            m_fskNextBitSample = m_postIdSamples + qRound64(0.015 * SampleRate);
            m_fskSymbol = 0;
            m_fskBit = 0;
        }
        break;
    case FskIdState::Bits:
        if (m_postIdSamples < m_fskNextBitSample)
            break;
        if (qAbs(frequency - 1900.0) < qAbs(frequency - 2100.0))
            m_fskSymbol |= static_cast<quint8>(1U << m_fskBit);
        ++m_fskBit;
        m_fskNextBitSample += qRound64(0.022 * SampleRate);
        if (m_fskBit == 6) {
            processFskSymbol(m_fskSymbol & 0x3f);
            m_fskSymbol = 0;
            m_fskBit = 0;
        }
        break;
    }
}

void SstvDecoder::processFskSymbol(quint8 symbol) {
    if (!m_fskHaveHeader) {
        if (symbol != 0x2a) {
            resetFskIdSearch();
            return;
        }
        m_fskHaveHeader = true;
        return;
    }
    if (m_fskExpectChecksum) {
        const QString callsign = QString::fromLatin1(m_fskText).trimmed().toUpper();
        if ((m_fskChecksum & 0x3f) == symbol && plausibleCallsign(callsign)) {
            m_postFskEmitted = true;
            emit callsignDetected(callsign, QStringLiteral("FSK ID"), 100);
        }
        resetFskIdSearch();
        return;
    }
    if (symbol == 0x01) {
        m_fskExpectChecksum = true;
        return;
    }
    if (m_fskText.size() >= 16) {
        resetFskIdSearch();
        return;
    }
    m_fskText.append(static_cast<char>(symbol + 0x20));
    m_fskChecksum ^= symbol;
}

void SstvDecoder::tryDecodeCwId() {
    constexpr int frameSamples = SampleRate / 100; // 10 ms
    const int frameCount = m_postIdAudio.size() / frameSamples;
    if (frameCount < 50)
        return;

    QVector<bool> tone(frameCount, false);
    for (int frame = 0; frame < frameCount; ++frame) {
        const float *data = m_postIdAudio.constData() + frame * frameSamples;
        double energy = 0.0;
        for (int i = 0; i < frameSamples; ++i)
            energy += static_cast<double>(data[i]) * data[i];
        if (energy < 0.0008)
            continue;
        double bestRatio = 0.0;
        for (int hz = 450; hz <= 1050; hz += 25) {
            const double omega = TwoPi * hz / SampleRate;
            const double coefficient = 2.0 * qCos(omega);
            double q0 = 0.0, q1 = 0.0, q2 = 0.0;
            for (int i = 0; i < frameSamples; ++i) {
                q0 = coefficient * q1 - q2 + data[i];
                q2 = q1;
                q1 = q0;
            }
            const double power = q1 * q1 + q2 * q2 - coefficient * q1 * q2;
            bestRatio = qMax(bestRatio, 2.0 * power / (frameSamples * energy));
        }
        tone[frame] = bestRatio >= 0.48;
    }

    // A complete ID has a quiet tail. This also prevents repeatedly decoding
    // a partially received final character.
    int quietTail = 0;
    for (int i = tone.size() - 1; i >= 0 && !tone.at(i); --i)
        ++quietTail;
    if (quietTail < 20)
        return;
    for (int i = 1; i + 1 < tone.size(); ++i) {
        if (tone.at(i - 1) == tone.at(i + 1) && tone.at(i) != tone.at(i - 1))
            tone[i] = tone.at(i - 1);
    }

    struct Run {
        bool on;
        int frames;
    };
    QVector<Run> runs;
    bool value = tone.first();
    int length = 1;
    for (int i = 1; i < tone.size(); ++i) {
        if (tone.at(i) == value) {
            ++length;
        } else {
            runs.append({value, length});
            value = tone.at(i);
            length = 1;
        }
    }
    runs.append({value, length});
    while (!runs.isEmpty() && !runs.first().on)
        runs.removeFirst();
    while (!runs.isEmpty() && !runs.last().on)
        runs.removeLast();
    if (runs.size() < 5)
        return;

    QVector<int> marks;
    for (const Run &run : std::as_const(runs))
        if (run.on && run.frames >= 2)
            marks.append(run.frames);
    if (marks.isEmpty())
        return;
    std::sort(marks.begin(), marks.end());
    const double dot = marks.at(qMin(marks.size() / 3, marks.size() - 1));
    if (dot < 2.0 || dot > 26.0)
        return;

    QString decoded;
    QString pattern;
    for (int i = 0; i < runs.size(); ++i) {
        if (!runs.at(i).on)
            continue;
        pattern.append(runs.at(i).frames >= 2.15 * dot ? QLatin1Char('-') : QLatin1Char('.'));
        const int gap = i + 1 < runs.size() ? runs.at(i + 1).frames : qRound(3.0 * dot);
        if (gap < 2.2 * dot)
            continue;
        const QChar character = morseCharacter(pattern);
        if (!character.isNull())
            decoded.append(character);
        pattern.clear();
        if (gap >= 5.2 * dot && !decoded.endsWith(QLatin1Char(' ')))
            decoded.append(QLatin1Char(' '));
    }

    const QStringList words = decoded.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (auto it = words.crbegin(); it != words.crend(); ++it) {
        if (!plausibleCallsign(*it))
            continue;
        m_postCwEmitted = true;
        emit callsignDetected(*it, QStringLiteral("CW ID"), 75);
        return;
    }
}

void SstvDecoder::decodeScanLine(int scanLine, double startSample) {
    m_renderTuningOffsetHz = offsetForLine(scanLine);
    const double scale = m_linePeriod / nominalLineSamples();
    auto samples = [scale](double ms) { return ms * SampleRate / 1000.0 * scale; };
    if (m_mode->colorFamily == SstvColorFamily::GbrSequential ||
        m_mode->colorFamily == SstvColorFamily::RgbSequential) {
        const double component = samples(m_mode->componentMs);
        const double pixel = component / m_mode->width;
        QVector<int> g(m_mode->width), b(m_mode->width), r(m_mode->width);
        if (isScottie(m_mode)) {
            // Sync is between B and R. Each scan has a porch on both sides.
            double greenCursor = startSample - samples(2.0 * m_mode->componentMs + 3.0 * m_mode->porchMs);
            double blueCursor = startSample - samples(m_mode->componentMs + m_mode->porchMs);
            double redCursor = startSample + samples(m_mode->lineSyncMs + m_mode->porchMs);
            for (int x = 0; x < m_mode->width; ++x) {
                g[x] = sampleLevel(greenCursor + (x + 0.5) * pixel, pixel);
                b[x] = sampleLevel(blueCursor + (x + 0.5) * pixel, pixel);
                r[x] = sampleLevel(redCursor + (x + 0.5) * pixel, pixel);
            }
            for (int x = 0; x < m_mode->width; ++x)
                m_image.setPixel(x, scanLine, qRgb(r[x], g[x], b[x]));
            return;
        }
        double cursor = startSample + samples(m_mode->lineSyncMs + m_mode->porchMs);
        const bool gbr = m_mode->colorFamily == SstvColorFamily::GbrSequential;
        QVector<int> *channels[] = {gbr ? &g : &r, gbr ? &b : &g, gbr ? &r : &b};
        for (int channel = 0; channel < 3; ++channel) {
            for (int x = 0; x < m_mode->width; ++x)
                (*channels[channel])[x] = sampleLevel(cursor + (x + 0.5) * pixel, pixel);
            cursor += component;
            if (m_mode->lineLayout != SstvLineLayout::Wraase && channel < 2)
                cursor += samples(m_mode->porchMs);
        }
        for (int x = 0; x < m_mode->width; ++x)
            m_image.setPixel(x, scanLine, qRgb(r[x], g[x], b[x]));
    } else if (m_mode->colorFamily == SstvColorFamily::RobotYuv) {
        double cursor = startSample + samples(9.0 + 3.0);
        const double yDuration = samples(88.0), yPixel = yDuration / m_mode->width;
        QVector<int> y(m_mode->width), chroma(m_mode->width);
        for (int x = 0; x < m_mode->width; ++x)
            y[x] = sampleLevel(cursor + (x + 0.5) * yPixel, yPixel);
        cursor += yDuration + samples(4.5 + 1.5);
        const double cDuration = samples(44.0), cPixel = cDuration / m_mode->width;
        for (int x = 0; x < m_mode->width; ++x)
            chroma[x] = sampleLevel(cursor + (x + 0.5) * cPixel, cPixel);
        if ((scanLine & 1) == 0) {
            m_robotRy = chroma; // Canonical Robot 36: even rows carry Cr/R-Y.
        } else {
            for (int x = 0; x < m_mode->width; ++x) {
                m_image.setPixel(x, scanLine - 1,
                                 yuvPixel(qGray(m_image.pixel(x, scanLine - 1)), m_robotRy[x], chroma[x]));
                m_image.setPixel(x, scanLine, yuvPixel(y[x], m_robotRy[x], chroma[x]));
            }
        }
        if ((scanLine & 1) == 0) {
            for (int x = 0; x < m_mode->width; ++x)
                m_image.setPixel(x, scanLine, qRgb(y[x], y[x], y[x]));
        }
    } else if (m_mode->colorFamily == SstvColorFamily::PdYuv) {
        double cursor = startSample + samples(m_mode->lineSyncMs + m_mode->porchMs);
        const double duration = samples(m_mode->componentMs);
        const double pixel = duration / m_mode->width;
        QVector<int> y0(m_mode->width), v(m_mode->width), u(m_mode->width), y1(m_mode->width);
        QVector<int> *components[] = {&y0, &v, &u, &y1};
        for (auto *component : components) {
            for (int x = 0; x < m_mode->width; ++x)
                (*component)[x] = sampleLevel(cursor + (x + 0.5) * pixel, pixel);
            cursor += duration;
        }
        const int row0 = scanLine * 2;
        for (int x = 0; x < m_mode->width; ++x) {
            m_image.setPixel(x, row0, yuvPixel(y0[x], v[x], u[x]));
            m_image.setPixel(x, row0 + 1, yuvPixel(y1[x], v[x], u[x]));
        }
    }
}

int SstvDecoder::sampleLevel(double position, double pixelSamples) const {
    // At 12 kHz, the shortest supported pixels contain only a few samples.
    // Use as much of the interior as is safely available, then take a median
    // instead of a mean so one FM phase slip cannot create a bright colour
    // speckle. The cap prevents slow modes from blurring fine transitions.
    const qint64 center = qRound64(position);
    if (pixelSamples < 3.0) {
        // Robot 36 chroma has only about 1.65 samples per transmitted pixel at
        // the K4's 12 kHz stream rate. There are not enough independent values
        // for a useful median, so retain a three-sample local mean there.
        const double frequency = meanFrequency(center - 1, center + 2) - m_renderTuningOffsetHz;
        return clampedByte((frequency - 1500.0) * 255.0 / 800.0);
    }
    const int radius = qMax(1, qMin(5, qRound(pixelSamples * 0.35)));
    const int sampleCount = 2 * radius + 1;
    const qint64 first = qRound64(position - 0.5 * (sampleCount - 1));
    const double median = medianFrequency(first, first + sampleCount);
    double sum = 0.0;
    int used = 0;
    for (qint64 i = qMax<qint64>(0, first); i < qMin<qint64>(m_frequency.size(), first + sampleCount); ++i) {
        // A winsorized mean retains the sub-pixel interpolation needed by the
        // 12 kHz PD/Robot modes while limiting a phase-slip sample's leverage.
        sum += qBound(median - 220.0, static_cast<double>(m_frequency.at(i)), median + 220.0);
        ++used;
    }
    const double frequency = (used > 0 ? sum / used : median) - m_renderTuningOffsetHz;
    return clampedByte((frequency - 1500.0) * 255.0 / 800.0);
}

QRgb SstvDecoder::yuvPixel(int y, int ry, int by) const {
    const double redDifference = ry - 128.0;
    const double blueDifference = by - 128.0;
    return qRgb(clampedByte(y + 1.402 * redDifference),
                clampedByte(y - 0.714136 * redDifference - 0.344136 * blueDifference),
                clampedByte(y + 1.772 * blueDifference));
}

double SstvDecoder::nominalLineSamples() const {
    if (!m_mode)
        return 0.0;
    return m_mode->lineTimeMs * SampleRate / 1000.0;
}

int SstvDecoder::scanLineCount() const {
    if (!m_mode)
        return 0;
    return m_mode->colorFamily == SstvColorFamily::PdYuv ? m_mode->height / 2 : m_mode->height;
}
