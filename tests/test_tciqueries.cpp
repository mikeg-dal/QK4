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

        const QStringList replies = client.collectUntil("device:QK4;");
        QVERIFY2(replies.contains(QStringLiteral("device:QK4;")), "the terminating query went unanswered");
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

    void queriesNeverMoveTheRadio() {
        // Read-only means read-only. These commands have SET forms in the protocol, and QK4 answers
        // them from its snapshot rather than acting on them, because none of the matching K4 sets
        // has been bench-tested. A client sending a SET gets the truth back, not a lie about having
        // applied it. See docs/tci-server-design.md, phase 8.
        TciServer server;
        QVERIFY(server.start(0));
        QSignalSpy frequency(&server, &TciServer::setFrequencyRequested);
        QSignalSpy modulation(&server, &TciServer::setModulationRequested);
        QSignalSpy split(&server, &TciServer::setSplitRequested);
        QSignalSpy ptt(&server, &TciServer::pttRequested);

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("drive:0,5;mic_level:99;mute:0,true;rit_offset:0,500;sql_level:0,80;device;");

        const QStringList replies = client.collectUntil("device:QK4;");
        QVERIFY(replies.contains(QStringLiteral("drive:0,100;")));
        QVERIFY(replies.contains(QStringLiteral("mic_level:50;")));
        QVERIFY(replies.contains(QStringLiteral("mute:0,false;")));
        QVERIFY(replies.contains(QStringLiteral("rit_offset:0,0;")));
        QVERIFY(replies.contains(QStringLiteral("sql_level:0,-140;")));

        QCOMPARE(frequency.count(), 0);
        QCOMPARE(modulation.count(), 0);
        QCOMPARE(split.count(), 0);
        QCOMPARE(ptt.count(), 0);

        client.close();
        server.stop();
    }
};

QTEST_MAIN(TestTciQueries)
#include "test_tciqueries.moc"
