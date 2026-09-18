#include "sstvencoder.h"

#include <QtMath>
#include <QStringList>

namespace {
constexpr double Pi = 3.14159265358979323846;
constexpr double LeaderFrequencyHz = 1900.0;
constexpr double BreakFrequencyHz = 1200.0;
constexpr double CenterFrequencyHz = 1900.0;
constexpr double VisZeroFrequencyHz = 1300.0;
constexpr double VisOneFrequencyHz = 1100.0;
constexpr double PixelLowFrequencyHz = 1500.0;
constexpr double PixelSpanHz = 800.0;

double pixelFrequency(quint8 value) {
    return PixelLowFrequencyHz + PixelSpanHz * static_cast<double>(value) / 255.0;
}

QString morsePattern(QChar character) {
    switch (character.toUpper().unicode()) {
    case 'A':
        return QStringLiteral(".-");
    case 'B':
        return QStringLiteral("-...");
    case 'C':
        return QStringLiteral("-.-.");
    case 'D':
        return QStringLiteral("-..");
    case 'E':
        return QStringLiteral(".");
    case 'F':
        return QStringLiteral("..-.");
    case 'G':
        return QStringLiteral("--.");
    case 'H':
        return QStringLiteral("....");
    case 'I':
        return QStringLiteral("..");
    case 'J':
        return QStringLiteral(".---");
    case 'K':
        return QStringLiteral("-.-");
    case 'L':
        return QStringLiteral(".-..");
    case 'M':
        return QStringLiteral("--");
    case 'N':
        return QStringLiteral("-.");
    case 'O':
        return QStringLiteral("---");
    case 'P':
        return QStringLiteral(".--.");
    case 'Q':
        return QStringLiteral("--.-");
    case 'R':
        return QStringLiteral(".-.");
    case 'S':
        return QStringLiteral("...");
    case 'T':
        return QStringLiteral("-");
    case 'U':
        return QStringLiteral("..-");
    case 'V':
        return QStringLiteral("...-");
    case 'W':
        return QStringLiteral(".--");
    case 'X':
        return QStringLiteral("-..-");
    case 'Y':
        return QStringLiteral("-.--");
    case 'Z':
        return QStringLiteral("--..");
    case '0':
        return QStringLiteral("-----");
    case '1':
        return QStringLiteral(".----");
    case '2':
        return QStringLiteral("..---");
    case '3':
        return QStringLiteral("...--");
    case '4':
        return QStringLiteral("....-");
    case '5':
        return QStringLiteral(".....");
    case '6':
        return QStringLiteral("-....");
    case '7':
        return QStringLiteral("--...");
    case '8':
        return QStringLiteral("---..");
    case '9':
        return QStringLiteral("----.");
    case '/':
        return QStringLiteral("-..-.");
    default:
        return QString();
    }
}
} // namespace

bool SstvEncoder::begin(const QImage &frame, SstvModeId modeId, QString *error, const QString &morseId, int morseWpm,
                        const QString &fskId, int preRollMs, int postRollMs) {
    m_mode = SstvModeRegistry::find(modeId);
    m_segments.clear();
    m_segmentIndex = 0;
    m_remainingSegmentSamples = 0;
    m_currentSegmentSamples = 0;
    m_phase = 0.0;
    m_roundingError = 0.0;
    m_totalSamples = 0;
    m_imageSamples = 0;
    m_fskIdEndSamples = 0;
    m_emittedSamples = 0;

    if (!m_mode || !m_mode->encoderImplemented) {
        if (error)
            *error = QStringLiteral("This SSTV mode is not available for transmit yet.");
        return false;
    }
    if (frame.isNull() || frame.width() != m_mode->width || frame.height() != m_mode->height) {
        if (error) {
            *error = QStringLiteral("%1 requires an exact %2 x %3 frame.")
                         .arg(m_mode->displayName)
                         .arg(m_mode->width)
                         .arg(m_mode->height);
        }
        return false;
    }

    m_image = frame.convertToFormat(QImage::Format_RGB32);
    if (preRollMs > 0)
        appendTone(0.0, qBound(0, preRollMs, 2000));
    appendTone(LeaderFrequencyHz, 300.0);
    appendTone(BreakFrequencyHz, 10.0);
    appendTone(LeaderFrequencyHz, 300.0);
    appendVis(m_mode->visCode);
    switch (m_mode->colorFamily) {
    case SstvColorFamily::GbrSequential:
    case SstvColorFamily::RgbSequential:
        appendSequentialImage();
        break;
    case SstvColorFamily::RobotYuv:
        appendRobot36Image();
        break;
    case SstvColorFamily::PdYuv:
        appendPdImage();
        break;
    }

    const int imageSegmentCount = m_segments.size();
    appendFskId(fskId.trimmed().toUpper());
    const int fskSegmentCount = m_segments.size();
    appendMorseId(morseId.trimmed().toUpper(), qBound(5, morseWpm, 40));
    if (postRollMs > 0)
        appendTone(0.0, qBound(0, postRollMs, 2000));
    for (int i = 0; i < m_segments.size(); ++i) {
        const ToneSegment &segment = m_segments.at(i);
        m_totalSamples += roundedSegmentSamples(segment.durationMs);
        if (i + 1 == imageSegmentCount)
            m_imageSamples = m_totalSamples;
        if (i + 1 == fskSegmentCount)
            m_fskIdEndSamples = m_totalSamples;
    }
    m_roundingError = 0.0;
    return true;
}

