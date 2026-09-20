#include "network/websocketserver.h"

#include <QHostAddress>
#include <QLoggingCategory>
#include <QTcpServer>
#include <QTcpSocket>

// Transport-level events only - liveness probes and backpressure. What a client actually said
// belongs to net.tci, which sits above this.
Q_LOGGING_CATEGORY(netWs, "net.ws")

namespace {

// End of an HTTP header block.
const char kHeaderEnd[] = "\r\n\r\n";

QString headerValue(const QByteArray &request, const char *name) {
    const QList<QByteArray> lines = request.split('\n');
    const QByteArray prefix = QByteArray(name).toLower() + ":";
    for (const QByteArray &line : lines) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.toLower().startsWith(prefix)) {
            return QString::fromLatin1(trimmed.mid(prefix.size()).trimmed());
        }
    }
    return QString();
}

} // namespace

WebSocketServer::WebSocketServer(QObject *parent)
    : QObject(parent), m_server(new QTcpServer(this)), m_livenessTimer(new QTimer(this)) {
    connect(m_server, &QTcpServer::newConnection, this, &WebSocketServer::onNewConnection);

    // Runs only while listening - see start()/stop(). An idle server with no sessions should not
    // wake the event loop every ten seconds for nothing.
    m_livenessTimer->setInterval(PING_INTERVAL_MS);
    connect(m_livenessTimer, &QTimer::timeout, this, &WebSocketServer::onLivenessTick);
}

WebSocketServer::~WebSocketServer() {
    disconnect(this);
    stop();
}

bool WebSocketServer::start(quint16 port, bool loopbackOnly) {
    if (m_server->isListening()) {
        return true;
    }
    const QHostAddress address = loopbackOnly ? QHostAddress(QHostAddress::LocalHost) : QHostAddress(QHostAddress::Any);
    if (!m_server->listen(address, port)) {
        m_errorString = m_server->errorString();
        emit errorOccurred(m_errorString);
        return false;
    }
    m_errorString.clear();
    m_livenessTimer->start();
    emit started(m_server->serverPort());
    return true;
}

void WebSocketServer::stop() {
    m_livenessTimer->stop();
    if (!m_server->isListening() && m_sessions.isEmpty()) {
        return;
    }
    const QList<int> ids = m_sessions.keys();
    for (int id : ids) {
        closeClient(id, WebSocketFrame::CloseNormal, QStringLiteral("server shutting down"));
        Session session = m_sessions.take(id);
        if (session.socket) {
            session.socket->disconnect(this);
            session.socket->abort();
            session.socket->deleteLater();
        }
        emit clientDisconnected(id);
    }
    if (m_server->isListening()) {
        m_server->close();
        emit stopped();
    }
}

bool WebSocketServer::isListening() const {
    return m_server->isListening();
}

quint16 WebSocketServer::port() const {
    return m_server->serverPort();
}

void WebSocketServer::onNewConnection() {
    while (QTcpSocket *socket = m_server->nextPendingConnection()) {
        if (m_sessions.size() >= MAX_CLIENTS) {
            // WHY abort rather than queue: a TCI client that cannot be served is better off failing
            // fast than sitting in a backlog while the operator wonders why nothing works.
            socket->abort();
            socket->deleteLater();
            emit errorOccurred(QStringLiteral("refused a connection: already at %1 clients").arg(MAX_CLIENTS));
            continue;
        }

        const int clientId = m_nextClientId++;
        Session session;
        session.socket = socket;
        m_sessions.insert(clientId, session);

        socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        connect(socket, &QTcpSocket::readyRead, this, [this, clientId]() { onReadyRead(clientId); });
        connect(socket, &QTcpSocket::disconnected, this, [this, clientId]() { onDisconnected(clientId); });
    }
}

// WHY every one of these re-looks-up by id instead of holding a Session& :
// emitting a signal runs a consumer's slot synchronously on this thread, and that slot typically
// writes to a socket. A write can surface a disconnect (erasing from m_sessions) and an accept can
// insert (rehashing it). Either invalidates a held reference, and the crash lands later, in a loop
// that looks unrelated. Found by a SIGSEGV that reproduced in roughly 1 run in 5.

