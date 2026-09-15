#include <QtTest>

#include <cmath>
#include <vector>

#include "network/tciaudioframe.h"

using namespace TciAudioFrame;

namespace {

constexpr double kPi = 3.14159265358979323846;

// Build an inbound TX_AUDIO frame the way WSJT-X does: declare `length` floats, but allocate a
// payload twice that size and leave stale data in the tail.
QByteArray wsjtxTxFrame(const std::vector<float> &monoSamples, quint32 channelsField, bool dirtyTail = true) {
    const int declared = static_cast<int>(monoSamples.size()) * 2; // duplicated pairs
    const int payloadFloats = declared * 2;                        // the buffer is twice as large

    QByteArray frame;
    auto put = [&frame](quint32 v) {
        char le[4];
        qToLittleEndian<quint32>(v, le);
        frame.append(le, 4);
    };
    put(0);             // receiver
    put(48000);         // sampleRate
    put(FormatFloat32); // format
    put(0);             // codec
    put(0);             // crc
    put(static_cast<quint32>(declared));
    put(TypeTxAudio);
    put(channelsField);
    frame.append(32, '\0');

    std::vector<float> body(static_cast<size_t>(payloadFloats), 0.0f);
    for (size_t i = 0; i < monoSamples.size(); ++i) {
        body[i * 2] = monoSamples[i];
        body[i * 2 + 1] = monoSamples[i]; // L == R
    }
    if (dirtyTail) {
        // Stale audio from a previous block, which is what the capture actually contained.
        for (int i = declared; i < payloadFloats; ++i) {
            body[static_cast<size_t>(i)] = 0.77f;
        }
    }
    frame.append(reinterpret_cast<const char *>(body.data()), payloadFloats * static_cast<int>(sizeof(float)));
    return frame;
}

std::vector<float> tone(double freqHz, double rateHz, int count) {
    std::vector<float> v(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        v[static_cast<size_t>(i)] = static_cast<float>(0.5 * std::sin(2.0 * kPi * freqHz * i / rateHz));
    }
    return v;
}

} // namespace

// TCI binary audio framing.
//
// Every expectation here is a measurement from a captured WSJT-X <-> AetherSDR session, not a
// reading of the specification. Where the two disagree, the capture wins - see
// docs/tci-server-design.md.
class TestTciAudioFrame : public QObject {
    Q_OBJECT

private slots:
    // ---- header ------------------------------------------------------------------------------

    void headerIsSixtyFourBytes() {
        const QByteArray f = encodeTxChrono(0);
        QCOMPARE(f.size(), HEADER_BYTES);
        QCOMPARE(HEADER_BYTES, 64);
    }

    void headerFieldsMatchTheCapturedBytes() {
        // The exact RX_AUDIO header AetherSDR put on the wire:
        //   00000000 80bb0000 03000000 00000000 00000000 00080000 01000000 02000000
        const std::vector<float> samples(2048, 0.0f);
        const QByteArray f = encodeRxAudio(0, 48000, samples.data(), 2048);
        QCOMPARE(f.left(32).toHex(), QByteArray("0000000080bb0000030000000000000000000000000800000100000002000000"));
    }

    void parsesItsOwnHeaderBack() {
        const std::vector<float> samples(16, 0.25f);
        const QByteArray f = encodeRxAudio(1, 12000, samples.data(), 16);

        Header h;
        QVERIFY(parseHeader(f, &h));
        QCOMPARE(h.receiver, 1u);
        QCOMPARE(h.sampleRate, 12000u);
        QCOMPARE(h.format, static_cast<quint32>(FormatFloat32));
        QCOMPARE(h.length, 16u);
        QCOMPARE(h.type, static_cast<quint32>(TypeRxAudio));
        QCOMPARE(h.channels, 2u);
    }

    void refusesAShortHeader() {
        Header h;
        QVERIFY(!parseHeader(QByteArray(63, '\0'), &h));
    }

    // ---- outbound RX audio ----------------------------------------------------------------------

