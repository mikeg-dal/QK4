#include "tcpclient.h"
#include "logging/logcategories.h"
#include "network/kzegress.h"
#include <QDateTime>
#include <QHostInfo>
#include <QLoggingCategory>
#include <QSslCipher>
#include <QSslConfiguration>
#include <QSslPreSharedKeyAuthenticator>
#include <QSslSocket>
#include <chrono>

// ConnectFailure::Phase mirrors ConnectionState rather than reusing it, so connect_failure.h - and
// the test that links it alone - stays clear of QSslSocket. These keep the two from drifting.
static_assert(static_cast<int>(ConnectFailure::Phase::Disconnected) == TcpClient::Disconnected);
static_assert(static_cast<int>(ConnectFailure::Phase::Connecting) == TcpClient::Connecting);
static_assert(static_cast<int>(ConnectFailure::Phase::Authenticating) == TcpClient::Authenticating);
static_assert(static_cast<int>(ConnectFailure::Phase::Connected) == TcpClient::Connected);

TcpClient::TcpClient(QObject *parent)
    : QObject(parent), m_socket(new QSslSocket(this)), m_protocol(new Protocol(this)), m_authTimer(new QTimer(this)),
      m_connectTimer(new QTimer(this)), m_pingTimer(new QTimer(this)), m_retryTimer(new QTimer(this)),
      m_port(K4Protocol::DEFAULT_PORT), m_useTls(false), m_encodeMode(3), m_streamingLatency(3),
      m_authResponseReceived(false), m_kzEgress(new KzEgress(this)) {
    connect(m_kzEgress, &KzEgress::framesReady, this, &TcpClient::writeKzFrames);

    // Socket signals
    connect(m_socket, &QSslSocket::connected, this, &TcpClient::onSocketConnected);
    connect(m_socket, &QSslSocket::encrypted, this, &TcpClient::onSocketEncrypted);
    connect(m_socket, &QSslSocket::disconnected, this, &TcpClient::onSocketDisconnected);
    connect(m_socket, &QSslSocket::readyRead, this, &TcpClient::onReadyRead);
    connect(m_socket, &QSslSocket::errorOccurred, this, &TcpClient::onSocketError);

    // SSL-specific signals
    connect(m_socket, &QSslSocket::sslErrors, this, &TcpClient::onSslErrors);
    connect(m_socket, &QSslSocket::preSharedKeyAuthenticationRequired, this,
            &TcpClient::onPreSharedKeyAuthenticationRequired);

    // Auth timeout timer (single shot)
    m_authTimer->setSingleShot(true);
    connect(m_authTimer, &QTimer::timeout, this, &TcpClient::onAuthTimeout);

    // Connect timeout timer — fires if TCP/TLS connection never establishes
    m_connectTimer->setSingleShot(true);
    connect(m_connectTimer, &QTimer::timeout, this, &TcpClient::onConnectTimeout);

    // Ping timer for keep-alive
    m_pingTimer->setInterval(K4Protocol::PING_INTERVAL_MS);
    connect(m_pingTimer, &QTimer::timeout, this, &TcpClient::onPingTimer);

    // Retry timer — single-shot, fires attemptConnection() after transient network errors
    m_retryTimer->setSingleShot(true);
    connect(m_retryTimer, &QTimer::timeout, this, &TcpClient::attemptConnection);

    // Protocol signals - any packet means auth succeeded
    connect(m_protocol, &Protocol::packetReceived, this, [this](quint8 type, const QByteArray &payload) {
        Q_UNUSED(payload)
        if (m_state.load(std::memory_order_acquire) == Authenticating && !m_authResponseReceived) {
            m_authResponseReceived = true;
            m_authTimer->stop();
            qCDebug(lcLinkTcp) << "Authentication successful, received packet type:" << type;
            setState(Connected);
            emit authenticated();
            startPingTimer();

            // Send startup macro BEFORE RDY so the state dump reflects the macro changes
            if (!m_startupMacro.isEmpty()) {
                qCDebug(lcLinkTcp) << "Sending startup macro (pre-RDY):" << m_startupMacro;
                sendCAT(m_startupMacro);
                m_startupMacro.clear();
            }

            // Send initialization sequence
            // RDY triggers comprehensive state dump containing all radio state:
            // FA, FB, MD, MD$, BW, BW$, IS, IS$, CW, KS, PC, SD (per mode), SQ, RG, SQ$, RG$,
            // RT, XT, RO, RT$, RO$, BS, AN, AR, AR$, PA, PA$, RA, RA$, NB, NB$, NR, NR$, NM, NM$,
            // #SPN, #REF, VXC, VXV, VXD, and all menu definitions (MEDF)
            sendCAT(K4Protocol::Commands::READY);              // Triggers comprehensive state dump
            sendCAT(K4Protocol::Commands::ENABLE_K4_MODE);     // Enable advanced K4 protocol mode
            sendCAT(K4Protocol::Commands::ENABLE_LONG_ERRORS); // Request long format error messages
            // Set audio encode mode (0=RAW S32LE (24-bit), 1=RAW S16LE, 2/3=Opus (same bitstream, int vs float decode))
            qCDebug(lcLinkTcp) << "Sending:" << QString("EM%1;").arg(m_encodeMode);
            sendCAT(QString("EM%1;").arg(m_encodeMode));
            // Set streaming audio latency (0-7, higher values for high-latency connections)
            qCDebug(lcLinkTcp) << "Sending:" << QString("SL%1;").arg(m_streamingLatency);
            sendCAT(QString("SL%1;").arg(m_streamingLatency));
        }
    });
    connect(m_protocol, &Protocol::catResponseReceived, this, &TcpClient::onCatResponse);
}