void WebSocketServer::onReadyRead(int clientId) {
    auto it = m_sessions.find(clientId);
    if (it == m_sessions.end() || !it->socket) {
        return;
    }
    const QByteArray chunk = it->socket->readAll();

    if (!it->upgraded) {
        it->handshakeBuffer.append(chunk);
        // tryUpgrade consumes the handshake buffer and emits; nothing may be held across it.
        if (!tryUpgrade(clientId)) {
            return; // still waiting for the rest, dropped, or gone during the emit
        }
    } else {
        it->decoder.append(chunk);
    }

    pumpFrames(clientId);
}

bool WebSocketServer::tryUpgrade(int clientId) {
    auto it = m_sessions.find(clientId);
    if (it == m_sessions.end() || !it->socket) {
        return false;
    }

    const int end = it->handshakeBuffer.indexOf(kHeaderEnd);
    if (end < 0) {
        if (it->handshakeBuffer.size() > MAX_HANDSHAKE_BYTES) {
            dropSession(clientId, WebSocketFrame::CloseProtocolError, QStringLiteral("handshake too large"));
        }
        return false;
    }

    const QByteArray request = it->handshakeBuffer.left(end);
    const QString key = headerValue(request, "Sec-WebSocket-Key");
    const QString upgrade = headerValue(request, "Upgrade");

    // Copied out before any write: flush() and disconnectFromHost() can re-enter and erase.
    QTcpSocket *socket = it->socket;

    if (key.isEmpty() || upgrade.compare(QStringLiteral("websocket"), Qt::CaseInsensitive) != 0) {
        // Answer plain HTTP rather than dropping silently: a browser or curl pointed here should
        // get a readable error, not a reset connection.
        socket->write("HTTP/1.1 400 Bad Request\r\nConnection: close\r\n"
                      "Content-Length: 31\r\n\r\nThis endpoint expects WebSocket\n");
        socket->flush();
        socket->disconnectFromHost();
        return false;
    }

    // No subprotocol is negotiated and the request path is not checked. Both are deliberate:
    // AetherSDR accepts any path and negotiates none, and TCI clients rely on that.
    const QByteArray response = "HTTP/1.1 101 Switching Protocols\r\n"
                                "Upgrade: websocket\r\n"
                                "Connection: Upgrade\r\n"
                                "Sec-WebSocket-Accept: " +
                                WebSocketFrame::acceptKey(key).toLatin1() +
                                "\r\n"
                                "Server: QK4\r\n\r\n";
    // Everything that reads the session is done BEFORE the first write, because a write or flush
    // can surface a disconnect and erase the entry underneath us.
    it->upgraded = true;
    // Starts the liveness clock here rather than on the first frame, so a peer that completes the
    // handshake and then says nothing at all is still covered by the timeout.
    it->lastInbound.start();
    const QByteArray leftover = it->handshakeBuffer.mid(end + 4);
    it->handshakeBuffer.clear();
    if (!leftover.isEmpty()) {
        it->decoder.append(leftover);
    }
    // host:port, not host alone. Every client on a loopback listener reports the same address, so
    // without the port two connections are indistinguishable in anything that lists them - which is
    // exactly what the TCI options page does. The port is what names the CONNECTION.
    //
    // IPv6 is bracketed, or the port separator would run into the address's own colons and produce
    // something like ::1:54321 that cannot be read back apart.
    const QString host = socket->peerAddress().toString();
    const QString peer = (host.contains(QLatin1Char(':')) ? QStringLiteral("[%1]").arg(host) : host) +
                         QLatin1Char(':') + QString::number(socket->peerPort());

    socket->write(response);
    socket->flush();

    emit clientConnected(clientId, peer);
    // The consumer's slot may have dropped this client while sending its greeting.
    return m_sessions.contains(clientId);
}

