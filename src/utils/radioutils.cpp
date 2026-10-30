#include "radioutils.h"

#include <algorithm>
#include <QHostAddress>
#include <QRegularExpression>
#include <QStringList>
#include <QtGlobal>

namespace RadioUtils {

namespace {
// Where the knob crosses between QRP and QRO: the top of QRP and the first QRO step above it.
constexpr int kQrpTopW = RF_POWER_TENTHS_MAX / 10;
constexpr int kQroAboveQrpW = kQrpTopW + 1;
} // namespace

std::optional<RfPower> nextRfPower(double value, LevelsState::PowerRange range, int steps) {
    if (value < 0.0)
        return std::nullopt;
    using Range = LevelsState::PowerRange;
    // WHY integers: QRP and XVTR step in tenths, and adding 0.1 to a double drifts off the grid the
    // radio reports on (PC099L is 9.9 W, not 9.900000000000002).
    if (range == Range::Qro) {
        const int watts = qRound(value);
        const int next = watts + steps;
        if (watts > kQrpTopW && next <= kQrpTopW)
            return RfPower{static_cast<double>(kQrpTopW), Range::Qrp};
        const int clamped = std::clamp(next, RF_POWER_QRO_MIN_W, RF_POWER_QRO_MAX_W);
        if (clamped == watts)
            return std::nullopt;
        return RfPower{static_cast<double>(clamped), Range::Qro};
    }
    const int tenths = qRound(value * 10.0);
    const int next = tenths + steps;
    if (range == Range::Qrp && next > RF_POWER_TENTHS_MAX)
        return RfPower{static_cast<double>(kQroAboveQrpW), Range::Qro};
    const int clamped = std::clamp(next, RF_POWER_TENTHS_MIN, RF_POWER_TENTHS_MAX);
    if (clamped == tenths)
        return std::nullopt;
    return RfPower{clamped / 10.0, range};
}

int tuningStepToHz(int step) {
    static const int table[] = {1, 10, 100, 1000, 10000, 100};
    return (step >= 0 && step <= 5) ? table[step] : 1000;
}

int tuningStepForDigit(int digitFromRight) {
    return (digitFromRight >= 0 && digitFromRight <= 4) ? digitFromRight : -1;
}

int getBandFromFrequency(quint64 freq) {
    if (freq >= 1800000 && freq <= 2000000)
        return 0; // 160m
    if (freq >= 3500000 && freq <= 4000000)
        return 1; // 80m
    if (freq >= 5330500 && freq <= 5405500)
        return 2; // 60m
    if (freq >= 7000000 && freq <= 7300000)
        return 3; // 40m
    if (freq >= 10100000 && freq <= 10150000)
        return 4; // 30m
    if (freq >= 14000000 && freq <= 14350000)
        return 5; // 20m
    if (freq >= 18068000 && freq <= 18168000)
        return 6; // 17m
    if (freq >= 21000000 && freq <= 21450000)
        return 7; // 15m
    if (freq >= 24890000 && freq <= 24990000)
        return 8; // 12m
    if (freq >= 28000000 && freq <= 29700000)
        return 9; // 10m
    if (freq >= 50000000 && freq <= 54000000)
        return 10; // 6m
    if (freq >= 144000000)
        return 16; // XVTR (transverter bands 16-25)
    return -1;     // Out of band / GEN coverage
}

int getNextSpanUp(int currentSpan) {
    if (currentSpan >= SPAN_MAX)
        return SPAN_MAX;
    int increment = (currentSpan < SPAN_THRESHOLD_UP) ? 1000 : 4000;
    int newSpan = currentSpan + increment;
    return qMin(newSpan, SPAN_MAX);
}

int spanAfterZoom(int currentSpan, bool zoomIn) {
    return zoomIn ? getNextSpanDown(currentSpan) : getNextSpanUp(currentSpan);
}

int nextDdcNoiseBlankerMode(int currentMode) {
    switch (currentMode) {
    case 1:
        return 2; // ON -> AUTO
    case 2:
        return 0; // AUTO -> OFF
    default:
        return 1; // OFF, or not reported yet -> ON
    }
}

int nextFilterPreset(int currentPreset) {
    switch (currentPreset) {
    case 1:
        return 3;
    case 3:
        return 2;
    case 2:
        return 1;
    default:
        return -1;
    }
}

int getNextSpanDown(int currentSpan) {
    if (currentSpan <= SPAN_MIN)
        return SPAN_MIN;
    int decrement = (currentSpan > SPAN_THRESHOLD_DOWN) ? 4000 : 1000;
    int newSpan = currentSpan - decrement;
    return qMax(newSpan, SPAN_MIN);
}

std::optional<FilterBwIs> filterAfterEdgeStep(FilterBwIs current, bool adjustHi, int steps,
                                              const FilterEdgeLimits &limits) {
    const int lo = qMax(0, current.isDah - current.bwDah / 2);
    const int hi = lo + current.bwDah;
    const int stepDah = steps * 2;

    int newBw;
    int newLo;
    int newHi;
    if (adjustHi) {
        const int wantedHi = hi + stepDah;
        if (wantedHi <= lo)
            return std::nullopt;
        newBw = qBound(limits.bwMinDah, wantedHi - lo, limits.bwMaxDah);
        newLo = lo;
        newHi = lo + newBw;
    } else {
        const int wantedLo = lo + stepDah;
        if (wantedLo >= hi)
            return std::nullopt;
        newBw = qBound(limits.bwMinDah, hi - wantedLo, limits.bwMaxDah);
        newHi = hi;
        newLo = hi - newBw;
        if (newLo < 0)
            return std::nullopt;
    }

    if (limits.isLocked) {
        if (newBw == current.bwDah)
            return std::nullopt;
        return FilterBwIs{newBw, current.isDah};
    }

    const int newIs = (newHi + newLo) / 2;
    if (newIs < limits.isMinDah || newIs > limits.isMaxDah)
        return std::nullopt;
    if (newBw == current.bwDah && newIs == current.isDah)
        return std::nullopt;
    return FilterBwIs{newBw, newIs};
}

QString buildEqCommand(const QString &prefix, const QVector<int> &bands) {
    Q_ASSERT(bands.size() == 8);
    QString cmd = prefix;
    for (int i = 0; i < 8; i++) {
        int value = (i < bands.size()) ? bands[i] : 0;
        cmd += QString("%1%2").arg(value >= 0 ? '+' : '-').arg(qAbs(value), 2, 10, QChar('0'));
    }
    return cmd;
}

int slTierToFrameSamples(int sl) {
    // K4 SL tier → Opus frame size (samples/channel at 12kHz)
    // Verified via pcap: SL0=240, SL1-2=480, SL3-5=720, SL6-7=1440
    switch (sl) {
    case 0:
        return 240; // 20ms
    case 1:
    case 2:
        return 480; // 40ms
    case 3:
    case 4:
    case 5:
        return 720; // 60ms
    case 6:
    case 7:
        return 1440; // 120ms
    default:
        return 720; // Default to SL3 tier
    }
}

FixedTuneMode fixedTuneModeFromCat(int fxt, int fxa) {
    if (fxt == 0)
        return FixedTuneMode::Track;
    switch (fxa) {
    case 0:
        return FixedTuneMode::Fixed1;
    case 1:
        return FixedTuneMode::Fixed2;
    case 2:
        return FixedTuneMode::Slide1;
    case 3:
        return FixedTuneMode::Static;
    case 4:
        return FixedTuneMode::Slide2;
    default:
        return FixedTuneMode::Track;
    }
}

QString fixedTuneSetCommand(FixedTuneMode mode) {
    switch (mode) {
    case FixedTuneMode::Track:
        return QStringLiteral("#FXT0;"); // FXA irrelevant when tracking
    case FixedTuneMode::Fixed1:
        return QStringLiteral("#FXA0;#FXT1;");
    case FixedTuneMode::Fixed2:
        return QStringLiteral("#FXA1;#FXT1;");
    case FixedTuneMode::Slide1:
        return QStringLiteral("#FXA2;#FXT1;");
    case FixedTuneMode::Static:
        return QStringLiteral("#FXA3;#FXT1;");
    case FixedTuneMode::Slide2:
        return QStringLiteral("#FXA4;#FXT1;");
    }
    return QStringLiteral("#FXT0;");
}

qint64 snapFreqToStep(qint64 freq, int stepHz) {
    // Integer division truncates toward zero; tune frequencies are positive, so this floors.
    return (stepHz > 0) ? (freq / stepHz) * stepHz : freq;
}

qint64 vfoWheelTuneTarget(quint64 currentHz, int steps, int stepHz, bool locked) {
    if (locked)
        return 0;
    const qint64 target = static_cast<qint64>(currentHz) + static_cast<qint64>(steps) * stepHz;
    return target > 0 ? target : 0;
}

int vfosMovedByCat(const QString &command) {
    QString cmd = command.trimmed();
    if (cmd.endsWith(QLatin1Char(';')))
        cmd.chop(1);
    int split = 0;
    while (split < cmd.size() && cmd.at(split).isLetter())
        ++split;
    const QString prefix = cmd.left(split).toUpper();
    const QString args = cmd.mid(split);
    auto allDigits = [](const QString &s) {
        return std::all_of(s.cbegin(), s.cend(), [](QChar c) { return c.isDigit(); });
    };

    if (prefix == QLatin1String("FA") && !args.isEmpty() && allDigits(args))
        return VfoA;
    if (prefix == QLatin1String("FB") && !args.isEmpty() && allDigits(args))
        return VfoB;
    if ((prefix == QLatin1String("UP") || prefix == QLatin1String("DN")) && allDigits(args))
        return VfoA;
    if ((prefix == QLatin1String("UPB") || prefix == QLatin1String("DNB")) && allDigits(args))
        return VfoB;
    if (prefix == QLatin1String("VO") && !args.isEmpty())
        return args.startsWith(QLatin1Char('$')) ? (args.size() > 1 ? VfoB : VfoNone) : VfoA;
    if (prefix == QLatin1String("AB") && args.size() == 1) {
        switch (args.at(0).toLatin1()) {
        case '0': // FA > FB
        case '3': // all A > B
            return VfoB;
        case '1': // FB > FA
        case '4': // all B > A
            return VfoA;
        case '2': // swap
        case '5': // all swap
            return VfoA | VfoB;
        default:
            return VfoNone;
        }
    }
    return VfoNone;
}

bool textDecodeSetAllowed(int mdCode) {
    return mdCode == 3 || mdCode == 7 || mdCode == 6 || mdCode == 9;
}

bool apfAvailable(int mdCode) {
    return mdCode == 3 || mdCode == 7;
}

bool catMovesLockedVfo(const QString &command, bool lockA, bool lockB) {
    const int moved = vfosMovedByCat(command);
    return ((moved & VfoA) && lockA) || ((moved & VfoB) && lockB);
}

QString agcLabel(int agcSpeed) {
    switch (agcSpeed) {
    case 1:
        return QStringLiteral("AGC-S");
    case 2:
        return QStringLiteral("AGC-F");
    default:
        return QStringLiteral("AGC");
    }
}

bool agcActive(int agcSpeed) {
    return agcSpeed == 1 || agcSpeed == 2;
}

QString txBandwidthLabel(int bwHundredsHz) {
    return QStringLiteral("%1k").arg(bwHundredsHz / 10.0, 0, 'f', 1);
}

int celsiusToFahrenheit(int celsius) {
    return qRound(celsius * 9.0 / 5.0 + 32.0);
}

bool isValidIpv4(const QString &s) {
    const QStringList parts = s.split('.');
    if (parts.size() != 4)
        return false;
    for (const QString &part : parts) {
        if (part.isEmpty())
            return false;
        // WHY: a leading zero makes the octet ambiguous. inet_aton() and several resolvers read
        // "010" as OCTAL 8, not 10, so accepting it would let QK4 store an address that another
        // part of the stack resolves to a different host. A bare "0" is still a valid octet.
        if (part.size() > 1 && part.startsWith(QLatin1Char('0')))
            return false;
        bool ok = false;
        const int octet = part.toInt(&ok);
        if (!ok || octet < 0 || octet > 255)
            return false;
        // toInt accepts a leading '+' / '-'; reject anything non-digit. WHY the two checks are
        // both needed: QChar::isDigit() is Unicode-aware and would pass non-ASCII digits, while
        // toInt() is locale-independent and rejects them. Callers arriving through
        // isValidHostOrIp() are already filtered by an ASCII-only gate, but this is public API.
        for (const QChar c : part) {
            if (!c.isDigit())
                return false;
        }
    }
    return true;
}

bool isValidHostOrIp(const QString &s) {
    const QString t = s.trimmed();
    if (t.isEmpty())
        return false;

    // IPv6 literal — QHostAddress parses these strictly (unlike its lenient IPv4 handling).
    if (t.contains(':')) {
        QHostAddress addr;
        return addr.setAddress(t) && addr.protocol() == QAbstractSocket::IPv6Protocol;
    }

    // WHY: an all-digits-and-dots string is an *attempted* IPv4 and must be a *valid*
    // one — otherwise "192.168.100.500" would slip through the hostname branch below.
    static const QRegularExpression numericLike(QStringLiteral("^[0-9.]+$"));
    if (numericLike.match(t).hasMatch())
        return isValidIpv4(t);

    // RFC-1123 hostname: dot-separated labels, total length <= 253.
    if (t.length() > 253)
        return false;
    static const QRegularExpression label(QStringLiteral("^[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?$"));
    const QStringList labels = t.split('.');
    for (const QString &lbl : labels) {
        if (!label.match(lbl).hasMatch())
            return false;
    }
    return true;
}

} // namespace RadioUtils
