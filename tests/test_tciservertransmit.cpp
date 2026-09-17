#include <QtTest>

#include <QSignalSpy>

#include <vector>

#include "network/tciaudioframe.h"
#include "network/tciprotocol.h"
#include "network/tciradiostate.h"
#include "network/tciserver.h"
#include "network/websocketframe.h"
#include "tcitestclient.h"

using namespace WebSocketFrame;

namespace {
// A minimal but well-formed TX_AUDIO frame. Built here rather than inline so a test about PTT
// OWNERSHIP is not also a test of frame construction.
QByteArray txAudioFrame(int pairCount = 8) {
    const std::vector<float> samples(static_cast<size_t>(pairCount) * 2, 0.1f);
    QByteArray frame;
    auto put = [&frame](quint32 v) {
        char le[4];
        qToLittleEndian<quint32>(v, le);
        frame.append(le, 4);
    };
    put(0);                                    // receiver
    put(48000);                                // sample rate
    put(3);                                    // format: float32
    put(0);                                    // codec
    put(0);                                    // crc
    put(static_cast<quint32>(samples.size())); // length, in floats
    put(TciAudioFrame::TypeTxAudio);           // type
    put(2);                                    // channels
    frame.append(32, '\0');                    // reserved
    frame.append(reinterpret_cast<const char *>(samples.data()), static_cast<int>(samples.size() * sizeof(float)));
    return frame;
}
} // namespace

// The TCI server's transmit path: PTT ownership, TX_CHRONO pacing and TX audio admission.
//
// Split out of test_tciserver.cpp, which covers the protocol surface. These are the tests that
// guard the transmitter - a wrong answer here either keys the radio when it should not, or leaves
// it keyed after the client that keyed it is gone.
class TestTciServerTransmit : public QObject {
    Q_OBJECT

private slots:
    void keysAndConfirmsOnTrxTrue() {
        TciServer server;
        QVERIFY(server.start(0));
        QSignalSpy ptt(&server, &TciServer::pttRequested);

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("trx:0,true,tci;");

        WebSocketDecoder::Message m;
        QVERIFY(client.next(m));
        QCOMPARE(QString::fromUtf8(m.payload), QStringLiteral("trx:0,true;"));
        QTRY_COMPARE(ptt.count(), 1);
        QCOMPARE(ptt.at(0).at(0).toBool(), true);

        client.close();
        server.stop();
    }

    void refusesPttOnAReceiverThatDoesNotExist() {
        // Silence surfaces in WSJT-X as "TCI failed to set ptt" with no cause, and PTT must never
        // fall back to receiver 0.
        TciServer server;
        QVERIFY(server.start(0));
        QSignalSpy ptt(&server, &TciServer::pttRequested);

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("trx:1,true;");

        WebSocketDecoder::Message m;
        QVERIFY(client.next(m));
        QCOMPARE(QString::fromUtf8(m.payload), QStringLiteral("trx:1,false;"));
        QCOMPARE(ptt.count(), 0);

        client.close();
        server.stop();
    }

    void doesNotKeyOnAMalformedBoolean() {
        // The reference server reads anything that is not "true" as false; coercing the other way
        // would key the transmitter on garbage.
        TciServer server;
        QVERIFY(server.start(0));
        QSignalSpy ptt(&server, &TciServer::pttRequested);

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("trx:0,yes;");

        WebSocketDecoder::Message m;
        QVERIFY(client.next(m));
        QCOMPARE(QString::fromUtf8(m.payload), QStringLiteral("trx:0,false;"));
        QCOMPARE(ptt.count(), 0);

        client.close();
        server.stop();
    }

    void aSecondClientCannotStealTheTransmitter() {
        TciServer server;
        QVERIFY(server.start(0));
        QSignalSpy ptt(&server, &TciServer::pttRequested);

        TciTestClient owner;
        TciTestClient other;
        QVERIFY(owner.connectTo(server.port()));
        owner.collectUntil("ready;");
        QVERIFY(other.connectTo(server.port()));
        other.collectUntil("ready;");

        owner.send("trx:0,true;");
        QTRY_COMPARE(ptt.count(), 1);

        // The OTHER client learns the transmitter was keyed, even though it did not ask. The
        // protocol makes the server a synchroniser, and a logger with an ON indicator is useless
        // if it only sees its own PTT.
        WebSocketDecoder::Message m;
        QVERIFY(other.next(m));
        QCOMPARE(QString::fromUtf8(m.payload), QStringLiteral("trx:0,true;"));

        other.send("trx:0,true;");
        QVERIFY(other.next(m));
        QCOMPARE(QString::fromUtf8(m.payload), QStringLiteral("trx:0,false;"));
        QCOMPARE(ptt.count(), 1); // still exactly one key event

        owner.close();
        other.close();
        server.stop();
    }

