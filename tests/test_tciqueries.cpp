#include <QtTest>

#include <QSet>
#include <QSignalSpy>

#include "network/tciprotocol.h"
#include "network/tciradiostate.h"
#include "network/tciserver.h"
#include "network/websocketframe.h"
#include "tcitestclient.h"

using namespace WebSocketFrame;

// What the TCI server ANSWERS, and whether the answer is legal.
//
// Split out of test_tciserver.cpp, which covers the burst, the CAT sets and the broadcasts. Two
// subjects live here: the read-only query group (phase 8a), and the spec value constraints -
// AGC vocabulary, squelch range, the shared RIT/XIT offset. Those pin the CONSTRAINT rather than
// a literal, because a pinned literal is exactly what let agc_mode ship answering "med".
class TestTciQueries : public QObject {
    Q_OBJECT

private slots:
    void agcModeIsAlwaysOneOfTheThreeSpecValues() {
        // REGRESSION. QK4 answered "med", which TCI does not define: the spec lists exactly
        // normal, fast and off. A client matching the documented vocabulary cannot parse
        // anything else, so this pins the CONSTRAINT rather than one literal value.
        TciRadioSnapshot snapshot;
        for (const QString &mode : {QStringLiteral("normal"), QStringLiteral("fast"), QStringLiteral("off")}) {
            // Both receivers, because the burst now carries an agc_mode for each.
            snapshot.rx[TciRadio::MAIN_RECEIVER].agcMode = mode;
            snapshot.rx[TciRadio::SUB_RECEIVER].agcMode = mode;
            TciServer server;
            server.setSnapshot(snapshot);
            bool seen = false;
            for (const QString &command : server.initBurst()) {
                if (!command.startsWith(QStringLiteral("agc_mode:"))) {
                    continue;
                }
                seen = true;
                const TciProtocol::Command c = TciProtocol::parseOne(command.chopped(1));
                const QString value = c.arg(1);
                QVERIFY2(value == QLatin1String("normal") || value == QLatin1String("fast") ||
                             value == QLatin1String("off"),
                         qPrintable(QStringLiteral("not a spec AGC mode: ") + command));
                QCOMPARE(value, mode);
            }
            QVERIFY(seen);
        }
    }

    void theKeyerSpeedCanBeSetAndNotJustRead() {
        // It used to be read-only: cw_macros_speed:35; was answered with the CURRENT value and the
        // 35 dropped. That left a TCI client no way to change sending speed except the macro speed
        // markers, whose step is fixed at 5 - so a logger whose increment is configurable and not 5
        // could not express it by any route at all.
        TciServer server;
        QSignalSpy spy(&server, &TciServer::setKeyerSpeedRequested);
        const quint16 port = server.start(0, true) ? server.port() : 0;
        QVERIFY(port != 0);

        TciTestClient client;
        QVERIFY(client.connectTo(port));
        client.collectUntil("ready;");

        client.send("cw_macros_speed:35;");
        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 35);

