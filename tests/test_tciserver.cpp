#include <QtTest>

#include <QSet>
#include <QSignalSpy>

#include <vector>

#include "network/tciaudioframe.h"
#include "network/tciprotocol.h"
#include "network/tciserver.h"
#include "network/websocketframe.h"
#include "tcitestclient.h"

using namespace WebSocketFrame;

// The TCI server's protocol surface: the init burst, audio subscription, CAT sets and the
// read-only queries. The transmit path - PTT, chrono pacing, TX audio - is in
// test_tciservertransmit.cpp.
//
// The init burst contents are pinned against a captured AetherSDR session that WSJT-X accepted;
// see docs/tci-server-design.md.
class TestTciServer : public QObject {
    Q_OBJECT

private slots:
    // ---- init burst, without a socket ---------------------------------------------------------

    void burstEndsWithReady() {
        // ready must be last: clients latch cached settings the moment it arrives.
        TciServer server;
        const QStringList burst = server.initBurst();
        QVERIFY(!burst.isEmpty());
        QCOMPARE(burst.last(), QStringLiteral("ready;"));
        QCOMPARE(burst.at(burst.size() - 2), QStringLiteral("start;"));
    }

    void burstNeverPrimesAudio() {
        // audio_start is client-owned; a greeting-side primer wedged SDC in the reference server.
        for (const QString &command : TciServer().initBurst()) {
            QVERIFY2(!command.startsWith(QStringLiteral("audio_start")), qPrintable(command));
            QVERIFY2(!command.startsWith(QStringLiteral("iq_start")), qPrintable(command));
        }
    }

    void burstUsesThePluralChannelsCount() {
        // The published PDF says CHANNEL_COUNT; the reference parser aborts on the singular form.
        QVERIFY(TciServer().initBurst().contains(QStringLiteral("channels_count:2;")));
    }

    void burstKeepsCommaBearingIdentityValuesIntact() {
        // "protocol:expertsdr3_1.5;" is the mangled string WSJT-X fails to match, after which it
        // halves transmit amplitude.
        const QStringList burst = TciServer().initBurst();
        QVERIFY(burst.contains(QStringLiteral("protocol:ExpertSDR3,1.5;")));
        QVERIFY(burst.contains(QStringLiteral("modulations_list:usb,lsb,cw,cwr,am,sam,fm,nfm,digu,digl,rtty;")));
    }

    void driveAlwaysCarriesReceiverAndPower() {
        // A bare "drive:0;" crashes ESDR3-mode WSJT-X and JTDX, which index args[1] unconditionally.
        const QStringList burst = TciServer().initBurst();
        for (const QString &command : burst) {
            if (command.startsWith(QStringLiteral("drive:")) || command.startsWith(QStringLiteral("tune_drive:"))) {
                const TciProtocol::Command c = TciProtocol::parseOne(command.chopped(1));
                QVERIFY2(c.argCount() == 2, qPrintable(command));
            }
        }
    }