void SstvEncoder::appendFskSymbol(quint8 symbol) {
    // MMSSTV/QSSTV FSK ID uses six data bits, least-significant bit first,
    // with 22 ms symbols. A one is 1900 Hz and a zero is 2100 Hz.
    for (int bit = 0; bit < 6; ++bit)
        appendTone((symbol & (1U << bit)) ? 1900.0 : 2100.0, 22.0);
}

void SstvEncoder::appendFskId(const QString &text) {
    if (text.isEmpty())
        return;

    QByteArray symbols;
    symbols.reserve(text.size());
    quint8 checksum = 0;
    for (const QChar character : text.left(16)) {
        const ushort code = character.toLatin1();
        if (code < 0x20 || code > 0x5f)
            continue;
        const quint8 symbol = static_cast<quint8>(code - 0x20);
        symbols.append(static_cast<char>(symbol));
        checksum ^= symbol;
    }
    if (symbols.isEmpty())
        return;

    appendTone(1500.0, 300.0);
    appendTone(2100.0, 100.0);
    appendTone(1900.0, 22.0);
    appendFskSymbol(0x2a);
    for (const char symbol : symbols)
        appendFskSymbol(static_cast<quint8>(symbol));
    appendFskSymbol(0x01);
    appendFskSymbol(checksum & 0x3f);
    appendTone(1900.0, 100.0);
}

QVector<qint16> SstvEncoder::nextSamples(int maximumSamples) {
    QVector<qint16> output;
    if (!isActive() || maximumSamples <= 0)
        return output;

    output.reserve(maximumSamples);
    while (output.size() < maximumSamples && m_segmentIndex < m_segments.size()) {
        const ToneSegment &segment = m_segments.at(m_segmentIndex);
        if (m_remainingSegmentSamples == 0) {
            m_currentSegmentSamples = roundedSegmentSamples(segment.durationMs);
            m_remainingSegmentSamples = m_currentSegmentSamples;
            if (segment.shaped)
                m_phase = 0.0;
        }

        const int count = qMin(maximumSamples - output.size(), m_remainingSegmentSamples);
        const double phaseStep = 2.0 * Pi * segment.frequencyHz / SampleRate;
        for (int i = 0; i < count; ++i) {
            if (segment.frequencyHz <= 0.0) {
                output.append(0);
                continue;
            }
            double amplitude = 26213.0;
            if (segment.shaped) {
                const int position = m_currentSegmentSamples - m_remainingSegmentSamples + i;
                const int remaining = m_currentSegmentSamples - position - 1;
                const int rampSamples = qMin(qRound(0.005 * SampleRate), qMax(1, m_currentSegmentSamples / 4));
                const double envelope = qMin(1.0, qMin(static_cast<double>(position) / rampSamples,
                                                       static_cast<double>(remaining) / rampSamples));
                amplitude *= qMax(0.0, envelope);
            }
            output.append(static_cast<qint16>(qRound(qSin(m_phase) * amplitude)));
            m_phase += phaseStep;
            if (m_phase >= 2.0 * Pi)
                m_phase -= 2.0 * Pi;
        }
        m_remainingSegmentSamples -= count;
        m_emittedSamples += count;
        if (m_remainingSegmentSamples == 0)
            ++m_segmentIndex;
    }
    return output;
}

bool SstvEncoder::isActive() const {
    return m_mode && m_segmentIndex < m_segments.size();
}

bool SstvEncoder::isComplete() const {
    return m_mode && m_segmentIndex >= m_segments.size();
}

int SstvEncoder::totalSamples() const {
    return m_totalSamples;
}

