#ifndef DXCLUSTERCLIENT_H
#define DXCLUSTERCLIENT_H

#include <QDateTime>
#include <QObject>
#include <QRegularExpression>
#include <QString>
#include <QTcpSocket>

struct DxSpot {
    QString spotterCall;
    QString spottedCall;
    qint64 frequencyHz = 0;
    QString mode;
    QString comment;
    QString timeUtc;
    QDateTime timestamp;
};

class DxClusterClient : public QObject {
    Q_OBJECT

public:
    enum ConnectionState { Disconnected, Connecting, Connected };
    Q_ENUM(ConnectionState)

    explicit DxClusterClient(QObject *parent = nullptr);
    ~DxClusterClient();

    ConnectionState connectionState() const { return m_state; }

    // Static for testability — parses a single DX spot line
    static bool parseSpotLine(const QString &line, DxSpot &spot);

public slots:
    void connectToHost(const QString &host, quint16 port, const QString &callsign);
    void disconnectFromHost();
    void sendCommand(const QString &command);

signals:
    void stateChanged(DxClusterClient::ConnectionState state);
    void spotReceived(const DxSpot &spot);
    void rawLineReceived(const QString &line);
    void errorOccurred(const QString &error);

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);

private:
    void setState(ConnectionState state);
    void processLine(const QString &line);

    QTcpSocket *m_socket;
    QString m_receiveBuffer;
    QString m_callsign;
    ConnectionState m_state = Disconnected;

    static const QRegularExpression s_spotRegex;
    static const QRegularExpression s_modeRegex;
    static const QRegularExpression s_loginRegex;
};

#endif // DXCLUSTERCLIENT_H
