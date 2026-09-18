#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QVector>
#include <optional>

namespace Ft8 {
enum class Mode { FT8, FT4 };
inline QString modeName(Mode mode) {
    return mode == Mode::FT4 ? "FT4" : "FT8";
}
inline int periodMs(Mode mode) {
    return mode == Mode::FT4 ? 7500 : 15000;
}
inline qint64 slot(qint64 utcMs, Mode mode) {
    return utcMs / periodMs(mode);
}
struct Band {
    QString name;
    qint64 lowHz, highHz, ft8Hz, ft4Hz;
};
const QVector<Band> &bands();
QString bandFor(qint64 hz);
bool validCall(const QString &text);
bool validGrid(const QString &text);
QString reportText(int report);

struct Decode {
    QDateTime utc;
    Mode mode = Mode::FT8;
    QString message;
    int audioHz = 1500;
    double dt = 0;
    std::optional<int> snr;
    int syncScore = 0;
    bool practice = false;
    QString id() const;
};
struct Message {
    QString from, to, grid, payload;
    bool cq = false;
    bool valid = false;
};
Message parseMessage(const QString &text);
} // namespace Ft8
Q_DECLARE_METATYPE(Ft8::Decode)
Q_DECLARE_METATYPE(QVector<Ft8::Decode>)
