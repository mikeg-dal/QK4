#include <QtTest>

#include <QSignalSpy>

#include "network/tciprotocol.h"
#include "network/tciserver.h"
#include "network/websocketframe.h"
#include "tcitestclient.h"

using namespace WebSocketFrame;

namespace {

// Long enough for several ticks at the fastest interval the spec allows.
constexpr int kSettleMs = 700;

// Collects text messages for a fixed window rather than until a terminator: sensor readings are
// periodic and there is no last one.
QStringList collectFor(TciTestClient &client, int ms) {
    QStringList out;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) {
        WebSocketDecoder::Message m;
        if (!client.next(m, 100)) {
            continue;
        }
        if (m.opcode == OpText) {
            out << QString::fromUtf8(m.payload);
        }
    }
    return out;
}

int countStartingWith(const QStringList &all, const QString &prefix) {
    int n = 0;
    for (const QString &s : all) {
        if (s.startsWith(prefix)) {
            ++n;
        }
    }
    return n;
}

} // namespace

// Sensor reporting: the server-to-client telemetry a client subscribes to and then simply
// receives. This is the class of behaviour an --audit sweep cannot see, because nothing here is
// ever a reply to a query.
class TestTciSensors : public QObject {
    Q_OBJECT

private slots:
    void sendsNothingUntilAClientAsks() {
        // The timer must not run for nobody. A radio reporting meters several times a second into
        // an idle server should put nothing at all on the wire.
        TciServer server;
        QVERIFY(server.start(0));

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");

        TciSensorReadings readings;
        readings.sMeterDbm = -73.0;
        server.setSensors(readings);

        const QStringList seen = collectFor(client, 400);
        QCOMPARE(countStartingWith(seen, QStringLiteral("rx_sensors:")), 0);
        QCOMPARE(countStartingWith(seen, QStringLiteral("rx_channel_sensors:")), 0);
        QCOMPARE(countStartingWith(seen, QStringLiteral("tx_sensors:")), 0);

        client.close();
        server.stop();
    }

    void reportsReceiveLevelsOnceEnabled() {
        TciServer server;
        QVERIFY(server.start(0));

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");

        TciSensorReadings readings;
        readings.sMeterDbm = -73.0; // S9
        server.setSensors(readings);
        client.send("rx_sensors_enable:true,50;");

        const QStringList seen = collectFor(client, kSettleMs);
        QVERIFY2(countStartingWith(seen, QStringLiteral("rx_sensors:")) > 0, "no legacy rx_sensors");
        QVERIFY2(countStartingWith(seen, QStringLiteral("rx_channel_sensors:")) > 0, "no rx_channel_sensors");
        QVERIFY(seen.contains(QStringLiteral("rx_sensors:0,-73.0;")));
        QVERIFY(seen.contains(QStringLiteral("rx_channel_sensors:0,0,-73.0;")));

        client.close();
        server.stop();
    }

    void withholdsChannelBUntilTheSubReceiverIsOn() {
        // Claiming a level for a receiver that is switched off is a reading nobody took.
        TciServer server;
        QVERIFY(server.start(0));

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("rx_sensors_enable:true,50;");

        QStringList seen = collectFor(client, 400);
        QCOMPARE(countStartingWith(seen, QStringLiteral("rx_channel_sensors:0,1,")), 0);
        QVERIFY(countStartingWith(seen, QStringLiteral("rx_channel_sensors:0,0,")) > 0);

        TciRadioSnapshot snapshot;
        snapshot.subEnabled = true;
        server.setSnapshot(snapshot);
        TciSensorReadings readings;
        readings.sMeterSubDbm = -91.0;
        server.setSensors(readings);

        seen = collectFor(client, 400);
        QVERIFY2(seen.contains(QStringLiteral("rx_channel_sensors:0,1,-91.0;")), "sub level never reported");

        client.close();
        server.stop();
    }

    void reportsTransmitReadingsWithAllFiveArguments() {
        // A client indexing args[4] for SWR must not be handed a short message.
        TciServer server;
        QVERIFY(server.start(0));

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");

        TciSensorReadings readings;
        readings.forwardPowerW = 47.4;
        readings.peakPowerW = 47.4;
        readings.swr = 1.35;
        server.setSensors(readings);
        client.send("tx_sensors_enable:true,50;");

        const QStringList seen = collectFor(client, kSettleMs);
        int checked = 0;
        for (const QString &line : seen) {
            if (!line.startsWith(QStringLiteral("tx_sensors:"))) {
                continue;
            }
            ++checked;
            const TciProtocol::Command c = TciProtocol::parseOne(line.chopped(1));
            QCOMPARE(c.argCount(), 5);
            QCOMPARE(c.arg(0), QStringLiteral("0"));
            QCOMPARE(c.arg(2), QStringLiteral("47.4"));
            QCOMPARE(c.arg(4), QStringLiteral("1.35"));
        }
        QVERIFY2(checked > 0, "no tx_sensors reading arrived");

        client.close();
        server.stop();
    }