int SstvEncoder::emittedSamples() const {
    return m_emittedSamples;
}

int SstvEncoder::progressPercent() const {
    if (m_totalSamples <= 0)
        return 0;
    return qBound(0, static_cast<int>((100LL * m_emittedSamples) / m_totalSamples), 100);
}

void SstvEncoder::appendTone(double frequencyHz, double durationMs, bool shaped) {
    m_segments.append({frequencyHz, durationMs, shaped});
}

void SstvEncoder::appendVis(int visCode) {
    appendTone(BreakFrequencyHz, 30.0); // start bit
    int parity = 0;
    for (int bit = 0; bit < 7; ++bit) {
        const bool one = (visCode & (1 << bit)) != 0;
        appendTone(one ? VisOneFrequencyHz : VisZeroFrequencyHz, 30.0);
        parity ^= one ? 1 : 0;
    }
    appendTone(parity ? VisOneFrequencyHz : VisZeroFrequencyHz, 30.0); // even parity
    appendTone(BreakFrequencyHz, 30.0);                                // stop bit
}

void SstvEncoder::appendMorseId(const QString &text, int wpm) {
    QStringList characters;
    for (const QChar character : text) {
        if (!morsePattern(character).isEmpty())
            characters.append(QString(character));
    }
    if (characters.isEmpty())
        return;

    const double dotMs = 1200.0 / wpm;
    appendTone(0.0, 300.0);
    for (int characterIndex = 0; characterIndex < characters.size(); ++characterIndex) {
        const QString pattern = morsePattern(characters.at(characterIndex).at(0));
        for (int symbolIndex = 0; symbolIndex < pattern.size(); ++symbolIndex) {
            appendTone(700.0, pattern.at(symbolIndex) == QLatin1Char('-') ? 3.0 * dotMs : dotMs, true);
            if (symbolIndex + 1 < pattern.size())
                appendTone(0.0, dotMs);
        }
        if (characterIndex + 1 < characters.size())
            appendTone(0.0, 3.0 * dotMs);
    }
    appendTone(0.0, 300.0);
}

void SstvEncoder::appendSequentialImage() {
    const double pixelDurationMs = m_mode->componentMs / m_mode->width;
    if (m_mode->lineLayout == SstvLineLayout::Scottie) {
        // Scottie places sync between blue and red. Every component has a
        // porch on both sides; the next row's G/B scans precede its sync.
        for (int y = 0; y < m_mode->height; ++y) {
            const QRgb *line = reinterpret_cast<const QRgb *>(m_image.constScanLine(y));
            appendTone(PixelLowFrequencyHz, m_mode->porchMs);
            for (int x = 0; x < m_mode->width; ++x)
                appendTone(pixelFrequency(qGreen(line[x])), pixelDurationMs);
            appendTone(PixelLowFrequencyHz, m_mode->porchMs);
            appendTone(PixelLowFrequencyHz, m_mode->porchMs);
            for (int x = 0; x < m_mode->width; ++x)
                appendTone(pixelFrequency(qBlue(line[x])), pixelDurationMs);
            appendTone(PixelLowFrequencyHz, m_mode->porchMs);
            appendTone(BreakFrequencyHz, m_mode->lineSyncMs);
            appendTone(PixelLowFrequencyHz, m_mode->porchMs);
            for (int x = 0; x < m_mode->width; ++x)
                appendTone(pixelFrequency(qRed(line[x])), pixelDurationMs);
            appendTone(PixelLowFrequencyHz, m_mode->porchMs);
        }
        return;
    }

    const bool gbr = m_mode->colorFamily == SstvColorFamily::GbrSequential;
    const int order[3] = {gbr ? 1 : 0, gbr ? 2 : 1, gbr ? 0 : 2};

    for (int y = 0; y < m_mode->height; ++y) {
        appendTone(BreakFrequencyHz, m_mode->lineSyncMs);
        appendTone(PixelLowFrequencyHz, m_mode->porchMs);
        for (int channel : order) {
            const QRgb *line = reinterpret_cast<const QRgb *>(m_image.constScanLine(y));
            for (int x = 0; x < m_mode->width; ++x)
                appendTone(pixelFrequency(componentValue(line[x], channel)), pixelDurationMs);
            if (m_mode->lineLayout != SstvLineLayout::Wraase && channel != order[2])
                appendTone(PixelLowFrequencyHz, m_mode->porchMs);
        }
        if (m_mode->lineLayout != SstvLineLayout::Wraase)
            appendTone(PixelLowFrequencyHz, m_mode->porchMs);
    }
}

