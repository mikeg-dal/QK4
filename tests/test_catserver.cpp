#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTest>
#include "models/radiostate.h"
#include "network/catframes.h"
#include "network/catserver.h"

class TestCatServer : public QObject {
    Q_OBJECT

private:
    // Upper bound on how long the server may take to answer. It is a bound, not a delay: a correct
    // server never reaches it, so raising it costs nothing and lowering it buys nothing.
    static constexpr int kReplyTimeoutMs = 2000;

    // How often to come back and look. Small enough that a reply is noticed as soon as it lands,
    // which is what makes the suite faster than the fixed wait it replaced, not just steadier.
    static constexpr int kPollStepMs = 5;

    // Spin the event loop until `ready()` reports true, or the budget runs out. Returns whether it
    // became true.
    //
    // WHY everything waits through here: the server under test lives on this same thread and only
    // runs when the event loop does, so every wait in this file has the same shape. It had been
    // written out twice with different step sizes and different outcomes on timeout, which is how
    // one copy came to be race-prone while the other was not.
    template <typename Predicate> bool spinUntil(Predicate ready, int timeoutMs) {
        QElapsedTimer timer;
        timer.start();
        while (!ready()) {
            if (timer.elapsed() >= timeoutMs)
                return false;
            QTest::qWait(kPollStepMs);
        }
        return true;
    }

