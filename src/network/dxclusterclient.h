#ifndef DXCLUSTERCLIENT_H
#define DXCLUSTERCLIENT_H

#include <QDateTime>
#include <QLoggingCategory>
#include <QObject>
#include <QRegularExpression>
#include <QString>
#include <QTcpSocket>

Q_DECLARE_LOGGING_CATEGORY(netDxCluster)

/**
 * @brief Parsed representation of a single DX cluster spot (spotter, spotted, frequency, mode,
 *        comment, UTC time). Consumed by DxSpotOverlay for panadapter rendering.
 */
struct DxSpot {
    QString spotterCall;
    QString spottedCall;
    qint64 frequencyHz = 0;
    QString mode;
    QString comment;
    QString timeUtc;
    QDateTime timestamp;
    // Which DxClusterController row the spot came from; -1 until the controller stamps it. Lets one
    // cluster's disconnect remove only its own spots.
    int clusterIndex = -1;
};

/**
 * @brief TCP client for a single DX cluster node. Owned by DxClusterController (one instance per
 *        cluster, each on its own thread). `parseSpotLine()` is static for unit-testability.
 *        Emits typed DxSpot objects plus raw text lines for the console view.
 */
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

    // Static for testability — strips ANSI CSI escape sequences and non-printable
    // C0/DEL control characters (tab preserved) from a received line. Cluster servers
    // emit BEL with spots (DXSpider beep flag — verified against n7od.pentux.net:7300,
    // which appends two 0x07 bytes to every spot) and can emit ANSI colors (set/ansi);
    // both render as garbage in the console and break the \s*$-anchored spot regex.
    static QString sanitizeLine(const QString &raw);

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
    static const QRegularExpression s_ansiRegex;
};

#endif // DXCLUSTERCLIENT_H
