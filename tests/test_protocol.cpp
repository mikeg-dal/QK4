#include <QSignalSpy>
#include <QTest>
#include <QtEndian>
#include "network/protocol.h"
#include <cstring>

// Helper: build a raw K4 packet from payload bytes
static QByteArray buildTestPacket(const QByteArray &payload) {
    QByteArray packet;
    packet.append(K4Protocol::START_MARKER);
    quint32 length = payload.size();
    char lengthBytes[4];
    qToBigEndian(length, reinterpret_cast<uchar *>(lengthBytes));
    packet.append(lengthBytes, 4);
    packet.append(payload);
    packet.append(K4Protocol::END_MARKER);
    return packet;
}

// Helper: build a CAT payload (type 0x00 + 2 padding bytes + ASCII)
static QByteArray buildCATPayload(const QString &command) {
    QByteArray payload;
    payload.append(static_cast<char>(K4Protocol::CAT)); // 0x00
    payload.append('\x00');
    payload.append('\x00');
    payload.append(command.toLatin1());
    return payload;
}

// Two MiniPAN payloads exactly as a K4D (SN1641) sent them on 2026-10-30, WWV 10 MHz: Main in CW
// (FA 10,000,000, CW pitch 500) and Sub in DATA-A (FB 10,131,000). Type byte included, framing
// markers not.
// Main, CW: t=6.78 s, receiver 0, 1051 bytes
static const char kMiniPanCwFrameHex[] =
    "0300C00000000400000000749898000000000003000000070100000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000002020000000000000000000000000000000000000303050501010000000004"
    "04070708080808090903030606080804040303050508080B0B080809090B0B0C0C0909020200000303050500000000080809"
    "0904040000020200000000000007070A0A09090606060603030000030300000606040407070404010107071212242426261B"
    "1B080807070606040400000707080806060B0B070704040000000005050B0B0A0A000000000000040405050202070708080A"
    "0A0404000000000000060606060303050500000404040406060000000007070B0B0A0A050505050303000006060000000000"
    "000505000003030303080807070000060609090000070706060202060605050A0A10100909040404040202040407070C0C10"
    "10101008080F0F11111010101003032D2D33332C2C04040E0E0E0E0C0C0A0A00000C0C0C0C0B0B060608080C0C0B0B060609"
    "090F0F0808000007070404000008080B0B06060202010100000808050504040808080803030606000002020A0A0E0E0D0D04"
    "0404040404070708080505000004040A0A05050000020202020707070700000000000008080B0B060607070A0A0E0E0F0F0B"
    "0B0707131310100E0E0E0E14141212040406061313151508081717181817171B1B212135353F3F3D3D2B2B1E1E17170E0E19"
    "190F0F1111181816160D0D0909111110100D0D0F0F1515121205050E0E10100C0C08080A0A02020C0C0A0A050508080C0C0B"
    "0B08080000030308080606000004040202000005050000020200000000050500000505080802020303000000000606090907"
    "0708080808000007070707040406060C0C0D0D050504040E0E101009090A0A0D0D0D0D000007070A0A0A0A080800000E0E0D"
    "0D0D0D11111C1C2E2E30302424121208080D0D0C0C0E0E0A0A040409090303000006060000000001010B0B0D0D0000010104"
    "040606000000000000000000000404010107070D0D0909040408080606050505050000000004040606040400000505000008"
    "0809090808080804040101050501010000030300000000000000000000000000000000020200000000040405050000000000"
    "0002020505050504040000070705050000050507070404000005051F1F24241E1E0606030303030202000000000000000000"
    "0001010303060602020D0D09090000000000000000010107070909000007070000000008080303020204040A0A0A0A070706"
    "0605050606070702020303060603030505020200000000050507070000020201010000010100000000020201010000000002"
    "0203030101000000000404010100000000000000000000000000000000000000000000000000000000000000000000000003"
    "03";
