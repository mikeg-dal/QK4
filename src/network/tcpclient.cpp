#include "tcpclient.h"
#include <QDateTime>
#include <QHostInfo>
#include <QLoggingCategory>
#include <QSslCipher>
#include <QSslConfiguration>
#include <QSslPreSharedKeyAuthenticator>
#include <QSslSocket>

Q_LOGGING_CATEGORY(catTx, "CAT.TX")
Q_LOGGING_CATEGORY(netTcp, "net.tcp")

TcpClient::TcpClient(QObject *parent)
    : QObject(parent), m_socket(new QSslSocket(this)), m_protocol(new Protocol(this)), m_authTimer(new QTimer(this)),
      m_connectTimer(new QTimer(this)), m_pingTimer(new QTimer(this)), m_retryTimer(new QTimer(this)),
      m_port(K4Protocol::DEFAULT_PORT), m_useTls(false), m_encodeMode(3), m_streamingLatency(3), m_state(Disconnected),
      m_authResponseReceived(false) {
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
        if (m_state == Authenticating && !m_authResponseReceived) {
            m_authResponseReceived = true;
            m_authTimer->stop();
            qCDebug(netTcp) << "Authentication successful, received packet type:" << type;
            setState(Connected);
            emit authenticated();
            startPingTimer();

            // Send startup macro BEFORE RDY so the state dump reflects the macro changes
            if (!m_startupMacro.isEmpty()) {
                qCDebug(netTcp) << "Sending startup macro (pre-RDY):" << m_startupMacro;
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
            // Set audio encode mode (0=RAW32, 1=RAW16, 2=Opus Int, 3=Opus Float)
            qCDebug(netTcp) << "Sending:" << QString("EM%1;").arg(m_encodeMode);
            sendCAT(QString("EM%1;").arg(m_encodeMode));
            // Set streaming audio latency (0-7, higher values for high-latency connections)
            qCDebug(netTcp) << "Sending:" << QString("SL%1;").arg(m_streamingLatency);
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
    qCDebug(netTcp) << "connectToHost called, state=" << m_state << "socket=" << m_socket->state();
    if (m_state != Disconnected) {
        qCDebug(netTcp) << "Not disconnected, calling disconnectFromHost first";
        disconnectFromHost();
        qCDebug(netTcp) << "After disconnect: state=" << m_state << "socket=" << m_socket->state();
    }

    m_host = host;
    m_port = port;
    m_password = password; // Also used as PSK when TLS enabled
    m_useTls = useTls;
    m_identity = identity;                 // TLS-PSK identity (optional)
    m_encodeMode = encodeMode;             // Audio encode mode (0=RAW32, 1=RAW16, 2=Opus Int, 3=Opus Float)
    m_streamingLatency = streamingLatency; // Remote streaming audio latency (0-7)
    m_authResponseReceived = false;

    m_retryCount = 0;
    setState(Connecting);

    // Resolve .local (mDNS) hostnames before connecting — Qt's SSL socket
    // may not go through the system mDNS resolver, causing connection timeouts.
    // K4, K4D, and K4Z radios only listen on IPv4, so prefer IPv4 results.
    // The context-object overload of lookupHost() cancels if `this` is destroyed.
    if (m_host.endsWith(QStringLiteral(".local"), Qt::CaseInsensitive)) {
        qCDebug(netTcp) << "Resolving mDNS hostname:" << m_host;
        QHostInfo::lookupHost(m_host, this, [this](const QHostInfo &info) {
            // Guard: user may have disconnected while resolution was in flight
            if (m_state != Connecting)
                return;

            if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
                qWarning() << "mDNS resolution failed for" << m_host << ":" << info.errorString();
                emit errorOccurred(QString("Could not resolve %1: %2").arg(m_host, info.errorString()));
                setState(Disconnected);
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
            qCDebug(netTcp) << "Resolved" << m_host << "to" << resolved;
            m_host = resolved;
            attemptConnection();
        });
        return;
    }

    attemptConnection();
}

void TcpClient::attemptConnection() {
    qCDebug(netTcp) << "attemptConnection host=" << m_host << "port=" << m_port << "tls=" << m_useTls
                    << "socketState=" << m_socket->state()
                    << "thread=" << reinterpret_cast<quintptr>(QThread::currentThread());

    if (m_useTls) {
        // Log OpenSSL version Qt is using (first attempt only)
        if (m_retryCount == 0) {
            qCDebug(netTcp) << "=== SSL Library Info ===";
            qCDebug(netTcp) << "  Build version:" << QSslSocket::sslLibraryBuildVersionString();
            qCDebug(netTcp) << "  Runtime version:" << QSslSocket::sslLibraryVersionString();
            qCDebug(netTcp) << "  Supports SSL:" << QSslSocket::supportsSsl();
        }

        // Configure TLS for PSK authentication - require TLS 1.2 minimum
        QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
        sslConfig.setProtocol(QSsl::TlsV1_2OrLater);
        sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone); // PSK doesn't use certificates

        // Filter to only TLS 1.2+ PSK ciphers
        QList<QSslCipher> tls12PskCiphers;
        if (m_retryCount == 0) {
            qCDebug(netTcp) << "=== Available PSK Ciphers ===";
        }
        for (const QSslCipher &cipher : QSslConfiguration::supportedCiphers()) {
            if (cipher.name().contains("PSK")) {
                if (m_retryCount == 0) {
                    qCDebug(netTcp) << "  " << cipher.name() << "(" << cipher.protocolString() << ")";
                }
                // Only include TLS 1.2+ ciphers
                if (cipher.protocol() == QSsl::TlsV1_2 || cipher.protocol() == QSsl::TlsV1_3) {
                    tls12PskCiphers.append(cipher);
                }
            }
        }
        if (m_retryCount == 0) {
            qCDebug(netTcp) << "=== Offering" << tls12PskCiphers.size() << "TLS 1.2+ PSK ciphers ===";
            for (const QSslCipher &cipher : tls12PskCiphers) {
                qCDebug(netTcp) << "  " << cipher.name();
            }
        }
        if (!tls12PskCiphers.isEmpty()) {
            sslConfig.setCiphers(tls12PskCiphers);
        }

        m_socket->setSslConfiguration(sslConfig);

        qCDebug(netTcp) << "Connecting with TLS/PSK to" << m_host << ":" << m_port;
        m_socket->connectToHostEncrypted(m_host, m_port);
    } else {
        qCDebug(netTcp) << "Connecting (unencrypted) to" << m_host << ":" << m_port;
        m_socket->connectToHost(m_host, m_port);
    }

    m_connectTimer->start(K4Protocol::CONNECTION_TIMEOUT_MS);
}

void TcpClient::disconnectFromHost() {
    qCDebug(netTcp) << "disconnectFromHost called, state=" << m_state << "socket=" << m_socket->state();
    m_connectTimer->stop();
    m_retryTimer->stop();
    stopPingTimer();
    m_authTimer->stop();

    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        // Send graceful disconnect command
        if (m_state == Connected) {
            sendCAT(K4Protocol::Commands::DISCONNECT);
        }
        m_socket->disconnectFromHost();
    }

    setState(Disconnected);
}