    void anUnownedUnkeyReportsRatherThanUnkeying() {
        // Otherwise any client could drop the operator's transmission.
        TciServer server;
        QVERIFY(server.start(0));
        QSignalSpy ptt(&server, &TciServer::pttRequested);

        TciTestClient owner;
        TciTestClient other;
        QVERIFY(owner.connectTo(server.port()));
        owner.collectUntil("ready;");
        QVERIFY(other.connectTo(server.port()));
        other.collectUntil("ready;");

        owner.send("trx:0,true;");
        QTRY_COMPARE(ptt.count(), 1);

        other.send("trx:0,false;");
        WebSocketDecoder::Message m;
        QVERIFY(other.next(m));
        // Reports the real state - still transmitting - and does not unkey.
        QCOMPARE(QString::fromUtf8(m.payload), QStringLiteral("trx:0,true;"));
        QCOMPARE(ptt.count(), 1);

        owner.close();
        other.close();
        server.stop();
    }

    void broadcastsTransmitStartedByTheRadioItself() {
        // REGRESSION, found with TR4W as the client. A transmit begun anywhere other than a TCI
        // client - the microphone, a footswitch, another CAT client, the radio's own keying -
        // never reached TCI clients at all, because the snapshot's transmit field was
        // unconditionally carried over from the previous one. A logger sat showing RX while the
        // radio was transmitting.
        TciServer server;
        QVERIFY(server.start(0));

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");

        TciRadioSnapshot snapshot;
        snapshot.transmitting = true; // the radio keyed; no TCI client asked for it
        server.setSnapshot(snapshot);

        WebSocketDecoder::Message m;
        QVERIFY(client.next(m));
        QCOMPARE(QString::fromUtf8(m.payload), QStringLiteral("trx:0,true;"));

        snapshot.transmitting = false;
        server.setSnapshot(snapshot);
        QVERIFY(client.next(m));
        QCOMPARE(QString::fromUtf8(m.payload), QStringLiteral("trx:0,false;"));

        client.close();
        server.stop();
    }

    void aLocalUnkeyReleasesTheClientHoldingTheTransmitter() {
        // THE DEFECT: every QK4-side unkey (Esc, the PTT button, the HaliKey PTT line, the side
        // panel, CatServer) went through AudioController::setPttActive and told the TCI side
        // nothing. The radio dropped to receive, but the server went on believing the client held
        // the transmitter until its own transmit period ended - up to ~15 s for FT8. Keying QK4's
        // PTT inside that window sent the client's tones instead of the operator's microphone.
        TciServer server;
        QVERIFY(server.start(0));

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("trx:0,true;");

        WebSocketDecoder::Message m;
        QVERIFY(client.next(m));
        QCOMPARE(QString::fromUtf8(m.payload), QStringLiteral("trx:0,true;"));

        // QK4 unkeys locally. The radio is already back in receive by this point.
        server.releaseLocalPtt();

        // The client is told, rather than being left to discover it when its period ends. Skip the
        // TX_CHRONO frames: the clock is running, so binary is interleaved with the text.
        bool sawUnkey = false;
        for (int i = 0; i < 12 && !sawUnkey; ++i) {
            if (!client.next(m, 500)) {
                break;
            }
            sawUnkey =
                (m.opcode == WebSocketFrame::OpText && QString::fromUtf8(m.payload) == QStringLiteral("trx:0,false;"));
        }
        QVERIFY2(sawUnkey, "the client was never told the transmitter dropped");
        QVERIFY2(!server.snapshot().transmitting, "the server still thinks it is transmitting");
    }