    void rxAudioPayloadMatchesItsDeclaredLength() {
        // The measured ratio for the server->client direction is exactly 1.0: length counts the
        // floats actually present. The inbound direction is the one that differs.
        const std::vector<float> samples(2048, 0.1f);
        const QByteArray f = encodeRxAudio(0, 48000, samples.data(), 2048);

        Header h;
        QVERIFY(parseHeader(f, &h));
        const int payloadFloats = (f.size() - HEADER_BYTES) / static_cast<int>(sizeof(float));
        QCOMPARE(payloadFloats, static_cast<int>(h.length));
        QCOMPARE(f.size(), 64 + 2048 * 4);
    }

    void rxAudioCarriesSamplesUnaltered() {
        const std::vector<float> samples = tone(1500.0, 48000.0, 512);
        const QByteArray f = encodeRxAudio(0, 48000, samples.data(), 512);

        const float *body = reinterpret_cast<const float *>(f.constData() + HEADER_BYTES);
        for (int i = 0; i < 512; ++i) {
            QCOMPARE(body[i], samples[static_cast<size_t>(i)]);
        }
    }

    // ---- TX_CHRONO -------------------------------------------------------------------------------

    void chronoIsHeaderOnlyAndAsksFor2048Floats() {
        // 2048 floats = 1024 stereo frames = 21.333 ms at 48 kHz, matching audio_stream_samples.
        const QByteArray f = encodeTxChrono(0);
        QCOMPARE(f.size(), HEADER_BYTES);

        Header h;
        QVERIFY(parseHeader(f, &h));
        QCOMPARE(h.type, static_cast<quint32>(TypeTxChrono));
        QCOMPARE(h.length, static_cast<quint32>(CHRONO_FLOATS));
        QCOMPARE(h.length, 2048u);
        QCOMPARE(1000.0 * (h.length / 2) / 48000.0, 21.333333333333332);
    }

    // ---- inbound TX audio: the three rules --------------------------------------------------------

    void takesOnlyTheDeclaredLengthAndIgnoresTheStaleTail() {
        // The captured tail was 81.77% non-zero. Reading it as audio appends garbage.
        const std::vector<float> wanted = tone(1500.0, 48000.0, 1024);
        const QByteArray frame = wsjtxTxFrame(wanted, /*channelsField=*/2, /*dirtyTail=*/true);

        std::vector<float> got;
        QVERIFY(decodeTxAudioToMono(frame, &got));
        QCOMPARE(got.size(), wanted.size());
        for (size_t i = 0; i < wanted.size(); ++i) {
            QCOMPARE(got[i], wanted[i]);
        }
        // Nothing from the 0.77 tail leaked in.
        for (float v : got) {
            QVERIFY(std::fabs(v - 0.77f) > 1e-6f);
        }
    }

    void deduplicatesStereoPairsToMono() {
        const std::vector<float> wanted = tone(1500.0, 48000.0, 1024);
        const QByteArray frame = wsjtxTxFrame(wanted, 2);

        std::vector<float> got;
        QVERIFY(decodeTxAudioToMono(frame, &got));
        // Declared length was 2048 floats; correct output is 1024 mono samples, not 2048.
        QCOMPARE(got.size(), size_t(1024));
    }

    void ignoresTheGarbageChannelsField() {
        // All eight values observed across the 762 captured frames. Decoding must not depend on it.
        const std::vector<float> wanted = tone(1000.0, 48000.0, 256);
        for (quint32 garbage : {2u, 0u, 1818781545u, 2959447138u, 3523932582u, 1017483539u, 4098833031u, 2059641691u}) {
            const QByteArray frame = wsjtxTxFrame(wanted, garbage);
            std::vector<float> got;
            QVERIFY2(decodeTxAudioToMono(frame, &got), qPrintable(QString("channels=%1").arg(garbage)));
            QCOMPARE(got.size(), wanted.size());
            QCOMPARE(got[10], wanted[10]);
        }
    }

