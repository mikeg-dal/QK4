#include <QtTest>

#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>

#include "network/kpa1500client.h"

// KPA1500 response parsing, driven through a real loopback socket standing in for the amplifier.
class TestKpa1500Client : public QObject {
    Q_OBJECT

private:
    // Connects `client` to a local fake amp and returns the amp's end of the connection.
    static QTcpSocket *connectToFakeAmp(QTcpServer &amp, KPA1500Client &client) {
        if (!amp.listen(QHostAddress::LocalHost, 0)) {
            return nullptr;
        }
        client.connectToHost(QStringLiteral("127.0.0.1"), amp.serverPort());
        if (!QTest::qWaitFor([&]() { return amp.hasPendingConnections(); }, 5000)) {
            return nullptr;
        }
        QTcpSocket *peer = amp.nextPendingConnection();
        if (!QTest::qWaitFor([&]() { return client.isConnected(); }, 5000)) {
            return nullptr;
        }
        return peer;
    }

private slots:
    void faultFollowsTheFlCodeAndIsReportedOnlyOnChange() {
        // ^FL is the fault code (^FC is the fan minimum speed): 00 is no fault, anything else is.
        QTcpServer amp;
        KPA1500Client client;
        QTcpSocket *peer = connectToFakeAmp(amp, client);
        QVERIFY(peer);
        QSignalSpy faults(&client, &KPA1500Client::faultStatusChanged);

        peer->write("^FL00;^FL20;^FL20;^FC3;");
        peer->flush();
        QTRY_COMPARE(client.faultStatus(), KPA1500Client::FaultActive);
        QTest::qWait(50);
        QCOMPARE(faults.count(), 2); // the first 00 (code now known), then 20 once - not per poll
        QCOMPARE(faults.at(0).at(0).value<KPA1500Client::FaultStatus>(), KPA1500Client::FaultNone);
        QCOMPARE(client.faultCode(), QStringLiteral("20"));

        peer->write("^FL00;^FL00;");
        peer->flush();
        QTRY_COMPARE(client.faultStatus(), KPA1500Client::FaultNone);
        QTest::qWait(50);
        QCOMPARE(faults.count(), 3);
        QCOMPARE(faults.at(2).at(0).value<KPA1500Client::FaultStatus>(), KPA1500Client::FaultNone);

        // Hex, as the reference documents: B0 is the dissipated-power fault.
        peer->write("^FLB0;");
        peer->flush();
        QTRY_COMPARE(faults.count(), 4);
        QCOMPARE(client.faultStatus(), KPA1500Client::FaultActive);

        client.disconnectFromHost();
    }

    void deviceInfoIsAnnouncedWhenSerialOrFirmwareArrive() {
        QTcpServer amp;
        KPA1500Client client;
        QTcpSocket *peer = connectToFakeAmp(amp, client);
        QVERIFY(peer);
        QSignalSpy info(&client, &KPA1500Client::deviceInfoChanged);

        peer->write("^SN01470;^RVM03.07;^SN01470;^RVM03.07;");
        peer->flush();
        QTRY_COMPARE(info.count(), 2);
        QTest::qWait(50);
        QCOMPARE(info.count(), 2);
        QCOMPARE(client.serialNumber(), QStringLiteral("01470"));
        QCOMPARE(client.firmwareVersion(), QStringLiteral("03.07"));

        client.disconnectFromHost();
    }

    void aReadingUnchangedAcrossAReconnectIsReportedAgain() {
        // The mini panel clears its gauges on disconnect. A steady reading (the PA temperature is
        // the usual one) must therefore reach it again after a reconnect, or TMP sits at 0 degrees.
        QTcpServer amp;
        KPA1500Client client;
        QTcpSocket *peer = connectToFakeAmp(amp, client);
        QVERIFY(peer);
        QSignalSpy temperature(&client, &KPA1500Client::paTemperatureChanged);
        QSignalSpy state(&client, &KPA1500Client::operatingStateChanged);

        peer->write("^TM024;^OS0;");
        peer->flush();
        QTRY_COMPARE(temperature.count(), 1);
        QTRY_COMPARE(state.count(), 1);

        client.disconnectFromHost();
        QTRY_VERIFY(!client.isConnected());
        amp.close();

        QTcpServer amp2;
        peer = connectToFakeAmp(amp2, client);
        QVERIFY(peer);
        peer->write("^TM024;^OS0;");
        peer->flush();
        QTRY_COMPARE(temperature.count(), 2);
        QCOMPARE(temperature.at(1).at(0).toDouble(), 24.0);
        QTRY_COMPARE(state.count(), 2);

        client.disconnectFromHost();
    }
};

QTEST_MAIN(TestKpa1500Client)
#include "test_kpa1500client.moc"