void WebSocketServer::pumpFrames(int clientId) {
    for (;;) {
        auto it = m_sessions.find(clientId);
        if (it == m_sessions.end() || !it->socket) {
            return;
        }

        WebSocketDecoder::Message message;
        const WebSocketDecoder::Status status = it->decoder.next(message);
        if (status == WebSocketDecoder::Status::NeedMoreData) {
            return;
        }
        if (status == WebSocketDecoder::Status::Error) {
            const quint16 code = it->decoder.closeCode();
            const QString why = it->decoder.errorString();
            dropSession(clientId, code, why);
            return;
        }

        // ANY frame is proof of life, not just the PONG. A client streaming transmit audio or
        // polling state never needs to be probed, and must never be dropped for not answering a
        // PING it was too busy to notice.
        it->lastInbound.restart();

        switch (message.opcode) {
        case WebSocketFrame::OpText:
            emit textMessageReceived(clientId, QString::fromUtf8(message.payload));
            break;
        case WebSocketFrame::OpBinary:
            emit binaryMessageReceived(clientId, message.payload);
            break;
        case WebSocketFrame::OpPing:
            // RFC 6455 5.5.3: a PONG must carry the PING's payload verbatim.
            sendFrame(clientId, WebSocketFrame::OpPong, message.payload);
            break;
        case WebSocketFrame::OpPong:
            break; // unsolicited PONGs are legal and carry no obligation
        case WebSocketFrame::OpClose:
            sendFrame(clientId, WebSocketFrame::OpClose, message.payload);
            if (auto it = m_sessions.find(clientId); it != m_sessions.end() && it->socket) {
                it->socket->disconnectFromHost();
            }
            return;
        default:
            dropSession(clientId, WebSocketFrame::CloseProtocolError, QStringLiteral("unexpected opcode"));
            return;
        }

        // The handler above may have dropped the session.
        if (!m_sessions.contains(clientId)) {
            return;
        }
    }
}

void WebSocketServer::onLivenessTick() {
    // Snapshot the ids: dropSession erases from m_sessions, and it is reachable from this loop.
    const QList<int> ids = m_sessions.keys();
    for (int id : ids) {
        auto it = m_sessions.find(id);
        if (it == m_sessions.end() || !it->upgraded || !it->lastInbound.isValid()) {
            continue; // still handshaking; MAX_HANDSHAKE_BYTES covers that phase
        }
        if (it->lastInbound.elapsed() > m_silenceTimeoutMs) {
            qCWarning(netWs) << "client" << id << "has not answered in" << it->lastInbound.elapsed()
                             << "ms - dropping it as dead";
            // Whatever it was holding is released by the clientDisconnected this raises. For the
            // TCI server that is the transmitter; see TciServer::onClientDisconnected.
            dropSession(id, WebSocketFrame::CloseGoingAway, QStringLiteral("no response"));
            continue;
        }
        // RFC 6455 5.5.2: the peer must answer this. A client that does not is indistinguishable
        // from one that has gone, and will be dropped at the timeout above.
        sendFrame(id, WebSocketFrame::OpPing, QByteArray());
    }
}

void WebSocketServer::dropSession(int clientId, quint16 code, const QString &why) {
    auto it = m_sessions.find(clientId);
    if (it == m_sessions.end()) {
        return;
    }
    // Same rule as closeClient: copy out before anything that can re-enter, and emit last so a
    // consumer cannot mutate m_sessions while a reference is live.
    QTcpSocket *socket = it->socket;
    const bool upgraded = it->upgraded;

    if (socket) {
        if (upgraded) {
            socket->write(WebSocketFrame::encodeClose(code, why));
            socket->flush();
        }
        socket->disconnectFromHost();
    }
    emit errorOccurred(QStringLiteral("client %1: %2").arg(clientId).arg(why));
}

void WebSocketServer::onDisconnected(int clientId) {
    auto it = m_sessions.find(clientId);
    if (it == m_sessions.end()) {
        return;
    }
    QTcpSocket *socket = it->socket;
    const bool wasUpgraded = it->upgraded;
    m_sessions.erase(it);
    if (socket) {
        socket->disconnect(this);
        socket->deleteLater();
    }
    // A peer that never completed the upgrade was never a client, so no paired signal is owed.
    if (wasUpgraded) {
        emit clientDisconnected(clientId);
    }
}

void WebSocketServer::setLivenessPolicy(int pingIntervalMs, int silenceTimeoutMs) {
    m_livenessTimer->setInterval(pingIntervalMs);
    m_silenceTimeoutMs = silenceTimeoutMs;
}