    // Helper: connect to CatServer, send command, return the complete response.
    //
    // WHY this waits for the terminator instead of a fixed interval: it used to write the command,
    // QTest::qWait(50), and read whatever had arrived. Fifty milliseconds is not a protocol
    // requirement - it was a guess at loopback latency - so on a loaded machine the reply landed
    // after the read and the test compared against an empty string while the server had done
    // everything right. Its own log said so: "TX -> 2072 FA00014074000;" on the line above the
    // failure. The case that failed moved between runs (testFrequencyA, testFrequencyB,
    // testPcxXvtrRangeCanBeEmitted), which is the signature of a race in the shared helper rather
    // than a fault in any one command. Every CAT reply ends in ';', so waiting for that makes the
    // result depend on what the server sent rather than on how busy the machine was.
    QString sendCommand(CatServer &server, const QString &cmd) {
        QTcpSocket client;
        client.connectToHost("127.0.0.1", server.port());
        if (!client.waitForConnected(1000))
            return QString();

        // Process pending events so server sees the connection
        QCoreApplication::processEvents();

        client.write(cmd.toUtf8());
        client.flush();

        // Accumulating inside the predicate is deliberate: readAll() drains the socket, so the
        // bytes have to be kept as they arrive or a reply split across segments would be lost
        // between polls.
        QByteArray received;
        spinUntil(
            [&] {
                received += client.readAll();
                return received.endsWith(';');
            },
            kReplyTimeoutMs);

        client.disconnectFromHost();
        // On timeout this returns whatever did arrive rather than a bare QString(), so a genuine
        // failure reports the partial reply instead of looking identical to the race it replaced.
        return QString::fromUtf8(received);
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

    // Used where the expected traffic is a broadcast rather than an answer to a command, so the
    // caller knows a byte count but not a terminator.
    QByteArray waitForBytes(QTcpSocket *sock, int minBytes, int timeoutMs = 500) {
        spinUntil([&] { return sock->bytesAvailable() >= minBytes; }, timeoutMs);
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

    void antennaConfigIsTerminated() {
        // ACM / ACS / ACT from the ANT CFG popups were sent without their ';'. Nothing below
        // sendCAT() adds one, so the K4 could hold the edit or run it into the next command.
        QCOMPARE(CatFrames::setAntennaConfig("ACM", false, {false, true, true, true, true, true, true}, 7),
                 QByteArray("ACM00111111;"));
        QCOMPARE(CatFrames::setAntennaConfig("ACS", true, {true, false, false, false, false, false, true}, 7),
                 QByteArray("ACS11000001;"));
        QCOMPARE(CatFrames::setAntennaConfig("ACT", false, {true, true, true}, 3), QByteArray("ACT0111;"));
        // A short mask pads with disabled, matching what the popups sent before.
        QCOMPARE(CatFrames::setAntennaConfig("ACT", true, {true}, 3), QByteArray("ACT1100;"));
    }

    void setRfPowerUsesThePcFormNotPcx() {
        // REGRESSION, found on a live K4. PCX is the extended QUERY; the radio ignores it as a
        // set, so drive silently did nothing. The set form is PCnnnr, which is what QK4's own UI
        // sends and what the radio echoes back (PC045H).
        QCOMPARE(CatFrames::setRfPower(45, LevelsState::PowerRange::Qro), QByteArray("PC045H;"));
        QCOMPARE(CatFrames::setRfPower(100, LevelsState::PowerRange::Qro), QByteArray("PC100H;"));
        // QRP is reported in tenths of a watt, so a request in watts is scaled: 5 W is PC050L.
        QCOMPARE(CatFrames::setRfPower(5, LevelsState::PowerRange::Qrp), QByteArray("PC050L;"));
        QCOMPARE(CatFrames::setRfPower(10, LevelsState::PowerRange::Qrp), QByteArray("PC100L;"));
        // Fractional QRP and the XVTR range (tenths of a mW, letter X) go out as the radio reports them.
        QCOMPARE(CatFrames::setRfPower(2.5, LevelsState::PowerRange::Qrp), QByteArray("PC025L;"));
        QCOMPARE(CatFrames::setRfPower(1.5, LevelsState::PowerRange::Xvtr), QByteArray("PC015X;"));
    }

    void sMeterReplyIsTheInverseOfTheRadioReport() {
        // SM0028 is S9+30 dB. Above S9 each bar is 3 dB, so the reply must read back unchanged -
        // it used to answer SM0021 (about S9+9).
        for (int bars = 0; bars <= 40; ++bars) {
            RadioState rs;
            const QByteArray wire = QStringLiteral("SM%1;").arg(bars, 4, 10, QChar('0')).toUtf8();
            rs.parseCATCommand(QString::fromLatin1(wire));
            QCOMPARE(CatFrames::sMeterMain(rs.sMeter()), wire);
        }
        QCOMPARE(CatFrames::sMeterMain(12.0), QByteArray("SM0028;"));
    }

    void signalLevelReplyIsTheInverseOfTheRadioReport() {
        for (int dbm = -150; dbm <= 20; ++dbm) {
            for (const bool sub : {false, true}) {
                RadioState rs;
                const QByteArray wire = CatFrames::signalLevelDbm(dbm, sub);
                rs.parseCATCommand(QString::fromLatin1(wire));
                QCOMPARE(sub ? rs.sMeterDbmB() : rs.sMeterDbm(), std::optional<int>(dbm));
            }
        }
        QCOMPARE(CatFrames::signalLevelDbm(-110, false), QByteArray("SMH-110;"));
        QCOMPARE(CatFrames::signalLevelDbm(-73, false), QByteArray("SMH-073;"));
        QCOMPARE(CatFrames::signalLevelDbm(5, true), QByteArray("SMH$+005;"));
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
        QCOMPARE(CatFrames::setRfPower(999, LevelsState::PowerRange::Qro), QByteArray("PC110H;"));
        QCOMPARE(CatFrames::setRfPower(99, LevelsState::PowerRange::Qrp), QByteArray("PC100L;"));
        // The K4's own limits (rev. D5): SQ "000-040", RG "00 to 60".
        QCOMPARE(CatFrames::squelch(41), QByteArray("SQ040;"));
        QCOMPARE(CatFrames::rfGain(99), QByteArray("RG-60;"));
    }

    // =========================================================================
    // GET command responses (answered from RadioState cache)
    // =========================================================================

    void startedReportsTheBoundPort() {
        // Port 0 asks the OS for one; listeners need the port actually bound, not the request.
        RadioState rs;
        CatServer server(&rs);
        QSignalSpy started(&server, &CatServer::started);
        QVERIFY(server.start(0));
        QCOMPARE(started.count(), 1);
        QVERIFY(server.port() != 0);
        QCOMPARE(started.at(0).at(0).value<quint16>(), server.port());
    }

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

        QCOMPARE(sendCommand(server, "NB;"), QString("NB0000;")); // level 00, off, filter 0
    }

    void testNoiseReductionOff() {
        RadioState rs;
        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "NR;"), QString("NR000;")); // level 00, off
    }

    void testVoxOff() {
        RadioState rs;
        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "VX;"), QString("VXV0;")); // no mode yet: the voice group
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

    // A locked VFO's frequency is not changed by a client: LK guards VFO A, LK$ guards VFO B, each on
    // its own. Nothing is forwarded for the locked one; the other still is.
    void testFrequencySetRefusedForLockedVfo() {
        RadioState rs;
        rs.parseCATCommand("LK1;"); // VFO A locked, B free
        CatServer server(&rs);
        QVERIFY(server.start(0));
        QSignalSpy spy(&server, &CatServer::catCommandReceived);

        QTcpSocket client;
        client.connectToHost("127.0.0.1", server.port());
        QVERIFY(client.waitForConnected(1000));
        client.write("FA00014074000;UP;AB1;FB00007100000;UPB;AB0;");
        client.flush();
        QTest::qWait(100);

        QStringList forwarded;
        for (const auto &args : spy)
            forwarded << args.at(0).toString();
        QCOMPARE(forwarded, QStringList({"FB00007100000;", "UPB;", "AB0;"}));

        rs.parseCATCommand("LK0;");
        rs.parseCATCommand("LK$1;"); // now only VFO B locked
        spy.clear();
        client.write("FA00014074000;FB00007100000;AB2;");
        client.flush();
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

    // Regression: a CAT client that keyed and then vanished used to leave PTT asserted. Nothing
    // released it — only an explicit RX; did — so a WSJT-X crash mid-transmission kept the K4
    // transmitting until the operator pressed Escape.
    void testPttReleasedWhenKeyingClientDisconnects() {
        RadioState rs;
        CatServer server(&rs);
        QVERIFY(server.start(0));

        QSignalSpy spy(&server, &CatServer::pttRequested);

        QTcpSocket *client = connectAndKeepOpen(server.port());
        QVERIFY(client);

        client->write("TX;");
        client->flush();
        QTest::qWait(100);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toBool(), true);

        // Drop the connection without sending RX;.
        client->disconnectFromHost();
        QTest::qWait(200);

        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(0).toBool(), false);
        client->deleteLater();
    }

    // A client that never keyed must not release someone else's transmission on its way out.
    void testDisconnectOfNonKeyingClientDoesNotRelease() {
        RadioState rs;
        CatServer server(&rs);
        QVERIFY(server.start(0));

        QTcpSocket *keyer = connectAndKeepOpen(server.port());
        QTcpSocket *bystander = connectAndKeepOpen(server.port());
        QVERIFY(keyer);
        QVERIFY(bystander);

        keyer->write("TX;");
        keyer->flush();
        QTest::qWait(100);

        QSignalSpy spy(&server, &CatServer::pttRequested);
        bystander->disconnectFromHost();
        QTest::qWait(200);

        QCOMPARE(spy.count(), 0);

        keyer->disconnectFromHost();
        QTest::qWait(200);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toBool(), false);
        keyer->deleteLater();
        bystander->deleteLater();
    }

    // Disabling the CAT server while a client is transmitting must unkey too.
    void testStoppingTheServerReleasesPtt() {
        RadioState rs;
        CatServer server(&rs);
        QVERIFY(server.start(0));

        QTcpSocket *client = connectAndKeepOpen(server.port());
        QVERIFY(client);
        client->write("TX;");
        client->flush();
        QTest::qWait(100);

        QSignalSpy spy(&server, &CatServer::pttRequested);
        server.stop();
        QTest::qWait(100);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toBool(), false);
        client->deleteLater();
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
        rs.setRfPower(100.0, LevelsState::PowerRange::Qro);

        CatServer server(&rs);
        QVERIFY(server.start(0));

        QString response = sendCommand(server, "PCX;");
        QCOMPARE(response, QString("PCX100H;"));
    }

    void testPcxQrpMode() {
        // CAT-005. This asserted PCX005L, which a client reads as 0.5 W - the reply was not
        // scaling QRP into the tenths the K4 reports it in, so every QRP power query was answered
        // ten times low. The SET path (setRfPowerUsesThePcFormNotPcx, above) has always scaled
        // correctly, so the two halves of this same file disagreed about what 5 W looks like on
        // the wire and both tests passed.
        RadioState rs;
        rs.parseCATCommand("PC050L;"); // the radio's own form: 5.0 W in the QRP range

        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "PCX;"), QString("PCX050L;"));
        QCOMPARE(sendCommand(server, "PC;"), QString("PC050L;"));
    }

    void testPcxXvtrRangeCanBeEmitted() {
        // The XVTR range could never appear: rfPowerExtended took a bool, so every non-QRP value
        // was labelled H. Milliwatts are reported in tenths like QRP.
        RadioState rs;
        rs.parseCATCommand("PC050X;"); // 5.0 mW in the XVTR range

        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "PCX;"), QString("PCX050X;"));
    }

    void testPcxQroIsWholeWatts() {
        // QRO is NOT scaled - whole watts - so the fix must not push it ten times high.
        RadioState rs;
        rs.parseCATCommand("PC100H;");

        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "PCX;"), QString("PCX100H;"));
        QCOMPARE(sendCommand(server, "PC;"), QString("PC100H;"));
    }

    // #134. Each pair was captured from a K4D at R40: what the radio reported, then its own answer
    // to a bare `PC;`. Without the range letter PC006 reads as 6 W and PC100 as 100 W.
    void testPcReplyMatchesTheRadio_data() {
        QTest::addColumn<QString>("fromRadio");
        QTest::addColumn<QString>("expected");
        QTest::newRow("15 W, QRO") << "PC015H;" << "PC015H;";
        QTest::newRow("10.0 W, QRP") << "PC100L;" << "PC100L;";
        QTest::newRow("0.6 W, QRP") << "PC006L;" << "PC006L;";
        QTest::newRow("5.0 mW, XVTR") << "PC050X;" << "PC050X;";
    }

    void testPcReplyMatchesTheRadio() {
        QFETCH(QString, fromRadio);
        QFETCH(QString, expected);
        RadioState rs;
        rs.parseCATCommand(fromRadio);

        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "PC;"), expected);
    }

    // #134. Captured from a K4D at R40: BW0300 is 3000 Hz. The reply was BW3000;, which a client
    // reads as 30 kHz.
    void testBwReplyIsInTenHertzUnits_data() {
        QTest::addColumn<QString>("fromRadio");
        QTest::newRow("3000 Hz") << "BW0300;";
        QTest::newRow("3420 Hz") << "BW0342;";
        QTest::newRow("200 Hz") << "BW0020;";
    }

    void testBwReplyIsInTenHertzUnits() {
        QFETCH(QString, fromRadio);
        RadioState rs;
        rs.parseCATCommand(fromRadio);

        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "BW;"), fromRadio);
    }

    // #134. Captured from a K4D at R40: NB0301 is level 03, off, filter 1. The reply was NB0;.
    void testNbReplyIsTheRadiosForm_data() {
        QTest::addColumn<QString>("fromRadio");
        QTest::newRow("level 3, off, filter 1") << "NB0301;";
        QTest::newRow("level 15, on, filter 2") << "NB1512;";
    }

    void testNbReplyIsTheRadiosForm() {
        QFETCH(QString, fromRadio);
        RadioState rs;
        rs.parseCATCommand(fromRadio);

        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "NB;"), fromRadio);
    }

    // #134. Captured from a K4D at R40: NR050 is level 05, off. The reply was NR0;.
    void testNrReplyIsTheRadiosForm_data() {
        QTest::addColumn<QString>("fromRadio");
        QTest::newRow("level 5, off") << "NR050;";
        QTest::newRow("level 10, on") << "NR101;";
    }

    void testNrReplyIsTheRadiosForm() {
        QFETCH(QString, fromRadio);
        RadioState rs;
        rs.parseCATCommand(fromRadio);

        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "NR;"), fromRadio);
    }

    // #134. The K4D at R40 reported VXC1, VXV0, VXD0 in its state dump and was in DATA (MD6); it
    // answered VX; with VXD0. QK4 answered VX1 - VOX on - because it reported on if ANY mode had it.
    void testVxReplyIsForTheCurrentMode_data() {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<QString>("expected");
        QTest::newRow("DATA") << "MD6;" << "VXD0;";
        QTest::newRow("DATA-R") << "MD9;" << "VXD0;";
        QTest::newRow("CW") << "MD3;" << "VXC1;";
        QTest::newRow("CW-R") << "MD7;" << "VXC1;";
        QTest::newRow("USB") << "MD2;" << "VXV0;";
        QTest::newRow("AM") << "MD5;" << "VXV0;";
    }

    void testVxReplyIsForTheCurrentMode() {
        QFETCH(QString, mode);
        QFETCH(QString, expected);
        RadioState rs;
        rs.parseCATCommand("VXC1;");
        rs.parseCATCommand("VXV0;");
        rs.parseCATCommand("VXD0;");
        rs.parseCATCommand(mode);

        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "VX;"), expected);
    }

    // #134. The K4D at R40 answered GT2; QK4 answered GT002, the K3's three-digit form. The same
    // builder sends TCI's agc_mode to the radio, so GT002 went to the K4 as well.
    void testGtIsOneDigit_data() {
        QTest::addColumn<QString>("fromRadio");
        QTest::newRow("off") << "GT0;";
        QTest::newRow("slow") << "GT1;";
        QTest::newRow("fast") << "GT2;";
    }

    void testGtIsOneDigit() {
        QFETCH(QString, fromRadio);
        RadioState rs;
        rs.parseCATCommand(fromRadio);

        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "GT;"), fromRadio);
        QCOMPARE(CatFrames::agcSpeed(fromRadio.mid(2, 1).toInt()), fromRadio.toUtf8());
    }

    // #134. SQ was answered SQ000 whatever the radio said, and SQ$ not at all. Captured from a
    // K4D at R40 after the squelch was turned up: SQ010, SQ$005.
    void testSqAnswersTheRadiosSquelch() {
        RadioState rs;
        rs.parseCATCommand("SQ010;");
        rs.parseCATCommand("SQ$005;");

        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "SQ;"), QString("SQ010;"));
        QCOMPARE(sendCommand(server, "SQ$;"), QString("SQ$005;"));
    }

    void testSqIsSilentUntilTheRadioReportsIt() {
        RadioState rs;
        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "SQ;"), QString());
        QCOMPARE(sendCommand(server, "SQ$;"), QString());
    }

    // SMH / SMH$ answer the radio's own dBm reading in its own form (SMH-110, SMH$-122 from a K4D,
    // 2026-10-30), and nothing before the radio has reported one.
    void testSmhAnswersTheRadiosSignalLevel() {
        RadioState rs;
        CatServer server(&rs);
        QVERIFY(server.start(0));
        QCOMPARE(sendCommand(server, "SMH;"), QString());
        QCOMPARE(sendCommand(server, "SMH$;"), QString());

        rs.parseCATCommand("SMH-110;");
        rs.parseCATCommand("SMH$-122;");
        QCOMPARE(sendCommand(server, "SMH;"), QString("SMH-110;"));
        QCOMPARE(sendCommand(server, "SMH$;"), QString("SMH$-122;"));
        // SM stays the bar reading, not a dBm figure.
        rs.parseCATCommand("SM04;");
        QCOMPARE(sendCommand(server, "SM;"), QString("SM0004;"));
    }

    // #134. RG and RG$ went unanswered although QK4 stores both. Captured from a K4D at R40: RG-00.
    void testRgAnswersTheRadiosRfGain() {
        RadioState rs;
        rs.parseCATCommand("RG-00;");
        rs.parseCATCommand("RG$-12;");

        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "RG;"), QString("RG-00;"));
        QCOMPARE(sendCommand(server, "RG$;"), QString("RG$-12;"));
    }

    // #134. Only MD$ was answered; every other Sub RX query reached the radio and its reply went to
    // QK4, not the client. Each row is the radio's own reply, captured from a K4D at R40.
    void testSubRxQueriesAnswerTheRadiosForm_data() {
        QTest::addColumn<QString>("fromRadio");
        QTest::addColumn<QString>("query");
        QTest::newRow("RO$") << "RO$+0000;" << "RO$;";
        QTest::newRow("RT$") << "RT$0;" << "RT$;";
        QTest::newRow("GT$") << "GT$2;" << "GT$;";
        QTest::newRow("NB$") << "NB$0301;" << "NB$;";
        QTest::newRow("NR$") << "NR$050;" << "NR$;";
        QTest::newRow("BW$") << "BW$0020;" << "BW$;";
        QTest::newRow("DT$") << "DT$0;" << "DT$;";
    }

    void testSubRxQueriesAnswerTheRadiosForm() {
        QFETCH(QString, fromRadio);
        QFETCH(QString, query);
        RadioState rs;
        rs.parseCATCommand(fromRadio);

        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, query), fromRadio);
    }

    // Before the radio reports them, BW and DT have nothing true to say. DT answered DT-1; - not even
    // well-formed.
    void testBwAndDtAreSilentUntilTheRadioReportsThem() {
        RadioState rs;
        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "DT;"), QString());
        QCOMPARE(sendCommand(server, "BW$;"), QString());
    }

    // =========================================================================
    // AG / AG$ - answered by asking the radio (#134)
    //
    // The radio does not report AF gain when the knob moves (a K4D at R40 sent no AG traffic while
    // it was turned), so only the radio knows the current value. Each test plays the radio's side
    // by calling onRadioResponse().
    // =========================================================================

