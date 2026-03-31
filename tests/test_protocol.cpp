#include <QSignalSpy>
#include <QTest>
#include <QtEndian>
#include "network/protocol.h"

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

    void testMiniPanPacketRouting() {
        Protocol p;
        QSignalSpy spy(&p, &Protocol::miniSpectrumDataReady);

        // Build MiniPAN payload: type=0x03, ver=0x01, seq=0, reserved=0, receiver=0, bins...
        QByteArray panPayload;
        panPayload.append(static_cast<char>(K4Protocol::MiniPAN)); // type
        panPayload.append('\x01');                                 // version
        panPayload.append('\x00');                                 // sequence
        panPayload.append('\x00');                                 // reserved
        panPayload.append('\x00');                                 // receiver (0 = Main)
        panPayload.append(QByteArray(128, '\x80'));                // 128 bins of data

        p.parse(buildTestPacket(panPayload));

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 0); // receiver = Main
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
};

QTEST_MAIN(TestProtocol)
#include "test_protocol.moc"
