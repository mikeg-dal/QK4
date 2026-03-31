#include <QSignalSpy>
#include <QTcpSocket>
#include <QTest>
#include "models/radiostate.h"
#include "network/catserver.h"

class TestCatServer : public QObject {
    Q_OBJECT

private:
    // Helper: connect to CatServer, send command, return response
    QString sendCommand(CatServer &server, const QString &cmd) {
        QTcpSocket client;
        client.connectToHost("127.0.0.1", server.port());
        if (!client.waitForConnected(1000))
            return QString();

        // Process pending events so server sees the connection
        QCoreApplication::processEvents();

        client.write(cmd.toUtf8());
        client.flush();

        // Process events to let server handle the data and write response
        QTest::qWait(50);

        QString response = QString::fromUtf8(client.readAll());
        client.disconnectFromHost();
        return response;
    }

private slots:
    // =========================================================================
    // GET command responses (answered from RadioState cache)
    // =========================================================================

    void testFrequencyA() {
        RadioState rs;
        rs.parseCATCommand("FA00014074000;");

        CatServer server(&rs);
        QVERIFY(server.start(0)); // port 0 = OS picks a free port

        QString response = sendCommand(server, "FA;");
        QCOMPARE(response, QString("FA00014074000;"));
    }

    void testFrequencyB() {
        RadioState rs;
        rs.parseCATCommand("FB00007074000;");

        CatServer server(&rs);
        QVERIFY(server.start(0));

        QString response = sendCommand(server, "FB;");
        QCOMPARE(response, QString("FB00007074000;"));
    }

    void testModeUSB() {
        RadioState rs;
        rs.parseCATCommand("MD2;");

        CatServer server(&rs);
        QVERIFY(server.start(0));

        QString response = sendCommand(server, "MD;");
        QCOMPARE(response, QString("MD2;"));
    }

    void testModeLSB() {
        RadioState rs;
        rs.parseCATCommand("MD1;");

        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "MD;"), QString("MD1;"));
    }

    void testModeCW() {
        RadioState rs;
        rs.parseCATCommand("MD3;");

        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "MD;"), QString("MD3;"));
    }

    void testPttStateOff() {
        RadioState rs;
        // Default: not transmitting

        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "TQ;"), QString("TQ0;"));
    }

    void testSplitOff() {
        RadioState rs;
        // Default: split off

        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "FT;"), QString("FT0;"));
    }

    void testSplitOn() {
        RadioState rs;
        rs.parseCATCommand("FT1;");

        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "FT;"), QString("FT1;"));
    }

    void testRadioID() {
        RadioState rs;
        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "ID;"), QString("ID017;"));
    }

    void testPowerStatus() {
        RadioState rs;
        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "PS;"), QString("PS1;"));
    }

    void testK2Mode() {
        RadioState rs;
        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "K2;"), QString("K22;"));
    }

    void testK3Mode() {
        RadioState rs;
        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "K3;"), QString("K31;"));
    }

    void testAIQuery() {
        RadioState rs;
        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "AI;"), QString("AI4;"));
    }

    void testRxVfo() {
        RadioState rs;
        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "FR;"), QString("FR0;"));
    }

    void testRitOffset() {
        RadioState rs;
        // Default RIT offset = 0
        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "RO;"), QString("RO+0000;"));
    }

    void testRitOff() {
        RadioState rs;
        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "RT;"), QString("RT0;"));
    }

    void testXitOff() {
        RadioState rs;
        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "XT;"), QString("XT0;"));
    }

    void testNoiseBlankerOff() {
        RadioState rs;
        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "NB;"), QString("NB0;"));
    }

    void testNoiseReductionOff() {
        RadioState rs;
        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "NR;"), QString("NR0;"));
    }

    void testVoxOff() {
        RadioState rs;
        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "VX;"), QString("VX0;"));
    }

    // =========================================================================
    // SET commands — should emit catCommandReceived signal
    // =========================================================================

    void testSetFrequencyForwarded() {
        RadioState rs;
        CatServer server(&rs);
        QVERIFY(server.start(0));

        QSignalSpy spy(&server, &CatServer::catCommandReceived);

        QTcpSocket client;
        client.connectToHost("127.0.0.1", server.port());
        QVERIFY(client.waitForConnected(1000));

        client.write("FA00014074000;");
        client.flush();
        // Give event loop time to process
        QTest::qWait(100);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QString("FA00014074000;"));
        client.disconnectFromHost();
    }

    void testTxCommandEmitsPtt() {
        RadioState rs;
        CatServer server(&rs);
        QVERIFY(server.start(0));

        QSignalSpy spy(&server, &CatServer::pttRequested);

        QTcpSocket client;
        client.connectToHost("127.0.0.1", server.port());
        QVERIFY(client.waitForConnected(1000));

        client.write("TX;");
        client.flush();
        QTest::qWait(100);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toBool(), true);

        client.write("RX;");
        client.flush();
        QTest::qWait(100);

        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(0).toBool(), false);
        client.disconnectFromHost();
    }

    // =========================================================================
    // Server lifecycle
    // =========================================================================

    void testStartStop() {
        RadioState rs;
        CatServer server(&rs);

        QVERIFY(server.start(0));
        QVERIFY(server.isListening());
        QVERIFY(server.port() > 0);

        server.stop();
        QVERIFY(!server.isListening());
    }

    void testMultipleCommands() {
        RadioState rs;
        rs.parseCATCommand("FA00014074000;");
        rs.parseCATCommand("MD2;");

        CatServer server(&rs);
        QVERIFY(server.start(0));

        QTcpSocket client;
        client.connectToHost("127.0.0.1", server.port());
        QVERIFY(client.waitForConnected(1000));
        QCoreApplication::processEvents();

        // Send two queries in one write
        client.write("FA;MD;");
        client.flush();
        QTest::qWait(100);

        QString response = QString::fromUtf8(client.readAll());
        QVERIFY(response.contains("FA00014074000;"));
        QVERIFY(response.contains("MD2;"));
        client.disconnectFromHost();
    }
};

QTEST_MAIN(TestCatServer)
#include "test_catserver.moc"
