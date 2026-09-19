#include <QtTest/QtTest>
#include <cstring>
#include <vector>

#include "audio/rawaudioformat.h"

// Wire format for the K4's RAW TX audio modes. The gate on AUD-003: EM0 shipped IEEE floats to a
// radio whose EM0 is S32LE, which put a 1-bit hard limit at ~8000x full scale on the air and
// sounded like static on a quiet microphone. Confirmed on hardware 2026-09-19.
//
// These assert the bytes, not the intent — the defect was invisible to every other observer QK4
// has, because the K4 is the only thing that reads them.
class TestRawAudioFormat : public QObject {
    Q_OBJECT

private slots:
    // --- EM0: the regression gate -------------------------------------------------------

    void em0IsS32NotFloat();
    void em0SilenceStaysSilent();
    void em0PreservesDynamicRange();
    void em0IsLittleEndian();
    void em0DuplicatesMonoToBothChannels();
    void em0RoundTripsThroughDecoderNormalisation();
    void em0HandlesFullScaleExtremes();

    // --- EM1 ----------------------------------------------------------------------------

    void em1IsUnscaledS16();
    void em1DuplicatesMonoToBothChannels();

    // --- Sizing -------------------------------------------------------------------------

    void payloadSizes();

private:
    // Read one little-endian S32 out of a wire buffer, independently of host byte order.
    static qint32 readS32LE(const unsigned char *p) {
        quint32 raw = static_cast<quint32>(p[0]);
        raw |= static_cast<quint32>(p[1]) << 8;
        raw |= static_cast<quint32>(p[2]) << 16;
        raw |= static_cast<quint32>(p[3]) << 24;
        return static_cast<qint32>(raw);
    }

    static qint16 readS16LE(const unsigned char *p) {
        quint16 raw = static_cast<quint16>(p[0]);
        raw |= static_cast<quint16>(p[1]) << 8;
        return static_cast<qint16>(raw);
    }
};

void TestRawAudioFormat::em0IsS32NotFloat() {
    // A ramp spanning the S16 range, including both signs and zero.
    const std::vector<std::int16_t> mono = {0, 1, -1, 1000, -1000, 32767, -32768};
    std::vector<unsigned char> wire(RawAudioFormat::em0BytesFor(static_cast<int>(mono.size())));
    RawAudioFormat::encodeEm0(mono.data(), static_cast<int>(mono.size()), wire.data());

    // Every sample must arrive as an integer scaled onto EM0 full scale — not a float bit pattern.
    for (size_t i = 0; i < mono.size(); i++) {
        const qint32 left = readS32LE(&wire[i * 8]);
        QCOMPARE(left, static_cast<qint32>(mono[i]) * RawAudioFormat::EM0_S16_TO_FULL_SCALE);
    }

    // And explicitly NOT the old encoding. This is the assertion that fails on the pre-fix code:
    // the float32 bit pattern for 1000/32768 is nowhere in the buffer.
    const float asFloatWouldBe = static_cast<float>(1000) / 32768.0f;
    quint32 floatBits = 0;
    std::memcpy(&floatBits, &asFloatWouldBe, sizeof(floatBits));
    for (size_t i = 0; i < mono.size(); i++) {
        QVERIFY(static_cast<quint32>(readS32LE(&wire[i * 8])) != floatBits);
    }
}

void TestRawAudioFormat::em0SilenceStaysSilent() {
    // The exact failure heard on the bench: a microphone dithering at +/-1 LSB. Under the float
    // encoding these became +939,524,096 and -1,207,959,552 — a 2.1-billion swing at random sign
    // across a 131,072 full scale, which is full-scale white noise. Silence must stay silent.
    const std::vector<std::int16_t> mono = {0, 1, -1, 2, -2, 0, 1, -1};
    std::vector<unsigned char> wire(RawAudioFormat::em0BytesFor(static_cast<int>(mono.size())));
    RawAudioFormat::encodeEm0(mono.data(), static_cast<int>(mono.size()), wire.data());

    const int words = static_cast<int>(wire.size() / 4);
    qint64 peak = 0;
    for (int i = 0; i < words; i++)
        peak = qMax(peak, qAbs(static_cast<qint64>(readS32LE(&wire[i * 4]))));

    // A 2-LSB mic stays 2 LSB, and remains a vanishing fraction of full scale.
    QCOMPARE(peak, static_cast<qint64>(2 * RawAudioFormat::EM0_S16_TO_FULL_SCALE));
    QVERIFY(static_cast<double>(peak) / RawAudioFormat::EM0_FULL_SCALE < 0.0001);
}

void TestRawAudioFormat::em0PreservesDynamicRange() {
    // The float encoding compressed 90 dB of range into a 1.13:1 amplitude ratio, leaving only the
    // sign bit carrying the waveform. Assert the range actually survives.
    const std::int16_t quiet = 1;
    const std::int16_t loud = 32767;

    unsigned char quietWire[8] = {};
    unsigned char loudWire[8] = {};
    RawAudioFormat::encodeEm0(&quiet, 1, quietWire);
    RawAudioFormat::encodeEm0(&loud, 1, loudWire);

    const double ratio = static_cast<double>(readS32LE(loudWire)) / static_cast<double>(readS32LE(quietWire));
    QCOMPARE(readS32LE(quietWire), 1 * RawAudioFormat::EM0_S16_TO_FULL_SCALE);
    QCOMPARE(readS32LE(loudWire), 32767 * RawAudioFormat::EM0_S16_TO_FULL_SCALE);
    QVERIFY2(ratio > 30000.0, "EM0 must preserve S16 dynamic range, not collapse it toward 1:1");
}