TcpClient::~TcpClient() {
    m_connectTimer->stop();
    m_retryTimer->stop();
    stopPingTimer();
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->abort();
    }
}

void TcpClient::connectToHost(const QString &host, quint16 port, const QString &password, bool useTls,
                              const QString &identity, int encodeMode, int streamingLatency) {
    qCDebug(lcLinkTcp) << "connectToHost called, state=" << m_state.load(std::memory_order_acquire)
                       << "socket=" << m_socket->state();
    if (m_state.load(std::memory_order_acquire) != Disconnected) {
        qCDebug(lcLinkTcp) << "Not disconnected, calling disconnectFromHost first";
        disconnectFromHost();
        qCDebug(lcLinkTcp) << "After disconnect: state=" << m_state.load(std::memory_order_acquire)
                           << "socket=" << m_socket->state();
    }

    m_host = host;
    m_port = port;
    m_password = password; // Also used as PSK when TLS enabled
    m_useTls = useTls;
    m_identity = identity;     // TLS-PSK identity (optional)
    m_encodeMode = encodeMode; // 0=RAW S32LE (24-bit), 1=RAW S16LE, 2/3=Opus (same bitstream, int vs float decode)
    m_streamingLatency = streamingLatency; // Remote streaming audio latency (0-7)
    m_authResponseReceived = false;

    m_retryCount = 0;
    ++m_attemptId;
    setState(Connecting);

    // Resolve .local (mDNS) hostnames before connecting — Qt's SSL socket
    // may not go through the system mDNS resolver, causing connection timeouts.
    // K4, K4D, and K4Z radios only listen on IPv4, so prefer IPv4 results.
    // The context-object overload of lookupHost() cancels if `this` is destroyed.
    if (m_host.endsWith(QStringLiteral(".local"), Qt::CaseInsensitive)) {
        qCDebug(lcLinkTcp) << "Resolving mDNS hostname:" << m_host;
        QHostInfo::lookupHost(m_host, this, [this, attempt = m_attemptId, name = m_host](const QHostInfo &info) {
            // WHY the attempt id and not just the state: switching radios while this lookup is in
            // flight starts a new attempt that is ALSO Connecting. Without the id, this answer for
            // the previous radio would overwrite m_host and connect the new attempt to the old radio.
            if (attempt != m_attemptId || m_state.load(std::memory_order_acquire) != Connecting)
                return;

            if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
                qCWarning(lcLinkTcp) << "mDNS resolution failed for" << name << ":" << info.errorString();
                emit errorOccurred(QString("Could not resolve %1: %2").arg(name, info.errorString()));
                disconnectFromHost();
                return;
            }
            // Prefer IPv4 — K4, K4D, and K4Z radios only listen on IPv4
            QString resolved;
            for (const auto &addr : info.addresses()) {
                if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
                    resolved = addr.toString();
                    break;
                }
            }
            if (resolved.isEmpty()) {
                resolved = info.addresses().first().toString();
            }
            qCDebug(lcLinkTcp) << "Resolved" << name << "to" << resolved;
            m_host = resolved;
            attemptConnection();
        });
        return;
    }

    attemptConnection();
}