void SstvEncoder::appendRobot36Image() {
    constexpr double luminancePixelMs = 88.0 / 320.0;
    constexpr double chromaPixelMs = 44.0 / 320.0;
    // Open-SSTV identified an interoperability defect in PySSTV's Robot 36
    // encoder. Canonical Robot 36 shares averaged Cr/Cb across each row pair:
    // the even half carries Cr after a 1500 Hz separator and the odd half Cb
    // after a 2300 Hz separator. Each 150 ms half retains its own sync.
    for (int y = 0; y < m_mode->height; y += 2) {
        const QRgb *even = reinterpret_cast<const QRgb *>(m_image.constScanLine(y));
        const QRgb *odd = reinterpret_cast<const QRgb *>(m_image.constScanLine(y + 1));

        appendTone(BreakFrequencyHz, 9.0);
        appendTone(PixelLowFrequencyHz, 3.0);
        for (int x = 0; x < m_mode->width; ++x)
            appendTone(pixelFrequency(luminance(even[x])), luminancePixelMs);
        appendTone(PixelLowFrequencyHz, 4.5);
        appendTone(CenterFrequencyHz, 1.5);
        for (int x = 0; x < m_mode->width; ++x)
            appendTone(pixelFrequency(static_cast<quint8>((redDifference(even[x]) + redDifference(odd[x])) / 2)),
                       chromaPixelMs);

        appendTone(BreakFrequencyHz, 9.0);
        appendTone(PixelLowFrequencyHz, 3.0);
        for (int x = 0; x < m_mode->width; ++x)
            appendTone(pixelFrequency(luminance(odd[x])), luminancePixelMs);
        appendTone(2300.0, 4.5);
        appendTone(CenterFrequencyHz, 1.5);
        for (int x = 0; x < m_mode->width; ++x)
            appendTone(pixelFrequency(static_cast<quint8>((blueDifference(even[x]) + blueDifference(odd[x])) / 2)),
                       chromaPixelMs);
    }
}

void SstvEncoder::appendPdImage() {
    const double pixelMs = m_mode->componentMs / m_mode->width;
    for (int y = 0; y < m_mode->height; y += 2) {
        const QRgb *even = reinterpret_cast<const QRgb *>(m_image.constScanLine(y));
        const QRgb *odd = reinterpret_cast<const QRgb *>(m_image.constScanLine(y + 1));
        appendTone(BreakFrequencyHz, m_mode->lineSyncMs);
        appendTone(PixelLowFrequencyHz, m_mode->porchMs);
        for (int x = 0; x < m_mode->width; ++x)
            appendTone(pixelFrequency(luminance(even[x])), pixelMs);
        for (int x = 0; x < m_mode->width; ++x)
            appendTone(pixelFrequency(static_cast<quint8>((redDifference(even[x]) + redDifference(odd[x])) / 2)),
                       pixelMs);
        for (int x = 0; x < m_mode->width; ++x)
            appendTone(pixelFrequency(static_cast<quint8>((blueDifference(even[x]) + blueDifference(odd[x])) / 2)),
                       pixelMs);
        for (int x = 0; x < m_mode->width; ++x)
            appendTone(pixelFrequency(luminance(odd[x])), pixelMs);
    }
}

int SstvEncoder::roundedSegmentSamples(double durationMs) {
    const double exact = durationMs * SampleRate / 1000.0 + m_roundingError;
    const int rounded = qMax(1, qRound(exact));
    m_roundingError = exact - rounded;
    return rounded;
}

quint8 SstvEncoder::componentValue(QRgb pixel, int component) const {
    switch (component) {
    case 0:
        return static_cast<quint8>(qRed(pixel));
    case 1:
        return static_cast<quint8>(qGreen(pixel));
    default:
        return static_cast<quint8>(qBlue(pixel));
    }
}

quint8 SstvEncoder::luminance(QRgb pixel) const {
    return static_cast<quint8>(
        qBound(0, qRound(0.299 * qRed(pixel) + 0.587 * qGreen(pixel) + 0.114 * qBlue(pixel)), 255));
}

quint8 SstvEncoder::redDifference(QRgb pixel) const {
    return static_cast<quint8>(qBound(0, qRound(128.0 + (qRed(pixel) - luminance(pixel)) / 1.402), 255));
}

quint8 SstvEncoder::blueDifference(QRgb pixel) const {
    return static_cast<quint8>(qBound(0, qRound(128.0 + (qBlue(pixel) - luminance(pixel)) / 1.772), 255));
}