void TestRawAudioFormat::em0IsLittleEndian() {
    // 258 * 256 = 66048 = 0x00010200, so the byte order is unambiguous.
    const std::int16_t mono = 0x0102;
    unsigned char wire[8] = {};
    RawAudioFormat::encodeEm0(&mono, 1, wire);

    QCOMPARE(wire[0], static_cast<unsigned char>(0x00));
    QCOMPARE(wire[1], static_cast<unsigned char>(0x02));
    QCOMPARE(wire[2], static_cast<unsigned char>(0x01));
    QCOMPARE(wire[3], static_cast<unsigned char>(0x00));

    // Negative values sign-extend into the upper bytes: -1 * 256 = -256 = 0xFFFFFF00.
    const std::int16_t negative = -1;
    RawAudioFormat::encodeEm0(&negative, 1, wire);
    QCOMPARE(wire[0], static_cast<unsigned char>(0x00));
    for (int i = 1; i < 4; i++)
        QCOMPARE(wire[i], static_cast<unsigned char>(0xFF));
}

void TestRawAudioFormat::em0DuplicatesMonoToBothChannels() {
    const std::vector<std::int16_t> mono = {100, -200, 300};
    std::vector<unsigned char> wire(RawAudioFormat::em0BytesFor(static_cast<int>(mono.size())));
    RawAudioFormat::encodeEm0(mono.data(), static_cast<int>(mono.size()), wire.data());

    for (size_t i = 0; i < mono.size(); i++) {
        const qint32 left = readS32LE(&wire[i * 8]);
        const qint32 right = readS32LE(&wire[i * 8 + 4]);
        QCOMPARE(left, static_cast<qint32>(mono[i]) * RawAudioFormat::EM0_S16_TO_FULL_SCALE); // Main
        QCOMPARE(right, left);                                                                // Sub
    }
}

void TestRawAudioFormat::em0RoundTripsThroughDecoderNormalisation() {
    // Apply the RX side's own normalisation to our TX bytes. A full-scale microphone must land at
    // full scale — reaching 1.0 and never exceeding it.
    const std::vector<std::int16_t> mono = {32767, -32768, 0, 16384};
    std::vector<unsigned char> wire(RawAudioFormat::em0BytesFor(static_cast<int>(mono.size())));
    RawAudioFormat::encodeEm0(mono.data(), static_cast<int>(mono.size()), wire.data());

    for (size_t i = 0; i < mono.size(); i++) {
        const float normalised = static_cast<float>(readS32LE(&wire[i * 8])) / RawAudioFormat::EM0_FULL_SCALE;
        QVERIFY(normalised <= 1.0f && normalised >= -1.0f);
    }

    const float fullScale = static_cast<float>(readS32LE(&wire[0])) / RawAudioFormat::EM0_FULL_SCALE;
    QVERIFY2(qAbs(fullScale - 1.0f) < 0.001f,
             "a full-scale mic must reach EM0 full modulation; mic gain clamps at unity and cannot make "
             "up a shortfall here");
}

void TestRawAudioFormat::em0HandlesFullScaleExtremes() {
    // INT16_MIN has no positive counterpart; it must widen cleanly rather than wrap.
    const std::int16_t mono = -32768;
    unsigned char wire[8] = {};
    RawAudioFormat::encodeEm0(&mono, 1, wire);
    QCOMPARE(readS32LE(wire), -8388608); // -2^23, EM0 negative full scale
}

void TestRawAudioFormat::em1IsUnscaledS16() {
    const std::vector<std::int16_t> mono = {0, 1, -1, 32767, -32768};
    std::vector<unsigned char> wire(RawAudioFormat::em1BytesFor(static_cast<int>(mono.size())));
    RawAudioFormat::encodeEm1(mono.data(), static_cast<int>(mono.size()), wire.data());

    for (size_t i = 0; i < mono.size(); i++)
        QCOMPARE(readS16LE(&wire[i * 4]), mono[i]);
}

void TestRawAudioFormat::em1DuplicatesMonoToBothChannels() {
    const std::vector<std::int16_t> mono = {500, -600};
    std::vector<unsigned char> wire(RawAudioFormat::em1BytesFor(static_cast<int>(mono.size())));
    RawAudioFormat::encodeEm1(mono.data(), static_cast<int>(mono.size()), wire.data());

    for (size_t i = 0; i < mono.size(); i++) {
        QCOMPARE(readS16LE(&wire[i * 4]), mono[i]);     // Main
        QCOMPARE(readS16LE(&wire[i * 4 + 2]), mono[i]); // Sub
    }
}

void TestRawAudioFormat::payloadSizes() {
    // Frame sizes are the SL tiers: 240 / 480 / 720 / 1440 samples at 12 kHz.
    for (int samples : {240, 480, 720, 1440}) {
        QCOMPARE(RawAudioFormat::em0BytesFor(samples), samples * 8);
        QCOMPARE(RawAudioFormat::em1BytesFor(samples), samples * 4);
    }
    QCOMPARE(RawAudioFormat::em0BytesFor(0), 0);
    QCOMPARE(RawAudioFormat::em1BytesFor(0), 0);
}

QTEST_MAIN(TestRawAudioFormat)
#include "test_rawaudioformat.moc"
