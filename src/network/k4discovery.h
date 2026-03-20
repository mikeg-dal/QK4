// K4Discovery - Network discovery for Elecraft K4 radios

#ifndef K4DISCOVERY_H
#define K4DISCOVERY_H

#include <QObject>
#include <QUdpSocket>
#include <QNetworkInterface>
#include <QTimer>
#include <QList>

struct K4RadioInfo {
    QString rigType;      // "k4" or "k4z"
    int rigIndex;         // Radio index (typically 0)
    QString ipAddress;    // IP address (e.g., "192.168.1.100")
    QString serialNumber; // Serial number (e.g., "278")

    QString hostname() const {
        QString prefix = (rigType.toLower() == "k4z") ? "K4Z" : "K4";
        return QString("%1-SN%2.local").arg(prefix).arg(serialNumber.rightJustified(5, '0'));
    }

    bool isK4Zero() const { return rigType.toLower() == "k4z"; }
};

class K4Discovery : public QObject {
    Q_OBJECT

public:
    explicit K4Discovery(QObject *parent = nullptr);
    ~K4Discovery() override;

    void startDiscovery();
    QList<K4RadioInfo> discoveredRadios() const { return m_discoveredRadios; }
    static constexpr int TIMEOUT_MS = 5000;

signals:
    void radioFound(const K4RadioInfo &radio);
    void discoveryFinished(int count);
    void error(const QString &errorMessage);

private slots:
    void onReadyRead();
    void onDiscoveryTimeout();

private:
    void sendDiscoveryMessage(const QNetworkInterface &netInterface);
    bool parseK4Response(const QByteArray &data, K4RadioInfo &radioInfo);

    static constexpr int UDP_PORT = 9100;
    static const char *DISCOVERY_MESSAGE;
    static const char *K4_RESPONSE_PREFIX;

    QList<QUdpSocket *> m_sockets;
    QTimer *m_timeoutTimer;
    QList<K4RadioInfo> m_discoveredRadios;
    bool m_discoveryActive;
};

#endif // K4DISCOVERY_H
