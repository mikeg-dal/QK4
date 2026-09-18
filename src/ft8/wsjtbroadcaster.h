#pragma once

#include "ft8/ft8logbook.h"
#include "ft8/ft8types.h"
#include <QObject>

class QUdpSocket;
class QTimer;

class WsjtBroadcaster final : public QObject {
    Q_OBJECT
public:
    explicit WsjtBroadcaster(QObject *parent = nullptr);
    void sendHeartbeat();
    void sendStatus(qint64 dialFrequency, const QString &mode, bool transmitting, int rxHz, int txHz,
                    const QString &myCall, const QString &myGrid);
    void sendDecode(const Ft8::Decode &decode);
    void sendQsoLogged(const AdifRecord &record);
    static QByteArray header(quint32 type);

private:
    bool send(const QByteArray &packet);
    QUdpSocket *m_socket = nullptr;
    QTimer *m_timer = nullptr;
};
