#include <QtTest>

#include <QSignalSpy>

#include "network/tciclientinfo.h"
#include "network/tciradiostate.h"
#include "network/tciserver.h"
#include "tcitestclient.h"

// The client roster the TCI options page lists: address, last-seen time, and the last message
// exchanged in either direction.
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

        QTRY_COMPARE(server.clients().size(), 1);
        const QVector<TciClientInfo> clients = server.clients();
        QVERIFY(!clients[0].address.isEmpty());
        // A loopback listener can report either form depending on the stack; both are the local
        // host, and pinning one would make this fail on a machine configured the other way.
        QVERIFY2(clients[0].address.contains(QLatin1String("127.0.0.1")) ||
                     clients[0].address.contains(QLatin1String("::1")),
                 qPrintable(clients[0].address));
        // The PORT is what distinguishes two clients from the same host, which on a loopback
        // listener is every client. Without it the table cannot tell two rows apart.
        QVERIFY2(clients[0].address.contains(QLatin1Char(':')), qPrintable(clients[0].address));
        bool portOk = false;
        clients[0].address.section(QLatin1Char(':'), -1).toUShort(&portOk);
        QVERIFY2(portOk, qPrintable(QStringLiteral("no peer port in ") + clients[0].address));

        QVERIFY(clients[0].lastMessageTime.isValid());

        // The init burst counts: a client that has only just connected has still been sent forty
        // commands, and the roster says so rather than showing a placeholder.
        QVERIFY(clients[0].lastMessageOutbound);
        QCOMPARE(clients[0].lastMessage, QStringLiteral("ready;"));
    }

    void aCommandWithNoReplyIsRecordedAsInbound() {
        // An unhandled TCI command is answered with silence by design, which makes it the one
        // exchange that leaves an INBOUND message as the last thing that happened.
        TciServer server;
        const quint16 port = startServer(&server);
        QVERIFY(port != 0);

        TciTestClient client;
        QVERIFY(client.connectTo(port));
        drainBurst(&client);

        client.send("not_a_tci_command:1;");
        QTRY_VERIFY(server.clients()[0].lastMessage.contains(QLatin1String("not_a_tci_command")));
        QVERIFY(!server.clients()[0].lastMessageOutbound);
    }

    void aReplyOverwritesTheCommandThatCausedIt() {
        // The ordinary case, and the consequence of counting both directions: every SET is
        // confirmed immediately, so what the roster shows a moment later is QK4's answer rather
        // than the client's command. The arrow is what tells them apart, which is why direction is
        // stored rather than baked into the text.
        TciServer server;
        const quint16 port = startServer(&server);
        QVERIFY(port != 0);

        TciTestClient client;
        QVERIFY(client.connectTo(port));
        drainBurst(&client);

        client.send("modulation:0,cw;");
        QTRY_VERIFY(server.clients()[0].lastMessage.startsWith(QLatin1String("modulation:")));
        QVERIFY(server.clients()[0].lastMessageOutbound);
    }

    void aBroadcastMarksEveryRowAndAnnouncesOnce() {
        // A broadcast reaches every client at the same instant. Routing it through the per-client
        // path would emit the whole roster once per client for one message.
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

        // Wait out the throttle so the announcement below is the one being counted.
        QTest::qWait(600);
        QSignalSpy spy(&server, &TciServer::clientsChanged);

        TciRadioSnapshot snapshot = server.snapshot();
        snapshot.rx[TciRadio::MAIN_RECEIVER].vfoHz = 21074000;
        server.setSnapshot(snapshot);

        const QVector<TciClientInfo> clients = server.clients();
        QCOMPARE(clients.size(), 2);
        for (const TciClientInfo &c : clients) {
            QVERIFY2(c.lastMessageOutbound, "a broadcast is something QK4 sent");
            QVERIFY2(c.lastMessage.contains(QLatin1String("21074000")), qPrintable(c.lastMessage));
        }
        // One frequency change broadcasts vfo and dds, so two announcements is the ceiling. The
        // point is that it does not scale with the number of CLIENTS.
        QVERIFY2(spy.count() <= 2, qPrintable(QStringLiteral("announced %1 times").arg(spy.count())));
    }

    void chatterIsThrottledIntoFarFewerAnnouncements() {
        // RECORDING is not throttled, only ANNOUNCING is - and the distinction matters both ways.
        // Announcing every frame would put a queued signal carrying a copy of the whole roster on
        // the wire ~47 times a second during transmit; recording only when announcing would lose
        // what was last exchanged the moment a client went quiet.
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
        QTRY_VERIFY(server.clients()[0].lastMessage.startsWith(QLatin1String("modulation:")));

        // Bounded rather than pinned: the throttle is a 500 ms clock, so the exact count depends on
        // how long the loop took. Even a machine slow enough to spend ten seconds on 50 messages
        // announces ~20 times, well under 50 - while an unthrottled server would announce at least
        // 100, since each of these produces a reply as well.
        QVERIFY2(spy.count() < kMessages / 2,
                 qPrintable(QStringLiteral("announced %1 times for %2 messages").arg(spy.count()).arg(kMessages)));
    }

    void aTxAudioFrameCountsAsActivity() {
        // A WSJT-X client sends no text for the length of a 15-second transmission. If binary
        // frames did not count, the roster would report it as stale at the one moment it is
        // busiest. Recorded as a single "(tx audio)" rather than per frame, and inbound - the
        // matching RX audio stream is deliberately NOT recorded, or it would pin every row.
        TciServer server;
        const quint16 port = startServer(&server);
        QVERIFY(port != 0);

        TciTestClient client;
        QVERIFY(client.connectTo(port));
        drainBurst(&client);

        client.sendBinary(QByteArray(64, '\0'));
        QTRY_VERIFY(server.clients()[0].lastMessage.contains(QLatin1String("tx audio")));
        QVERIFY(!server.clients()[0].lastMessageOutbound);
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
        QTRY_COMPARE(server.clients().size(), 1);

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
        QTRY_COMPARE(server.clients().size(), 1);

        QSignalSpy spy(&server, &TciServer::clientsChanged);
        server.stop();
        QCOMPARE(server.clients().size(), 0);
        QVERIFY(spy.count() >= 1);
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