    void aLocalUnkeyStopsTheClientsAudioBeingAccepted() {
        // Ownership is what gates TX audio: onBinaryMessageReceived drops a frame from anyone who
        // is not the owner. So releasing ownership has to actually stop the audio, not just change
        // what the server reports - otherwise the client's tones are still queued and go out the
        // moment PTT is asserted again.
        TciServer server;
        QVERIFY(server.start(0));
        QSignalSpy txAudio(&server, &TciServer::txAudioReceived);

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("trx:0,true;");

        WebSocketDecoder::Message m;
        QVERIFY(client.next(m));

        // While it owns PTT its audio is taken.
        client.sendBinary(txAudioFrame());
        QTRY_VERIFY(txAudio.count() >= 1);
        const int acceptedWhileOwning = txAudio.count();

        server.releaseLocalPtt();

        // After the local unkey the same client's audio is ignored.
        client.sendBinary(txAudioFrame());
        QTest::qWait(200);
        QCOMPARE(txAudio.count(), acceptedWhileOwning);
    }

    void aLocalUnkeyWithNobodyKeyedSaysNothing() {
        // The no-op case is what stops a CLIENT's own unkey recursing: setPtt clears the owner
        // before it emits pttRequested, so by the time the controller's PTT handler reaches back
        // into the server there is nothing left to release. If this broadcast anyway, every client
        // unkey would produce a second, spurious trx:0,false.
        TciServer server;
        QVERIFY(server.start(0));

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");

        server.releaseLocalPtt();

        WebSocketDecoder::Message m;
        QVERIFY2(!client.next(m, 300), "released PTT nobody was holding");
    }

    void aRadioStateUpdateStillDoesNotUnkeyTheOwningClient() {
        // The other half of the same rule, and the defect that came FIRST: while a TCI client
        // holds PTT, the radio's own state must not override it. The K4 keys only once TX audio
        // starts arriving, so RadioState reports false for the first packets of a transmission,
        // and broadcasting that stopped WSJT-X sending audio at all.
        TciServer server;
        QVERIFY(server.start(0));

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("trx:0,true;");

        WebSocketDecoder::Message m;
        QVERIFY(client.next(m));
        QCOMPARE(QString::fromUtf8(m.payload), QStringLiteral("trx:0,true;"));

        // A snapshot arrives from a radio that has not caught up yet.
        TciRadioSnapshot lagging;
        lagging.transmitting = false;
        lagging.rx[TciRadio::MAIN_RECEIVER].vfoHz = 7074000; // something really did change, so a broadcast does happen
        server.setSnapshot(lagging);

        // Whatever else is broadcast, it must never be an unkey.
        for (int i = 0; i < 4; ++i) {
            if (!client.next(m, 200)) {
                break;
            }
            QVERIFY2(QString::fromUtf8(m.payload) != QStringLiteral("trx:0,false;"),
                     "unkeyed a client that still holds PTT");
        }
        QVERIFY(server.snapshot().transmitting);

        client.close();
        server.stop();
    }

    void unkeysWhenTheKeyingClientVanishes() {
        // Fail closed. A stuck PTT after a crashed client is the worst failure this server has.
        TciServer server;
        QVERIFY(server.start(0));
        QSignalSpy ptt(&server, &TciServer::pttRequested);

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("trx:0,true;");
        QTRY_COMPARE(ptt.count(), 1);

        client.close();
        QTRY_COMPARE(ptt.count(), 2);
        QCOMPARE(ptt.at(1).at(0).toBool(), false);

        server.stop();
    }

    void stoppingTheServerUnkeys() {
        TciServer server;
        QVERIFY(server.start(0));
        QSignalSpy ptt(&server, &TciServer::pttRequested);

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("trx:0,true;");
        QTRY_COMPARE(ptt.count(), 1);

        server.stop();
        QCOMPARE(ptt.count(), 2);
        QCOMPARE(ptt.at(1).at(0).toBool(), false);
        client.close();
    }