void TcpClient::attemptConnection() {
    qCDebug(lcLinkTcp) << "attemptConnection host=" << m_host << "port=" << m_port << "tls=" << m_useTls
                       << "socketState=" << m_socket->state()
                       << "thread=" << reinterpret_cast<quintptr>(QThread::currentThread());

    // WHY here, which every attempt and every retry passes through: a session that ended mid-packet
    // leaves its header in the parser, and the new session's first bytes - the auth reply - would be
    // read as the rest of that packet. A stale header announcing a large payload makes the parser
    // wait for bytes that never come, and the reconnect times out in authentication.
    m_protocol->reset();

    // WHY: connectToHost() on a socket that is not Unconnected fails with OperationError while the
    // previous connection carries on. A graceful close (the K4's disconnect command still being
    // written) or a failed attempt can leave it Closing; this attempt replaces it outright.
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->abort();

    if (m_useTls) {
        // Log OpenSSL version Qt is using (first attempt only)
        if (m_retryCount == 0) {
            qCDebug(lcLinkTcp) << "=== SSL Library Info ===";
            qCDebug(lcLinkTcp) << "  Build version:" << QSslSocket::sslLibraryBuildVersionString();
            qCDebug(lcLinkTcp) << "  Runtime version:" << QSslSocket::sslLibraryVersionString();
            qCDebug(lcLinkTcp) << "  Supports SSL:" << QSslSocket::supportsSsl();
        }

        // Configure TLS for PSK authentication - require TLS 1.2 minimum
        QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
        sslConfig.setProtocol(QSsl::TlsV1_2OrLater);
        sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone); // PSK doesn't use certificates

        // Filter to only TLS 1.2+ PSK ciphers
        QList<QSslCipher> tls12PskCiphers;
        if (m_retryCount == 0) {
            qCDebug(lcLinkTcp) << "=== Available PSK Ciphers ===";
        }
        for (const QSslCipher &cipher : QSslConfiguration::supportedCiphers()) {
            if (cipher.name().contains("PSK")) {
                if (m_retryCount == 0) {
                    qCDebug(lcLinkTcp) << "  " << cipher.name() << "(" << cipher.protocolString() << ")";
                }
                // Only include TLS 1.2+ ciphers
                if (cipher.protocol() == QSsl::TlsV1_2 || cipher.protocol() == QSsl::TlsV1_3) {
                    tls12PskCiphers.append(cipher);
                }
            }
        }
        if (m_retryCount == 0) {
            qCDebug(lcLinkTcp) << "=== Offering" << tls12PskCiphers.size() << "TLS 1.2+ PSK ciphers ===";
            for (const QSslCipher &cipher : tls12PskCiphers) {
                qCDebug(lcLinkTcp) << "  " << cipher.name();
            }
        }
        if (tls12PskCiphers.isEmpty()) {
            // WHY: the Schannel and cert-only backends expose no PSK ciphersuites. Starting
            // the handshake anyway surfaces as a generic connection timeout, indistinguishable
            // from a wrong PSK or a firewall drop — which is what made the Windows packaging
            // regression (missing qopensslbackend.dll) so expensive to diagnose from the field.
            const QString backend = QSslSocket::activeBackend();
            qCWarning(lcLinkTcp) << "No TLS-PSK ciphers available; active backend =" << backend;
            emit errorOccurred(QString("TLS unavailable: the active TLS backend (%1) has no PSK "
                                       "support. Reinstall QK4, or connect on the unencrypted port.")
                                   .arg(backend));
            disconnectFromHost();
            return;
        }
        sslConfig.setCiphers(tls12PskCiphers);

        m_socket->setSslConfiguration(sslConfig);

        qCDebug(lcLinkTcp) << "Connecting with TLS/PSK to" << m_host << ":" << m_port;
        m_socket->connectToHostEncrypted(m_host, m_port);
    } else {
        qCDebug(lcLinkTcp) << "Connecting (unencrypted) to" << m_host << ":" << m_port;
        m_socket->connectToHost(m_host, m_port);
    }

    m_connectTimer->start(K4Protocol::CONNECTION_TIMEOUT_MS);
}

