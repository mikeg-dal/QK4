#include <QtTest>

#include <QSignalSpy>

#include "network/tciclientinfo.h"
#include "network/tciserver.h"
#include "tcitestclient.h"

// The client roster the TCI options page lists: address, last-seen time, last message.
//
// Split from test_tciserver.cpp, which is at 754 of the 800-line limit (CONVENTIONS.md rule 7).
//
// These drive a real socket rather than calling the slots directly, because the peer address is
// supplied by WebSocketServer and only exists on a real connection - a test that fed the slot a
// string of its own would prove nothing about the column the operator actually reads.
class TestTciClients : public QObject {
    Q_OBJECT

private:
    // Port 0 lets the OS pick, so a developer running two suites at once does not collide.
    static quint16 startServer(TciServer *server) {
        const bool ok = server->start(0, /*loopbackOnly=*/true);
        return ok ? server->port() : 0;
    }

    // Reads past the init burst, which arrives unasked on connect.
    static void drainBurst(TciTestClient *client) { client->collectUntil("ready;"); }

private slots:
    void aConnectedClientIsListedWithItsAddress() {
        TciServer server;
        const quint16 port = startServer(&server);
        QVERIFY(port != 0);

        TciTestClient client;
        QVERIFY(client.connectTo(port));
        drainBurst(&client);

        const QVector<TciClientInfo> clients = server.clients();
        QCOMPARE(clients.size(), 1);
        QVERIFY(!clients[0].address.isEmpty());
        // A loopback listener can report either form depending on the stack; both are the local
        // host, and pinning one would make this fail on a machine configured the other way.
        QVERIFY2(clients[0].address.contains(QLatin1String("127.0.0.1")) ||
                     clients[0].address.contains(QLatin1String("::1")),
                 qPrintable(clients[0].address));
        // Never blank: a client that has not said anything yet still has a connect time.
        QVERIFY(clients[0].lastMessageTime.isValid());
        QVERIFY(!clients[0].lastMessage.isEmpty());
    }

    void theLastMessageFromAClientIsRecorded() {
        TciServer server;
        const quint16 port = startServer(&server);
        QVERIFY(port != 0);

        TciTestClient client;
        QVERIFY(client.connectTo(port));
        drainBurst(&client);

        client.send("vfo:0,0,14074000;");
        QTRY_VERIFY(server.clients().size() == 1 &&
                    server.clients()[0].lastMessage.contains(QLatin1String("vfo:0,0,14074000")));

        client.send("modulation:0,cw;");
        QTRY_VERIFY(server.clients()[0].lastMessage.contains(QLatin1String("modulation:0,cw")));
    }

    void chatterIsThrottledIntoFarFewerAnnouncements() {
        // RECORDING is not throttled, only ANNOUNCING is - and the distinction matters both ways.
        // Announcing every frame would put a queued signal carrying a copy of the whole roster on
        // the wire ~47 times a second during transmit; recording only when announcing would lose
        // what a client last said the moment it went quiet.
        TciServer server;
        const quint16 port = startServer(&server);
        QVERIFY(port != 0);

        TciTestClient client;
        QVERIFY(client.connectTo(port));
        drainBurst(&client);

        QSignalSpy spy(&server, &TciServer::clientsChanged);
        constexpr int kMessages = 50;
        for (int i = 0; i < kMessages; ++i) {
            client.send("modulation:0,cw;");
        }
        QTRY_VERIFY(server.clients()[0].lastMessage.contains(QLatin1String("modulation:0,cw")));

        // Bounded rather than pinned: the throttle is a 500 ms clock, so the exact count depends on
        // how long the loop took. Even a machine slow enough to spend ten seconds on 50 messages
        // announces ~20 times, well under 50 - while an unthrottled server would announce 50 for
        // 50, every time.
        QVERIFY2(spy.count() < kMessages / 2,
                 qPrintable(QStringLiteral("announced %1 times for %2 messages").arg(spy.count()).arg(kMessages)));
    }

    void aTxAudioFrameCountsAsActivity() {
        // A WSJT-X client sends no text for the length of a 15-second transmission. If binary
        // frames did not count, the roster would report it as stale at the one moment it is
        // busiest.
        TciServer server;
        const quint16 port = startServer(&server);
        QVERIFY(port != 0);

        TciTestClient client;
        QVERIFY(client.connectTo(port));
        drainBurst(&client);
        client.send("vfo:0,0,14074000;");
        QTRY_VERIFY(server.clients()[0].lastMessage.contains(QLatin1String("vfo")));

        client.sendBinary(QByteArray(64, '\0'));
        QTRY_VERIFY(server.clients()[0].lastMessage.contains(QLatin1String("tx audio")));
    }

    void aDisconnectDropsTheRowAtOnce() {
        // Not throttled: this changes the row SET, and a table still listing a client that hung up
        // is worse than one that updates a cell late.
        TciServer server;
        const quint16 port = startServer(&server);
        QVERIFY(port != 0);

        auto *client = new TciTestClient();
        QVERIFY(client->connectTo(port));
        drainBurst(client);
        QCOMPARE(server.clients().size(), 1);

        QSignalSpy spy(&server, &TciServer::clientsChanged);
        delete client;
        QTRY_COMPARE(server.clients().size(), 0);
        QVERIFY(spy.count() >= 1);
    }

    void stoppingTheServerEmptiesTheRoster() {
        // WebSocketServer::stop() closes the sockets without emitting clientDisconnected for each,
        // so without an explicit clear the page would keep listing clients of a dead listener.
        TciServer server;
        const quint16 port = startServer(&server);
        QVERIFY(port != 0);

        TciTestClient client;
        QVERIFY(client.connectTo(port));
        drainBurst(&client);
        QCOMPARE(server.clients().size(), 1);

        QSignalSpy spy(&server, &TciServer::clientsChanged);
        server.stop();
        QCOMPARE(server.clients().size(), 0);
        QCOMPARE(spy.count(), 1);
    }

    void rowsAreOrderedOldestFirst() {
        // Ids ascend with arrival, so ordering by id keeps a row from jumping around the table
        // because the underlying QHash rehashed.
        TciServer server;
        const quint16 port = startServer(&server);
        QVERIFY(port != 0);

        TciTestClient first;
        QVERIFY(first.connectTo(port));
        drainBurst(&first);
        TciTestClient second;
        QVERIFY(second.connectTo(port));
        drainBurst(&second);

        QTRY_COMPARE(server.clients().size(), 2);
        const QVector<TciClientInfo> clients = server.clients();
        QVERIFY(clients[0].id < clients[1].id);
    }
};

QTEST_MAIN(TestTciClients)
#include "test_tciclients.moc"
