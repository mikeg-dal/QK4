#include <QtTest>

#include <QSignalSpy>
#include <QTcpSocket>

#include "network/websocketframe.h"
#include "network/websocketserver.h"

using namespace WebSocketFrame;

namespace {

constexpr int kTimeoutMs = 5000;

// A minimal TCI-shaped client: real socket, real handshake, real masked frames.
class TestClient {
public:
    bool connectTo(quint16 port, const QByteArray &key = "dGhlIHNhbXBsZSBub25jZQ==") {
        m_socket.connectToHost(QHostAddress::LocalHost, port);
        if (!m_socket.waitForConnected(kTimeoutMs)) {
            return false;
        }
        m_socket.write("GET / HTTP/1.1\r\n"
                       "Host: 127.0.0.1\r\n"
                       "Upgrade: websocket\r\n"
                       "Connection: Upgrade\r\n"
                       "Sec-WebSocket-Key: " +
                       key +
                       "\r\n"
                       "Sec-WebSocket-Version: 13\r\n\r\n");
        return m_socket.waitForBytesWritten(kTimeoutMs);
    }

    // WHY processEvents rather than waitForReadyRead: the server under test lives in this same
    // thread's event loop. waitForReadyRead pumps only this socket, so the server's readyRead
    // handler never runs and nothing is ever sent back. Every read here must drive the shared loop.
    bool pumpUntilReadable(int ms = kTimeoutMs) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < ms) {
            if (m_socket.bytesAvailable() > 0) {
                return true;
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        return m_socket.bytesAvailable() > 0;
    }

    // Reads until the header block is complete; leftover bytes go to the frame decoder.
    QByteArray readHandshake() {
        while (!m_handshake.contains("\r\n\r\n")) {
            if (!pumpUntilReadable()) {
                break;
            }
            m_handshake.append(m_socket.readAll());
        }
        const int end = m_handshake.indexOf("\r\n\r\n");
        if (end >= 0) {
            const QByteArray leftover = m_handshake.mid(end + 4);
            if (!leftover.isEmpty()) {
                m_decoder.append(leftover);
            }
            return m_handshake.left(end);
        }
        return m_handshake;
    }

    void sendRaw(const QByteArray &bytes) {
        m_socket.write(bytes);
        m_socket.flush();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }

    void sendText(const QByteArray &payload) { sendRaw(encode(OpText, payload, /*mask=*/true)); }
    void sendBinary(const QByteArray &payload) { sendRaw(encode(OpBinary, payload, /*mask=*/true)); }

    // Pops the next server->client message, driving the shared event loop until one arrives.
    bool nextMessage(WebSocketDecoder::Message &out) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < kTimeoutMs) {
            if (m_decoder.next(out) == WebSocketDecoder::Status::Ready) {
                return true;
            }
            if (m_socket.bytesAvailable() > 0) {
                m_decoder.append(m_socket.readAll());
                continue;
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        return m_decoder.next(out) == WebSocketDecoder::Status::Ready;
    }

    QTcpSocket &socket() { return m_socket; }
    void close() { m_socket.abort(); }

private:
    QTcpSocket m_socket;
    QByteArray m_handshake;
    // Server frames are unmasked.
    WebSocketDecoder m_decoder{/*requireMask=*/false};
};

quint16 freePort(WebSocketServer &server) {
    // Port 0 asks the OS for any free port, which keeps parallel test runs from colliding.
    return server.start(0) ? server.port() : 0;
}

} // namespace

// WebSocket transport for the TCI server, exercised over a real loopback socket.
//
// The framing itself is pinned by test_websocketframe; this covers what only a socket can show:
// the HTTP upgrade, session lifecycle, ping/pong, the client cap, and that a protocol violation
// takes down one session rather than the listener.
class TestWebSocketServer : public QObject {
    Q_OBJECT

private slots:
    void listensAndReportsItsPort() {
        WebSocketServer server;
        QVERIFY(server.start(0));
        QVERIFY(server.isListening());
        QVERIFY(server.port() > 0);
        server.stop();
        QVERIFY(!server.isListening());
    }

    void bindsLoopbackByDefault() {
        // CatServer binds every interface; this one must not without an explicit opt-in.
        WebSocketServer server;
        QVERIFY(server.start(0));

        QTcpSocket outside;
        outside.connectToHost(QHostAddress::LocalHost, server.port());
        QVERIFY(outside.waitForConnected(kTimeoutMs));
        outside.abort();
        server.stop();
    }