private:
    // A client that stays connected, since the answer arrives after the radio replies.
    struct Client {
        QTcpSocket socket;
        QByteArray received;
    };

    void connectClient(CatServer &server, Client &c) {
        c.socket.connectToHost("127.0.0.1", server.port());
        QVERIFY(c.socket.waitForConnected(1000));
        QCoreApplication::processEvents();
    }

    void send(Client &c, const char *cmd) {
        c.socket.write(cmd);
        c.socket.flush();
    }

    bool waitForReply(Client &c, const QByteArray &expected) {
        return spinUntil(
            [&] {
                c.received += c.socket.readAll();
                return c.received.contains(expected);
            },
            kReplyTimeoutMs);
    }

private slots:
    void testAgIsAskedOfTheRadioAndItsAnswerReturned() {
        RadioState rs;
        CatServer server(&rs);
        QVERIFY(server.start(0));
        QSignalSpy asked(&server, &CatServer::radioQueryRequested);

        Client c;
        connectClient(server, c);
        send(c, "AG;");
        QVERIFY(spinUntil([&] { return asked.count() == 1; }, kReplyTimeoutMs));
        QCOMPARE(asked.at(0).at(0).toString(), QString("AG;"));

        server.onRadioResponse("AG$005;"); // Sub's answer starts with "AG" too and must not satisfy it
        server.onRadioResponse("SM04;AG018;FA00014049500;"); // batched, as the radio sends them
        QVERIFY(waitForReply(c, "AG018;"));
        QCOMPARE(c.received, QByteArray("AG018;"));
    }

    void testSubAgIsKeptApartFromMain() {
        RadioState rs;
        CatServer server(&rs);
        QVERIFY(server.start(0));
        QSignalSpy asked(&server, &CatServer::radioQueryRequested);

        Client c;
        connectClient(server, c);
        send(c, "AG$;");
        QVERIFY(spinUntil([&] { return asked.count() == 1; }, kReplyTimeoutMs));
        QCOMPARE(asked.at(0).at(0).toString(), QString("AG$;"));

        server.onRadioResponse("AG000;"); // Main's answer must not satisfy a Sub query
        server.onRadioResponse("AG$018;");
        QVERIFY(waitForReply(c, "AG$018;"));
        QCOMPARE(c.received, QByteArray("AG$018;"));
    }

    void testTwoWaitersShareOneQuestionToTheRadio() {
        RadioState rs;
        CatServer server(&rs);
        QVERIFY(server.start(0));
        QSignalSpy asked(&server, &CatServer::radioQueryRequested);

        Client a, b;
        connectClient(server, a);
        connectClient(server, b);
        send(a, "AG;");
        QVERIFY(spinUntil([&] { return asked.count() == 1; }, kReplyTimeoutMs));
        send(b, "AG;");
        QTest::qWait(100);
        QCOMPARE(asked.count(), 1);

        server.onRadioResponse("AG018;");
        QVERIFY(waitForReply(a, "AG018;"));
        QVERIFY(waitForReply(b, "AG018;"));
    }

    // No answer from the radio - disconnected, or off - leaves the client with no reply, as a real
    // radio that is off would. A reply arriving after the timeout goes nowhere.
    void testUnansweredQueryIsDroppedAfterTheTimeout() {
        RadioState rs;
        CatServer server(&rs);
        QVERIFY(server.start(0));
        QSignalSpy asked(&server, &CatServer::radioQueryRequested);

        Client c;
        connectClient(server, c);
        send(c, "AG;");
        QVERIFY(spinUntil([&] { return asked.count() == 1; }, kReplyTimeoutMs));
        QTest::qWait(CatServer::kRadioQueryTimeoutMs + 200);

        server.onRadioResponse("AG018;");
        QTest::qWait(100);
        c.received += c.socket.readAll();
        QCOMPARE(c.received, QByteArray());

        send(c, "AG;"); // and the next query asks the radio again
        QVERIFY(spinUntil([&] { return asked.count() == 2; }, kReplyTimeoutMs));
    }

    void testWaiterThatDisconnectsIsSkipped() {
        RadioState rs;
        CatServer server(&rs);
        QVERIFY(server.start(0));
        QSignalSpy asked(&server, &CatServer::radioQueryRequested);

        auto *gone = new Client;
        connectClient(server, *gone);
        Client stays;
        connectClient(server, stays);
        send(*gone, "AG;");
        QVERIFY(spinUntil([&] { return asked.count() == 1; }, kReplyTimeoutMs));
        send(stays, "AG;");
        QTest::qWait(50);

        gone->socket.disconnectFromHost();
        delete gone;
        QVERIFY(spinUntil([&] { return server.clientCount() == 1; }, kReplyTimeoutMs));

        server.onRadioResponse("AG018;");
        QVERIFY(waitForReply(stays, "AG018;"));
    }
};

QTEST_MAIN(TestCatServer)
#include "test_catserver.moc"
