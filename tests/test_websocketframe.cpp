#include <QtTest>

#include "network/websocketframe.h"

using namespace WebSocketFrame;

namespace {

// Synthesise client->server traffic, which RFC 6455 requires to be masked.
QByteArray clientFrame(quint8 opcode, const QByteArray &payload) {
    return encode(opcode, payload, /*mask=*/true);
}

// Build a frame by hand so tests can produce things encode() deliberately cannot: fragments,
// reserved bits, bad lengths.
QByteArray rawFrame(bool fin, quint8 rsv, quint8 opcode, const QByteArray &payload, bool mask,
                    const quint8 maskKey[4] = nullptr) {
    static const quint8 kDefaultKey[4] = {0x37, 0xFA, 0x21, 0x3D};
    const quint8 *key = maskKey ? maskKey : kDefaultKey;

    QByteArray f;
    f.append(static_cast<char>((fin ? 0x80 : 0x00) | ((rsv & 0x07) << 4) | (opcode & 0x0F)));
    const char maskBit = mask ? static_cast<char>(0x80) : static_cast<char>(0x00);
    const int len = payload.size();
    if (len < 126) {
        f.append(static_cast<char>(maskBit | len));
    } else if (len <= 0xFFFF) {
        f.append(static_cast<char>(maskBit | 126));
        f.append(static_cast<char>((len >> 8) & 0xFF));
        f.append(static_cast<char>(len & 0xFF));
    } else {
        f.append(static_cast<char>(maskBit | 127));
        for (int i = 7; i >= 0; --i) {
            f.append(static_cast<char>((static_cast<quint64>(len) >> (8 * i)) & 0xFF));
        }
    }
    if (!mask) {
        f.append(payload);
        return f;
    }
    f.append(reinterpret_cast<const char *>(key), 4);
    QByteArray m = payload;
    for (int i = 0; i < m.size(); ++i) {
        m[i] = static_cast<char>(static_cast<quint8>(m[i]) ^ key[i % 4]);
    }
    f.append(m);
    return f;
}

} // namespace

// RFC 6455 framing for the TCI server.
//
// Split from the socket layer specifically so these cases are reachable. The interesting failures
// here are all silent ones: a wrong length form truncates, a missed mask check turns a hostile
// frame into garbage payload, and a fragment-reassembly slip corrupts audio without any error.
class TestWebSocketFrame : public QObject {
    Q_OBJECT

private slots:
    // ---- handshake -----------------------------------------------------------------------