// The one way an attempt or a session ends: asked for by the operator, replaced by a new attempt, or
// ended by a failure that has already been reported.
void TcpClient::disconnectFromHost() {
    const ConnectionState was = m_state.load(std::memory_order_acquire);
    qCDebug(lcLinkTcp) << "disconnectFromHost called, state=" << was << "socket=" << m_socket->state();
    const bool graceful = was == Connected && m_socket->state() == QAbstractSocket::ConnectedState;
    if (graceful) {
        // WHY first: a KZU the keyer posted on its way out (a straight key held at quit) must
        // reach the K4 before the session ends. The egress wakes this thread through a
        // notifier, which the event loop may service after this queued call.
        writeKzFrames();
        sendCAT(K4Protocol::Commands::DISCONNECT);
    }

    // WHY all of this before the socket is touched: closing it can emit disconnected() or
    // errorOccurred() synchronously, from inside the call. With the attempt already retired and the
    // state already Disconnected, those slots see a teardown we started and report nothing - not a
    // second error after an auth timeout, nor "closed without responding" when the operator
    // cancels during authentication.
    ++m_attemptId; // anything still in flight for the attempt being ended is now stale
    m_connectTimer->stop();
    m_retryTimer->stop();
    m_authTimer->stop();
    stopPingTimer();
    setState(Disconnected);
    m_protocol->reset(); // nothing after this belongs to the session that just ended

    if (m_socket->state() == QAbstractSocket::UnconnectedState)
        return;
    if (graceful) {
        m_socket->disconnectFromHost();
    } else {
        // WHY abort and not disconnectFromHost(): during HostLookup or Connecting, Qt's
        // disconnectFromHost() only marks a close as pending. The cancelled attempt then still
        // completes, emits connected(), and the next attempt's credentials - the password hash
        // or the PSK, already replaced by connectToHost() - go to the host being abandoned.
        // abort() "immediately closes the socket, discarding any pending data" (Qt 6.10
        // QAbstractSocket::abort), and nothing of an unauthenticated attempt is worth keeping.
        m_socket->abort();
    }
}

bool TcpClient::isConnected() const {
    return m_connected.load(std::memory_order_relaxed);
}

TcpClient::ConnectionState TcpClient::connectionState() const {
    return m_state.load(std::memory_order_acquire);
}

void TcpClient::sendCAT(const QString &command) {
    if (QThread::currentThread() != thread()) {
        qCDebug(lcCatTx) << "cross-thread marshal:" << command;
        QMetaObject::invokeMethod(this, &TcpClient::sendCAT, Qt::QueuedConnection, command);
        return;
    }
    if (m_state.load(std::memory_order_acquire) == Connected) {
        QByteArray packet = Protocol::buildCATPacket(command);
        m_socket->write(packet);
        m_socket->flush();
        qCDebug(lcCatTx) << "sent:" << command << "(" << packet.size() << "bytes)";
    } else {
        qCWarning(lcCatTx) << "DROPPED (state=" << m_state.load(std::memory_order_acquire) << "):" << command;
    }
}

void TcpClient::writeKzFrames() {
    const bool connected = m_state.load(std::memory_order_acquire) == Connected;
    m_kzEgress->drain([this, connected](std::string_view frame, std::int64_t enqueuedNs) {
        const auto text = QLatin1StringView(frame.data(), static_cast<qsizetype>(frame.size()));
        if (!connected) {
            qCWarning(lcCatTx) << "DROPPED (state=" << m_state.load(std::memory_order_acquire) << "):" << text;
            return;
        }
        char packet[KzEgress::kMaxFrameBytes + Protocol::kCatPacketOverhead];
        const std::size_t length = Protocol::writeCATPacket(frame, packet, sizeof packet);
        m_socket->write(packet, static_cast<qint64>(length));
        m_socket->flush();
        const std::int64_t queuedUs = (KzEgress::nowNs() - enqueuedNs) / 1000;
        m_kzLatency.record(queuedUs);
        qCDebug(lcCatTx) << "sent:" << text << "(" << length << "bytes," << queuedUs << "us from the keying thread)";
    });
}

