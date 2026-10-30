#ifndef CONNECTIONCONTROLLER_H
#define CONNECTIONCONTROLLER_H

#include <QObject>
#include <QThread>
#include "network/tcpclient.h"
#include "settings/radiosettings.h"
#include "utils/rtscheduling.h"

class NetworkMetrics;

/**
 * @brief Owns the IO thread + TcpClient + NetworkMetrics. Exposes connection lifecycle and a
 *        thread-safe `sendCAT()` / `sendRawPacket()` interface. Re-emits Protocol's inbound
 *        signals so callers never reach into `tcpClient()->protocol()`.
 *
 * Rule 2 exception: `tcpClient()` is exposed only for AudioController (TX audio bypass) and
 * CwController (keyer-thread KZD/KZU). See CONVENTIONS.md rule 2 for the rationale.
 */
class ConnectionController : public QObject {
    Q_OBJECT

public:
    explicit ConnectionController(QObject *parent = nullptr);
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
    // and for CwController's keyer link, which sends from the keyer thread — both require the raw
    // TcpClient.
    TcpClient *tcpClient() const { return m_tcpClient; }
    NetworkMetrics *networkMetrics() const { return m_networkMetrics; }

signals:
    void radioReady();                                             // Auth succeeded, K4 is live
    void connectionError(const QString &error);                    // Connection error
    void connectionStateChanged(TcpClient::ConnectionState state); // Any state transition
    // (Observe connectionStateChanged(TcpClient::Disconnected) for the disconnect event.)

    // Re-emitted Protocol signals (eliminates tcpClient()->protocol() chains from callers)
    void catResponseReceived(const QString &response);
    void spectrumDataReceived(int receiver, const QByteArray &payload, int binsOffset, int binCount, qint64 centerFreq,
                              qint32 sampleRate, float noiseFloor);
    void miniSpectrumDataReceived(int receiver, const QByteArray &payload, int binsOffset, int binCount,
                                  qint64 centerFreq, int spanHz);

private slots:
    void onStateChanged(TcpClient::ConnectionState state);

private:
    TcpClient *m_tcpClient;
    QThread *m_ioThread = nullptr;
    NetworkMetrics *m_networkMetrics;
    RadioEntry m_currentRadio;
    // Held while connected (macOS: keeps App Nap and timer coalescing off; a no-op elsewhere).
    RtScheduling::LatencyCriticalActivity m_latencyActivity;
};

#endif // CONNECTIONCONTROLLER_H