// HARD LIMIT FIRST, and it applies to every frame including control: past it the peer is not
// reading anything at all, so there is nothing to be gained by holding the session open while a
// megabyte of our memory stays committed to it.
//
// The soft limit sheds AUDIO ONLY. For a live stream, late audio is worthless and dropping is the
// correct response where queueing is not; control frames are small and carry state the client
// cannot re-derive, so they are never shed.
WebSocketServer::SendDecision WebSocketServer::decideSend(qint64 queuedBytes, bool sheddable) {
    if (queuedBytes > SEND_QUEUE_HARD_LIMIT_BYTES) {
        return SendDecision::DropSession;
    }
    if (sheddable && queuedBytes > SEND_QUEUE_AUDIO_DROP_BYTES) {
        return SendDecision::DropFrame;
    }
    return SendDecision::Send;
}

bool WebSocketServer::writeFrame(int clientId, const QByteArray &frame, bool sheddable) {
    auto it = m_sessions.find(clientId);
    if (it == m_sessions.end() || !it->socket || !it->upgraded) {
        return false;
    }
    QTcpSocket *socket = it->socket;
    const qint64 queued = socket->bytesToWrite();
    const SendDecision decision = decideSend(queued, sheddable);

    if (decision == SendDecision::DropSession) {
        qCWarning(netWs) << "client" << clientId << "has" << queued
                         << "bytes unread and is not draining - dropping the session";
        dropSession(clientId, WebSocketFrame::ClosePolicyViolation, QStringLiteral("send queue overflow"));
        return false;
    }

    if (decision == SendDecision::DropFrame) {
        ++it->droppedAudioFrames;
        if (!it->reportedShedding) {
            it->reportedShedding = true;
            qCInfo(netWs) << "client" << clientId << "is not keeping up (" << queued
                          << "bytes queued) - dropping audio frames until it does";
        }
        return false;
    }

    // Recovered: say so once, so a log shows the episode ending as well as starting.
    if (it->reportedShedding && queued <= SEND_QUEUE_AUDIO_DROP_BYTES) {
        it->reportedShedding = false;
        qCInfo(netWs) << "client" << clientId << "caught up after dropping" << it->droppedAudioFrames
                      << "audio frame(s)";
    }

    socket->write(frame); // write() can re-enter; nothing above is held across it
    return true;
}

void WebSocketServer::sendFrame(int clientId, quint8 opcode, const QByteArray &payload) {
    // Binary is audio on this server, and audio is the only thing that may be shed.
    writeFrame(clientId, WebSocketFrame::encode(opcode, payload), opcode == WebSocketFrame::OpBinary);
}

void WebSocketServer::sendText(int clientId, const QString &text) {
    sendFrame(clientId, WebSocketFrame::OpText, text.toUtf8());
}

void WebSocketServer::sendBinary(int clientId, const QByteArray &payload) {
    sendFrame(clientId, WebSocketFrame::OpBinary, payload);
}

void WebSocketServer::broadcastText(const QString &text) {
    const QByteArray frame = WebSocketFrame::encode(WebSocketFrame::OpText, text.toUtf8());

    // WHY snapshot instead of writing while iterating: write() can surface a disconnect
    // synchronously, and erasing from m_sessions mid-loop invalidates the iterator. This is the
    // path every CAT broadcast takes, so it has to be safe by construction.
    //
    // IDS, not socket pointers, which the previous version snapshotted. A dropped session deletes
    // its socket, so a pointer captured before the loop could be dangling by the time the loop
    // reaches it - now reachable, because writeFrame can drop a session on overflow. Re-looking up
    // by id makes that a miss rather than a use-after-free.
    const QList<int> targets = m_sessions.keys();
    for (int id : targets) {
        writeFrame(id, frame, /*sheddable=*/false); // broadcasts are control text, never shed
    }
}

void WebSocketServer::closeClient(int clientId, quint16 code, const QString &reason) {
    auto it = m_sessions.find(clientId);
    if (it == m_sessions.end() || !it->socket) {
        return;
    }
    // WHY copy both out first: flush() on a socket whose peer has already gone delivers
    // `disconnected` SYNCHRONOUSLY, which runs onDisconnected and erases this entry. Touching the
    // iterator afterwards dereferences a dead node - it asserted in QHash about one run in twenty.
    QTcpSocket *socket = it->socket;
    const bool upgraded = it->upgraded;

    if (upgraded) {
        socket->write(WebSocketFrame::encodeClose(code, reason));
        socket->flush();
    }
    socket->disconnectFromHost();
}
