#include <QtTest/QtTest>
#include <cmath>
#include <QSet>
#include <cstring>
#include <vector>

#include "audio/rawaudioformat.h"

// Wire format for the K4's RAW TX audio modes. Two gates in one suite:
//
//   AUD-003 — EM0 shipped IEEE floats to a radio whose EM0 is S32LE integers, which put a 1-bit
//   hard limit well over full scale on the air and sounded like static on a quiet microphone.
//
//   The quantisation fix — mic gain used to be applied and the signal quantised to S16 *before*
//   the wire format was chosen, so EM0 carried 16 bits shifted into a 24-bit container and quiet
//   audio occupied a handful of codes. Confirmed on hardware 2026-09-19: dead air produced a tonal
//   buzz at low mic gain that vanished completely at 50% and above.
//
// These assert the bytes, not the intent — the defect was invisible to every other observer QK4
// has, because the K4 is the only thing that reads them.
class TestRawAudioFormat : public QObject {
    Q_OBJECT

private slots:
    // --- EM0: format -------------------------------------------------------------------

    void em0IsS32NotFloat();
    void em0SilenceStaysSilent();
    void em0PreservesDynamicRange();
    void em0IsLittleEndian();
    void em0DuplicatesMonoToBothChannels();
    void em0RoundTripsThroughDecoderNormalisation();
    void em0ClampsBeyondFullScale();

    // --- EM0: depth — the gate on the quantisation fix ----------------------------------

    void em0KeepsDetailFinerThanAnS16Step();
    void em0QuietSignalUsesFarMoreCodesThanS16();

    // --- EM1 ----------------------------------------------------------------------------

    void em1QuantisesToS16();
    void em1DuplicatesMonoToBothChannels();
    void em1IsSixteenBitByDefinition();

    // --- Sizing -------------------------------------------------------------------------

    void payloadSizes();

private:
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

    // What the pre-fix path produced: quantise to S16 first, then widen into EM0's container.
    static qint32 viaS16Intermediate(float v) {
        const qint16 s16 = static_cast<qint16>(qBound(-1.0f, v, 1.0f) * 32767.0f);
        return static_cast<qint32>(s16) * RawAudioFormat::EM0_S16_TO_FULL_SCALE;
    }
};

void TestRawAudioFormat::em0IsS32NotFloat() {
    const std::vector<float> mono = {0.0f, 0.25f, -0.25f, 0.5f, -0.5f};
    std::vector<unsigned char> wire(RawAudioFormat::em0BytesFor(static_cast<int>(mono.size())));
    RawAudioFormat::encodeEm0(mono.data(), static_cast<int>(mono.size()), wire.data());

    for (size_t i = 0; i < mono.size(); i++)
        QCOMPARE(readS32LE(&wire[i * 8]), RawAudioFormat::toEm0Sample(mono[i]));

    // And explicitly NOT the old encoding: the float32 bit pattern for 0.25 appears nowhere.
    const float quarter = 0.25f;
    quint32 floatBits = 0;
    std::memcpy(&floatBits, &quarter, sizeof(floatBits));
    for (size_t i = 0; i < mono.size(); i++)
        QVERIFY(static_cast<quint32>(readS32LE(&wire[i * 8])) != floatBits);
}

void TestRawAudioFormat::em0SilenceStaysSilent() {
    // The failure heard on the bench: under the float encoding a microphone dithering near zero
    // produced values swinging by billions at random sign, which is full-scale white noise.
    const std::vector<float> mono = {0.0f, 1e-6f, -1e-6f, 0.0f, 2e-6f, -2e-6f};
    std::vector<unsigned char> wire(RawAudioFormat::em0BytesFor(static_cast<int>(mono.size())));
    RawAudioFormat::encodeEm0(mono.data(), static_cast<int>(mono.size()), wire.data());

    const int words = static_cast<int>(wire.size() / 4);
    qint64 peak = 0;
    for (int i = 0; i < words; i++)
        peak = qMax(peak, qAbs(static_cast<qint64>(readS32LE(&wire[i * 4]))));

    QVERIFY2(static_cast<double>(peak) / RawAudioFormat::EM0_FULL_SCALE < 0.0001,
             "near-silent input must stay near silent on the wire");
}