    void agcModeIsAlwaysOneOfTheThreeSpecValues() {
        // REGRESSION. QK4 answered "med", which TCI does not define: the spec lists exactly
        // normal, fast and off. A client matching the documented vocabulary cannot parse
        // anything else, so this pins the CONSTRAINT rather than one literal value.
        TciRadioSnapshot snapshot;
        for (const QString &mode : {QStringLiteral("normal"), QStringLiteral("fast"), QStringLiteral("off")}) {
            snapshot.agcMode = mode;
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
        snapshot.ritXitOffsetHz = 95; // a real value read off the radio
        TciServer server;
        server.setSnapshot(snapshot);

        QVERIFY(server.initBurst().contains(QStringLiteral("rit_offset:0,95;")));
        QVERIFY(server.initBurst().contains(QStringLiteral("xit_offset:0,95;")));
    }

    void channelOneReportsTheReceiveFrequencyWhenSplitIsOff() {
        // Never the 0 a blank VFO B holds - a client will try to tune to it.
        TciServer server;
        TciRadioSnapshot s;
        s.vfoAHz = 14074000;
        s.vfoBHz = 0;
        s.split = false;
        server.setSnapshot(s);
        QVERIFY(server.initBurst().contains(QStringLiteral("vfo:0,1,14074000;")));
    }

    void channelOneFollowsVfoBWhenSplitIsOn() {
        TciServer server;
        TciRadioSnapshot s;
        s.vfoAHz = 14074000;
        s.vfoBHz = 14080000;
        s.split = true;
        server.setSnapshot(s);
        const QStringList burst = server.initBurst();
        QVERIFY(burst.contains(QStringLiteral("vfo:0,0,14074000;")));
        QVERIFY(burst.contains(QStringLiteral("vfo:0,1,14080000;")));
    }

    void burstDeclares48kHzAudio() {
        // WSJT-X ignores audio_samplerate and always uses 48 kHz; declaring anything else misleads.
        const QStringList burst = TciServer().initBurst();
        QVERIFY(burst.contains(QStringLiteral("audio_samplerate:48000;")));
        QVERIFY(burst.contains(QStringLiteral("audio_stream_samples:2048;")));
        QVERIFY(burst.contains(QStringLiteral("audio_stream_sample_type:float32;")));
    }

    void everyBurstCommandParsesBack() {
        TciProtocol::Parser parser;
        const QStringList burst = TciServer().initBurst();
        const auto parsed = parser.feed(burst.join(QString()));
        QCOMPARE(parsed.size(), burst.size());
    }

    // ---- over a real socket -------------------------------------------------------------------

    void sendsTheBurstOnConnect() {
        TciServer server;
        QVERIFY(server.start(0));

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        const QStringList received = client.collectUntil("ready;");

        QCOMPARE(received, server.initBurst());
        client.close();
        server.stop();
    }

    void echoesAudioStartAndEmitsTheRequest() {
        TciServer server;
        QVERIFY(server.start(0));
        QSignalSpy started(&server, &TciServer::audioStartRequested);

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("audio_start:0;");

        WebSocketDecoder::Message m;
        QVERIFY(client.next(m));
        QCOMPARE(QString::fromUtf8(m.payload), QStringLiteral("audio_start:0;"));
        QTRY_COMPARE(started.count(), 1);
        QCOMPARE(server.audioClientCount(), 1);

        client.close();
        server.stop();
    }

    void sendsRxAudioOnlyToClientsThatAskedForIt() {
        TciServer server;
        QVERIFY(server.start(0));

        TciTestClient listener;
        TciTestClient silent;
        QVERIFY(listener.connectTo(server.port()));
        listener.collectUntil("ready;");
        QVERIFY(silent.connectTo(server.port()));
        silent.collectUntil("ready;");

        listener.send("audio_start:0;");
        QTRY_COMPARE(server.audioClientCount(), 1);

        // Consume the audio_start echo, which is queued ahead of any audio frame.
        WebSocketDecoder::Message echoed;
        QVERIFY(listener.next(echoed));
        QCOMPARE(QString::fromUtf8(echoed.payload), QStringLiteral("audio_start:0;"));

        std::vector<float> audio(2048);
        for (size_t i = 0; i < audio.size(); ++i) {
            audio[i] = static_cast<float>(i % 100) / 100.0f;
        }
        server.sendRxAudio(audio);

        // The subscriber gets a well-formed RX_AUDIO frame...
        WebSocketDecoder::Message m;
        QVERIFY(listener.next(m));
        QCOMPARE(m.opcode, static_cast<quint8>(OpBinary));
        TciAudioFrame::Header h;
        QVERIFY(TciAudioFrame::parseHeader(m.payload, &h));
        QCOMPARE(h.type, static_cast<quint32>(TciAudioFrame::TypeRxAudio));
        QCOMPARE(h.sampleRate, 48000u);
        QCOMPARE(h.length, 2048u);
        QCOMPARE(m.payload.size(), 64 + 2048 * 4);

        // ...and the client that never asked gets nothing.
        WebSocketDecoder::Message none;
        QVERIFY(!silent.next(none, 300));

        listener.close();
        silent.close();
        server.stop();
    }

    void stopsAudioWhenTheLastSubscriberLeaves() {
        TciServer server;
        QVERIFY(server.start(0));
        QSignalSpy stopped(&server, &TciServer::audioStopRequested);

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("audio_start:0;");
        QTRY_COMPARE(server.audioClientCount(), 1);

        client.send("audio_stop:0;");
        QTRY_COMPARE(stopped.count(), 1);
        QCOMPARE(server.audioClientCount(), 0);

        client.close();
        server.stop();
    }

    void aVanishingSubscriberStopsAudio() {
        // Fail closed: a client that disappears mid-stream must not leave the source running.
        TciServer server;
        QVERIFY(server.start(0));
        QSignalSpy stopped(&server, &TciServer::audioStopRequested);

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("audio_start:0;");
        QTRY_COMPARE(server.audioClientCount(), 1);

        client.close();
        QTRY_COMPARE(stopped.count(), 1);
        QCOMPARE(server.audioClientCount(), 0);
        server.stop();
    }

    void echoesTheSensorCommands() {
        TciServer server;
        QVERIFY(server.start(0));

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("rx_sensors_enable:false,500;");

        WebSocketDecoder::Message m;
        QVERIFY(client.next(m));
        QCOMPARE(QString::fromUtf8(m.payload), QStringLiteral("rx_sensors_enable:false,500;"));

        client.close();
        server.stop();
    }

    void answersTheGlobalSplitEnableFormWithAnIndexedReply() {
        // WSJT-X sends "split_enable:false;" with no receiver index. Unexpanded it reads as a GET
        // for receiver -1 and is answered with silence.
        TciServer server;
        QVERIFY(server.start(0));

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("split_enable:false;");

        WebSocketDecoder::Message m;
        QVERIFY(client.next(m));
        QCOMPARE(QString::fromUtf8(m.payload), QStringLiteral("split_enable:0,false;"));

        client.close();
        server.stop();
    }

    void staysSilentOnCommandsItDoesNotImplementYet() {
        // An unhandled TCI command is silence, not an error - which is what makes deferring the
        // rest of the grammar safe.
        TciServer server;
        QVERIFY(server.start(0));

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("cw_macros_speed:20;");

        WebSocketDecoder::Message m;
        QVERIFY(!client.next(m, 300));

        client.close();
        server.stop();
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
            QStringLiteral("trx_count:1;"),
            QStringLiteral("channels_count:2;"),
            QStringLiteral("protocol:ExpertSDR3,1.5;"),
            QStringLiteral("modulations_list:usb,lsb,cw,cwr,am,sam,fm,nfm,digu,digl,rtty;"),
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

        client.send("rit_offset:1;");
        WebSocketDecoder::Message m;
        QVERIFY(!client.next(m, 300));

        // The same query for the receiver that does exist is answered - which is what proves the
        // silence above came from the receiver check and not from the command being unhandled.
        client.send("rit_offset:0;");
        QVERIFY(client.next(m));
        QCOMPARE(QString::fromUtf8(m.payload), QStringLiteral("rit_offset:0,0;"));

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

    // ---- CAT sets --------------------------------------------------------------------------------

    void requestsAFrequencyChangeAndConfirmsWithWhatItHolds() {
        // The confirmation carries the model's current value, never silence. The authoritative echo
        // is the broadcast that follows once the radio actually moves.
        TciServer server;
        TciRadioSnapshot s;
        s.vfoAHz = 14074000;
        s.vfoBHz = 14074000;
        server.setSnapshot(s);
        QVERIFY(server.start(0));
        QSignalSpy freq(&server, &TciServer::setFrequencyRequested);

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("vfo:0,0,7074000;");

        QTRY_COMPARE(freq.count(), 1);
        QCOMPARE(freq.at(0).at(0).toInt(), 0);
        QCOMPARE(freq.at(0).at(1).toLongLong(), 7074000LL);

        WebSocketDecoder::Message m;
        QVERIFY(client.next(m));
        QCOMPARE(QString::fromUtf8(m.payload), QStringLiteral("vfo:0,0,14074000;"));

        client.close();
        server.stop();
    }

    void rangeChecksTheVfoChannel() {
        // "vfo:0,2,..." must produce no request at all, not a write to channel 0.
        TciServer server;
        QVERIFY(server.start(0));
        QSignalSpy freq(&server, &TciServer::setFrequencyRequested);

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("vfo:0,2,7074000;");

        QTest::qWait(200);
        QCOMPARE(freq.count(), 0);

        client.close();
        server.stop();
    }

    void refusesAnUnknownModulationRatherThanCoercingIt() {
        // The reference server coerces to usb, which puts the radio in a mode nobody asked for.
        TciServer server;
        QVERIFY(server.start(0));
        QSignalSpy mode(&server, &TciServer::setModulationRequested);

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");

        client.send("modulation:0,banana;");
        QTest::qWait(200);
        QCOMPARE(mode.count(), 0);

        client.send("modulation:0,digu;");
        QTRY_COMPARE(mode.count(), 1);
        QCOMPARE(mode.at(0).at(0).toString(), QStringLiteral("digu"));

        client.close();
        server.stop();
    }

    void aSteadySplitFalseIsNotAnEdge() {
        // WSJT-X sends split_enable:<n>,false before programming channel 1. Acting on it every time
        // would tear down a split the operator had just set up.
        TciServer server;
        QVERIFY(server.start(0)); // snapshot defaults to split == false
        QSignalSpy split(&server, &TciServer::setSplitRequested);

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");

        client.send("split_enable:false;"); // the global one-argument form, already false
        QTest::qWait(200);
        QCOMPARE(split.count(), 0);

        client.send("split_enable:0,true;"); // a real transition
        QTRY_COMPARE(split.count(), 1);
        QCOMPARE(split.at(0).at(0).toBool(), true);

        client.close();
        server.stop();
    }

    // ---- broadcast on change ---------------------------------------------------------------------

    void broadcastsOnlyWhatActuallyMoved() {
        // A message that arrives must mean something changed, or a chatty radio floods every client
        // on every CAT echo.
        TciServer server;
        TciRadioSnapshot s;
        s.vfoAHz = 14074000;
        s.vfoBHz = 14074000;
        s.modulation = QStringLiteral("usb");
        server.setSnapshot(s);
        QVERIFY(server.start(0));

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");

        // Same snapshot again: nothing should be sent.
        server.setSnapshot(s);
        WebSocketDecoder::Message m;
        QVERIFY(!client.next(m, 300));

        // Now move the frequency.
        s.vfoAHz = 7074000;
        s.vfoBHz = 7074000;
        server.setSnapshot(s);

        QStringList got;
        for (int i = 0; i < 3; ++i) {
            if (!client.next(m, 500)) {
                break;
            }
            if (m.opcode == OpText) {
                got << QString::fromUtf8(m.payload);
            }
        }
        QVERIFY2(got.contains(QStringLiteral("vfo:0,0,7074000;")), qPrintable(got.join(QLatin1Char(' '))));

        client.close();
        server.stop();
    }

    void broadcastsAModeChange() {
        TciServer server;
        TciRadioSnapshot s;
        s.modulation = QStringLiteral("usb");
        server.setSnapshot(s);
        QVERIFY(server.start(0));

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");

        s.modulation = QStringLiteral("digu");
        server.setSnapshot(s);

        WebSocketDecoder::Message m;
        QVERIFY(client.next(m));
        QCOMPARE(QString::fromUtf8(m.payload), QStringLiteral("modulation:0,digu;"));

        client.close();
        server.stop();
    }

    void handlesTheWholeWsjtxOpeningSequence() {
        // Exactly what the captured client sends after connecting, in order and in one frame.
        TciServer server;
        QVERIFY(server.start(0));
        QSignalSpy started(&server, &TciServer::audioStartRequested);

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("split_enable:false;audio_start:0;rx_sensors_enable:false,500;tx_sensors_enable:false,500;");

        QTRY_COMPARE(started.count(), 1);
        QCOMPARE(server.audioClientCount(), 1);

        client.close();
        server.stop();
    }
};

QTEST_MAIN(TestTciServer)
#include "test_tciserver.moc"