void TcpClient::logKzLatency() {
    const LatencyHistogram::Counts c = m_kzLatency.snapshot();
    if (c.total == m_kzLatencyLogged)
        return;
    m_kzLatencyLogged = c.total;
    qCInfo(lcLinkTcp).nospace() << "KZ egress, keying thread to socket write, this session: " << c.total
                                << " frames, median <= " << c.quantileBoundUs(0.5)
                                << " us, 99% <= " << c.quantileBoundUs(0.99) << " us, max " << c.maxUs
                                << " us, dropped " << m_kzEgress->droppedCount();
}

void TcpClient::sendRaw(const QByteArray &data) {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, &TcpClient::sendRaw, Qt::QueuedConnection, data);
        return;
    }
    // WHY the session state and not the socket's: the socket is ConnectedState from the TCP
    // handshake on, before the radio has accepted the password, and QSslSocket queues anything
    // written before encrypted() and sends it once the handshake completes. Audio written then
    // would reach the radio ahead of authentication.
    if (m_state.load(std::memory_order_acquire) != Connected) {
        ++m_rawDropped;
        return;
    }
    m_socket->write(data);
}

void TcpClient::setState(ConnectionState state) {
    const ConnectionState prev = m_state.load(std::memory_order_acquire);
    if (prev != state) {
        static const char *names[] = {"Disconnected", "Connecting", "Authenticating", "Connected"};
        qCDebug(lcLinkTcp) << "State:" << names[prev] << "->" << names[state];
        // Counted per write (audio arrives every few ms), reported once per state left.
        if (m_rawDropped > 0) {
            qCInfo(lcLinkTcp) << "Dropped" << m_rawDropped << "raw packets written while" << names[prev];
            m_rawDropped = 0;
        }
        m_state.store(state, std::memory_order_release);
        m_connected.store(state == Connected, std::memory_order_relaxed);
        if (state == Connected) {
            m_kzLatency.reset();
            m_kzLatencyLogged = 0;
            m_pingTicks = 0;
            m_pings.reset();
            m_linkClock.start();
        } else if (prev == Connected) {
            logKzLatency();
        }
        emit stateChanged(state);

        if (state == Connected) {
            emit connected();
        } else if (state == Disconnected) {
            emit disconnected();
        }
    }
}

void TcpClient::onSocketConnected() {
    // A connection for no live attempt must not be authenticated: the credentials now held belong
    // to whichever radio was asked for since.
    if (m_state.load(std::memory_order_acquire) != Connecting) {
        qCWarning(lcLinkTcp) << "Socket connected with no attempt in progress; closing it";
        m_socket->abort();
        return;
    }
    // WHY: small-write CAT traffic (KZ keying bursts in particular) plus K4-side delayed-ACK
    // produces ~40 ms RTT stalls when Nagle is on. Setting LowDelayOption after the TCP socket
    // is connected applies TCP_NODELAY to the actual FD and benefits the TLS handshake itself.
    // KeepAliveOption catches half-open sockets across NAT keepalive expirations on WAN links.
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    m_socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);

    if (m_useTls) {
        // TLS connection: TCP connected, now waiting for TLS handshake to complete
        // The encrypted() signal will fire when TLS is fully established
        qCDebug(lcLinkTcp) << "TCP connected, starting TLS handshake...";
        // Don't change state yet - wait for encrypted() signal
    } else {
        // Non-TLS: TCP connected — stop connect timer, start auth phase
        m_connectTimer->stop();
        qCDebug(lcLinkTcp) << "Socket connected, sending authentication...";
        setState(Authenticating);
        sendAuthentication();
        m_authTimer->start(K4Protocol::AUTH_TIMEOUT_MS);
    }
}

void TcpClient::onSocketEncrypted() {
    if (m_state.load(std::memory_order_acquire) != Connecting) {
        qCWarning(lcLinkTcp) << "TLS session established with no attempt in progress; closing it";
        m_socket->abort();
        return;
    }
    m_connectTimer->stop();
    // TLS handshake completed successfully
    QSslCipher negotiated = m_socket->sessionCipher();
    qCDebug(lcLinkTcp) << "=== TLS/PSK Connection Established ===";
    qCDebug(lcLinkTcp) << "  Negotiated cipher:" << negotiated.name();
    qCDebug(lcLinkTcp) << "  Protocol:" << negotiated.protocolString();
    qCDebug(lcLinkTcp) << "  Key exchange:" << negotiated.keyExchangeMethod();
    qCDebug(lcLinkTcp) << "  Encryption:" << negotiated.encryptionMethod();
    setState(Authenticating);
    // Start auth timeout - waiting for first packet to confirm connection works
    m_authTimer->start(K4Protocol::AUTH_TIMEOUT_MS);
    // Note: For TLS/PSK, no additional password auth needed - data flows immediately
}

