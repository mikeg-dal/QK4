#include <QSignalSpy>
#include <QTcpSocket>
#include <QTest>
#include "models/radiostate.h"
#include "network/catframes.h"
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

    QTcpSocket *connectAndKeepOpen(quint16 port) {
        auto *sock = new QTcpSocket(this);
        sock->connectToHost("127.0.0.1", port);
        if (!sock->waitForConnected(1000)) {
            delete sock;
            return nullptr;
        }
        QCoreApplication::processEvents();
        return sock;
    }

    QByteArray waitForBytes(QTcpSocket *sock, int minBytes, int timeoutMs = 500) {
        int elapsed = 0;
        while (sock->bytesAvailable() < minBytes && elapsed < timeoutMs) {
            QTest::qWait(20);
            elapsed += 20;
        }
        return sock->readAll();
    }

private slots:
    void setFilterBandwidthUsesTenHertzUnits() {
        // REGRESSION, found on a live K4. The radio takes BW in 10-Hz units - BW0280 is 2800 Hz,
        // which is what RadioState::handleBW parses - so sending the width in Hz asks for ten
        // times the filter and the radio ignores it as out of range.
        QCOMPARE(CatFrames::setFilterBandwidth(2800), QByteArray("BW0280;"));
        QCOMPARE(CatFrames::setFilterBandwidth(5000), QByteArray("BW0500;"));
        QCOMPARE(CatFrames::setFilterBandwidth(400), QByteArray("BW0040;"));
    }

    void setNoiseBlankerUsesTheLevelAndFlagForm() {
        // REGRESSION, found on a live K4. The K4 wants NBnnm - nn the level 00-15, m on/off - and
        // a bare "NB1;" is rejected by the radio and by RadioState's own parser.
        QCOMPARE(CatFrames::setNoiseBlanker(0, true), QByteArray("NB001;"));
        QCOMPARE(CatFrames::setNoiseBlanker(0, false), QByteArray("NB000;"));
        // The level is carried through, because TCI's RX_NB_ENABLE is on/off only and must not
        // move the level as a side effect.
        QCOMPARE(CatFrames::setNoiseBlanker(7, true), QByteArray("NB071;"));
        QCOMPARE(CatFrames::setNoiseBlanker(15, true), QByteArray("NB151;"));
    }

    void setRfPowerUsesThePcFormNotPcx() {
        // REGRESSION, found on a live K4. PCX is the extended QUERY; the radio ignores it as a
        // set, so drive silently did nothing. The set form is PCnnnr, which is what QK4's own UI
        // sends and what the radio echoes back (PC045H).
        QCOMPARE(CatFrames::setRfPower(45, false), QByteArray("PC045H;"));
        QCOMPARE(CatFrames::setRfPower(100, false), QByteArray("PC100H;"));
        // QRP is reported in tenths of a watt, so a request in watts is scaled: 5 W is PC050L.
        QCOMPARE(CatFrames::setRfPower(5, true), QByteArray("PC050L;"));
        QCOMPARE(CatFrames::setRfPower(10, true), QByteArray("PC100L;"));
    }

    void setMenuValueUsesTheAbsoluteMeForm() {
        // The K4's tune power is menu item 69, set with MEnnnn.vvvv - ME0069.0020 is 20 W.
        // MenuController already sends the RELATIVE forms (ME0069.+ and .-); this is absolute.
        QCOMPARE(CatFrames::setMenuValue(69, 20), QByteArray("ME0069.0020;"));
        QCOMPARE(CatFrames::setMenuValue(69, 5), QByteArray("ME0069.0005;"));
        QCOMPARE(CatFrames::setMenuValue(7, 200), QByteArray("ME0007.0200;"));
    }

    void setBuildersClampRatherThanEmitNonsense() {
        // A malformed frame reaches the radio; refusing to overflow the field is cheaper than
        // finding out what an out-of-range one does.
        QCOMPARE(CatFrames::setNoiseBlanker(99, true), QByteArray("NB151;"));
        QCOMPARE(CatFrames::setNoiseBlanker(-3, true), QByteArray("NB001;"));
        QCOMPARE(CatFrames::setFilterBandwidth(-100), QByteArray("BW0000;"));
        QCOMPARE(CatFrames::setRfPower(999, false), QByteArray("PC110H;"));
        QCOMPARE(CatFrames::setRfPower(99, true), QByteArray("PC100L;"));
    }

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

        QCOMPARE(sendCommand(server, "AI;"), QString("AI0;"));
    }

    void testAiSetUpdatesPerClient() {
        RadioState rs;
        CatServer server(&rs);
        QVERIFY(server.start(0));

        QTcpSocket *sock = connectAndKeepOpen(server.port());
        QVERIFY(sock != nullptr);

        sock->write("AI2;");
        sock->flush();
        QTest::qWait(50);

        sock->write("AI;");
        sock->flush();
        QByteArray response = waitForBytes(sock, 4);
        QCOMPARE(response, QByteArray("AI2;"));

        sock->disconnectFromHost();
        sock->deleteLater();
    }

    void testTwoClientsDifferentAiModes() {
        RadioState rs;
        rs.parseCATCommand("FA00007000000;"); // baseline so FA push has a delta to compare

        CatServer server(&rs);
        QVERIFY(server.start(0));

        QTcpSocket *sockA = connectAndKeepOpen(server.port());
        QVERIFY(sockA != nullptr);
        QTcpSocket *sockB = connectAndKeepOpen(server.port());
        QVERIFY(sockB != nullptr);

        sockA->write("AI0;");
        sockA->flush();
        sockB->write("AI2;");
        sockB->flush();
        QTest::qWait(50);

        // Drain any stray bytes from the AI SET round-trip (there shouldn't be any).
        sockA->readAll();
        sockB->readAll();

        rs.parseCATCommand("FA00014074000;");

        QByteArray bResp = waitForBytes(sockB, 14);
        QCOMPARE(bResp, QByteArray("FA00014074000;"));

        QTest::qWait(50);
        QCOMPARE(sockA->bytesAvailable(), qint64(0));

        sockA->disconnectFromHost();
        sockB->disconnectFromHost();
        sockA->deleteLater();
        sockB->deleteLater();
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
    // IF response — byte-exact K4 spec layout (38 chars)
    // Format: IF[freq:11]     [+/-][offset:4][r][x] 00[t][m]0[s][p][b][d]1 ;
    // =========================================================================

    void testIfResponseCwIdle() {
        // Matches a real K4 capture: 7.031740 MHz, CW, RX idle, no RIT/XIT/split
        RadioState rs;
        rs.parseCATCommand("FA00007031740;");
        rs.parseCATCommand("MD3;");

        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "IF;"), QString("IF00007031740     +000000 0003000001 ;"));
    }

    void testIfResponseLsbIdle() {
        // 14.250000 MHz, LSB, RX idle
        RadioState rs;
        rs.parseCATCommand("FA00014250000;");
        rs.parseCATCommand("MD1;");

        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "IF;"), QString("IF00014250000     +000000 0001000001 ;"));
    }

    void testIfResponseComposite() {
        // 14.074000 MHz, USB, RIT on at +50 Hz, split on, TX on.
        // Locks down the per-bit field positions so a future width regression fails loudly.
        RadioState rs;
        rs.parseCATCommand("FA00014074000;");
        rs.parseCATCommand("MD2;");
        rs.parseCATCommand("RT1;");
        rs.parseCATCommand("RO+0050;");
        rs.parseCATCommand("FT1;");
        rs.parseCATCommand("TX;");

        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "IF;"), QString("IF00014074000     +005010 0012001001 ;"));
    }

    // =========================================================================
    // MD$ — VFO B mode query (N1MM polls this every cycle)
    // =========================================================================

    void testModeBQuery() {
        RadioState rs;
        rs.parseCATCommand("MD$2;"); // VFO B → USB

        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "MD$;"), QString("MD$2;"));
    }

    // =========================================================================
    // DV — Diversity state (N1MM polls this every cycle)
    // =========================================================================

    void testDiversityOff() {
        RadioState rs;
        // Default: diversity off
        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "DV;"), QString("DV0;"));
    }

    void testDiversityOn() {
        RadioState rs;
        rs.parseCATCommand("DV1;");

        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "DV;"), QString("DV1;"));
    }

    // =========================================================================
    // Regression: external SET command → optimistic RadioState update
    // MainWindow wires catCommandReceived → parseCATCommand so external KSxxx;
    // immediately updates QK4's keyerSpeed (and emits keyerSpeedChanged for UI).
    // =========================================================================

    void testExternalKsUpdatesKeyerSpeed() {
        RadioState rs;
        rs.parseCATCommand("KS020;"); // baseline 20 WPM

        CatServer server(&rs);
        QVERIFY(server.start(0));

        // Mirror MainWindow's catCommandReceived handler: optimistic local parse.
        QObject::connect(&server, &CatServer::catCommandReceived, &rs,
                         [&rs](const QString &cmd) { rs.parseCATCommand(cmd); });

        QSignalSpy spy(&rs, &RadioState::keyerSpeedChanged);

        QTcpSocket client;
        client.connectToHost("127.0.0.1", server.port());
        QVERIFY(client.waitForConnected(1000));
        QCoreApplication::processEvents();

        client.write("KS025;");
        client.flush();
        QTest::qWait(100);

        QCOMPARE(rs.keyerSpeed(), 25);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 25);
        client.disconnectFromHost();
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

    void testTxToggleCommand() {
        RadioState rs;
        CatServer server(&rs);
        QVERIFY(server.start(0));

        QSignalSpy spy(&server, &CatServer::pttRequested);

        QTcpSocket client;
        client.connectToHost("127.0.0.1", server.port());
        QVERIFY(client.waitForConnected(1000));

        // Not transmitting → "TX/;" toggles PTT on
        client.write("TX/;");
        client.flush();
        QTest::qWait(100);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toBool(), true);

        // Simulate the radio now reporting transmit
        rs.parseCATCommand("TX;");

        // Transmitting → "TX/;" toggles PTT off
        client.write("TX/;");
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

    // =========================================================================
    // PCX extended power response
    // =========================================================================

    void testPcxHighPower() {
        RadioState rs;
        rs.setRfPower(100.0); // High power mode (>10W)

        CatServer server(&rs);
        QVERIFY(server.start(0));

        QString response = sendCommand(server, "PCX;");
        QCOMPARE(response, QString("PCX100H;"));
    }

    void testPcxQrpMode() {
        RadioState rs;
        rs.setRfPower(5.0); // QRP mode (<=10W)

        CatServer server(&rs);
        QVERIFY(server.start(0));

        QString response = sendCommand(server, "PCX;");
        QCOMPARE(response, QString("PCX005L;"));
    }
};

QTEST_MAIN(TestCatServer)
#include "test_catserver.moc"
