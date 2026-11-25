#include "models/memorymodel.h"

#include "utils/bandplan.h"

#include <QStringList>

namespace {

constexpr int FieldsPerSide = 7;
constexpr int FieldCount = 1 + 2 * FieldsPerSide; // label + VFO A + VFO B

// Reads one side's seven fields out of @p fields starting at @p first.
MemoryChannel::Side sideFrom(const QStringList &fields, int first) {
    MemoryChannel::Side side;
    side.bandwidthKhz = fields.at(first);
    side.shiftKhz = fields.at(first + 1);
    side.frequencyMhz = fields.at(first + 2);
    side.mode = fields.at(first + 3);
    side.panCentreMhz = fields.at(first + 4);
    side.spanKhz = fields.at(first + 5);
    side.unidentified = fields.at(first + 6);
    return side;
}

// The channel number carried by a command like MC002, MCNF003 or MCDF021. Returns -1 when the
// trailing text is not a channel number in range — which is what the `MCDF?;` refusal looks like.
int channelNumberIn(QStringView digits) {
    if (digits.isEmpty())
        return -1;
    bool ok = false;
    const int number = digits.toInt(&ok);
    if (!ok || number < MemoryModel::MinChannel || number > MemoryModel::MaxChannel)
        return -1;
    return number;
}

} // namespace

qint64 MemoryChannel::Side::frequencyHz() const {
    if (frequencyMhz.isEmpty())
        return 0;
    bool ok = false;
    const double mhz = frequencyMhz.toDouble(&ok);
    if (!ok || mhz <= 0.0)
        return 0;
    return static_cast<qint64>(mhz * 1e6 + 0.5);
}

QString MemoryChannel::displayLabel() const {
    if (!label.isEmpty())
        return label;
    return QStringLiteral("#%1").arg(number, 3, 10, QChar('0'));
}

QStringList MemoryChannel::fields() const {
    return {label,          a.bandwidthKhz, a.shiftKhz,     a.frequencyMhz, a.mode,
            a.panCentreMhz, a.spanKhz,      a.unidentified, b.bandwidthKhz, b.shiftKhz,
            b.frequencyMhz, b.mode,         b.panCentreMhz, b.spanKhz,      b.unidentified};
}

MemoryModel::MemoryModel(QObject *parent) : QObject(parent) {}

bool MemoryModel::parseMCDF(const QString &line) {
    QString body = line.trimmed();
    if (!body.startsWith(QLatin1String("MCDF")))
        return false;
    if (body.endsWith(QLatin1Char(';')))
        body.chop(1);
    body = body.mid(4); // past "MCDF"

    const int comma = body.indexOf(QLatin1Char(','));
    if (comma < 0)
        return false; // "MCDF?" and a bare "MCDF" carry no fields

    const int number = channelNumberIn(QStringView(body).left(comma));
    if (number < 0)
        return false;

    // Qt drops a trailing empty field unless asked to keep every part, and the last field of a
    // blank channel is empty: without KeepEmptyParts a blank channel parses one field short.
    const QStringList fields = body.mid(comma + 1).split(QLatin1Char(','), Qt::KeepEmptyParts);
    if (fields.size() != FieldCount)
        return false;

    MemoryChannel channel;
    channel.number = number;
    channel.label = fields.at(0);
    channel.a = sideFrom(fields, 1);
    channel.b = sideFrom(fields, 1 + FieldsPerSide);

    m_channels.insert(number, channel);
    emit channelChanged(number);
    return true;
}

bool MemoryModel::parseSelection(const QString &line) {
    QString body = line.trimmed();
    if (body.endsWith(QLatin1Char(';')))
        body.chop(1);

    // MC002, MCNF003, MCNU021 — all report the channel now selected.
    int prefixLength = 0;
    for (const QLatin1String prefix : {QLatin1String("MCNF"), QLatin1String("MCNU"), QLatin1String("MC")}) {
        if (body.startsWith(prefix)) {
            prefixLength = prefix.size();
            break;
        }
    }
    if (prefixLength == 0)
        return false;

    const int number = channelNumberIn(QStringView(body).mid(prefixLength));
    if (number < 0)
        return false;
    if (number != m_selectedChannel) {
        m_selectedChannel = number;
        emit selectedChannelChanged(number);
    }
    return true;
}

void MemoryModel::clear() {
    if (m_channels.isEmpty() && m_selectedChannel < 0)
        return;
    m_channels.clear();
    m_selectedChannel = -1;
    emit modelCleared();
}

const MemoryChannel *MemoryModel::channel(int number) const {
    const auto it = m_channels.constFind(number);
    return it == m_channels.constEnd() ? nullptr : &it.value();
}

QList<int> MemoryModel::channelsInUse() const {
    QList<int> result;
    for (auto it = m_channels.constBegin(); it != m_channels.constEnd(); ++it) {
        if (!it.value().isBlank())
            result.append(it.key());
    }
    return result; // QMap iterates in key order
}

QList<int> MemoryModel::firstMatching(int count, bool inHamBand) const {
    QList<int> result;
    if (count <= 0)
        return result;
    for (auto it = m_channels.constBegin(); it != m_channels.constEnd() && result.size() < count; ++it) {
        const MemoryChannel &channel = it.value();
        if (channel.isBlank() || !channel.a.isSet())
            continue;
        if (BandPlan::bandName(channel.a.frequencyHz()).isEmpty() == inHamBand)
            continue;
        result.append(it.key());
    }
    return result;
}

QList<int> MemoryModel::firstInHamBands(int count) const {
    return firstMatching(count, /*inHamBand=*/true);
}

QList<int> MemoryModel::firstOutsideHamBands(int count) const {
    return firstMatching(count, /*inHamBand=*/false);
}