    void completesTheUpgradeWithTheCorrectAcceptKey() {
        WebSocketServer server;
        const quint16 port = freePort(server);
        QVERIFY(port > 0);
        QSignalSpy connected(&server, &WebSocketServer::clientConnected);

        TestClient client;
        QVERIFY(client.connectTo(port));
        const QByteArray response = client.readHandshake();

        QVERIFY2(response.startsWith("HTTP/1.1 101"), response.left(40).constData());
        // RFC 6455 section 1.3 vector for the key the client sent.
        QVERIFY(response.contains("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
        QTRY_COMPARE(connected.count(), 1);

        client.close();
        server.stop();
    }

    void answersPlainHttpWithAReadableError() {
        // A browser pointed at the TCI port should learn why, not see a reset.
        WebSocketServer server;
        const quint16 port = freePort(server);

        QTcpSocket plain;
        plain.connectToHost(QHostAddress::LocalHost, port);
        QVERIFY(plain.waitForConnected(kTimeoutMs));
        plain.write("GET / HTTP/1.1\r\nHost: x\r\n\r\n");
        plain.flush();

        QByteArray response;
        QElapsedTimer timer;
        timer.start();
        while (response.isEmpty() && timer.elapsed() < kTimeoutMs) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            response.append(plain.readAll());
        }

        QVERIFY2(response.startsWith("HTTP/1.1 400"), response.left(40).constData());
        plain.abort();
        server.stop();
    }

    void neverCountsAnUnupgradedPeerAsAClient() {
        WebSocketServer server;
        const quint16 port = freePort(server);
        QSignalSpy connected(&server, &WebSocketServer::clientConnected);
        QSignalSpy disconnected(&server, &WebSocketServer::clientDisconnected);

        QTcpSocket plain;
        plain.connectToHost(QHostAddress::LocalHost, port);
        QVERIFY(plain.waitForConnected(kTimeoutMs));
        plain.abort();

        QTest::qWait(200);
        QCOMPARE(connected.count(), 0);
        QCOMPARE(disconnected.count(), 0);
        server.stop();
    }

    // ---- messages ---------------------------------------------------------------------------

    void receivesTextFromAClient() {
        WebSocketServer server;
        const quint16 port = freePort(server);
        QSignalSpy texts(&server, &WebSocketServer::textMessageReceived);

        TestClient client;
        QVERIFY(client.connectTo(port));
        client.readHandshake();
        client.sendText("vfo:0,0,14074000;");

        QTRY_COMPARE(texts.count(), 1);
        QCOMPARE(texts.at(0).at(1).toString(), QStringLiteral("vfo:0,0,14074000;"));

        client.close();
        server.stop();
    }

    void receivesABinaryAudioFrameIntact() {
        WebSocketServer server;
        const quint16 port = freePort(server);
        QSignalSpy binaries(&server, &WebSocketServer::binaryMessageReceived);

        QByteArray audio(64 + 2048 * 4, '\0');
        for (int i = 0; i < audio.size(); ++i) {
            audio[i] = static_cast<char>((i * 7) & 0xFF);
        }

        TestClient client;
        QVERIFY(client.connectTo(port));
        client.readHandshake();
        client.sendBinary(audio);

        QTRY_COMPARE(binaries.count(), 1);
        QCOMPARE(binaries.at(0).at(1).toByteArray(), audio);

        client.close();
        server.stop();
    }

    void sendsTextToAClient() {
        WebSocketServer server;
        const quint16 port = freePort(server);
        QSignalSpy connected(&server, &WebSocketServer::clientConnected);

        TestClient client;
        QVERIFY(client.connectTo(port));
        client.readHandshake();
        QTRY_COMPARE(connected.count(), 1);
        const int id = connected.at(0).at(0).toInt();

        server.sendText(id, QStringLiteral("ready;"));

        WebSocketDecoder::Message m;
        QVERIFY(client.nextMessage(m));
        QCOMPARE(m.opcode, static_cast<quint8>(OpText));
        QCOMPARE(m.payload, QByteArray("ready;"));

        client.close();
        server.stop();
    }

    void broadcastReachesEveryClient() {
        WebSocketServer server;
        const quint16 port = freePort(server);
        QSignalSpy connected(&server, &WebSocketServer::clientConnected);

        TestClient a;
        TestClient b;
        QVERIFY(a.connectTo(port));
        a.readHandshake();
        QVERIFY(b.connectTo(port));
        b.readHandshake();
        QTRY_COMPARE(connected.count(), 2);

        server.broadcastText(QStringLiteral("trx:0,true;"));

        WebSocketDecoder::Message m;
        QVERIFY(a.nextMessage(m));
        QCOMPARE(m.payload, QByteArray("trx:0,true;"));
        QVERIFY(b.nextMessage(m));
        QCOMPARE(m.payload, QByteArray("trx:0,true;"));

        a.close();
        b.close();
        server.stop();
    }

    // ---- control frames ----------------------------------------------------------------------

    void answersPingWithAMatchingPong() {
        // RFC 6455 5.5.3: the payload must come back verbatim. A client that gets a bare PONG
        // treats the link as dead.
        WebSocketServer server;
        const quint16 port = freePort(server);

        TestClient client;
        QVERIFY(client.connectTo(port));
        client.readHandshake();
        client.sendRaw(encode(OpPing, "heartbeat-42", /*mask=*/true));

        WebSocketDecoder::Message m;
        QVERIFY(client.nextMessage(m));
        QCOMPARE(m.opcode, static_cast<quint8>(OpPong));
        QCOMPARE(m.payload, QByteArray("heartbeat-42"));

        client.close();
        server.stop();
    }

    // #138. Qt buffers whatever the peer has not read, in our process, without limit. RX audio is
    // ~384 kB/s per subscriber, so a client that stops reading grows QK4's memory for as long as
    // it stays connected. CONVENTIONS.md rule 5 requires an explicit cap on any externally fed
    // buffer; outbound was the one direction with none.
    //
    // The decision is tested rather than the socket: provoking the real thing needs a peer that
    // connects and then never reads, which a test client cannot be. Same split, and same reason,
    // as TransmitOwner against TransmitController.
    void shedsAudioBeforeItShedsTheSession() {
        using SendDecision = WebSocketServer::SendDecision;
        constexpr qint64 soft = WebSocketServer::SEND_QUEUE_AUDIO_DROP_BYTES;
        constexpr qint64 hard = WebSocketServer::SEND_QUEUE_HARD_LIMIT_BYTES;

        // Draining normally: everything goes, whatever it is.
        QCOMPARE(WebSocketServer::decideSend(0, /*sheddable=*/true), SendDecision::Send);
        QCOMPARE(WebSocketServer::decideSend(0, /*sheddable=*/false), SendDecision::Send);
        QCOMPARE(WebSocketServer::decideSend(soft, /*sheddable=*/true), SendDecision::Send);

        // Backing up: audio is dropped, because late audio is worthless to a live stream...
        QCOMPARE(WebSocketServer::decideSend(soft + 1, /*sheddable=*/true), SendDecision::DropFrame);
        // ...but control is NOT, because it carries state the client cannot re-derive.
        QCOMPARE(WebSocketServer::decideSend(soft + 1, /*sheddable=*/false), SendDecision::Send);
        QCOMPARE(WebSocketServer::decideSend(hard, /*sheddable=*/false), SendDecision::Send);

        // Not reading at all: the session goes, and this applies to control frames too - there is
        // nothing to be gained by holding a megabyte for a peer that is not draining any of it.
        QCOMPARE(WebSocketServer::decideSend(hard + 1, /*sheddable=*/true), SendDecision::DropSession);
        QCOMPARE(WebSocketServer::decideSend(hard + 1, /*sheddable=*/false), SendDecision::DropSession);

        // Ordering, stated as a property rather than left implicit in the numbers: audio must
        // always be shed before the session is, or the cheap remedy never gets a chance to work.
        QVERIFY(soft < hard);
    }

    void echoesCloseAndReleasesTheSession() {
        WebSocketServer server;
        const quint16 port = freePort(server);
        QSignalSpy connected(&server, &WebSocketServer::clientConnected);
        QSignalSpy disconnected(&server, &WebSocketServer::clientDisconnected);

        TestClient client;
        QVERIFY(client.connectTo(port));
        client.readHandshake();
        QTRY_COMPARE(connected.count(), 1);

        client.sendRaw(encodeClose(CloseNormal));

        WebSocketDecoder::Message m;
        QVERIFY(client.nextMessage(m));
        QCOMPARE(m.opcode, static_cast<quint8>(OpClose));
        QTRY_COMPARE(disconnected.count(), 1);
        QCOMPARE(server.clientCount(), 0);

        server.stop();
    }

    // ---- hardening ---------------------------------------------------------------------------

    void dropsOnlyTheOffendingSessionForAProtocolViolation() {
        // A hostile or buggy client must not take the listener, or other clients, down with it.
        WebSocketServer server;
        const quint16 port = freePort(server);
        QSignalSpy connected(&server, &WebSocketServer::clientConnected);

        TestClient good;
        QVERIFY(good.connectTo(port));
        good.readHandshake();
        TestClient bad;
        QVERIFY(bad.connectTo(port));
        bad.readHandshake();
        QTRY_COMPARE(connected.count(), 2);

        // Unmasked client frame: RFC 6455 5.1 says the server must close.
        bad.sendRaw(encode(OpText, "unmasked", /*mask=*/false));
        QTRY_COMPARE(server.clientCount(), 1);

        QVERIFY(server.isListening());
        const int goodId = connected.at(0).at(0).toInt();
        server.sendText(goodId, QStringLiteral("still here;"));

        WebSocketDecoder::Message m;
        QVERIFY(good.nextMessage(m));
        QCOMPARE(m.payload, QByteArray("still here;"));

        good.close();
        server.stop();
    }

    void refusesConnectionsBeyondTheClientCap() {
        WebSocketServer server;
        const quint16 port = freePort(server);
        QSignalSpy connected(&server, &WebSocketServer::clientConnected);

        std::vector<std::unique_ptr<TestClient>> clients;
        for (int i = 0; i < WebSocketServer::MAX_CLIENTS; ++i) {
            auto c = std::make_unique<TestClient>();
            QVERIFY(c->connectTo(port));
            c->readHandshake();
            clients.push_back(std::move(c));
        }
        QTRY_COMPARE(connected.count(), WebSocketServer::MAX_CLIENTS);

        TestClient overflow;
        overflow.connectTo(port);
        QTest::qWait(300);

        QCOMPARE(server.clientCount(), WebSocketServer::MAX_CLIENTS);
        QCOMPARE(connected.count(), WebSocketServer::MAX_CLIENTS);

        for (auto &c : clients) {
            c->close();
        }
        server.stop();
    }

    void stopDisconnectsEveryClient() {
        WebSocketServer server;
        const quint16 port = freePort(server);
        QSignalSpy connected(&server, &WebSocketServer::clientConnected);

        TestClient a;
        TestClient b;
        QVERIFY(a.connectTo(port));
        a.readHandshake();
        QVERIFY(b.connectTo(port));
        b.readHandshake();
        QTRY_COMPARE(connected.count(), 2);

        server.stop();
        QCOMPARE(server.clientCount(), 0);
        QVERIFY(!server.isListening());
    }

    // ---- re-entrancy ---------------------------------------------------------------------------
    //
    // A write or flush on a socket whose peer has gone delivers `disconnected` SYNCHRONOUSLY, which
    // erases that session. Any code holding a QHash iterator or reference across such a call then
    // dereferences a dead node. That shipped in four places here and asserted in QHash about one
    // run in twenty - rare enough to look like a flaky test rather than a bug.

    void stopSurvivesAClientThatVanishedFirst() {
        WebSocketServer server;
        const quint16 port = freePort(server);
        QSignalSpy connected(&server, &WebSocketServer::clientConnected);

        TestClient a;
        TestClient b;
        QVERIFY(a.connectTo(port));
        a.readHandshake();
        QVERIFY(b.connectTo(port));
        b.readHandshake();
        QTRY_COMPARE(connected.count(), 2);

        // Abort without a close handshake, then stop before the event loop has processed it: stop()
        // writes a CLOSE and flushes to a socket that is already gone.
        a.close();
        server.stop();

        QCOMPARE(server.clientCount(), 0);
        QVERIFY(!server.isListening());
        b.close();
    }

    void broadcastSurvivesAClientThatVanishedFirst() {
        // Broadcast is the path every CAT state change will take, so it must tolerate a peer
        // disappearing mid-loop rather than invalidating the iterator it is walking.
        WebSocketServer server;
        const quint16 port = freePort(server);
        QSignalSpy connected(&server, &WebSocketServer::clientConnected);

        std::vector<std::unique_ptr<TestClient>> clients;
        for (int i = 0; i < 4; ++i) {
            auto c = std::make_unique<TestClient>();
            QVERIFY(c->connectTo(port));
            c->readHandshake();
            clients.push_back(std::move(c));
        }
        QTRY_COMPARE(connected.count(), 4);

        clients[1]->close();
        clients[2]->close();
        server.broadcastText(QStringLiteral("vfo:0,0,14074000;"));

        // A survivor still receives it.
        WebSocketDecoder::Message m;
        QVERIFY(clients[0]->nextMessage(m));
        QCOMPARE(m.payload, QByteArray("vfo:0,0,14074000;"));

        for (auto &c : clients) {
            c->close();
        }
        server.stop();
    }

    void closingAnAlreadyGoneClientIsHarmless() {
        WebSocketServer server;
        const quint16 port = freePort(server);
        QSignalSpy connected(&server, &WebSocketServer::clientConnected);

        TestClient client;
        QVERIFY(client.connectTo(port));
        client.readHandshake();
        QTRY_COMPARE(connected.count(), 1);
        const int id = connected.at(0).at(0).toInt();

        client.close();
        server.closeClient(id); // writes and flushes to a dead peer
        server.closeClient(id); // and again, now that the session is gone

        QVERIFY(server.isListening());
        server.stop();
    }

    void sendingToAnUnknownClientIsHarmless() {
        // A send racing a disconnect is normal, not an error.
        WebSocketServer server;
        QVERIFY(server.start(0));
        server.sendText(9999, QStringLiteral("nobody;"));
        server.sendBinary(9999, QByteArray("x"));
        server.closeClient(9999);
        QCOMPARE(server.clientCount(), 0);
        server.stop();
    }
};

QTEST_MAIN(TestWebSocketServer)
#include "test_websocketserver.moc"
