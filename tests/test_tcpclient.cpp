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
};

QTEST_GUILESS_MAIN(TestTcpClient)
#include "test_tcpclient.moc"