void TestRawAudioFormat::em0PreservesDynamicRange() {
    // The float encoding compressed 90 dB of range into a 1.13:1 amplitude ratio, leaving only the
    // sign bit carrying the waveform. Assert the range actually survives.
    const qint32 quiet = RawAudioFormat::toEm0Sample(1.0f / 32768.0f);
    const qint32 loud = RawAudioFormat::toEm0Sample(1.0f);
    const double ratio = static_cast<double>(loud) / static_cast<double>(quiet);
    QVERIFY2(ratio > 30000.0, "EM0 must preserve the input's dynamic range, not collapse it toward 1:1");
}

void TestRawAudioFormat::em0IsLittleEndian() {
    // 0.5 -> 4194304 = 0x00400000, so the byte order is unambiguous.
    const float mono = 0.5f;
    unsigned char wire[8] = {};
    RawAudioFormat::encodeEm0(&mono, 1, wire);
    QCOMPARE(readS32LE(wire), 4194304);
    QCOMPARE(wire[0], static_cast<unsigned char>(0x00));
    QCOMPARE(wire[1], static_cast<unsigned char>(0x00));
    QCOMPARE(wire[2], static_cast<unsigned char>(0x40));
    QCOMPARE(wire[3], static_cast<unsigned char>(0x00));

    // Negative values sign-extend into the upper bytes: -0.5 -> -4194304 = 0xFFC00000.
    const float negative = -0.5f;
    RawAudioFormat::encodeEm0(&negative, 1, wire);
    QCOMPARE(readS32LE(wire), -4194304);
    QCOMPARE(wire[3], static_cast<unsigned char>(0xFF));
    QCOMPARE(wire[2], static_cast<unsigned char>(0xC0));
}

void TestRawAudioFormat::em0DuplicatesMonoToBothChannels() {
    const std::vector<float> mono = {0.1f, -0.2f, 0.3f};
    std::vector<unsigned char> wire(RawAudioFormat::em0BytesFor(static_cast<int>(mono.size())));
    RawAudioFormat::encodeEm0(mono.data(), static_cast<int>(mono.size()), wire.data());

    for (size_t i = 0; i < mono.size(); i++) {
        const qint32 left = readS32LE(&wire[i * 8]);
        const qint32 right = readS32LE(&wire[i * 8 + 4]);
        QCOMPARE(left, RawAudioFormat::toEm0Sample(mono[i])); // Main
        QCOMPARE(right, left);                                // Sub — duplicate, as the K4 sends
    }
}

void TestRawAudioFormat::em0RoundTripsThroughDecoderNormalisation() {
    // Apply the RX side's own normalisation to our TX bytes. A full-scale input must reach full
    // modulation — reaching 1.0 and never exceeding it.
    const std::vector<float> mono = {1.0f, -1.0f, 0.0f, 0.5f};
    std::vector<unsigned char> wire(RawAudioFormat::em0BytesFor(static_cast<int>(mono.size())));
    RawAudioFormat::encodeEm0(mono.data(), static_cast<int>(mono.size()), wire.data());

    for (size_t i = 0; i < mono.size(); i++) {
        const float normalised = static_cast<float>(readS32LE(&wire[i * 8])) / RawAudioFormat::EM0_FULL_SCALE;
        QVERIFY(normalised <= 1.0f && normalised >= -1.0f);
        QVERIFY2(std::fabs(normalised - mono[i]) < 0.001f, "EM0 round trip must preserve the level");
    }
}

void TestRawAudioFormat::em0ClampsBeyondFullScale() {
    // Input outside ±1.0 must clamp, never wrap to the opposite sign.
    const std::vector<float> mono = {2.0f, -2.0f, 1000.0f, -1000.0f};
    std::vector<unsigned char> wire(RawAudioFormat::em0BytesFor(static_cast<int>(mono.size())));
    RawAudioFormat::encodeEm0(mono.data(), static_cast<int>(mono.size()), wire.data());

    QCOMPARE(readS32LE(&wire[0]), 8388607);  // +2^23 - 1
    QCOMPARE(readS32LE(&wire[8]), -8388608); // -2^23
    QCOMPARE(readS32LE(&wire[16]), 8388607);
    QCOMPARE(readS32LE(&wire[24]), -8388608);
}