        // Both names drive the one setting - the spec's client-to-server name and the contest
        // logger's name for the same thing.
        client.send("cw_keyer_speed:22;");
        QTRY_COMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(0).toInt(), 22);
    }

    void aKeyerSpeedQueryIsStillAnsweredAndChangesNothing() {
        // A bare name is a GET. Turning the set on must not turn the query into a set of zero.
        TciRadioSnapshot snapshot;
        snapshot.cwKeyerSpeedWpm = 28;
        TciServer server;
        server.setSnapshot(snapshot);
        QSignalSpy spy(&server, &TciServer::setKeyerSpeedRequested);
        const quint16 port = server.start(0, true) ? server.port() : 0;
        QVERIFY(port != 0);

        TciTestClient client;
        QVERIFY(client.connectTo(port));
        client.collectUntil("ready;");

        client.send("cw_keyer_speed;");
        WebSocketDecoder::Message reply;
        QVERIFY(client.next(reply));
        QCOMPARE(QString::fromUtf8(reply.payload), QStringLiteral("cw_keyer_speed:28;"));
        QCOMPARE(spy.count(), 0);
    }

    void aKeyerSpeedSetIsConfirmedWithTheModelNotTheRequest() {
        // The radio has not moved yet, so the confirmation must carry what QK4 HOLDS. Answering
        // with the requested value is the stale-confirmation bug that made WSJT-X transmit out of
        // band in the reference server.
        TciRadioSnapshot snapshot;
        snapshot.cwKeyerSpeedWpm = 20;
        TciServer server;
        server.setSnapshot(snapshot);
        const quint16 port = server.start(0, true) ? server.port() : 0;
        QVERIFY(port != 0);

        TciTestClient client;
        QVERIFY(client.connectTo(port));
        client.collectUntil("ready;");

        client.send("cw_macros_speed:35;");
        WebSocketDecoder::Message reply;
        QVERIFY(client.next(reply));
        QCOMPARE(QString::fromUtf8(reply.payload), QStringLiteral("cw_macros_speed:20;"));
    }

    void theDeviceFieldNamesTheProgramAsAPrefix() {
        // device: is the ONLY field naming the program - protocol: deliberately says ExpertSDR3 so
        // WSJT-X is happy - so a client gating on which server it is talking to has nothing else.
        // The name must stay a PREFIX so a match keeps working when the version moves.
        const QStringList burst = TciServer().initBurst();
        QString device;
        for (const QString &command : burst) {
            if (command.startsWith(QStringLiteral("device:"))) {
                device = command;
            }
        }
        QVERIFY2(!device.isEmpty(), "the burst must name the device");
        QVERIFY2(device.startsWith(QStringLiteral("device:QK4")), qPrintable(device));

        // A version is present, not just the bare name.
        const QString value = device.mid(7).chopped(1);
        QVERIFY2(value.size() > 3, qPrintable(value));

        // And the query answers the SAME string the greeting did.
        QVERIFY2(!value.contains(QLatin1Char(',')), "a comma would split the argument");
    }

    void squelchLevelStaysInsideTheSpecRange() {
        // REGRESSION. QK4 answered 20; TCI defines the squelch threshold as dBm over -140..0, so
        // any positive value is outside the range in either direction of interpretation.
        const QStringList burst = TciServer().initBurst();
        bool seen = false;
        for (const QString &command : burst) {
            if (!command.startsWith(QStringLiteral("sql_level:"))) {
                continue;
            }
            seen = true;
            const TciProtocol::Command c = TciProtocol::parseOne(command.chopped(1));
            bool ok = false;
            const int level = c.arg(1).toInt(&ok);
            QVERIFY2(ok, qPrintable(command));
            QVERIFY2(level >= -140 && level <= 0, qPrintable(QStringLiteral("out of range: ") + command));
        }
        QVERIFY(seen);
    }

    void ritAndXitReportTheSameOffset() {
        // The K4 has ONE offset register (RO) shared by RIT and XIT, with RT and XT as separate
        // enables. Reporting two different offsets would describe a radio that does not exist.
        TciRadioSnapshot snapshot;
        snapshot.rx[TciRadio::MAIN_RECEIVER].ritXitOffsetHz = 95; // a real value read off the radio
        TciServer server;
        server.setSnapshot(snapshot);

        QVERIFY(server.initBurst().contains(QStringLiteral("rit_offset:0,95;")));
        QVERIFY(server.initBurst().contains(QStringLiteral("xit_offset:0,95;")));
    }

    // ---- read-only queries -----------------------------------------------------------------------

    void answersQueriesConsistentlyWithTheInitBurst() {
        // The init burst is a set of CLAIMS about the radio. A client is free to re-read any of them
        // later, and an answer that disagrees with what was advertised is worse than no answer -
        // WSJT-X caches the burst and acts on the difference. So: every reply to a bare GET must be
        // a string the burst already contains, verbatim.
        TciServer server;
        QVERIFY(server.start(0));

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        const QStringList burst = client.collectUntil("ready;");
        QVERIFY(burst.contains(QStringLiteral("ready;")));
        const QSet<QString> advertised(burst.begin(), burst.end());

        // Ask for everything the burst named, as a bare GET. "device" goes last and alone so its
        // reply is the terminator - replies come back in command order on one socket.
        QStringList queries;
        for (const QString &command : burst) {
            const QString name = command.left(command.indexOf(QLatin1Char(':')));
            if (name.isEmpty() || name == QLatin1String("device")) {
                continue;
            }
            if (!queries.contains(name)) {
                queries << name;
            }
        }
        QVERIFY(queries.size() > 20);
        client.send((queries.join(QLatin1Char(';')) + QStringLiteral(";device;")).toUtf8());

        const QStringList replies = client.collectUntil("device:QK4");
        bool sawDevice = false;
        for (const QString &reply : replies) {
            sawDevice = sawDevice || reply.startsWith(QStringLiteral("device:QK4"));
        }
        QVERIFY2(sawDevice, "the terminating query went unanswered");
        for (const QString &reply : replies) {
            QVERIFY2(advertised.contains(reply), qPrintable(QStringLiteral("not advertised: ") + reply));
        }

        // The ones that matter, spelled out - a server that answered nothing would also satisfy the
        // subset check above.
        const QSet<QString> got(replies.begin(), replies.end());
        const QStringList required{
            QStringLiteral("drive:0,100;"),
            QStringLiteral("tune_drive:0,100;"),
            QStringLiteral("mic_level:50;"),
            QStringLiteral("agc_mode:0,normal;"),
            QStringLiteral("rx_filter_band:0,100,2800;"),
            QStringLiteral("sql_level:0,-140;"),
            QStringLiteral("trx_count:2;"),
            QStringLiteral("channels_count:2;"),
            QStringLiteral("protocol:ExpertSDR3,1.5;"),
            QStringLiteral("modulations_list:usb,lsb,cw,cwr,am,sam,fm,digu,digl,rtty;"),
            QStringLiteral("audio_samplerate:48000;"),
            QStringLiteral("rit_offset:0,0;"),
        };
        for (const QString &want : required) {
            QVERIFY2(got.contains(want), qPrintable(QStringLiteral("unanswered: ") + want));
        }

        client.close();
        server.stop();
    }

    void driveQueriesAlwaysCarryReceiverAndPower() {
        // The same rule as the burst, over the wire: ESDR3-mode WSJT-X and JTDX index args[1]
        // unconditionally, so a bare "drive:100;" crashes them.
        TciServer server;
        QVERIFY(server.start(0));

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("drive;tune_drive:0;");

        for (int i = 0; i < 2; ++i) {
            WebSocketDecoder::Message m;
            QVERIFY(client.next(m));
            const QString reply = QString::fromUtf8(m.payload);
            const TciProtocol::Command c = TciProtocol::parseOne(reply.chopped(1));
            QVERIFY2(c.argCount() == 2, qPrintable(reply));
            QCOMPARE(c.arg(0), QStringLiteral("0"));
        }

        client.close();
        server.stop();
    }

    void refusesQueriesForAReceiverThatDoesNotExist() {
        // Only receiver 0 exists. Answering for it anyway would tell a client its second receiver
        // is real, and the next command would address something that cannot be driven.
        TciServer server;
        QVERIFY(server.start(0));

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");

        // Receiver 2: there are two receivers, 0 and 1, so this is the first that does not exist.
        client.send("rit_offset:2;");
        WebSocketDecoder::Message m;
        QVERIFY(!client.next(m, 300));

        // The same query for the receiver that does exist is answered - which is what proves the
        // silence above came from the receiver check and not from the command being unhandled.
        client.send("rit_offset:0;");
        QVERIFY(client.next(m));
        QCOMPARE(QString::fromUtf8(m.payload), QStringLiteral("rit_offset:0,0;"));

        // And the sub receiver answers for itself, which is the whole point of it being a
        // receiver rather than a channel.
        client.send("rit_offset:1;");
        QVERIFY(client.next(m));
        QCOMPARE(QString::fromUtf8(m.payload), QStringLiteral("rit_offset:1,0;"));

        client.close();
        server.stop();
    }

    void answersTheGlobalFormOfAPerReceiverQuery() {
        // "rit_enable:true;" carries a VALUE where the receiver index normally goes. Treating a
        // failed integer parse as a bad receiver dropped this form silently.
        TciServer server;
        QVERIFY(server.start(0));

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("rit_enable:true;");

        WebSocketDecoder::Message m;
        QVERIFY(client.next(m));
        // false, not true: the reply reports what the radio holds. These queries are read-only.
        QCOMPARE(QString::fromUtf8(m.payload), QStringLiteral("rit_enable:0,false;"));

        client.close();
        server.stop();
    }

    void aReplyReportsTheHeldValueNotTheRequestedOne() {
        // A SET is confirmed with what the radio ACTUALLY holds, never with an optimistic echo of
        // what was asked for. The authoritative value arrives later as a broadcast once the radio
        // confirms. A stale optimistic confirmation is what made WSJT-X transmit out of band in
        // the reference server.
        TciServer server;
        QVERIFY(server.start(0));

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("drive:0,5;rit_offset:0,500;device;");

        const QStringList replies = client.collectUntil("device:QK4");
        QVERIFY2(replies.contains(QStringLiteral("drive:0,100;")), "echoed the requested drive");
        QVERIFY2(replies.contains(QStringLiteral("rit_offset:0,0;")), "echoed the requested offset");

        client.close();
        server.stop();
    }

    void commandsWithoutABuilderStillDoNotMoveTheRadio() {
        // The group that is still report-only, because QK4 has no way to send them. They must be
        // ANSWERED - silence leaves a client waiting - while changing nothing.
        TciServer server;
        QVERIFY(server.start(0));
        QSignalSpy setBool(&server, &TciServer::setBoolRequested);
        QSignalSpy setInt(&server, &TciServer::setIntRequested);

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("mute:0,true;sql_level:0,-80;lock:0,true;device;");

        const QStringList replies = client.collectUntil("device:QK4");
        QVERIFY(replies.contains(QStringLiteral("mute:0,false;")));
        QVERIFY(replies.contains(QStringLiteral("sql_level:0,-140;")));
        QVERIFY(replies.contains(QStringLiteral("lock:0,false;")));
        QCOMPARE(setBool.count(), 0);
        QCOMPARE(setInt.count(), 0);

        client.close();
        server.stop();
    }

    void requestsTheSetsThatHaveABuilder() {
        TciServer server;
        QVERIFY(server.start(0));
        QSignalSpy setBool(&server, &TciServer::setBoolRequested);
        QSignalSpy setInt(&server, &TciServer::setIntRequested);
        QSignalSpy setFilter(&server, &TciServer::setFilterBandRequested);

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("rit_enable:0,true;rx_nb_enable:0,true;rit_offset:0,250;drive:0,35;"
                    "agc_mode:0,fast;rx_filter_band:0,300,2700;device;");
        client.collectUntil("device:QK4");

        QTRY_COMPARE(setBool.count(), 2);
        QCOMPARE(setBool.at(0).at(1).toString(), QStringLiteral("rit_enable"));
        QCOMPARE(setBool.at(0).at(2).toBool(), true);
        QCOMPARE(setBool.at(1).at(1).toString(), QStringLiteral("rx_nb_enable"));

        QCOMPARE(setInt.count(), 3);
        QCOMPARE(setInt.at(0).at(1).toString(), QStringLiteral("rit_offset"));
        QCOMPARE(setInt.at(0).at(2).toInt(), 250);
        QCOMPARE(setInt.at(1).at(1).toString(), QStringLiteral("drive"));
        QCOMPARE(setInt.at(1).at(2).toInt(), 35);
        // agc_mode arrives as the K4's GT value: 0 off, 1 slow/normal, 2 fast.
        QCOMPARE(setInt.at(2).at(1).toString(), QStringLiteral("agc_mode"));
        QCOMPARE(setInt.at(2).at(2).toInt(), 2);

        QCOMPARE(setFilter.count(), 1);
        QCOMPARE(setFilter.at(0).at(1).toInt(), 300);
        QCOMPARE(setFilter.at(0).at(2).toInt(), 2700);

        client.close();
        server.stop();
    }

    void agcGainIsReportedRawAndSetsAsAnInteger() {
        // TCI calls it agc_gain but it is the AGC THRESHOLD - AetherSDR's cmdAgcGain reads and
        // writes AgcThreshold - which on a K4 is menu item 10, range 2-8.
        //
        // Reported RAW rather than rescaled into the protocol's -20..120 dB. The scales do not
        // correspond, so a conversion would encode a guess about what threshold 6 means in dB,
        // which is the mistake sql_level is still carrying.
        TciRadioSnapshot snapshot;
        snapshot.rx[TciRadio::MAIN_RECEIVER].agcGain = 4;
        snapshot.rx[TciRadio::SUB_RECEIVER].agcGain = 4;

        TciServer server;
        server.setSnapshot(snapshot);
        const QStringList burst = server.initBurst();
        QVERIFY(burst.contains(QStringLiteral("agc_gain:0,4;")));
        // One radio-wide menu item, so both receivers report the same number.
        QVERIFY(burst.contains(QStringLiteral("agc_gain:1,4;")));

        QVERIFY(server.start(0));
        QSignalSpy setInt(&server, &TciServer::setIntRequested);
        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");

        client.send("agc_gain:0,7;");
        QTRY_COMPARE(setInt.count(), 1);
        QCOMPARE(setInt.at(0).at(1).toString(), QStringLiteral("agc_gain"));
        QCOMPARE(setInt.at(0).at(2).toInt(), 7);

        client.close();
        server.stop();
    }

    void tuneDriveAndDriveAreSeparateControls() {
        // REGRESSION, found on the radio. tune_drive briefly shared drive's handler, so asking for
        // tune power sent PC and moved the OPERATING power instead. They are different controls:
        // drive is PC, tune power is MENU ITEM 69 ("TUNE LP", 1-50 W).
        TciServer server;
        QVERIFY(server.start(0));
        QSignalSpy setInt(&server, &TciServer::setIntRequested);

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");

        client.send("tune_drive:0,20;");
        QTRY_COMPARE(setInt.count(), 1);
        QCOMPARE(setInt.at(0).at(1).toString(), QStringLiteral("tune_drive"));
        QCOMPARE(setInt.at(0).at(2).toInt(), 20);

        client.send("drive:0,35;");
        QTRY_COMPARE(setInt.count(), 2);
        // The two must arrive under their own names, so the controller can send different
        // commands for them. Collapsing them is precisely the bug.
        QCOMPARE(setInt.at(1).at(1).toString(), QStringLiteral("drive"));
        QCOMPARE(setInt.at(1).at(2).toInt(), 35);

        client.close();
        server.stop();
    }

    void refusesAnAgcModeOutsideTheSpecVocabulary() {
        // Coercing an unknown mode is how a radio ends up in a state nobody asked for. The old
        // value is reported back instead.
        TciServer server;
        QVERIFY(server.start(0));
        QSignalSpy setInt(&server, &TciServer::setIntRequested);

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("agc_mode:0,med;");

        WebSocketDecoder::Message m;
        QVERIFY(client.next(m));
        QCOMPARE(QString::fromUtf8(m.payload), QStringLiteral("agc_mode:0,normal;"));
        QCOMPARE(setInt.count(), 0);

        client.close();
        server.stop();
    }

    void refusesAnInvertedFilterBand() {
        // high must be above low; a reversed pair would become a negative width at the K4.
        TciServer server;
        QVERIFY(server.start(0));
        QSignalSpy setFilter(&server, &TciServer::setFilterBandRequested);

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("rx_filter_band:0,2700,300;device;");
        client.collectUntil("device:QK4");
        QCOMPARE(setFilter.count(), 0);

        client.close();
        server.stop();
    }

    void doesNotApplySetsToTheSubReceiver() {
        // The K4's sub-receiver forms are $-suffixed and CatFrames has no builders for them yet.
        // Refusing is better than silently moving the MAIN receiver when a client asked for the
        // sub - which is the failure this guards.
        TciServer server;
        QVERIFY(server.start(0));
        QSignalSpy setBool(&server, &TciServer::setBoolRequested);
        QSignalSpy setInt(&server, &TciServer::setIntRequested);

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("rit_enable:1,true;rit_offset:1,250;device;");

        const QStringList replies = client.collectUntil("device:QK4");
        // Still answered, just not acted on.
        QVERIFY(replies.contains(QStringLiteral("rit_enable:1,false;")));
        QCOMPARE(setBool.count(), 0);
        QCOMPARE(setInt.count(), 0);

        client.close();
        server.stop();
    }
};

QTEST_MAIN(TestTciQueries)
#include "test_tciqueries.moc"