    void treatsGenuineMonoAsMono() {
        // A client that sends true mono must not have every other sample thrown away.
        std::vector<float> mono = tone(1000.0, 48000.0, 512);
        QByteArray frame;
        auto put = [&frame](quint32 v) {
            char le[4];
            qToLittleEndian<quint32>(v, le);
            frame.append(le, 4);
        };
        put(0);
        put(48000);
        put(FormatFloat32);
        put(0);
        put(0);
        put(static_cast<quint32>(mono.size()));
        put(TypeTxAudio);
        put(1);
        frame.append(32, '\0');
        frame.append(reinterpret_cast<const char *>(mono.data()), static_cast<int>(mono.size() * sizeof(float)));

        std::vector<float> got;
        QVERIFY(decodeTxAudioToMono(frame, &got));
        QCOMPARE(got.size(), mono.size());
    }

    void detectsDuplicatedStereoAndRejectsIndependentChannels() {
        const std::vector<float> mono = tone(1000.0, 48000.0, 512);
        std::vector<float> duplicated(mono.size() * 2);
        std::vector<float> independent(mono.size() * 2);
        for (size_t i = 0; i < mono.size(); ++i) {
            duplicated[i * 2] = mono[i];
            duplicated[i * 2 + 1] = mono[i];
            independent[i * 2] = mono[i];
            independent[i * 2 + 1] = -mono[i]; // a real second receiver
        }
        QVERIFY(looksLikeDuplicatedStereo(duplicated.data(), static_cast<int>(duplicated.size())));
        QVERIFY(!looksLikeDuplicatedStereo(independent.data(), static_cast<int>(independent.size())));
    }

    void decodesTheInt16Format() {
        // Legal per the spec and handled by the reference server, even though the observed client
        // sends float32.
        QByteArray frame;
        auto put = [&frame](quint32 v) {
            char le[4];
            qToLittleEndian<quint32>(v, le);
            frame.append(le, 4);
        };
        const int monoCount = 128;
        const int declared = monoCount * 2;
        put(0);
        put(48000);
        put(FormatInt16);
        put(0);
        put(0);
        put(static_cast<quint32>(declared));
        put(TypeTxAudio);
        put(2);
        frame.append(32, '\0');
        for (int i = 0; i < monoCount; ++i) {
            const qint16 s = static_cast<qint16>(i * 100);
            char le[2];
            qToLittleEndian<qint16>(s, le);
            frame.append(le, 2);
            frame.append(le, 2); // duplicated pair
        }

        std::vector<float> got;
        QVERIFY(decodeTxAudioToMono(frame, &got));
        QCOMPARE(got.size(), size_t(monoCount));
        QVERIFY(std::fabs(got[1] - (100.0f / 32768.0f)) < 1e-6f);
    }

    // ---- refusals -------------------------------------------------------------------------------

    void refusesAFrameThatIsNotTxAudio() {
        std::vector<float> got;
        QVERIFY(!decodeTxAudioToMono(encodeTxChrono(0), &got));

        const std::vector<float> s(16, 0.0f);
        QVERIFY(!decodeTxAudioToMono(encodeRxAudio(0, 48000, s.data(), 16), &got));
    }

    void refusesAnEmptyOrTruncatedFrame() {
        std::vector<float> got;
        QVERIFY(!decodeTxAudioToMono(QByteArray(), &got));
        QVERIFY(!decodeTxAudioToMono(QByteArray(32, '\0'), &got));
        QVERIFY(!decodeTxAudioToMono(QByteArray(HEADER_BYTES, '\0'), &got)); // header only, length 0
    }

    void clampsADeclaredLengthLongerThanThePayload() {
        // A truncated or lying frame must not read past the buffer.
        QByteArray frame;
        auto put = [&frame](quint32 v) {
            char le[4];
            qToLittleEndian<quint32>(v, le);
            frame.append(le, 4);
        };
        put(0);
        put(48000);
        put(FormatFloat32);
        put(0);
        put(0);
        put(99999); // claims far more than is present
        put(TypeTxAudio);
        put(2);
        frame.append(32, '\0');
        const std::vector<float> body(64, 0.25f);
        frame.append(reinterpret_cast<const char *>(body.data()), 64 * static_cast<int>(sizeof(float)));

        std::vector<float> got;
        QVERIFY(decodeTxAudioToMono(frame, &got));
        QVERIFY(got.size() <= 64);
    }
};

QTEST_MAIN(TestTciAudioFrame)
#include "test_tciaudioframe.moc"