bool TcpClient::isConnected() const {
    return m_connected.load(std::memory_order_relaxed);
}

TcpClient::ConnectionState TcpClient::connectionState() const {
    return m_state;
}

void TcpClient::sendCAT(const QString &command) {
    if (QThread::currentThread() != thread()) {
        qCDebug(catTx) << "cross-thread marshal:" << command;
        QMetaObject::invokeMethod(this, "sendCAT", Qt::QueuedConnection, Q_ARG(QString, command));
        return;
    }
    if (m_state == Connected) {
        QByteArray packet = Protocol::buildCATPacket(command);
        m_socket->write(packet);
        m_socket->flush();
        qCDebug(catTx) << "sent:" << command << "(" << packet.size() << "bytes)";
    } else {
        qWarning() << "[CAT TX] DROPPED (state=" << m_state << "):" << command;
    }
}

void TcpClient::sendRaw(const QByteArray &data) {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "sendRaw", Qt::QueuedConnection, Q_ARG(QByteArray, data));
        return;
    }
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->write(data);
    }
}

void TcpClient::setState(ConnectionState state) {
    if (m_state != state) {
        static const char *names[] = {"Disconnected", "Connecting", "Authenticating", "Connected"};
        qCDebug(netTcp) << "State:" << names[m_state] << "->" << names[state];
        m_state = state;
        m_connected.store(state == Connected, std::memory_order_relaxed);
        emit stateChanged(state);

        if (state == Connected) {
            emit connected();
        } else if (state == Disconnected) {
            emit disconnected();
        }
    }
}

void TcpClient::onSocketConnected() {
    if (m_useTls) {
        // TLS connection: TCP connected, now waiting for TLS handshake to complete
        // The encrypted() signal will fire when TLS is fully established
        qCDebug(netTcp) << "TCP connected, starting TLS handshake...";
        // Don't change state yet - wait for encrypted() signal
    } else {
        // Non-TLS: TCP connected — stop connect timer, start auth phase
        m_connectTimer->stop();
        qCDebug(netTcp) << "Socket connected, sending authentication...";
        setState(Authenticating);
        sendAuthentication();
        m_authTimer->start(K4Protocol::AUTH_TIMEOUT_MS);
    }
}

