#ifndef TCPCLIENT_H
#define TCPCLIENT_H

#include <QElapsedTimer>
#include <QObject>
#include <QSslSocket>
#include <QThread>
#include <QTimer>
#include <atomic>
#include "connect_failure.h"
#include "protocol.h"

class TcpClient : public QObject {
    Q_OBJECT

public:
    enum ConnectionState { Disconnected, Connecting, Authenticating, Connected };
    Q_ENUM(ConnectionState)

    explicit TcpClient(QObject *parent = nullptr);
    ~TcpClient();

    /**
     * @brief Connect to a K4/0 server, authenticate, and enter the streaming state.
     *
     * @param host    IPv4 or IPv6 address / hostname of the K4/0 server.
     * @param port    TCP port. 9205 = unencrypted (SHA-384 auth); 9204 = TLS/PSK.
     * @param password Account password. Used as the PSK when @p useTls is true.
     * @param useTls  Select the encrypted transport (TLS + PSK on port 9204).
     * @param identity PSK identity. Only used in TLS mode; empty is accepted.
     * @param encodeMode Opus encode profile sent to the K4 in the startup macro.
     *                   Valid range: 0 = RAW32, 1 = RAW16, 2 = Opus Int, 3 = Opus Float (default).
     *                   Values outside 0-3 are undefined.
     * @param streamingLatency SL tier (audio packet bundling). Valid range 0–7; there are four
     *                   distinct tiers — 20/40/60/120 ms per packet, verified in
     *                   `memory/k4-streaming-latency.md`. Default 3 ≈ 40 ms.
     *
     * Must be called on the IO thread (or via `QMetaObject::invokeMethod`); annotated Q_INVOKABLE
     * so callers on the main thread can queue it.
     */
    Q_INVOKABLE void connectToHost(const QString &host, quint16 port, const QString &password, bool useTls = false,
                                   const QString &identity = QString(), int encodeMode = 3, int streamingLatency = 3);
    Q_INVOKABLE void disconnectFromHost();
    bool isConnected() const;
    ConnectionState connectionState() const;
    bool isUsingTls() const { return m_useTls; }

    Q_INVOKABLE void sendCAT(const QString &command);
    Q_INVOKABLE void sendRaw(const QByteArray &data);

    // WHY: lets the KPOD+ EP02 reader (HighPriority worker thread) deliver
    // KZ batches straight to the I/O thread via a queued connection,
    // bypassing the main thread. Trims trailing NUL padding from the
    // device's 32-byte fixed-size frames and dispatches through sendCAT()
    // so existing K4-packet framing and the cross-thread marshal apply.
    Q_INVOKABLE void sendCATBytes(const QByteArray &raw);

    // Pre-RDY command string sent before the state dump on connect.
    // Must be called before connectToHost() — both are queued to the IO thread,
    // so ordering is guaranteed as long as the caller doesn't interleave.
    Q_INVOKABLE void setStartupMacro(const QString &macro) { m_startupMacro = macro; }

    Protocol *protocol() { return m_protocol; }

    int latencyMs() const { return m_latencyMs; }

signals:
    void stateChanged(ConnectionState state);
    void connected();
    void disconnected();
    void errorOccurred(const QString &error);
    void authenticated();
    void latencyChanged(int ms);

private:
    // Classifies a failed connection attempt and emits errorOccurred() if it amounts to one.
    // The decision itself lives in connect_failure.h so it can be tested without a socket.
    void reportFailure(ConnectFailure::Event event, ConnectFailure::Phase phase,
                       const QString &socketErrorText = QString());

private slots:
    void onSocketConnected();
    void onSocketEncrypted();
    void onSocketDisconnected();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);
    void onSslErrors(const QList<QSslError> &errors);
    void onPreSharedKeyAuthenticationRequired(QSslPreSharedKeyAuthenticator *authenticator);
    void onConnectTimeout();
    void onAuthTimeout();
    void onPingTimer();
    void onCatResponse(const QString &response);

private:
    void setState(ConnectionState state);
    void sendAuthentication();
    void startPingTimer();
    void stopPingTimer();
    void attemptConnection();

    QSslSocket *m_socket;
    Protocol *m_protocol;
    QTimer *m_authTimer;
    QTimer *m_connectTimer;
    QTimer *m_pingTimer;
    QTimer *m_retryTimer;
    int m_retryCount = 0;

    QString m_host;
    quint16 m_port;
    QString m_password; // Also used as PSK when TLS enabled
    bool m_useTls;
    QString m_identity;     // TLS-PSK identity (optional)
    int m_encodeMode;       // Audio encode mode (0-3)
    int m_streamingLatency; // Remote streaming audio latency (0-7)
    // WHY atomic: m_state is written on the IO thread (this object's affinity)
    // by setState() and read on the main thread via connectionState() →
    // ConnectionController::connectionState() → MainWindow seeding paths. A
    // plain enum read/written across threads is a data race (UB on weakly
    // ordered architectures). Writes use memory_order_release; reads use
    // memory_order_acquire — including the IO-thread internal reads, so the
    // pattern is uniform (compiles to the same code as relaxed on x86 and one
    // dmb ish on ARM). Sister field m_connected has the same shape for the
    // same reason.
    std::atomic<ConnectionState> m_state{Disconnected};
    std::atomic<bool> m_connected{false}; // Thread-safe read for isConnected()
    bool m_authResponseReceived;

    QElapsedTimer m_pingElapsed;
    int m_latencyMs = -1;
    QString m_startupMacro; // Sent before RDY so state dump reflects macro changes
};

#endif // TCPCLIENT_H