// Kind::None is the ordinary case - a clean close, or a timer that lost its race - and says nothing.
void TcpClient::reportFailure(ConnectFailure::Event event, ConnectFailure::Phase phase,
                              const QString &socketErrorText) {
    const ConnectFailure::Result result =
        ConnectFailure::classify(event, phase, m_authResponseReceived, m_host, m_port, socketErrorText);
    if (result.kind != ConnectFailure::Kind::None)
        emit errorOccurred(result.message);
}

void TcpClient::onSocketDisconnected() {
    const ConnectionState stateNow = m_state.load(std::memory_order_acquire);
    qCDebug(lcLinkTcp) << "Socket disconnected (was state=" << stateNow << "authReceived=" << m_authResponseReceived
                       << ")";
    // Disconnected already: a teardown disconnectFromHost() started, or a failure already reported.
    if (stateNow == Disconnected)
        return;

    // WHY this no longer says "authentication failed" (see connect_failure.h for the full reasoning):
    // with the radio powered off, macOS reports the connect() failure while Qt emits connected()
    // anyway, so QK4 enters Authenticating, writes the auth hash into a dead socket and lands here —
    // indistinguishable from a password the K4 refused. TCP cannot rescue the distinction either:
    // the discriminator would be whether the connection truly reached ESTABLISHED, and in exactly
    // this failure both socket signals lie the same way — connected() fires when it has not, and the
    // error arrives as RemoteHostClosedError, which normally means it had.
    reportFailure(ConnectFailure::Event::SocketClosed, static_cast<ConnectFailure::Phase>(stateNow));
    disconnectFromHost();
}

void TcpClient::onReadyRead() {
    QByteArray data = m_socket->readAll();
    // Bytes still arriving on a session already ended belong to no one and must not be parsed into
    // the next one.
    if (m_state.load(std::memory_order_acquire) == Disconnected)
        return;
    m_protocol->parse(data);
}

void TcpClient::onSocketError(QAbstractSocket::SocketError error) {
    const QString errorMsg = m_socket->errorString();
    const ConnectionState stateNow = m_state.load(std::memory_order_acquire);
    if (stateNow == Disconnected) {
        // The error of a socket we already closed, or of a failure already reported.
        qCDebug(lcLinkTcp) << "Socket error after teardown (ignored):" << error << errorMsg;
        return;
    }
    qCDebug(lcLinkTcp) << "Socket error:" << error << errorMsg << "state=" << stateNow << "socket=" << m_socket->state()
                       << "retry=" << m_retryCount;

    // On local subnets, macOS returns EHOSTUNREACH immediately when the ARP cache
    // is cold (no MAC address entry for the destination IP). This happens consistently
    // on first connect after a fresh app launch via Finder/open — the kernel's connect()
    // syscall fails synchronously instead of waiting for ARP resolution. The ARP request
    // IS sent, so a brief retry after 500ms succeeds once the ARP reply populates the cache.
    // This is not a workaround — it's correct handling of a real network transient.
    constexpr std::chrono::milliseconds kArpRetryInterval{500};
    constexpr int kArpMaxRetries = 2;
    bool arpTransient =
        (error == QAbstractSocket::NetworkError && stateNow == Connecting && m_retryCount < kArpMaxRetries);
    if (arpTransient) {
        m_retryCount++;
        m_connectTimer->stop();
        qCDebug(lcLinkTcp) << "ARP-cold retry" << m_retryCount << "in" << kArpRetryInterval.count() << "ms";
        m_socket->abort();
        m_retryTimer->start(kArpRetryInterval);
        return;
    }

    // One diagnostic line carrying everything that tells the failure modes apart, because the
    // user-visible message deliberately cannot: which phase we reached, what the socket called it,
    // and whether the radio had ever answered. Verified against the radio: a wrong password on 9204
    // makes the K4 send its ServerHello and then drop TCP with no TLS alert, so the socket error is
    // indistinguishable from a host that went away.
    qCWarning(lcLinkTcp) << "Connect attempt failed: phase=" << stateNow << "socketError=" << error << "port=" << m_port
                         << "tls=" << m_useTls << "everAnswered=" << m_authResponseReceived << "detail=" << errorMsg;

    reportFailure(ConnectFailure::Event::SocketError, static_cast<ConnectFailure::Phase>(stateNow), errorMsg);
    disconnectFromHost();
}

