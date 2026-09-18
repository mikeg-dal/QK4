#include "wsjtbroadcaster.h"

#include <QDataStream>
#include <QDateTime>
#include <QHostAddress>
#include <QSettings>
#include <QTimer>
#include <QTimeZone>
#include <QUdpSocket>

namespace {
constexpr quint32 Magic = 0xadbccbda;
constexpr quint32 Schema = 3;
const QByteArray Id("QK4");

QDateTime adifDateTime(const AdifRecord &record, const char *dateKey, const char *timeKey) {
    const QString date = record.value(QString::fromLatin1(dateKey));
    QString time = record.value(QString::fromLatin1(timeKey));
    while (time.size() < 6)
        time.append(QLatin1Char('0'));
    return QDateTime(QDate::fromString(date, QStringLiteral("yyyyMMdd")),
                     QTime::fromString(time.left(6), QStringLiteral("HHmmss")), QTimeZone::UTC);
}
quint64 dialHz(const AdifRecord &record) {
    bool ok = false;
    const double mhz = record.value(QStringLiteral("FREQ")).toDouble(&ok);
    return ok ? quint64(qRound64(mhz * 1e6)) : 0;
}
} // namespace

WsjtBroadcaster::WsjtBroadcaster(QObject *parent) : QObject(parent) {
    m_socket = new QUdpSocket(this);
    m_timer = new QTimer(this);
    m_timer->setInterval(15000);
    connect(m_timer, &QTimer::timeout, this, &WsjtBroadcaster::sendHeartbeat);
    m_timer->start();
}

QByteArray WsjtBroadcaster::header(quint32 type) {
    QByteArray packet;
    QDataStream out(&packet, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::BigEndian);
    out.setVersion(QDataStream::Qt_5_4);
    out << Magic << Schema << type << Id;
    return packet;
}

bool WsjtBroadcaster::send(const QByteArray &packet) {
    QSettings settings;
    if (!settings.value(QStringLiteral("wsjtUdp/enabled"), false).toBool())
        return false;
    const QHostAddress address(
        settings.value(QStringLiteral("wsjtUdp/address"), QStringLiteral("127.0.0.1")).toString());
    const quint16 port = quint16(settings.value(QStringLiteral("wsjtUdp/port"), 2237).toUInt());
    return !address.isNull() && port > 0 && m_socket->writeDatagram(packet, address, port) == packet.size();
}

void WsjtBroadcaster::sendHeartbeat() {
    QByteArray packet = header(0);
    QDataStream out(&packet, QIODevice::Append);
    out.setByteOrder(QDataStream::BigEndian);
    out.setVersion(QDataStream::Qt_5_4);
    out << Schema << QByteArray("QK4 0.7") << QByteArray();
    send(packet);
}

void WsjtBroadcaster::sendStatus(qint64 frequency, const QString &mode, bool transmitting, int rxHz, int txHz,
                                 const QString &myCall, const QString &myGrid) {
    QByteArray packet = header(1);
    QDataStream out(&packet, QIODevice::Append);
    out.setByteOrder(QDataStream::BigEndian);
    out.setVersion(QDataStream::Qt_5_4);
    out << quint64(qMax<qint64>(0, frequency)) << mode.toUtf8() << QByteArray() << QByteArray() << mode.toUtf8() << true
        << transmitting << false << quint32(rxHz) << quint32(txHz) << myCall.toUtf8() << myGrid.toUtf8() << QByteArray()
        << false << QByteArray() << false << quint8(0) << quint32(0) << quint32(mode == QStringLiteral("FT4") ? 8 : 15)
        << QByteArray("QK4") << QByteArray();
    send(packet);
}

void WsjtBroadcaster::sendDecode(const Ft8::Decode &decode) {
    QByteArray packet = header(2);
    QDataStream out(&packet, QIODevice::Append);
    out.setByteOrder(QDataStream::BigEndian);
    out.setVersion(QDataStream::Qt_5_4);
    out << true << decode.utc.toUTC().time() << qint32(decode.snr.value_or(0)) << decode.dt << quint32(decode.audioHz)
        << Ft8::modeName(decode.mode).toUtf8() << decode.message.toUtf8() << false << false;
    send(packet);
}

void WsjtBroadcaster::sendQsoLogged(const AdifRecord &record) {
    QByteArray packet = header(5);
    QDataStream out(&packet, QIODevice::Append);
    out.setByteOrder(QDataStream::BigEndian);
    out.setVersion(QDataStream::Qt_5_4);
    const QDateTime off = adifDateTime(record, "QSO_DATE_OFF", "TIME_OFF");
    const QDateTime on = adifDateTime(record, "QSO_DATE", "TIME_ON");
    out << off << record.value(QStringLiteral("CALL")).toUtf8() << record.value(QStringLiteral("GRIDSQUARE")).toUtf8()
        << dialHz(record) << record.value(QStringLiteral("MODE")).toUtf8()
        << record.value(QStringLiteral("RST_SENT")).toUtf8() << record.value(QStringLiteral("RST_RCVD")).toUtf8()
        << record.value(QStringLiteral("TX_PWR")).toUtf8() << record.value(QStringLiteral("COMMENT")).toUtf8()
        << record.value(QStringLiteral("NAME")).toUtf8() << on << record.value(QStringLiteral("OPERATOR")).toUtf8()
        << record.value(QStringLiteral("STATION_CALLSIGN")).toUtf8()
        << record.value(QStringLiteral("MY_GRIDSQUARE")).toUtf8() << record.value(QStringLiteral("STX_STRING")).toUtf8()
        << record.value(QStringLiteral("SRX_STRING")).toUtf8() << record.value(QStringLiteral("PROP_MODE")).toUtf8();
    send(packet);
}