    void theTwoDirectionsSubscribeIndependently() {
        // A client that asked for TX readings must not be sent RX levels it never wanted.
        TciServer server;
        QVERIFY(server.start(0));

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("tx_sensors_enable:true,50;");

        const QStringList seen = collectFor(client, kSettleMs);
        QVERIFY(countStartingWith(seen, QStringLiteral("tx_sensors:")) > 0);
        QCOMPARE(countStartingWith(seen, QStringLiteral("rx_sensors:")), 0);
        QCOMPARE(countStartingWith(seen, QStringLiteral("rx_channel_sensors:")), 0);

        client.close();
        server.stop();
    }

    void stopsWhenTheClientDisables() {
        TciServer server;
        QVERIFY(server.start(0));

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("rx_sensors_enable:true,50;");
        QVERIFY(countStartingWith(collectFor(client, 400), QStringLiteral("rx_sensors:")) > 0);

        client.send("rx_sensors_enable:false;");
        collectFor(client, 150); // let the echo and any in-flight reading drain
        const QStringList after = collectFor(client, 400);
        QCOMPARE(countStartingWith(after, QStringLiteral("rx_sensors:")), 0);

        client.close();
        server.stop();
    }

    void sendsOnlyToClientsThatSubscribed() {
        TciServer server;
        QVERIFY(server.start(0));

        TciTestClient subscriber;
        TciTestClient bystander;
        QVERIFY(subscriber.connectTo(server.port()));
        subscriber.collectUntil("ready;");
        QVERIFY(bystander.connectTo(server.port()));
        bystander.collectUntil("ready;");

        subscriber.send("rx_sensors_enable:true,50;");

        const QStringList got = collectFor(subscriber, 400);
        const QStringList none = collectFor(bystander, 400);
        QVERIFY(countStartingWith(got, QStringLiteral("rx_sensors:")) > 0);
        QCOMPARE(countStartingWith(none, QStringLiteral("rx_sensors:")), 0);

        subscriber.close();
        bystander.close();
        server.stop();
    }

    void aMalformedEnableIsNotADisable() {
        // Coercing a non-boolean to false is how a client silently loses its meters.
        TciServer server;
        QVERIFY(server.start(0));

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("rx_sensors_enable:true,50;");
        QVERIFY(countStartingWith(collectFor(client, 400), QStringLiteral("rx_sensors:")) > 0);

        client.send("rx_sensors_enable:yes;");
        const QStringList after = collectFor(client, 400);
        QVERIFY2(countStartingWith(after, QStringLiteral("rx_sensors:")) > 0,
                 "a malformed enable switched the meters off");

        client.close();
        server.stop();
    }

    void aVanishingSubscriberStopsTheReporting() {
        // Fail closed: the timer must not keep running for a client that is gone.
        TciServer server;
        QVERIFY(server.start(0));
        {
            TciTestClient client;
            QVERIFY(client.connectTo(server.port()));
            client.collectUntil("ready;");
            client.send("rx_sensors_enable:true,50;");
            QVERIFY(countStartingWith(collectFor(client, 400), QStringLiteral("rx_sensors:")) > 0);
            client.close();
        }
        // Pump the loop so the disconnect is processed, then confirm a fresh client is silent.
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 300) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }

        TciTestClient fresh;
        QVERIFY(fresh.connectTo(server.port()));
        fresh.collectUntil("ready;");
        const QStringList seen = collectFor(fresh, 400);
        QCOMPARE(countStartingWith(seen, QStringLiteral("rx_sensors:")), 0);

        fresh.close();
        server.stop();
    }

    void clampsTheIntervalToTheSpecRange() {
        // The spec gives 30..1000 ms. A client asking for 1 ms wants "as fast as you can", not a
        // refusal - and must not be able to turn the server into a packet generator.
        TciServer server;
        QVERIFY(server.start(0));

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("rx_sensors_enable:true,1;");

        const QStringList seen = collectFor(client, 500);
        const int readings = countStartingWith(seen, QStringLiteral("rx_sensors:"));
        QVERIFY2(readings > 0, "nothing arrived at all");
        // At the 30 ms floor, 500 ms can hold about 16. A 1 ms interval would be ~500.
        QVERIFY2(readings < 60,
                 qPrintable(QStringLiteral("interval not clamped: %1 readings in 500 ms").arg(readings)));

        client.close();
        server.stop();
    }
};

QTEST_MAIN(TestTciSensors)
#include "test_tcisensors.moc"