    void acceptKeyMatchesTheRfcVector() {
        // RFC 6455 section 1.3, verbatim.
        QCOMPARE(acceptKey(QStringLiteral("dGhlIHNhbXBsZSBub25jZQ==")), QStringLiteral("s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
    }

    void acceptKeyIgnoresSurroundingWhitespace() {
        // Header values arrive with the leading space from "Sec-WebSocket-Key: ..." already
        // stripped by most parsers, but not all; tolerate it rather than compute a wrong digest.
        QCOMPARE(acceptKey(QStringLiteral("  dGhlIHNhbXBsZSBub25jZQ==  ")),
                 QStringLiteral("s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
    }

    // ---- encoding ------------------------------------------------------------------------

    void serverFramesAreNeverMasked() {
        // RFC 6455 5.1. A masked server frame is a protocol violation that some clients drop
        // silently rather than report.
        const QByteArray f = encode(OpText, "hello");
        QVERIFY((static_cast<quint8>(f[1]) & 0x80) == 0);
    }

    void encodesTheShortLengthForm() {
        const QByteArray f = encode(OpText, QByteArray(125, 'x'));
        QCOMPARE(static_cast<quint8>(f[1]) & 0x7F, 125);
        QCOMPARE(f.size(), 2 + 125);
    }

    void encodesTheSixteenBitLengthForm() {
        const QByteArray f = encode(OpBinary, QByteArray(126, 'x'));
        QCOMPARE(static_cast<quint8>(f[1]) & 0x7F, 126);
        QCOMPARE(static_cast<quint8>(f[2]), 0);
        QCOMPARE(static_cast<quint8>(f[3]), 126);
        QCOMPARE(f.size(), 4 + 126);
    }

    void encodesTheSixtyFourBitLengthForm() {
        const int n = 70000;
        const QByteArray f = encode(OpBinary, QByteArray(n, 'x'));
        QCOMPARE(static_cast<quint8>(f[1]) & 0x7F, 127);
        QCOMPARE(f.size(), 10 + n);
    }

    void closeFrameCarriesTheCodeBigEndian() {
        const QByteArray f = encodeClose(CloseTooBig);
        QCOMPARE(static_cast<quint8>(f[0]) & 0x0F, static_cast<int>(OpClose));
        QCOMPARE(static_cast<quint8>(f[2]), 0x03);
        QCOMPARE(static_cast<quint8>(f[3]), 0xF1); // 1009
    }

    // ---- decoding ------------------------------------------------------------------------

    void decodesAMaskedClientTextFrame() {
        WebSocketDecoder d;
        d.append(clientFrame(OpText, "trx:0,true;"));

        WebSocketDecoder::Message m;
        QCOMPARE(d.next(m), WebSocketDecoder::Status::Ready);
        QCOMPARE(m.opcode, static_cast<quint8>(OpText));
        QCOMPARE(m.payload, QByteArray("trx:0,true;"));
    }

    void roundTripsEveryLengthForm() {
        for (int n : {0, 1, 125, 126, 127, 65535, 65536}) {
            const QByteArray payload(n, 'q');
            WebSocketDecoder d;
            d.append(clientFrame(OpBinary, payload));

            WebSocketDecoder::Message m;
            QCOMPARE(d.next(m), WebSocketDecoder::Status::Ready);
            QCOMPARE(m.payload.size(), n);
            QCOMPARE(m.payload, payload);
        }
    }

    void rejectsAnUnmaskedClientFrame() {
        // The one rule a server must enforce, and the one a client-derived implementation forgets.
        WebSocketDecoder d(/*requireMask=*/true);
        d.append(encode(OpText, "unmasked"));

        WebSocketDecoder::Message m;
        QCOMPARE(d.next(m), WebSocketDecoder::Status::Error);
        QCOMPARE(d.closeCode(), static_cast<quint16>(CloseProtocolError));
    }

    void acceptsUnmaskedWhenDecodingServerTraffic() {
        WebSocketDecoder d(/*requireMask=*/false);
        d.append(encode(OpText, "ready;"));

        WebSocketDecoder::Message m;
        QCOMPARE(d.next(m), WebSocketDecoder::Status::Ready);
        QCOMPARE(m.payload, QByteArray("ready;"));
    }

    // ---- partial and batched delivery -----------------------------------------------------

    void waitsForTheRestOfASplitFrame() {
        // TCP splits wherever it likes; a header arriving alone must not be parsed as a frame.
        const QByteArray f = clientFrame(OpText, "vfo:0,0,14074000;");
        WebSocketDecoder d;
        WebSocketDecoder::Message m;

        for (int cut = 1; cut < f.size(); ++cut) {
            WebSocketDecoder partial;
            partial.append(f.left(cut));
            QCOMPARE(partial.next(m), WebSocketDecoder::Status::NeedMoreData);
            partial.append(f.mid(cut));
            QCOMPARE(partial.next(m), WebSocketDecoder::Status::Ready);
            QCOMPARE(m.payload, QByteArray("vfo:0,0,14074000;"));
        }
    }

    void popsSeveralMessagesFromOneAppend() {
        // AetherSDR sends its whole init burst as one write; several frames can share a read.
        WebSocketDecoder d;
        d.append(clientFrame(OpText, "one") + clientFrame(OpText, "two") + clientFrame(OpText, "three"));

        WebSocketDecoder::Message m;
        QCOMPARE(d.next(m), WebSocketDecoder::Status::Ready);
        QCOMPARE(m.payload, QByteArray("one"));
        QCOMPARE(d.next(m), WebSocketDecoder::Status::Ready);
        QCOMPARE(m.payload, QByteArray("two"));
        QCOMPARE(d.next(m), WebSocketDecoder::Status::Ready);
        QCOMPARE(m.payload, QByteArray("three"));
        QCOMPARE(d.next(m), WebSocketDecoder::Status::NeedMoreData);
    }

    // ---- fragmentation ---------------------------------------------------------------------

    void reassemblesAFragmentedMessage() {
        WebSocketDecoder d;
        d.append(rawFrame(false, 0, OpText, "audio_", true));
        d.append(rawFrame(false, 0, OpContinuation, "start", true));
        d.append(rawFrame(true, 0, OpContinuation, ":0;", true));

        WebSocketDecoder::Message m;
        QCOMPARE(d.next(m), WebSocketDecoder::Status::Ready);
        QCOMPARE(m.opcode, static_cast<quint8>(OpText));
        QCOMPARE(m.payload, QByteArray("audio_start:0;"));
    }

    void deliversAControlFrameInterleavedInsideAMessage() {
        // RFC 6455 5.4 permits this and real clients do it. Mishandling it either drops the ping
        // (peer times out) or corrupts the message with the ping's payload.
        WebSocketDecoder d;
        d.append(rawFrame(false, 0, OpText, "first-", true));
        d.append(rawFrame(true, 0, OpPing, "hb", true));
        d.append(rawFrame(true, 0, OpContinuation, "half", true));

        WebSocketDecoder::Message m;
        QCOMPARE(d.next(m), WebSocketDecoder::Status::Ready);
        QCOMPARE(m.opcode, static_cast<quint8>(OpPing));
        QCOMPARE(m.payload, QByteArray("hb"));

        QCOMPARE(d.next(m), WebSocketDecoder::Status::Ready);
        QCOMPARE(m.opcode, static_cast<quint8>(OpText));
        QCOMPARE(m.payload, QByteArray("first-half"));
    }

    void rejectsAContinuationWithNothingToContinue() {
        WebSocketDecoder d;
        d.append(rawFrame(true, 0, OpContinuation, "orphan", true));

        WebSocketDecoder::Message m;
        QCOMPARE(d.next(m), WebSocketDecoder::Status::Error);
    }

    void rejectsANewDataFrameMidFragment() {
        WebSocketDecoder d;
        d.append(rawFrame(false, 0, OpText, "open", true));
        d.append(rawFrame(true, 0, OpText, "interrupting", true));

        WebSocketDecoder::Message m;
        QCOMPARE(d.next(m), WebSocketDecoder::Status::Error);
    }

    // ---- protocol violations ----------------------------------------------------------------

    void rejectsReservedBits() {
        // Only meaningful with a negotiated extension; this server negotiates none, so a frame
        // carrying them would be mis-parsed rather than merely unsupported.
        WebSocketDecoder d;
        d.append(rawFrame(true, 0x04, OpText, "deflated?", true));

        WebSocketDecoder::Message m;
        QCOMPARE(d.next(m), WebSocketDecoder::Status::Error);
        QCOMPARE(d.closeCode(), static_cast<quint16>(CloseProtocolError));
    }

    void rejectsAFragmentedControlFrame() {
        WebSocketDecoder d;
        d.append(rawFrame(false, 0, OpPing, "x", true));

        WebSocketDecoder::Message m;
        QCOMPARE(d.next(m), WebSocketDecoder::Status::Error);
    }

    void rejectsAnOversizeControlPayload() {
        WebSocketDecoder d;
        d.append(rawFrame(true, 0, OpPing, QByteArray(126, 'x'), true));

        WebSocketDecoder::Message m;
        QCOMPARE(d.next(m), WebSocketDecoder::Status::Error);
    }

    void rejectsAFrameOverTheSizeLimit() {
        // CONVENTIONS.md rule 5: an externally-fed buffer needs an explicit ceiling.
        WebSocketDecoder d(true, /*maxFrameBytes=*/1024, /*maxMessageBytes=*/1024);
        d.append(clientFrame(OpBinary, QByteArray(2048, 'x')));

        WebSocketDecoder::Message m;
        QCOMPARE(d.next(m), WebSocketDecoder::Status::Error);
        QCOMPARE(d.closeCode(), static_cast<quint16>(CloseTooBig));
    }

    void rejectsAReassembledMessageOverTheLimit() {
        // Each fragment is legal on its own; only the total is not. A per-frame check alone would
        // let a peer allocate without bound.
        WebSocketDecoder d(true, /*maxFrameBytes=*/1024, /*maxMessageBytes=*/1536);
        d.append(rawFrame(false, 0, OpBinary, QByteArray(1000, 'x'), true));
        d.append(rawFrame(true, 0, OpContinuation, QByteArray(1000, 'x'), true));

        WebSocketDecoder::Message m;
        QCOMPARE(d.next(m), WebSocketDecoder::Status::Error);
        QCOMPARE(d.closeCode(), static_cast<quint16>(CloseTooBig));
    }

    void rejectsAnUnknownOpcode() {
        WebSocketDecoder d;
        d.append(rawFrame(true, 0, 0x03, "reserved", true));

        WebSocketDecoder::Message m;
        QCOMPARE(d.next(m), WebSocketDecoder::Status::Error);
    }

    void staysInErrorOnceItHasFailed() {
        // A session that saw a violation must not be coaxed back into parsing.
        WebSocketDecoder d;
        d.append(encode(OpText, "unmasked"));

        WebSocketDecoder::Message m;
        QCOMPARE(d.next(m), WebSocketDecoder::Status::Error);
        d.append(clientFrame(OpText, "well-formed"));
        QCOMPARE(d.next(m), WebSocketDecoder::Status::Error);
    }

    void resetClearsTheErrorAndBuffer() {
        WebSocketDecoder d;
        d.append(encode(OpText, "unmasked"));
        WebSocketDecoder::Message m;
        QCOMPARE(d.next(m), WebSocketDecoder::Status::Error);

        d.reset();
        d.append(clientFrame(OpText, "fresh"));
        QCOMPARE(d.next(m), WebSocketDecoder::Status::Ready);
        QCOMPARE(m.payload, QByteArray("fresh"));
    }

    // ---- the payloads this server actually carries -------------------------------------------

    void carriesATciAudioFrameIntact() {
        // 64-byte header plus 2048 float32 samples: the RX_AUDIO shape measured on the wire.
        QByteArray audio(64 + 2048 * 4, '\0');
        for (int i = 0; i < audio.size(); ++i) {
            audio[i] = static_cast<char>(i & 0xFF);
        }
        WebSocketDecoder d;
        d.append(clientFrame(OpBinary, audio));

        WebSocketDecoder::Message m;
        QCOMPARE(d.next(m), WebSocketDecoder::Status::Ready);
        QCOMPARE(m.opcode, static_cast<quint8>(OpBinary));
        QCOMPARE(m.payload.size(), audio.size());
        QCOMPARE(m.payload, audio);
    }

    void carriesAWholeInitBurstAsOneTextFrame() {
        // AetherSDR sends all 42 commands in a single frame; WSJT-X accepts it that way.
        QByteArray burst;
        for (int i = 0; i < 42; ++i) {
            burst += QByteArray("modulation:0,digu;");
        }
        WebSocketDecoder d;
        d.append(clientFrame(OpText, burst));

        WebSocketDecoder::Message m;
        QCOMPARE(d.next(m), WebSocketDecoder::Status::Ready);
        QCOMPARE(m.payload, burst);
    }
};

QTEST_MAIN(TestWebSocketFrame)
#include "test_websocketframe.moc"