void TcpClient::onSslErrors(const QList<QSslError> &errors) {
    // Log SSL errors but continue - PSK doesn't use certificates so some errors are expected
    for (const QSslError &error : errors) {
        qCDebug(lcLinkTcp) << "SSL error (ignored for PSK):" << error.errorString();
    }
    // Ignore all SSL errors for PSK connections (no certificate verification)
    m_socket->ignoreSslErrors();
}

void TcpClient::onPreSharedKeyAuthenticationRequired(QSslPreSharedKeyAuthenticator *authenticator) {
    qCDebug(lcLinkTcp) << "PSK authentication requested, identity hint:" << authenticator->identityHint();

    // Set the identity (empty or user-specified) and the pre-shared key (password field)
    authenticator->setIdentity(m_identity.toUtf8());
    authenticator->setPreSharedKey(m_password.toUtf8());

    qCDebug(lcLinkTcp) << "PSK credentials provided, identity:" << (m_identity.isEmpty() ? "empty" : "set");
}

void TcpClient::onConnectTimeout() {
    if (m_state.load(std::memory_order_acquire) == Connecting) {
        qCDebug(lcLinkTcp) << "Connection timeout - failed to establish" << (m_useTls ? "TLS" : "TCP") << "connection";
        // Verified: with nothing at the address, the socket reports neither connected() nor an
        // error - it simply stays in ConnectingState. This timer is the only thing that speaks.
        reportFailure(ConnectFailure::Event::ConnectTimeout, ConnectFailure::Phase::Connecting);
        disconnectFromHost();
    }
}

void TcpClient::onAuthTimeout() {
    if (m_state.load(std::memory_order_acquire) == Authenticating && !m_authResponseReceived) {
        // Distinct from the closed-socket case above: something is there and holding the connection
        // open without answering. Still not attributable to the password - the K4 has no way to
        // tell us it refused one - so the message names the port as well.
        qCWarning(lcLinkTcp) << "Connect attempt failed: phase=Authenticating, socket still up, no data"
                             << "port=" << m_port << "tls=" << m_useTls;
        reportFailure(ConnectFailure::Event::AuthTimeout, ConnectFailure::Phase::Authenticating);
        disconnectFromHost();
    }
}

void TcpClient::onPingTimer() {
    if (m_state.load(std::memory_order_acquire) == Connected) {
        const qint64 epoch = QDateTime::currentSecsSinceEpoch();
        sendCAT(QString("PING%1;").arg(epoch));
        m_pings.pingSent(epoch, m_linkClock.elapsed());
        // WHY on the ping: it is the I/O thread's only steady tick, and a summary a minute is plenty.
        if (++m_pingTicks * K4Protocol::PING_INTERVAL_MS >= 60'000) {
            m_pingTicks = 0;
            logKzLatency();
        }
    }
}

void TcpClient::onCatResponse(const QString &response) {
    if (m_state.load(std::memory_order_acquire) != Connected)
        return;
    const qint64 rtt = m_pings.onCatPayload(response, m_linkClock.elapsed());
    if (rtt >= 0) {
        m_latencyMs = static_cast<int>(rtt);
        emit latencyChanged(m_latencyMs);
    }
}

void TcpClient::sendAuthentication() {
    // Build SHA-384 hash of password as hex string
    QByteArray authData = Protocol::buildAuthData(m_password);
    qCDebug(lcLinkTcp) << "Sending auth hash (" << authData.size() << "bytes)";

    // Send raw auth data (not wrapped in K4 packet - just the hex string)
    m_socket->write(authData);
    m_socket->flush();
    // Radio will respond with packets, which triggers auth success and init sequence
}

void TcpClient::startPingTimer() {
    m_pingTimer->start();
}

void TcpClient::stopPingTimer() {
    m_pingTimer->stop();
}
