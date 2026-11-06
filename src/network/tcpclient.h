#ifndef TCPCLIENT_H
#define TCPCLIENT_H

#include <QElapsedTimer>
#include <QObject>
#include <QSslSocket>
#include <QThread>
#include <QTimer>
#include <atomic>
#include "connect_failure.h"
#include "pingtracker.h"
#include "protocol.h"
#include "settings/radiosettings.h"
#include "utils/latencyhistogram.h"

class KzEgress;

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
     * @param encodeMode Audio encode mode, sent as `EMn;` once the radio has accepted the session.
     *                   0=RAW S32LE (24-bit), 1=RAW S16LE, 2/3=Opus (same bitstream, int vs float
     *                   decode); 0-3, EM3 the radio's default (Programmer's Reference D12, EM).
     * @param streamingLatency Streaming audio latency, sent as `SLn;` after `EMn;`. 0-7, 3 the
     *                   radio's default (Programmer's Reference D12, SL). The tiers give 20/40/60/120
     *                   ms packets (SL0, SL1-2, SL3-5, SL6-7), so the default is 60 ms; the map is
     *                   RadioUtils::slTierToFrameSamples().
     *
     * Must be called on the IO thread; callers elsewhere queue it with the functor overload of
     * `QMetaObject::invokeMethod` (checked at compile time).
     */
    void connectToHost(const QString &host, quint16 port, const QString &password, bool useTls = false,
                       const QString &identity = QString(), int encodeMode = RadioEntry::EncodeModeDefault,
                       int streamingLatency = RadioEntry::StreamingLatencyDefault);
    void disconnectFromHost();
    bool isConnected() const;
    ConnectionState connectionState() const;

    void sendCAT(const QString &command);
    void sendRaw(const QByteArray &data);

    // Where the CW keying threads post KZ and ML frames (the keyer, and the KPOD+'s own KZ strings).
    // Posting is lock-free and allocation-free from any thread; this object writes the frames from
    // the I/O thread, in order. Lives as long as this TcpClient.
    KzEgress *kzEgress() const { return m_kzEgress; }

    // Pre-RDY command string sent before the state dump on connect.
    // Must be called before connectToHost() — both are queued to the IO thread,
    // so ordering is guaranteed as long as the caller doesn't interleave.
    void setStartupMacro(const QString &macro) { m_startupMacro = macro; }

    Protocol *protocol() { return m_protocol; }

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
    void writeKzFrames();
    void logKzLatency();
    void sendAuthentication();
    void startPingTimer();
    void stopPingTimer();
    void attemptConnection();
    enum class Close { Graceful, Abort };
    void endSession(Close how);

    QSslSocket *m_socket;
    Protocol *m_protocol;
    QTimer *m_authTimer;
    QTimer *m_connectTimer;
    QTimer *m_pingTimer;
    QTimer *m_retryTimer;
    int m_retryCount = 0;
    // sendRaw() calls refused because the session was not Connected, since the last state change.
    // I/O thread only.
    quint64 m_rawDropped = 0;
    // Bumped by every connectToHost() and endSession(), so an asynchronous step started for
    // one attempt (the mDNS lookup) can tell that it has since been replaced. I/O thread only.
    quint64 m_attemptId = 0;

    QString m_host;
    quint16 m_port;
    QString m_password; // Also used as PSK when TLS enabled
    bool m_useTls;
    QString m_identity;                                           // TLS-PSK identity (optional)
    int m_encodeMode = RadioEntry::EncodeModeDefault;             // Audio encode mode (0-3)
    int m_streamingLatency = RadioEntry::StreamingLatencyDefault; // Remote streaming audio latency (0-7)
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

    KzEgress *m_kzEgress;
    // Enqueue on the keying thread to the socket write here, per frame, since the session began.
    // Written on the I/O thread only. Logged once a minute while keying and when the session ends.
    LatencyHistogram m_kzLatency;
    std::uint64_t m_kzLatencyLogged = 0; // samples covered by the last summary
    int m_pingTicks = 0;

    // One monotonic clock per session, started when it reaches Connected; every PING's send time
    // and every PONG's arrival are read from it. See PingTracker for why not a timer per ping.
    QElapsedTimer m_linkClock;
    PingTracker m_pings;
    QString m_startupMacro; // Sent before RDY so state dump reflects macro changes
};

#endif // TCPCLIENT_H