    void aRadioStateUpdateDoesNotUnkeyTheClient() {
        // REGRESSION. publishSnapshot builds a fresh snapshot and never sets `transmitting`, so it
        // arrives false. Once setSnapshot started broadcasting diffs, the first RadioState change
        // after keying - and the K4 echoes state constantly during TX - saw true->false and sent a
        // spurious trx:0,false;. The client concluded PTT had dropped and stopped sending audio.
        //
        // PTT belongs to this server's ownership logic, so a snapshot push must never move it.
        TciServer server;
        QVERIFY(server.start(0));
        QSignalSpy ptt(&server, &TciServer::pttRequested);

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");
        client.send("trx:0,true;");
        QTRY_COMPARE(ptt.count(), 1);

        WebSocketDecoder::Message m;
        QVERIFY(client.next(m));
        QCOMPARE(QString::fromUtf8(m.payload), QStringLiteral("trx:0,true;"));
        QVERIFY(client.next(m)); // the priming chrono

        // A radio-state push carrying the struct default of transmitting == false.
        TciRadioSnapshot s;
        s.rx[TciRadio::MAIN_RECEIVER].vfoHz = 7074000;
        s.rx[TciRadio::SUB_RECEIVER].vfoHz = 7074000;
        server.setSnapshot(s);

        // The frequency change may be broadcast; an unkey must NOT be.
        for (int i = 0; i < 4; ++i) {
            if (!client.next(m, 300)) {
                break;
            }
            if (m.opcode == OpText) {
                QVERIFY2(QString::fromUtf8(m.payload) != QStringLiteral("trx:0,false;"),
                         "a snapshot push unkeyed the client");
            }
        }
        QCOMPARE(ptt.count(), 1); // still keyed
        QVERIFY(server.snapshot().transmitting);

        client.close();
        server.stop();
    }

    void oneBadCommandDoesNotDiscardTheRestOfTheFrame() {
        // REGRESSION. The per-command loop used `break` to skip a malformed command, which exits
        // the whole loop - so everything after it in the same frame was silently dropped.
        TciServer server;
        QVERIFY(server.start(0));
        QSignalSpy ptt(&server, &TciServer::pttRequested);

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");

        // A vfo for a receiver that does not exist, then a perfectly good key request.
        client.send("vfo:9,0,7074000;trx:0,true;");

        QTRY_COMPARE(ptt.count(), 1);
        QCOMPARE(ptt.at(0).at(0).toBool(), true);

        client.close();
        server.stop();
    }

    void sendsChronoRequestsOnlyWhileKeyed() {
        // WSJT-X sends no audio until asked, so the chrono clock is both the pacing and the flow
        // control. It must not run when the transmitter is idle.
        TciServer server;
        QVERIFY(server.start(0));

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");

        client.send("trx:0,true;");
        WebSocketDecoder::Message m;
        QVERIFY(client.next(m)); // the trx confirmation
        QCOMPARE(QString::fromUtf8(m.payload), QStringLiteral("trx:0,true;"));

        // The next frame must be a header-only TX_CHRONO.
        QVERIFY(client.next(m));
        QCOMPARE(m.opcode, static_cast<quint8>(OpBinary));
        QCOMPARE(m.payload.size(), TciAudioFrame::HEADER_BYTES);
        TciAudioFrame::Header h;
        QVERIFY(TciAudioFrame::parseHeader(m.payload, &h));
        QCOMPARE(h.type, static_cast<quint32>(TciAudioFrame::TypeTxChrono));
        QCOMPARE(h.length, static_cast<quint32>(TciAudioFrame::CHRONO_FLOATS));

        client.close();
        server.stop();
    }

    void ignoresTxAudioFromAClientThatDoesNotHoldPtt() {
        TciServer server;
        QVERIFY(server.start(0));
        QSignalSpy audio(&server, &TciServer::txAudioReceived);

        TciTestClient client;
        QVERIFY(client.connectTo(server.port()));
        client.collectUntil("ready;");

        // Well-formed TX_AUDIO, but nobody has keyed.
        std::vector<float> mono(512, 0.25f);
        std::vector<float> pairs(mono.size() * 2);
        for (size_t i = 0; i < mono.size(); ++i) {
            pairs[i * 2] = mono[i];
            pairs[i * 2 + 1] = mono[i];
        }
        QByteArray frame;
        auto put = [&frame](quint32 v) {
            char le[4];
            qToLittleEndian<quint32>(v, le);
            frame.append(le, 4);
        };
        put(0);
        put(48000);
        put(3);
        put(0);
        put(0);
        put(static_cast<quint32>(pairs.size()));
        put(TciAudioFrame::TypeTxAudio);
        put(2);
        frame.append(32, '\0');
        frame.append(reinterpret_cast<const char *>(pairs.data()), static_cast<int>(pairs.size() * sizeof(float)));

        client.sendBinary(frame);
        QTest::qWait(200);
        QCOMPARE(audio.count(), 0);

        client.close();
        server.stop();
    }
};

QTEST_MAIN(TestTciServerTransmit)
#include "test_tciservertransmit.moc"
