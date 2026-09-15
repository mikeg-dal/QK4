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

void WebSocketServer::onReadyRead(int clientId) {
    auto it = m_sessions.find(clientId);
    if (it == m_sessions.end() || !it->socket) {
        return;
    }
    Session &session = *it;
    const QByteArray chunk = session.socket->readAll();

    if (!session.upgraded) {
        session.handshakeBuffer.append(chunk);
        if (!tryUpgrade(clientId, session)) {
            return; // either still waiting for the rest, or the session was dropped
        }
        // Anything after the header block is already WebSocket data.
        const int end = session.handshakeBuffer.indexOf(kHeaderEnd);
        const QByteArray leftover = session.handshakeBuffer.mid(end + 4);
        session.handshakeBuffer.clear();
        if (!leftover.isEmpty()) {
            session.decoder.append(leftover);
        }
    } else {
        session.decoder.append(chunk);
    }

    pumpFrames(clientId, session);
}

bool WebSocketServer::tryUpgrade(int clientId, Session &session) {
    const int end = session.handshakeBuffer.indexOf(kHeaderEnd);
    if (end < 0) {
        if (session.handshakeBuffer.size() > MAX_HANDSHAKE_BYTES) {
            dropSession(clientId, WebSocketFrame::CloseProtocolError, QStringLiteral("handshake too large"));
        }
        return false;
    }

    const QByteArray request = session.handshakeBuffer.left(end);
    const QString key = headerValue(request, "Sec-WebSocket-Key");
    const QString upgrade = headerValue(request, "Upgrade");

    if (key.isEmpty() || upgrade.compare(QStringLiteral("websocket"), Qt::CaseInsensitive) != 0) {
        // Answer plain HTTP rather than dropping silently: a browser or curl pointed here should
        // get a readable error, not a reset connection.
        if (session.socket) {
            session.socket->write("HTTP/1.1 400 Bad Request\r\nConnection: close\r\n"
                                  "Content-Length: 31\r\n\r\nThis endpoint expects WebSocket\n");
            session.socket->flush();
            session.socket->disconnectFromHost();
        }
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
    session.socket->write(response);
    session.socket->flush();
    session.upgraded = true;

    emit clientConnected(clientId, session.socket->peerAddress().toString());
    return true;
}

void WebSocketServer::pumpFrames(int clientId, Session &session) {
    for (;;) {
        WebSocketDecoder::Message message;
        const WebSocketDecoder::Status status = session.decoder.next(message);
        if (status == WebSocketDecoder::Status::NeedMoreData) {
            return;
        }
        if (status == WebSocketDecoder::Status::Error) {
            dropSession(clientId, session.decoder.closeCode(), session.decoder.errorString());
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
    emit errorOccurred(QStringLiteral("client %1: %2").arg(clientId).arg(why));
    if (it->socket) {
        if (it->upgraded) {
            it->socket->write(WebSocketFrame::encodeClose(code, why));
            it->socket->flush();
        }
        it->socket->disconnectFromHost();
    }
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
    it->socket->write(WebSocketFrame::encode(opcode, payload));
}

void WebSocketServer::sendText(int clientId, const QString &text) {
    sendFrame(clientId, WebSocketFrame::OpText, text.toUtf8());
}

void WebSocketServer::sendBinary(int clientId, const QByteArray &payload) {
    sendFrame(clientId, WebSocketFrame::OpBinary, payload);
}

void WebSocketServer::broadcastText(const QString &text) {
    const QByteArray frame = WebSocketFrame::encode(WebSocketFrame::OpText, text.toUtf8());
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        if (it->socket && it->upgraded) {
            it->socket->write(frame);
        }
    }
}

void WebSocketServer::closeClient(int clientId, quint16 code, const QString &reason) {
    auto it = m_sessions.find(clientId);
    if (it == m_sessions.end() || !it->socket) {
        return;
    }
    if (it->upgraded) {
        it->socket->write(WebSocketFrame::encodeClose(code, reason));
        it->socket->flush();
    }
    it->socket->disconnectFromHost();
}