void TestRawAudioFormat::em0KeepsDetailFinerThanAnS16Step() {
    // THE gate on the quantisation fix. Two inputs closer together than one S16 step
    // (1/32768 = 3.05e-5) must land on different EM0 codes. Under the old attenuate-then-quantise
    // path they collapsed onto the same value, because everything passed through S16 first.
    // Derived from the S16 grid rather than picked by hand: two points inside the SAME S16 code,
    // half a step apart. An arbitrary pair can straddle a code boundary and pass for the wrong
    // reason, which is what a first draft of this test did.
    const float s16Step = 1.0f / 32767.0f;
    // Inside one code under BOTH truncation (what the old path did) and rounding (what the new
    // one does), so neither can tell them apart while 24 bits resolves them easily.
    const float a = 32.05f * s16Step;
    const float b = 32.45f * s16Step;

    const qint32 nowA = RawAudioFormat::toEm0Sample(a);
    const qint32 nowB = RawAudioFormat::toEm0Sample(b);
    QVERIFY2(nowA != nowB, "EM0 must resolve detail finer than an S16 step");
    QVERIFY(qAbs(nowB - nowA) > 50); // ~102 codes at 24-bit for 0.4 of an S16 step

    // The pre-fix path cannot tell them apart at all. If that ever stops being true this test has
    // lost its meaning, so assert it explicitly rather than leaving it as a claim in a comment.
    QCOMPARE(viaS16Intermediate(a), viaS16Intermediate(b));
}

void TestRawAudioFormat::em0QuietSignalUsesFarMoreCodesThanS16() {
    // Count DISTINCT codes across a narrow quiet range, which is what resolution actually means.
    // Comparing magnitudes would not work: both paths represent the same LEVEL, they differ only
    // in how finely they can step within it - a first draft of this test compared magnitudes and
    // passed even with the old path restored.
    const int steps = 256;
    QSet<qint32> viaNew, viaOld;
    for (int i = 0; i < steps; i++) {
        const float v = 0.001f + (0.0001f * i) / steps; // ~-60 dBFS, spanning ~3 S16 codes
        viaNew.insert(RawAudioFormat::toEm0Sample(v));
        viaOld.insert(viaS16Intermediate(v));
    }

    QVERIFY2(viaNew.size() > viaOld.size() * 20,
             qPrintable(QString("24-bit resolved %1 distinct codes here, the S16 path only %2")
                            .arg(viaNew.size())
                            .arg(viaOld.size())));
}

void TestRawAudioFormat::em1QuantisesToS16() {
    const std::vector<float> mono = {0.0f, 0.5f, -0.5f, 1.0f, -1.0f};
    std::vector<unsigned char> wire(RawAudioFormat::em1BytesFor(static_cast<int>(mono.size())));
    RawAudioFormat::encodeEm1(mono.data(), static_cast<int>(mono.size()), wire.data());

    for (size_t i = 0; i < mono.size(); i++)
        QCOMPARE(readS16LE(&wire[i * 4]), RawAudioFormat::toEm1Sample(mono[i]));

    // Symmetric scaling: +-1.0 maps to +-32767. The extra negative code (-32768) exists in the
    // container but is only reached by input below -1.0, so full-scale audio cannot clip
    // asymmetrically. EM0 follows the same convention.
    QCOMPARE(RawAudioFormat::toEm1Sample(1.0f), static_cast<std::int16_t>(32767));
    QCOMPARE(RawAudioFormat::toEm1Sample(-1.0f), static_cast<std::int16_t>(-32767));
    QCOMPARE(RawAudioFormat::toEm1Sample(2.0f), static_cast<std::int16_t>(32767));   // clamps
    QCOMPARE(RawAudioFormat::toEm1Sample(-2.0f), static_cast<std::int16_t>(-32768)); // clamps
}

void TestRawAudioFormat::em1DuplicatesMonoToBothChannels() {
    const std::vector<float> mono = {0.2f, -0.3f};
    std::vector<unsigned char> wire(RawAudioFormat::em1BytesFor(static_cast<int>(mono.size())));
    RawAudioFormat::encodeEm1(mono.data(), static_cast<int>(mono.size()), wire.data());

    for (size_t i = 0; i < mono.size(); i++) {
        QCOMPARE(readS16LE(&wire[i * 4]), RawAudioFormat::toEm1Sample(mono[i]));     // Main
        QCOMPARE(readS16LE(&wire[i * 4 + 2]), RawAudioFormat::toEm1Sample(mono[i])); // Sub
    }
}

void TestRawAudioFormat::em1IsSixteenBitByDefinition() {
    // Documents the asymmetry honestly: EM1's container IS 16 bits, so unlike EM0 it cannot
    // resolve detail finer than an S16 step. That is a property of the wire format, not a defect,
    // and it is why EM1 is the one mode the quantisation fix cannot improve.
    const float s16Step = 1.0f / 32767.0f;
    QCOMPARE(RawAudioFormat::toEm1Sample(32.05f * s16Step), RawAudioFormat::toEm1Sample(32.45f * s16Step));
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
