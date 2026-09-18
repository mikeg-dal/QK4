#include "ft8types.h"
#include <QRegularExpression>

namespace Ft8 {
const QVector<Band> &bands() {
    // Common working frequencies, not band restrictions. Additional regional
    // channels and expedition frequencies are entered through Custom.
    static const QVector<Band> table = {
        {"160m", 1800000, 2000000, 1840000, 0},          {"80m", 3500000, 4000000, 3573000, 3575000},
        {"60m", 5250000, 5450000, 5357000, 0},           {"40m", 7000000, 7300000, 7074000, 7047500},
        {"30m", 10100000, 10150000, 10136000, 10140000}, {"20m", 14000000, 14350000, 14074000, 14080000},
        {"17m", 18068000, 18168000, 18100000, 18104000}, {"15m", 21000000, 21450000, 21074000, 21140000},
        {"12m", 24890000, 24990000, 24915000, 24919000}, {"10m", 28000000, 29700000, 28074000, 28180000},
        {"6m", 50000000, 54000000, 50313000, 50318000}};
    return table;
}
QString bandFor(qint64 hz) {
    for (const auto &b : bands())
        if (hz >= b.lowHz && hz <= b.highHz)
            return b.name;
    return {};
}
bool validCall(const QString &text) {
    static const QRegularExpression re("^(?=.{3,11}$)(?=.*[A-Z])(?=.*[0-9])[A-Z0-9]+(?:/[A-Z0-9]+)?$");
    return re.match(text).hasMatch();
}
bool validGrid(const QString &text) {
    static const QRegularExpression re("^[A-R]{2}[0-9]{2}([A-X]{2})?$");
    return re.match(text).hasMatch();
}
QString reportText(int report) {
    return QString(report < 0 ? "-" : "+") + QString::number(qAbs(report)).rightJustified(2, '0');
}
QString Decode::id() const {
    return QString::number(slot(utc.toMSecsSinceEpoch(), mode)) + ":" + modeName(mode) + ":" + message;
}
Message parseMessage(const QString &text) {
    Message m;
    const auto words = text.trimmed().toUpper().split(' ', Qt::SkipEmptyParts);
    if (words.size() < 2)
        return m;
    if (words[0] == "CQ" || words[0] == "QRZ") {
        m.cq = true;
        int callIndex = 1;
        if (!validCall(words[callIndex]) && words.size() > 2)
            ++callIndex;
        if (!validCall(words[callIndex]))
            return m;
        m.from = words[callIndex];
        if (words.size() > callIndex + 1 && validGrid(words[callIndex + 1]))
            m.grid = words[callIndex + 1];
        m.valid = true;
        return m;
    }
    if (words.size() != 3 || !validCall(words[0]) || !validCall(words[1]))
        return m;
    m.to = words[0];
    m.from = words[1];
    m.payload = words[2];
    // RR73 also matches the shape of a four-character locator. In a directed
    // exchange it is the acknowledgment token, never a grid/report request.
    if (m.payload != "RR73" && validGrid(m.payload))
        m.grid = m.payload;
    static const QRegularExpression report("^R?[+-][0-9]{2}$");
    m.valid = !m.grid.isEmpty() || report.match(m.payload).hasMatch() || m.payload == "RRR" || m.payload == "RR73" ||
              m.payload == "73";
    return m;
}
} // namespace Ft8
