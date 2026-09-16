#include <QtTest>

#include <QSet>
#include <QSignalSpy>

#include <vector>

#include "network/tciaudioframe.h"
#include "network/tciprotocol.h"
#include "network/tciradiostate.h"
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
        QVERIFY(burst.contains(QStringLiteral("modulations_list:usb,lsb,cw,cwr,am,sam,fm,digu,digl,rtty;")));
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

    void announcesTheTransmitFrequency() {
        // tx_frequency is server-to-client only: the protocol defines no read form, so a client
        // that wants it can never ask. It has to be seeded at connect and broadcast on change, or
        // it is unobtainable. Split is when it matters, and when it differs from the RX VFO.
        TciRadioSnapshot snapshot;
        snapshot.rx[TciRadio::MAIN_RECEIVER].vfoHz = 14074000;
        snapshot.rx[TciRadio::SUB_RECEIVER].vfoHz = 14095000;
        snapshot.split = true;

        TciServer server;
        server.setSnapshot(snapshot);
        QVERIFY(server.initBurst().contains(QStringLiteral("tx_frequency:14095000;")));

        QVERIFY(server.start(0));
        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");

        snapshot.rx[TciRadio::SUB_RECEIVER].vfoHz = 14097000;
        server.setSnapshot(snapshot);

        const QStringList replies = client.collectUntil("tx_frequency:14097000;");
        QVERIFY2(replies.contains(QStringLiteral("tx_frequency:14097000;")),
                 "a transmit-frequency change was never announced");

        client.close();
        server.stop();
    }

    void channelOneReportsTheReceiveFrequencyWhenSplitIsOff() {
        // Never the 0 a blank VFO B holds - a client will try to tune to it.
        TciServer server;
        TciRadioSnapshot s;
        s.rx[TciRadio::MAIN_RECEIVER].vfoHz = 14074000;
        s.rx[TciRadio::SUB_RECEIVER].vfoHz = 0;
        s.split = false;
        server.setSnapshot(s);
        QVERIFY(server.initBurst().contains(QStringLiteral("vfo:0,1,14074000;")));
    }

    void channelOneFollowsVfoBWhenSplitIsOn() {
        TciServer server;
        TciRadioSnapshot s;
        s.rx[TciRadio::MAIN_RECEIVER].vfoHz = 14074000;
        s.rx[TciRadio::SUB_RECEIVER].vfoHz = 14080000;
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
        // rx_bin_enable: binaural/pseudo-stereo, which the K4 has no equivalent for at all, so
        // it will not quietly become implemented and turn this test green for the wrong reason.
        // (It previously used cw_macros_speed, which then WAS implemented.)
        client.send("rx_bin_enable:0,true;");

        WebSocketDecoder::Message m;
        QVERIFY(!client.next(m, 300));

        client.close();
        server.stop();
    }

    // ---- CAT sets --------------------------------------------------------------------------------

    void requestsAFrequencyChangeAndConfirmsWithWhatItHolds() {
        // The confirmation carries the model's current value, never silence. The authoritative echo
        // is the broadcast that follows once the radio actually moves.
        TciServer server;
        TciRadioSnapshot s;
        s.rx[TciRadio::MAIN_RECEIVER].vfoHz = 14074000;
        s.rx[TciRadio::SUB_RECEIVER].vfoHz = 14074000;
        server.setSnapshot(s);
        QVERIFY(server.start(0));
        QSignalSpy freq(&server, &TciServer::setFrequencyRequested);

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("vfo:0,0,7074000;");

        QTRY_COMPARE(freq.count(), 1);
        // setFrequencyRequested now carries (receiver, channel, hz).
        QCOMPARE(freq.at(0).at(0).toInt(), 0); // main receiver
        QCOMPARE(freq.at(0).at(1).toInt(), 0); // channel A
        QCOMPARE(freq.at(0).at(2).toLongLong(), 7074000LL);

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
        // setModulationRequested now carries (receiver, modulation).
        QCOMPARE(mode.at(0).at(0).toInt(), 0);
        QCOMPARE(mode.at(0).at(1).toString(), QStringLiteral("digu"));

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

    // ---- Sub RX (TCI channel 1 / VFO B) ----------------------------------------------------------

    void advertisesBothChannelsInTheBurst() {
        // Channel A is always on; channel B follows the radio. A client that never sees channel B
        // advertised has no reason to ask for it.
        TciRadioSnapshot snapshot;
        snapshot.rx[TciRadio::SUB_RECEIVER].enabled = true;
        TciServer server;
        server.setSnapshot(snapshot);

        const QStringList burst = server.initBurst();
        QVERIFY(burst.contains(QStringLiteral("rx_channel_enable:0,0,true;")));
        QVERIFY(burst.contains(QStringLiteral("rx_channel_enable:0,1,true;")));
    }

    void reportsChannelAAsAlwaysOnAndNeverActsOnIt() {
        // The main receiver cannot be switched off. Silence would be worse than a refusal: a
        // client that believed it had turned the main receiver off would stop asking for audio.
        TciServer server;
        QVERIFY(server.start(0));
        QSignalSpy sub(&server, &TciServer::setSubReceiverRequested);

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("rx_channel_enable:0,0,false;");

        WebSocketDecoder::Message m;
        QVERIFY(client.next(m));
        QCOMPARE(QString::fromUtf8(m.payload), QStringLiteral("rx_channel_enable:0,0,true;"));
        QCOMPARE(sub.count(), 0);

        client.close();
        server.stop();
    }

    void requestsTheSubReceiverAndConfirmsWithWhatItHolds() {
        TciServer server;
        QVERIFY(server.start(0));
        QSignalSpy sub(&server, &TciServer::setSubReceiverRequested);

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("rx_channel_enable:0,1,true;");

        QTRY_COMPARE(sub.count(), 1);
        QCOMPARE(sub.at(0).at(0).toBool(), true);

        WebSocketDecoder::Message m;
        QVERIFY(client.next(m));
        // Still false: the radio has not confirmed yet, and a stale optimistic echo is what made
        // WSJT-X transmit out of band in the reference server.
        QCOMPARE(QString::fromUtf8(m.payload), QStringLiteral("rx_channel_enable:0,1,false;"));

        client.close();
        server.stop();
    }

    void aSteadySubReceiverValueIsNotAnEdge() {
        // Repeating the state the radio already holds must not generate CAT traffic, for the same
        // reason split_enable checks.
        TciRadioSnapshot snapshot;
        snapshot.rx[TciRadio::SUB_RECEIVER].enabled = true;
        TciServer server;
        server.setSnapshot(snapshot);
        QVERIFY(server.start(0));
        QSignalSpy sub(&server, &TciServer::setSubReceiverRequested);

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("rx_channel_enable:0,1,true;");

        WebSocketDecoder::Message m;
        QVERIFY(client.next(m));
        QCOMPARE(QString::fromUtf8(m.payload), QStringLiteral("rx_channel_enable:0,1,true;"));
        QCOMPARE(sub.count(), 0);

        client.close();
        server.stop();
    }

    void refusesAChannelThatDoesNotExist() {
        TciServer server;
        QVERIFY(server.start(0));

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("rx_channel_enable:0,2,true;");

        WebSocketDecoder::Message m;
        QVERIFY(!client.next(m, 300));

        client.close();
        server.stop();
    }

    void broadcastsWhenTheSubReceiverChanges() {
        TciServer server;
        QVERIFY(server.start(0));

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");

        TciRadioSnapshot snapshot;
        snapshot.rx[TciRadio::SUB_RECEIVER].enabled = true;
        server.setSnapshot(snapshot);

        // BOTH spellings go out, because clients disagree about how a second receiver is
        // addressed: rx_enable names the receiver, rx_channel_enable names channel B of receiver
        // 0. They describe the same K4 Sub RX.
        const QStringList seen = client.collectUntil("rx_channel_enable:0,1,true;");
        QVERIFY2(seen.contains(QStringLiteral("rx_enable:1,true;")), "receiver form missing");
        QVERIFY2(seen.contains(QStringLiteral("rx_channel_enable:0,1,true;")), "channel form missing");

        client.close();
        server.stop();
    }

    // ---- broadcast on change ---------------------------------------------------------------------

    void broadcastsOnlyWhatActuallyMoved() {
        // A message that arrives must mean something changed, or a chatty radio floods every client
        // on every CAT echo.
        TciServer server;
        TciRadioSnapshot s;
        s.rx[TciRadio::MAIN_RECEIVER].vfoHz = 14074000;
        s.rx[TciRadio::SUB_RECEIVER].vfoHz = 14074000;
        s.rx[TciRadio::MAIN_RECEIVER].modulation = QStringLiteral("usb");
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
        s.rx[TciRadio::MAIN_RECEIVER].vfoHz = 7074000;
        s.rx[TciRadio::SUB_RECEIVER].vfoHz = 7074000;
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

    void broadcastsAPowerChange() {
        // REGRESSION, found by reducing power on the radio and watching a client sit at 100.
        // DRIVE is a bidirectional control command, and the spec makes the server a synchroniser:
        // a parameter the radio changes must reach every client. drive was never populated from
        // RadioState and never diffed, so it reported the struct default forever.
        TciServer server;
        QVERIFY(server.start(0));

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");

        TciRadioSnapshot snapshot;
        snapshot.drive = 45;
        snapshot.tuneDrive = 45;
        server.setSnapshot(snapshot);

        const QStringList replies = client.collectUntil("tune_drive:0,45;");
        QVERIFY2(replies.contains(QStringLiteral("drive:0,45;")), "a power change was never announced");
        QVERIFY(replies.contains(QStringLiteral("tune_drive:0,45;")));

        client.close();
        server.stop();
    }

    void powerBroadcastsAlwaysCarryReceiverAndPower() {
        // The same arity rule as the burst and the query reply: a bare "drive:45;" crashes
        // ESDR3-mode WSJT-X and JTDX, which index args[1] unconditionally.
        TciServer server;
        QVERIFY(server.start(0));

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");

        TciRadioSnapshot snapshot;
        snapshot.drive = 12;
        snapshot.tuneDrive = 12;
        server.setSnapshot(snapshot);

        const QStringList replies = client.collectUntil("tune_drive:0,12;");
        int checked = 0;
        for (const QString &line : replies) {
            if (!line.startsWith(QStringLiteral("drive:")) && !line.startsWith(QStringLiteral("tune_drive:"))) {
                continue;
            }
            ++checked;
            const TciProtocol::Command c = TciProtocol::parseOne(line.chopped(1));
            QVERIFY2(c.argCount() == 2, qPrintable(line));
        }
        QVERIFY(checked > 0);

        client.close();
        server.stop();
    }

    void everyReportedFieldBroadcastsWhenItChanges() {
        // A CLASS test, not a field test. Four separate bugs this session had one shape: a value
        // that QK4 reports, that can change, and that was never announced when it did. drive sat
        // at 100 while the power knob moved; vfo_lock stayed false while the radio and QK4's own
        // UI showed it locked.
        //
        // This drives one snapshot where EVERY reported field differs from the defaults and
        // asserts each one reaches the client. A new field added to the snapshot without a
        // broadcast fails here rather than on someone's radio.
        TciServer server;
        QVERIFY(server.start(0));

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");

        TciRadioSnapshot snapshot;
        TciReceiverState &main = snapshot.rx[TciRadio::MAIN_RECEIVER];
        main.vfoHz = 7074000;
        main.modulation = QStringLiteral("cw");
        main.rit = true;
        main.xit = true;
        main.ritXitOffsetHz = 120;
        main.filterLowHz = -300;
        main.filterHighHz = 300;
        main.agcMode = QStringLiteral("fast");
        main.agcGain = 4;
        main.noiseBlanker = true;
        main.noiseReduction = true;
        main.autoNotch = true;
        main.apf = true;
        main.notchFilter = true;
        main.noiseBlankerLevel = 9;
        main.noiseBlankerFilterWidth = 2;
        main.lock = true;
        main.sqlEnabled = true;
        main.volumeDb = -12;
        snapshot.split = true;
        snapshot.drive = 45;
        snapshot.tuneDrive = 15;
        snapshot.micLevel = 33;
        snapshot.cwKeyerSpeedWpm = 28;
        snapshot.rx[TciRadio::SUB_RECEIVER].enabled = true;

        server.setSnapshot(snapshot);

        // device is answered from a constant, so it is a reliable terminator.
        client.send("device;");
        const QStringList seen = client.collectUntil("device:QK4;");

        const QStringList expected{
            QStringLiteral("vfo:0,0,7074000;"),
            QStringLiteral("modulation:0,cw;"),
            QStringLiteral("rit_enable:0,true;"),
            QStringLiteral("xit_enable:0,true;"),
            QStringLiteral("rit_offset:0,120;"),
            QStringLiteral("xit_offset:0,120;"),
            QStringLiteral("rx_filter_band:0,-300,300;"),
            QStringLiteral("agc_mode:0,fast;"),
            QStringLiteral("agc_gain:0,4;"),
            QStringLiteral("rx_nb_enable:0,true;"),
            QStringLiteral("rx_nr_enable:0,true;"),
            QStringLiteral("rx_anf_enable:0,true;"),
            QStringLiteral("rx_apf_enable:0,true;"),
            QStringLiteral("rx_nf_enable:0,true;"),
            QStringLiteral("rx_nb_param:0,9,2;"),
            QStringLiteral("vfo_lock:0,0,true;"),
            QStringLiteral("sql_enable:0,true;"),
            QStringLiteral("rx_volume:0,0,-12;"),
            QStringLiteral("split_enable:0,true;"),
            QStringLiteral("drive:0,45;"),
            QStringLiteral("tune_drive:0,15;"),
            QStringLiteral("mic_level:33;"),
            QStringLiteral("cw_keyer_speed:28;"),
            QStringLiteral("rx_enable:1,true;"),
        };
        for (const QString &want : expected) {
            QVERIFY2(seen.contains(want), qPrintable(QStringLiteral("never broadcast: ") + want));
        }

        client.close();
        server.stop();
    }

    void broadcastsAModeChange() {
        TciServer server;
        TciRadioSnapshot s;
        s.rx[TciRadio::MAIN_RECEIVER].modulation = QStringLiteral("usb");
        server.setSnapshot(s);
        QVERIFY(server.start(0));

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");

        s.rx[TciRadio::MAIN_RECEIVER].modulation = QStringLiteral("digu");
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
