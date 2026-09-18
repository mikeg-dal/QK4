#ifndef CONNECTIONCONTROLLER_H
#define CONNECTIONCONTROLLER_H

#include <QObject>
#include <QThread>
#include <atomic>
#include "network/tcpclient.h"
#include "settings/radiosettings.h"

class NetworkMetrics;
class RadioState;

/**
 * @brief Owns the IO thread + TcpClient + NetworkMetrics. Exposes connection lifecycle and a
 *        thread-safe `sendCAT()` / `sendRawPacket()` interface. Re-emits Protocol's inbound
 *        signals so callers never reach into `tcpClient()->protocol()`.
 *
 * Rule 2 exception: `tcpClient()` is exposed only for AudioController (TX audio bypass) and
 * CatServer (direct CAT forwarding). See CONVENTIONS.md rule 2 for the rationale.
 */
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

    // KPOD+ keyer ownership gate. Written from the main thread when the
    // KPOD+ device polling toggles; read on the I/O thread by queued
    // lambdas that forward local-iambic KZ commands. When true, the local
    // iambic path drops its KZ emissions because the KPOD+ device is the
    // authoritative keyer.
    // Memory ordering: release on store, acquire on load. The gate is written on main
    // (deviceConnected / deviceDisconnected handlers) and read on the I/O thread and on
    // the HaliKey worker threads. Acquire/release pairs guarantee that any state set up
    // before the writer flipped the gate is visible to readers after they observe the flip.
    void setKpodPlusKeyerActive(bool active) { m_kpodPlusKeyerActive.store(active, std::memory_order_release); }
    bool isKpodPlusKeyerActive() const { return m_kpodPlusKeyerActive.load(std::memory_order_acquire); }

signals:
    void radioReady();                                             // Auth succeeded, K4 is live
    void connectionError(const QString &error);                    // Connection error
    void connectionStateChanged(TcpClient::ConnectionState state); // Any state transition
    // (Observe connectionStateChanged(TcpClient::Disconnected) for the disconnect event.)

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
    std::atomic<bool> m_kpodPlusKeyerActive{false};
};

#endif // CONNECTIONCONTROLLER_H