// Sub, DATA-A: t=6.78 s, receiver 1, 1051 bytes
static const char kMiniPanDataFrameHex[] =
    "0300C1000100040000000038969A00000000000C000000CB0000000000000000000000000000000000000000000000000001"
    "0100000000000000000000000000000000000000000000000000000404070707070808080800000000000004040505070703"
    "0305050909050501010000020200000000000001010A0A020202020505050507070404000002020101030300000A0A0B0B06"
    "060000070701010101040403030000010101010606050501010000030300000A0A0D0D070705050707040400000303080805"
    "0502020101050506060000000004040404040409090808030302020000000009090E0E0C0C0909090903030A0A0909070702"
    "020505080805050606000005050808090904040000010106060909000006060A0A0909010100000101030305050A0A050500"
    "00000000000707040405050505050501010000000003030A0A0A0A0303080808080000070707070000060600000404060600"
    "000000000004040A0A0C0C0A0A0A0A0303000006060303040408080A0A070700000606000008080A0A070704040303050506"
    "060B0B0C0C05050909060604040808040400000606070700000A0A0909080800000B0B0B0B00000A0A0C0C07070909080801"
    "0104040202010101010202030301010B0B0B0B01010606060607070505000008080808030300000000000006060808070708"
    "0807070B0B0B0B0A0A09090505060609090A0A0A0A0C0C07070000050502020B0B0606000000000505050506060000000007"
    "0706060A0A090909090505010103030000070707070404080809090B0B020207070101000005050505040406060B0B080800"
    "0000000000040408080707000006060909080806060404000004040A0A060609090A0A070700000202070705050000060607"
    "070505000006060303000003030505000000000000060600000202050506060404000005050404000000000606050500000B"
    "0B0C0C05050101000003030101080809090000080808080707060606060202010104040505090909090B0B08080202040407"
    "0705050303080809090707050501010000000005050B0B090902020A0A09090505030303030606000005050808050500000A"
    "0A0C0C0B0B03030A0A0909060604040404030307070909060604040808020200000606030305050000010100000000060607"
    "070B0B0B0B070700000404010109090606050500000505080806060000010100000505000009090A0A050501010303010100"
    "0006060505000006060707000000000B0B0A0A00000606000000000909090906060303030305050808070706060000010100"
    "000A0A0909030305050606030300000303070702020000030304040000000000000000050503030000000000000000000002"
    "0203030404040402020303010100000000000000000000000000000000000002020000000000000101000000000000000000"
    "00";

class TestProtocol : public QObject {
    Q_OBJECT

private slots:
    // =========================================================================
    // Packet framing
    // =========================================================================

