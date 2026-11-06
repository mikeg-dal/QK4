// K4Discovery - Network discovery for Elecraft K4 radios

#ifndef K4DISCOVERY_H
#define K4DISCOVERY_H

#include <QObject>
#include <QUdpSocket>
#include <QNetworkInterface>
#include <QTimer>
#include <QList>
#include <optional>

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

    // Reads a discovery reply, "<rig type>:<index>:<IPv4 address>:<serial>" (e.g.
    // "k4:0:192.168.1.100:278"). The address becomes the host QK4 connects to, so the reply is
    // refused unless every field is well formed: a strict dotted IPv4 address (the K4 listens on
    // IPv4 only), a numeric index and a numeric serial. Surrounding whitespace and NULs are ignored.
    static std::optional<K4RadioInfo> parseResponse(const QByteArray &data);
    static constexpr int TIMEOUT_MS = 5000;

signals:
    void radioFound(const K4RadioInfo &radio);
    void discoveryFinished(int count);

private slots:
    void onReadyRead();
    void onDiscoveryTimeout();

private:
    void sendDiscoveryMessage(const QNetworkInterface &netInterface);

    static constexpr int UDP_PORT = 9100;
    static const char *DISCOVERY_MESSAGE;
    static const char *K4_RESPONSE_PREFIX;

    QList<QUdpSocket *> m_sockets;
    QTimer *m_timeoutTimer;
    QList<K4RadioInfo> m_discoveredRadios;
    bool m_discoveryActive;
};

#endif // K4DISCOVERY_H
