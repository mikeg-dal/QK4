#include "kpa1500antennas.h"

namespace Kpa1500Antennas {

QVector<int> parseEnableMap(const QString &map32) {
    if (map32.size() != kMaxAntenna)
        return {};
    QVector<int> connectors;
    connectors.reserve(kMaxAntenna);
    for (const QChar c : map32) {
        if (c == QLatin1Char('D'))
            connectors.append(0);
        else if (c == QLatin1Char('1'))
            connectors.append(1);
        else if (c == QLatin1Char('2'))
            connectors.append(2);
        else
            return {};
    }
    return connectors;
}

QString label(int antenna, int connector) {
    if (antenna < 1 || antenna > kMaxAntenna)
        return QString();
    if (antenna <= 2)
        return QStringLiteral("ANT%1").arg(antenna);
    if (connector == 1 || connector == 2)
        return QStringLiteral("ANT%1:%2").arg(connector).arg(antenna);
    return QStringLiteral("ANT:%1").arg(antenna);
}

} // namespace Kpa1500Antennas