void TcpClient::onSocketEncrypted() {
    m_connectTimer->stop();
    // TLS handshake completed successfully
    QSslCipher negotiated = m_socket->sessionCipher();
    qCDebug(netTcp) << "=== TLS/PSK Connection Established ===";
    qCDebug(netTcp) << "  Negotiated cipher:" << negotiated.name();
    qCDebug(netTcp) << "  Protocol:" << negotiated.protocolString();
    qCDebug(netTcp) << "  Key exchange:" << negotiated.keyExchangeMethod();
    qCDebug(netTcp) << "  Encryption:" << negotiated.encryptionMethod();
    setState(Authenticating);
    // Start auth timeout - waiting for first packet to confirm connection works
    m_authTimer->start(K4Protocol::AUTH_TIMEOUT_MS);
    // Note: For TLS/PSK, no additional password auth needed - data flows immediately
}

void TcpClient::onSocketDisconnected() {
    qCDebug(netTcp) << "Socket disconnected (was state=" << m_state << "authReceived=" << m_authResponseReceived << ")";
    stopPingTimer();
    m_authTimer->stop();

    if (m_state == Authenticating && !m_authResponseReceived) {
        emit authenticationFailed();
        emit errorOccurred("Authentication failed - connection closed by radio");
    }

    setState(Disconnected);
}

void TcpClient::onReadyRead() {
    QByteArray data = m_socket->readAll();
    m_protocol->parse(data);
}

void TcpClient::onSocketError(QAbstractSocket::SocketError error) {
    m_connectTimer->stop();
    stopPingTimer();
    m_authTimer->stop();

    QString errorMsg = m_socket->errorString();
    qCDebug(netTcp) << "Socket error:" << error << errorMsg << "state=" << m_state << "socket=" << m_socket->state()
                    << "retry=" << m_retryCount;

    // On local subnets, macOS returns EHOSTUNREACH immediately when the ARP cache
    // is cold (no MAC address entry for the destination IP). This happens consistently
    // on first connect after a fresh app launch via Finder/open — the kernel's connect()
    // syscall fails synchronously instead of waiting for ARP resolution. The ARP request
    // IS sent, so a brief retry after 500ms succeeds once the ARP reply populates the cache.
    // This is not a workaround — it's correct handling of a real network transient.
    bool arpTransient = (error == QAbstractSocket::NetworkError && m_state == Connecting && m_retryCount < 2);
    if (arpTransient) {
        m_retryCount++;
        qCDebug(netTcp) << "ARP-cold retry" << m_retryCount << "in 500ms";
        m_socket->abort();
        m_retryTimer->start(500);
        return;
    }

    if (m_state == Authenticating) {
        emit authenticationFailed();
    }

    emit errorOccurred(errorMsg);
    setState(Disconnected);
}

void TcpClient::onSslErrors(const QList<QSslError> &errors) {
    // Log SSL errors but continue - PSK doesn't use certificates so some errors are expected
    for (const QSslError &error : errors) {
        qCDebug(netTcp) << "SSL error (ignored for PSK):" << error.errorString();
    }
    // Ignore all SSL errors for PSK connections (no certificate verification)
    m_socket->ignoreSslErrors();
}

void TcpClient::onPreSharedKeyAuthenticationRequired(QSslPreSharedKeyAuthenticator *authenticator) {
    qCDebug(netTcp) << "PSK authentication requested, identity hint:" << authenticator->identityHint();

    // Set the identity (empty or user-specified) and the pre-shared key (password field)
    authenticator->setIdentity(m_identity.toUtf8());
    authenticator->setPreSharedKey(m_password.toUtf8());

    qCDebug(netTcp) << "PSK credentials provided, identity:" << (m_identity.isEmpty() ? "(empty)" : m_identity);
}

void TcpClient::onConnectTimeout() {
    if (m_state == Connecting) {
        qCDebug(netTcp) << "Connection timeout - failed to establish" << (m_useTls ? "TLS" : "TCP") << "connection";
        emit errorOccurred("Connection timed out - radio unreachable");
        m_socket->abort();
        setState(Disconnected);
    }
}

void TcpClient::onAuthTimeout() {
    if (m_state == Authenticating && !m_authResponseReceived) {
        qCDebug(netTcp) << "Authentication timeout";
        emit authenticationFailed();
        emit errorOccurred("Authentication timeout - no response from radio");
        disconnectFromHost();
    }
}

void TcpClient::onPingTimer() {
    if (m_state == Connected) {
        qint64 epoch = QDateTime::currentSecsSinceEpoch();
        sendCAT(QString("PING%1;").arg(epoch));
        m_pingElapsed.start();
    }
}

void TcpClient::onCatResponse(const QString &response) {
    if (response.startsWith("PONG")) {
        m_latencyMs = static_cast<int>(m_pingElapsed.elapsed());
        emit latencyChanged(m_latencyMs);
    }
}

void TcpClient::sendAuthentication() {
    // Build SHA-384 hash of password as hex string
    QByteArray authData = Protocol::buildAuthData(m_password);
    qCDebug(netTcp) << "Sending auth hash (" << authData.size() << "bytes)";

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