    void testValidCATPacket() {
        Protocol p;
        QSignalSpy spy(&p, &Protocol::catResponseReceived);

        QByteArray packet = buildTestPacket(buildCATPayload("FA00014074000;"));
        p.parse(packet);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QString("FA00014074000;"));
    }

    void testMultiplePacketsInOneChunk() {
        Protocol p;
        QSignalSpy spy(&p, &Protocol::catResponseReceived);

        QByteArray data;
        data.append(buildTestPacket(buildCATPayload("FA00014074000;")));
        data.append(buildTestPacket(buildCATPayload("MD2;")));
        p.parse(data);

        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(0).at(0).toString(), QString("FA00014074000;"));
        QCOMPARE(spy.at(1).at(0).toString(), QString("MD2;"));
    }

    void testManyPacketsThenAPartialOneInOneChunk() {
        // The consumed packets are compacted away once; the partial tail must survive intact.
        Protocol p;
        QSignalSpy spy(&p, &Protocol::catResponseReceived);

        const QByteArray last = buildTestPacket(buildCATPayload("FB00007074000;"));
        QByteArray data;
        for (int i = 0; i < 50; ++i) {
            data.append(buildTestPacket(buildCATPayload(QStringLiteral("RG-%1;").arg(i, 2, 10, QChar(u'0')))));
        }
        data.append(QByteArray("\x01\x02junk"));
        data.append(last.left(last.size() - 5));
        p.parse(data);
        QCOMPARE(spy.count(), 50);
        QCOMPARE(spy.at(49).at(0).toString(), QString("RG-49;"));

        p.parse(last.right(5));
        QCOMPARE(spy.count(), 51);
        QCOMPARE(spy.at(50).at(0).toString(), QString("FB00007074000;"));
    }

    void testResetFromASlotStopsTheRestOfTheChunk() {
        // A disconnect handled inside a packet's slot resets the parser; nothing after it in the
        // same read belongs to the session that just ended.
        Protocol p;
        QStringList seen;
        connect(&p, &Protocol::catResponseReceived, &p, [&](const QString &r) {
            seen << r;
            p.reset();
        });
        QByteArray data;
        data.append(buildTestPacket(buildCATPayload("FA00014074000;")));
        data.append(buildTestPacket(buildCATPayload("MD2;")));
        p.parse(data);
        QCOMPARE(seen, QStringList{QStringLiteral("FA00014074000;")});

        p.parse(buildTestPacket(buildCATPayload("MD3;")));
        QCOMPARE(seen.size(), 2);
        QCOMPARE(seen.at(1), QStringLiteral("MD3;"));
    }

    void testSplitPacketAcrossTwoParses() {
        Protocol p;
        QSignalSpy spy(&p, &Protocol::catResponseReceived);

        QByteArray packet = buildTestPacket(buildCATPayload("FA00014074000;"));
        int splitPoint = packet.size() / 2;

        p.parse(packet.left(splitPoint));
        QCOMPARE(spy.count(), 0); // not yet complete

        p.parse(packet.mid(splitPoint));
        QCOMPARE(spy.count(), 1); // now complete
        QCOMPARE(spy.at(0).at(0).toString(), QString("FA00014074000;"));
    }

    // A connection that drops mid-packet leaves a header in the parser. Without reset() the next
    // session's first packet - the auth reply - is read as that packet's missing body, and with a
    // large announced length nothing is emitted at all (#162).
    void testResetDiscardsAPacketCutOffByADisconnect() {
        Protocol p;
        QSignalSpy spy(&p, &Protocol::catResponseReceived);

        QByteArray stale = buildTestPacket(QByteArray(100000, 'x'));
        p.parse(stale.left(64)); // the old session ends here
        p.reset();

        p.parse(buildTestPacket(buildCATPayload("FA00014074000;")));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QString("FA00014074000;"));
    }

    void testEmptyPayload() {
        Protocol p;
        QSignalSpy spy(&p, &Protocol::packetReceived);

        QByteArray packet = buildTestPacket(QByteArray());
        p.parse(packet);

        QCOMPARE(spy.count(), 0); // empty payload ignored
    }

    void testGarbageBeforePacket() {
        Protocol p;
        QSignalSpy spy(&p, &Protocol::catResponseReceived);

        QByteArray data("garbage_data_here");
        data.append(buildTestPacket(buildCATPayload("MD1;")));
        p.parse(data);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QString("MD1;"));
    }

    void testBadEndMarker() {
        Protocol p;
        QSignalSpy spy(&p, &Protocol::catResponseReceived);

        QByteArray payload = buildCATPayload("FA00014074000;");
        QByteArray packet;
        packet.append(K4Protocol::START_MARKER);
        quint32 length = payload.size();
        char lengthBytes[4];
        qToBigEndian(length, reinterpret_cast<uchar *>(lengthBytes));
        packet.append(lengthBytes, 4);
        packet.append(payload);
        packet.append("\xAA\xBB\xCC\xDD", 4); // wrong end marker

        p.parse(packet);
        QCOMPARE(spy.count(), 0); // rejected
    }

    void testBufferOverflow() {
        Protocol p;
        QSignalSpy spy(&p, &Protocol::catResponseReceived);

        // Feed more than MAX_BUFFER_SIZE without a valid packet
        QByteArray huge(K4Protocol::MAX_BUFFER_SIZE + 100, 'X');
        p.parse(huge);

        // Buffer should be cleared, subsequent valid packets should work
        p.parse(buildTestPacket(buildCATPayload("MD2;")));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QString("MD2;"));
    }

    // TRN-004. A read larger than any buffer cap is still a stream of valid packets. The old code
    // checked the cap before parsing and threw the whole burst away.
    void testBurstLargerThanTheBufferCapIsDeliveredWhole() {
        Protocol p;
        QSignalSpy spy(&p, &Protocol::audioDataReady);

        QByteArray audio;
        audio.append(static_cast<char>(K4Protocol::Audio));
        audio.append("\x01\x00\x03\xF0\x00\x00", 6);
        audio.append(QByteArray(11520, '\x55')); // RAW EM0, 120 ms tier: the largest audio packet
        const QByteArray packet = buildTestPacket(audio);
        QByteArray burst;
        int count = 0;
        while (burst.size() <= K4Protocol::MAX_BUFFER_SIZE + packet.size()) {
            burst.append(packet);
            ++count;
        }
        p.parse(burst);
        QCOMPARE(spy.count(), count);
    }

    // TRN-004. Four payload bytes that read as a start marker, followed by a huge "length", must not
    // hold up the real packet behind them. The old code waited for the announced bytes (up to 1 MB).
    void testBogusLengthDoesNotStallTheNextPacket() {
        Protocol p;
        QSignalSpy spy(&p, &Protocol::catResponseReceived);

        QByteArray data = K4Protocol::START_MARKER;
        char lengthBytes[4];
        qToBigEndian(quint32(K4Protocol::MAX_PAYLOAD_SIZE + 1), reinterpret_cast<uchar *>(lengthBytes));
        data.append(lengthBytes, 4);
        data.append(buildTestPacket(buildCATPayload("FA00014074000;")));
        p.parse(data);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QString("FA00014074000;"));
    }

    // The largest payload the parser accepts is still delivered.
    void testPayloadAtTheLengthLimitIsAccepted() {
        Protocol p;
        QSignalSpy spy(&p, &Protocol::catResponseReceived);
        QByteArray payload = buildCATPayload("MD2;");
        payload.append(QByteArray(K4Protocol::MAX_PAYLOAD_SIZE - payload.size(), ' '));
        p.parse(buildTestPacket(payload));
        QCOMPARE(spy.count(), 1);
    }

    void testTruncatedHeader() {
        Protocol p;
        QSignalSpy spy(&p, &Protocol::packetReceived);

        // Just the start marker + 2 bytes (not enough for length field)
        QByteArray data = K4Protocol::START_MARKER;
        data.append("\x00\x00", 2);
        p.parse(data);

        QCOMPARE(spy.count(), 0); // not enough data
    }

    // =========================================================================
    // Packet type routing
    // =========================================================================

    void testAudioPacketRouting() {
        Protocol p;
        QSignalSpy dataSpy(&p, &Protocol::audioDataReady);
        QSignalSpy seqSpy(&p, &Protocol::audioSequenceReceived);

        // Build audio payload: type=0x01, ver=0x01, seq=42, mode=3, framesize=240, rate=0, data
        QByteArray audioPayload;
        audioPayload.append(static_cast<char>(K4Protocol::Audio)); // type
        audioPayload.append('\x01');                               // version
        audioPayload.append(static_cast<char>(42));                // sequence
        audioPayload.append('\x03');                               // encode mode (Opus Float)
        audioPayload.append('\xF0');                               // frame size low (240)
        audioPayload.append('\x00');                               // frame size high
        audioPayload.append('\x00');                               // sample rate code
        audioPayload.append("fake_audio_data", 15);                // audio data

        p.parse(buildTestPacket(audioPayload));

        QCOMPARE(dataSpy.count(), 1);
        QCOMPARE(seqSpy.count(), 1);
        QCOMPARE(seqSpy.at(0).at(0).value<quint8>(), quint8(42));
    }

    // TRN-006. PAN header as the K4 sends it: type, version, sequence, PAN type, receiver, u16 data
    // length at 5, 4 reserved bytes, int64 centre (Hz) at 11, int32 sample rate (kHz) at 19, int32
    // noise floor x10 at 23, bins from 27. Values are little-endian.
    void testPanPacketRouting() {
        Protocol p;
        QSignalSpy spy(&p, &Protocol::spectrumDataReady);

        constexpr int bins = 1024;
        QByteArray pan(27, '\0');
        pan[0] = static_cast<char>(K4Protocol::PAN);
        pan[1] = '\x01';
        pan[2] = '\x07';
        pan[3] = '\x00';
        pan[4] = '\x01'; // Sub
        qToLittleEndian<quint16>(bins, reinterpret_cast<uchar *>(pan.data() + 5));
        qToLittleEndian<qint64>(14'074'000, reinterpret_cast<uchar *>(pan.data() + 11));
        qToLittleEndian<qint32>(96, reinterpret_cast<uchar *>(pan.data() + 19));
        qToLittleEndian<qint32>(-1234, reinterpret_cast<uchar *>(pan.data() + 23));
        pan.append(QByteArray(bins, '\x40'));

        p.parse(buildTestPacket(pan));

        QCOMPARE(spy.count(), 1);
        const QList<QVariant> args = spy.at(0);
        QCOMPARE(args.at(0).toInt(), 1);                       // receiver
        QCOMPARE(args.at(1).toByteArray(), pan);               // payload
        QCOMPARE(args.at(2).toInt(), 27);                      // bins offset
        QCOMPARE(args.at(3).toInt(), bins);                    // bin count
        QCOMPARE(args.at(4).toLongLong(), qint64(14'074'000)); // centre, Hz
        QCOMPARE(args.at(5).toInt(), 96);                      // sample rate, kHz
        QCOMPARE(args.at(6).toFloat(), -123.4f);               // noise floor, dB
    }

    void testPanPacketWithOnlyAHeaderIsIgnored() {
        Protocol p;
        QSignalSpy spy(&p, &Protocol::spectrumDataReady);
        QByteArray pan(27, '\0');
        pan[0] = static_cast<char>(K4Protocol::PAN);
        p.parse(buildTestPacket(pan));
        QCOMPARE(spy.count(), 0);
    }

    // SPEC-001: a MiniPAN frame has the PAN header. Bins start at 27 (not 5), the data length says
    // how many, the int32 at 19 is the span in kHz and the int64 at 11 the centre.
    void testMiniPanCwFrameFromTheRadio() {
        Protocol p;
        QSignalSpy spy(&p, &Protocol::miniSpectrumDataReady);
        const QByteArray frame = QByteArray::fromHex(kMiniPanCwFrameHex);
        QCOMPARE(frame.size(), 1051);

        p.parse(buildTestPacket(frame));

        QCOMPARE(spy.size(), 1);
        const QList<QVariant> args = spy.at(0);
        QCOMPARE(args.at(0).toInt(), 0);                       // Main
        QCOMPARE(args.at(1).toByteArray(), frame);             // payload
        QCOMPARE(args.at(2).toInt(), 27);                      // bins offset
        QCOMPARE(args.at(3).toInt(), 1024);                    // bin count, from the header
        QCOMPARE(args.at(4).toLongLong(), qint64(10'000'500)); // dial + CW pitch
        QCOMPARE(args.at(5).toInt(), 3000);                    // span, Hz
    }

    void testMiniPanDataFrameFromTheRadio() {
        Protocol p;
        QSignalSpy spy(&p, &Protocol::miniSpectrumDataReady);
        p.parse(buildTestPacket(QByteArray::fromHex(kMiniPanDataFrameHex)));

        QCOMPARE(spy.size(), 1);
        const QList<QVariant> args = spy.at(0);
        QCOMPARE(args.at(0).toInt(), 1); // Sub
        QCOMPARE(args.at(2).toInt(), 27);
        QCOMPARE(args.at(3).toInt(), 1024);
        QCOMPARE(args.at(4).toLongLong(), qint64(10'131'000)); // the dial
        QCOMPARE(args.at(5).toInt(), 12'000);
    }

    // The header promises 1024 bins; a frame that carries fewer is dropped rather than read past.
    void testMiniPanFrameShorterThanItsDataLengthIsDropped() {
        Protocol p;
        QSignalSpy spy(&p, &Protocol::miniSpectrumDataReady);
        p.parse(buildTestPacket(QByteArray::fromHex(kMiniPanCwFrameHex).left(27 + 1000)));
        QCOMPARE(spy.size(), 0);
    }

    void testMiniPanFrameWithNoSpanIsDropped() {
        Protocol p;
        QSignalSpy spy(&p, &Protocol::miniSpectrumDataReady);
        QByteArray frame = QByteArray::fromHex(kMiniPanCwFrameHex);
        qToLittleEndian<qint32>(0, reinterpret_cast<uchar *>(frame.data() + 19));
        p.parse(buildTestPacket(frame));
        QCOMPARE(spy.size(), 0);
    }

    void testMiniPanHeaderOnlyIsIgnored() {
        Protocol p;
        QSignalSpy spy(&p, &Protocol::miniSpectrumDataReady);
        p.parse(buildTestPacket(QByteArray::fromHex(kMiniPanCwFrameHex).left(27)));
        QCOMPARE(spy.size(), 0);
    }

    void testUnknownPacketType() {
        Protocol p;
        QSignalSpy genericSpy(&p, &Protocol::packetReceived);
        QSignalSpy catSpy(&p, &Protocol::catResponseReceived);

        // Type 0xFF — unknown
        QByteArray payload;
        payload.append('\xFF');
        payload.append("data", 4);
        p.parse(buildTestPacket(payload));

        QCOMPARE(genericSpy.count(), 1); // generic signal fires
        QCOMPARE(catSpy.count(), 0);     // no CAT signal
    }

    // =========================================================================
    // Packet building (static methods)
    // =========================================================================

    void testBuildCATPacket() {
        QByteArray packet = Protocol::buildCATPacket("FA;");

        // Should have: START(4) + LENGTH(4) + PAYLOAD(6: type+pad+pad+"FA;") + END(4) = 18 bytes
        QVERIFY(packet.startsWith(K4Protocol::START_MARKER));
        QVERIFY(packet.endsWith(K4Protocol::END_MARKER));

        // Parse it back to verify roundtrip
        Protocol p;
        QSignalSpy spy(&p, &Protocol::catResponseReceived);
        p.parse(packet);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QString("FA;"));
    }

    // The KZ egress frames into a stack buffer; it must put the same bytes on the wire as
    // buildCATPacket, for a keyer frame and for the longest KPOD+ EP02 batch.
    void testWriteCATPacketMatchesBuildCATPacket() {
        for (const char *command : {"KZD0060;", "ML0000;", "KZ.;KZ-;KZ.;KZ-;KZ.;KZ-;KZ.;KZ-;"}) {
            char out[64];
            const std::size_t length = Protocol::writeCATPacket(command, out, sizeof out);
            QCOMPARE(QByteArray(out, static_cast<qsizetype>(length)), Protocol::buildCATPacket(command));
            QCOMPARE(length, Protocol::kCatPacketOverhead + std::strlen(command));
        }
    }

    void testWriteCATPacketRefusesASmallBuffer() {
        char out[16];
        QCOMPARE(Protocol::writeCATPacket("KZD0060;", out, sizeof out), std::size_t(0));
    }

    void testBuildAudioPacketRoundtrip() {
        QByteArray audioData("test_audio_samples", 18);
        QByteArray packet = Protocol::buildAudioPacket(audioData, 7, 0x03);

        // Parse it back
        Protocol p;
        QSignalSpy spy(&p, &Protocol::audioDataReady);
        p.parse(packet);
        QCOMPARE(spy.count(), 1);

        // Verify sequence number in the payload
        QByteArray receivedPayload = spy.at(0).at(0).toByteArray();
        QCOMPARE(static_cast<quint8>(receivedPayload[K4Protocol::AudioPacket::SEQUENCE_OFFSET]), quint8(7));
    }

    void testBuildAudioPacketFrameSize_default() {
        QByteArray audioData("test", 4);
        QByteArray packet = Protocol::buildAudioPacket(audioData, 0, 0x03);

        // Extract payload: skip START_MARKER(4) + LENGTH(4) = offset 8
        // Frame size is at payload bytes 4-5 (LE)
        int payloadOffset = K4Protocol::START_MARKER.size() + 4; // 4+4=8
        quint16 frameSize = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(
            packet.constData() + payloadOffset + K4Protocol::AudioPacket::FRAME_SIZE_OFFSET));
        QCOMPARE(frameSize, quint16(240)); // Default
    }

    void testBuildAudioPacketFrameSize_SL3() {
        QByteArray audioData("test", 4);
        QByteArray packet = Protocol::buildAudioPacket(audioData, 0, 0x03, 720);

        int payloadOffset = K4Protocol::START_MARKER.size() + 4;
        quint16 frameSize = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(
            packet.constData() + payloadOffset + K4Protocol::AudioPacket::FRAME_SIZE_OFFSET));
        QCOMPARE(frameSize, quint16(720));
    }

    void testBuildAudioPacketFrameSize_SL6() {
        QByteArray audioData("test", 4);
        QByteArray packet = Protocol::buildAudioPacket(audioData, 0, 0x03, 1440);

        int payloadOffset = K4Protocol::START_MARKER.size() + 4;
        quint16 frameSize = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(
            packet.constData() + payloadOffset + K4Protocol::AudioPacket::FRAME_SIZE_OFFSET));
        QCOMPARE(frameSize, quint16(1440));
    }
};

QTEST_MAIN(TestProtocol)
#include "test_protocol.moc"
