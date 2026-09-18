#include "network/websocketserver.h"

#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

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

WebSocketServer::WebSocketServer(QObject *parent) : QObject(parent), m_server(new QTcpServer(this)) {
    connect(m_server, &QTcpServer::newConnection, this, &WebSocketServer::onNewConnection);
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
    emit started(m_server->serverPort());
    return true;
}

void WebSocketServer::stop() {
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

void WebSocketServer::sendFrame(int clientId, quint8 opcode, const QByteArray &payload) {
    auto it = m_sessions.find(clientId);
    if (it == m_sessions.end() || !it->socket || !it->upgraded) {
        return;
    }
    QTcpSocket *socket = it->socket; // write() can re-enter; do not hold the iterator across it
    socket->write(WebSocketFrame::encode(opcode, payload));
}

void WebSocketServer::sendText(int clientId, const QString &text) {
    sendFrame(clientId, WebSocketFrame::OpText, text.toUtf8());
}

void WebSocketServer::sendBinary(int clientId, const QByteArray &payload) {
    sendFrame(clientId, WebSocketFrame::OpBinary, payload);
}

void WebSocketServer::broadcastText(const QString &text) {
    const QByteArray frame = WebSocketFrame::encode(WebSocketFrame::OpText, text.toUtf8());

    // WHY snapshot the sockets instead of writing while iterating: write() can surface a
    // disconnect synchronously, and erasing from m_sessions mid-loop invalidates the iterator.
    // This is the path every CAT broadcast will take, so it has to be safe by construction.
    QList<QTcpSocket *> targets;
    targets.reserve(m_sessions.size());
    for (auto it = m_sessions.cbegin(); it != m_sessions.cend(); ++it) {
        if (it->socket && it->upgraded) {
            targets.append(it->socket);
        }
    }
    for (QTcpSocket *socket : targets) {
        socket->write(frame);
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
