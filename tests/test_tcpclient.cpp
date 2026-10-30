// TcpClient against loopback fake radios on the unencrypted port's protocol: a SHA-384 password
// hash, then framed packets. Nothing here connects to a real radio.
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include "network/protocol.h"
#include "network/tcpclient.h"

namespace {

// A listening fake radio that keeps every connection it accepts and what each one sent.
class FakeRadio : public QObject {
public:
    FakeRadio() {
        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket *s = m_server.nextPendingConnection()) {
                m_sockets.append(s);
                m_received.append(QByteArray());
                const qsizetype index = m_sockets.size() - 1;
                connect(s, &QTcpSocket::readyRead, this, [this, s, index]() { m_received[index] += s->readAll(); });
            }
        });
    }

    bool listen() { return m_server.listen(QHostAddress::LocalHost); }
    quint16 port() const { return m_server.serverPort(); }
    void pauseAccepting() { m_server.pauseAccepting(); }
    void resumeAccepting() { m_server.resumeAccepting(); }

    qsizetype connectionCount() const { return m_sockets.size(); }
    QByteArray allReceived() const {
        QByteArray all;
        for (const QByteArray &b : m_received)
            all += b;
        return all;
    }
    QByteArray received(qsizetype i) const { return m_received.value(i); }
    QTcpSocket *socket(qsizetype i) const { return m_sockets.value(i); }

    // What the K4 does once it accepts the hash: start talking. Any packet completes authentication.
    void answer(qsizetype i, const QString &cat = QStringLiteral("RDY;")) {
        m_sockets.at(i)->write(Protocol::buildCATPacket(cat));
    }

private:
    QTcpServer m_server;
    QList<QTcpSocket *> m_sockets;
    QList<QByteArray> m_received;
};

const QString kHost = QStringLiteral("127.0.0.1");

} // namespace

class TestTcpClient : public QObject {
    Q_OBJECT

private slots:
    // TRN-001. Switching radios queues connectToHost(B) while the attempt on A is still connecting.
    // Qt's disconnectFromHost() leaves a connecting socket to finish, so the old code went on to
    // send B's password hash to A, while connectToHost(B) failed with OperationError.
    void switchingRadiosNeverSendsNewPasswordToOldHost() {
        FakeRadio oldRadio;
        FakeRadio newRadio;
        QVERIFY(oldRadio.listen());
        QVERIFY(newRadio.listen());
        // Not accepting yet: the kernel still completes the handshake, so A's connection is real
        // and anything sent on it is waiting for us once we accept.
        oldRadio.pauseAccepting();

        TcpClient client;
        QSignalSpy errors(&client, &TcpClient::errorOccurred);
        client.connectToHost(kHost, oldRadio.port(), QStringLiteral("old"));
        client.connectToHost(kHost, newRadio.port(), QStringLiteral("new"));

        const QByteArray newHash = Protocol::buildAuthData(QStringLiteral("new"));
        QTRY_COMPARE(newRadio.allReceived(), newHash);
        QCOMPARE(client.connectionState(), TcpClient::Authenticating);

        oldRadio.resumeAccepting();
        QTest::qWait(200);
        QCOMPARE(oldRadio.allReceived(), QByteArray());
        QVERIFY2(errors.isEmpty(), qPrintable(errors.isEmpty() ? QString() : errors.first().first().toString()));
    }

    // TRN-002. Cancelling while the radio has not yet answered is the operator's choice, not a
    // failure. The old code closed the socket before leaving Authenticating, so the synchronous
    // disconnected() was reported as "closed the connection without responding".
    void cancellingDuringAuthenticationReportsNothing() {
        FakeRadio radio;
        QVERIFY(radio.listen());
        TcpClient client;
        QSignalSpy errors(&client, &TcpClient::errorOccurred);
        QSignalSpy states(&client, &TcpClient::stateChanged);

        client.connectToHost(kHost, radio.port(), QStringLiteral("pw"));
        QTRY_COMPARE(client.connectionState(), TcpClient::Authenticating);
        client.disconnectFromHost();
        QCOMPARE(client.connectionState(), TcpClient::Disconnected);
        QTRY_VERIFY(radio.connectionCount() == 1 && radio.socket(0)->state() == QAbstractSocket::UnconnectedState);
        QTest::qWait(100);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.isEmpty() ? QString() : errors.first().first().toString()));
        QCOMPARE(states.last().first().value<TcpClient::ConnectionState>(), TcpClient::Disconnected);
    }

    // TRN-002. A radio that accepts the connection and never answers is one failure, reported once.
    // The old code reported the timeout and then, from the close it caused, a second and false
    // "closed the connection without responding".
    void authTimeoutIsReportedExactlyOnce() {
        FakeRadio radio;
        QVERIFY(radio.listen());
        TcpClient client;
        QSignalSpy errors(&client, &TcpClient::errorOccurred);

        client.connectToHost(kHost, radio.port(), QStringLiteral("pw"));
        QTRY_COMPARE(client.connectionState(), TcpClient::Authenticating);
        QTRY_COMPARE_WITH_TIMEOUT(client.connectionState(), TcpClient::Disconnected,
                                  K4Protocol::AUTH_TIMEOUT_MS + 2000);
        QTest::qWait(200);
        QCOMPARE(errors.size(), 1);
        QVERIFY(errors.first().first().toString().contains(QStringLiteral("sent no data")));
    }

    // The radio closing the connection before answering is still reported, once.
    void radioClosingBeforeAnsweringIsReportedOnce() {
        FakeRadio radio;
        QVERIFY(radio.listen());
        TcpClient client;
        QSignalSpy errors(&client, &TcpClient::errorOccurred);

        client.connectToHost(kHost, radio.port(), QStringLiteral("pw"));
        QTRY_COMPARE(client.connectionState(), TcpClient::Authenticating);
        QTRY_COMPARE(radio.connectionCount(), 1);
        radio.socket(0)->close();
        QTRY_COMPARE(client.connectionState(), TcpClient::Disconnected);
        QTest::qWait(100);
        QCOMPARE(errors.size(), 1);
    }

    // TRN-012. The socket is connected from the TCP handshake on, before the radio has accepted the
    // password; nothing but the password hash may be written until it has. The old gate was the
    // socket's state, so TX audio went out ahead of authentication.
    void sendRawBeforeAuthenticationWritesNothing() {
        FakeRadio radio;
        QVERIFY(radio.listen());
        TcpClient client;

        client.connectToHost(kHost, radio.port(), QStringLiteral("pw"));
        QTRY_COMPARE(client.connectionState(), TcpClient::Authenticating);
        client.sendRaw(QByteArrayLiteral("AUDIO"));
        const QByteArray hash = Protocol::buildAuthData(QStringLiteral("pw"));
        QTRY_COMPARE(radio.received(0).size(), hash.size());
        QTest::qWait(100);
        QCOMPARE(radio.received(0), hash);

        // Once the radio answers, the same write goes out.
        radio.answer(0);
        QTRY_COMPARE(client.connectionState(), TcpClient::Connected);
        client.sendRaw(QByteArrayLiteral("AUDIO"));
        QTRY_VERIFY(radio.received(0).endsWith("AUDIO"));
        client.disconnectFromHost();
    }
};

QTEST_GUILESS_MAIN(TestTcpClient)
#include "test_tcpclient.moc"
