#ifndef CONNECTIONCONTROLLER_H
#define CONNECTIONCONTROLLER_H

#include <QObject>
#include <QThread>
#include "network/tcpclient.h"
#include "settings/radiosettings.h"

class NetworkMetrics;
class RadioState;

class ConnectionController : public QObject {
    Q_OBJECT

public:
    explicit ConnectionController(RadioState *radioState, QObject *parent = nullptr);
    ~ConnectionController();

    // Connection lifecycle
    void connectToRadio(const RadioEntry &radio);
    void disconnectFromRadio();

    // Send CAT command to K4 (thread-safe — uses QueuedConnection internally)
    void sendCAT(const QString &command);

    // Send raw binary packet to K4 (for TX audio — thread-safe)
    void sendRawPacket(const QByteArray &packet);

    // State queries
    bool isConnected() const;
    TcpClient::ConnectionState connectionState() const;
    const RadioEntry &currentRadio() const { return m_currentRadio; }
    void setDisplayFps(int fps) { m_currentRadio.displayFps = fps; }

    // Access to owned objects
    // Prefer using re-emitted signals below instead of reaching into tcpClient/protocol directly.
    // tcpClient() is exposed for AudioController's performance-sensitive audio data path
    // and for CatServer's direct TCP forwarding — both require the raw TcpClient.
    TcpClient *tcpClient() const { return m_tcpClient; }
    NetworkMetrics *networkMetrics() const { return m_networkMetrics; }

signals:
    void radioReady();                                             // Auth succeeded, K4 is live
    void connectionLost();                                         // Disconnected (clean or error)
    void connectionError(const QString &error);                    // Connection error
    void authFailed();                                             // Authentication failed
    void connectionStateChanged(TcpClient::ConnectionState state); // Any state transition

    // Re-emitted Protocol signals (eliminates tcpClient()->protocol() chains from callers)
    void catResponseReceived(const QString &response);
    void spectrumDataReceived(int receiver, const QByteArray &payload, int binsOffset, int binCount, qint64 centerFreq,
                              qint32 sampleRate, float noiseFloor);
    void miniSpectrumDataReceived(int receiver, const QByteArray &payload, int binsOffset, int binCount);

private slots:
    void onStateChanged(TcpClient::ConnectionState state);

private:
    TcpClient *m_tcpClient;
    QThread *m_ioThread = nullptr;
    NetworkMetrics *m_networkMetrics;
    RadioState *m_radioState; // not owned
    RadioEntry m_currentRadio;
};

#endif // CONNECTIONCONTROLLER_H
