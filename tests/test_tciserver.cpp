#include <QtTest>

#include <QSignalSpy>
#include <QTcpSocket>

#include <vector>

#include "network/tciaudioframe.h"
#include "network/tciprotocol.h"
#include "network/tciserver.h"
#include "network/websocketframe.h"

using namespace WebSocketFrame;

namespace {

constexpr int kTimeoutMs = 5000;

// A TCI client over a real socket. Reads drive the shared event loop, because the server under
// test lives in this same thread — waitForReadyRead would pump only this socket and deadlock.
class TciTestClient {
public:
    bool connectTo(quint16 port) {
        m_socket.connectToHost(QHostAddress::LocalHost, port);
        if (!m_socket.waitForConnected(kTimeoutMs)) {
            return false;
        }
        m_socket.write("GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\n"
                       "Connection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                       "Sec-WebSocket-Version: 13\r\n\r\n");
        m_socket.flush();

        QByteArray header;
        QElapsedTimer timer;
        timer.start();
        while (!header.contains("\r\n\r\n") && timer.elapsed() < kTimeoutMs) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            header.append(m_socket.readAll());
        }
        const int end = header.indexOf("\r\n\r\n");
        if (end < 0) {
            return false;
        }
        const QByteArray leftover = header.mid(end + 4);
        if (!leftover.isEmpty()) {
            m_decoder.append(leftover);
        }
        return header.startsWith("HTTP/1.1 101");
    }

    void sendBinary(const QByteArray &payload) {
        m_socket.write(encode(OpBinary, payload, /*mask=*/true));
        m_socket.flush();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }

    void send(const QByteArray &payload) {
        m_socket.write(encode(OpText, payload, /*mask=*/true));
        m_socket.flush();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }

    bool next(WebSocketDecoder::Message &out, int ms = kTimeoutMs) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < ms) {
            if (m_decoder.next(out) == WebSocketDecoder::Status::Ready) {
                return true;
            }
            if (m_socket.bytesAvailable() > 0) {
                m_decoder.append(m_socket.readAll());
                continue;
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        return m_decoder.next(out) == WebSocketDecoder::Status::Ready;
    }

    // Collects text messages until one of them is `terminator`.
    QStringList collectUntil(const QByteArray &terminator, int ms = kTimeoutMs) {
        QStringList out;
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < ms) {
            WebSocketDecoder::Message m;
            if (!next(m, 500)) {
                continue;
            }
            if (m.opcode != OpText) {
                continue;
            }
            out << QString::fromUtf8(m.payload);
            if (m.payload == terminator) {
                break;
            }
        }
        return out;
    }

    void close() { m_socket.abort(); }

private:
    QTcpSocket m_socket;
    WebSocketDecoder m_decoder{/*requireMask=*/false};
};

} // namespace

// The TCI server, scoped to what reaches audio.
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

    // ---- PTT ------------------------------------------------------------------------------------
    //
    // These guard the transmitter. A wrong answer here either keys the radio when it should not, or
    // leaves it keyed when the client is gone.

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

        other.send("trx:0,true;");
        WebSocketDecoder::Message m;
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
