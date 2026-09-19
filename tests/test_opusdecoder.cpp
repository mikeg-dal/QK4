#include <QtTest/QtTest>
#include <QLoggingCategory>
#include <cmath>

#include "audio/opusdecoder.h"
#include "audio/opusencoder.h"
#include "audio/rawaudioformat.h"

// opusdecoder.cpp logs through qk4Audio, whose definition lives in controllers/audiocontroller.cpp.
// Linking that file would drag AudioEngine, RadioSettings and the controller layer into a decode
// test, so the category is defined here instead. (That the audio module's logging category is
// defined in the controllers module is a wart worth fixing on its own.)
Q_LOGGING_CATEGORY(qk4Audio, "qk4.audio")

// RX audio normalisation for EM0..EM3 — the gate on the 2026-09-19 calibration.
//
// Every mode must obey ONE rule:  out = native × 32 / full_scale, with full scale 2^23 for EM0
// (24-bit in a 32-bit container), 2^15 for EM1/EM2 and 1.0 for EM3. Measured against a stationary
// reference (dummy load, AGC off, fixed AF gain) with tools/k4_audio_capture.py; under that rule
// the four modes agreed to 0.04 dB on real radio audio.
//
// WHY this suite exists. Before it, nothing in the tree pinned these constants. They had been
// tuned by ear at different times against different signals, two of the four were wrong by 6 dB
// in opposite directions, and the build stayed green throughout. A constant nobody can regress is
// worth more than a constant somebody measured once.
class TestOpusDecoder : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // Regression gates — each fails on the pre-calibration constants.
    void em0FullScaleIsTwoToThe23();
    void em1SharesTheCommonBoost();
    void em0AndEm1AgreeExactly();

    void allFourModesAgreeOnOneSignal();
    void mainAndSubStaySeparate();
    void rejectsMalformedPackets();

private:
    OpusDecoder m_decoder;

    // Build an audio payload in the K4's own layout. Offsets per K4Protocol::AudioPacket:
    // [0]=type [1]=version [2]=seq [3]=mode [4..5]=frame size LE [6]=rate code [7..]=data
    static QByteArray payload(int mode, int frameSamples, const QByteArray &data) {
        QByteArray p;
        p.append(static_cast<char>(0x01));
        p.append(static_cast<char>(0x01));
        p.append(static_cast<char>(0x00));
        p.append(static_cast<char>(mode));
        p.append(static_cast<char>(frameSamples & 0xFF));
        p.append(static_cast<char>((frameSamples >> 8) & 0xFF));
        p.append(static_cast<char>(0x00));
        p.append(data);
        return p;
    }

    // Stereo-duplicated raw payloads, matching what the K4 sends.
    static QByteArray em0Data(const QVector<qint32> &mono) {
        QByteArray d(mono.size() * 2 * static_cast<int>(sizeof(qint32)), Qt::Uninitialized);
        qint32 *out = reinterpret_cast<qint32 *>(d.data());
        for (int i = 0; i < mono.size(); i++)
            out[i * 2] = out[i * 2 + 1] = mono[i];
        return d;
    }

    static QByteArray em1Data(const QVector<qint16> &mono) {
        QByteArray d(mono.size() * 2 * static_cast<int>(sizeof(qint16)), Qt::Uninitialized);
        qint16 *out = reinterpret_cast<qint16 *>(d.data());
        for (int i = 0; i < mono.size(); i++)
            out[i * 2] = out[i * 2 + 1] = mono[i];
        return d;
    }

    static QVector<float> decodeMain(OpusDecoder &dec, const QByteArray &packet) {
        const QByteArray out = dec.decodeK4Packet(packet);
        const float *f = reinterpret_cast<const float *>(out.constData());
        const int n = static_cast<int>(out.size() / sizeof(float));
        QVector<float> main;
        main.reserve(n / 2);
        for (int i = 0; i < n; i += 2)
            main.append(f[i]);
        return main;
    }

    static double rms(const QVector<float> &v) {
        if (v.isEmpty())
            return 0.0;
        double acc = 0.0;
        for (float s : v)
            acc += double(s) * double(s);
        return std::sqrt(acc / v.size());
    }
};

void TestOpusDecoder::initTestCase() {
    QVERIFY(m_decoder.initialize(12000, 2));
}

void TestOpusDecoder::em0FullScaleIsTwoToThe23() {
    // A native value of full-scale/32 must come out at exactly 1.0: the K4 ships ~32 dB below
    // its container's full scale, which is what the shared boost restores.
    //
    // Pre-calibration this yielded 2.0, because EM0's full scale was assumed to be 2^17 and no
    // boost was applied. This assertion is the regression gate on that 6 dB error.
    const qint32 native = static_cast<qint32>(RawAudioFormat::EM0_FULL_SCALE / 32.0f); // 262144
    const QVector<float> main = decodeMain(m_decoder, payload(0, 4, em0Data({native, native, native, native})));

    QCOMPARE(main.size(), 4);
    for (float s : main)
        QVERIFY2(std::fabs(s - 1.0f) < 1e-5f, qPrintable(QString("EM0 gave %1, expected 1.0").arg(s)));
}

void TestOpusDecoder::em1SharesTheCommonBoost() {
    // Same rule, 2^15 container. Pre-calibration EM1 used a 16x boost where every other mode used
    // 32x, so this returned 0.5 - the 6 dB in the other direction.
    const qint16 native = 32768 / 32; // 1024
    const QVector<float> main = decodeMain(m_decoder, payload(1, 4, em1Data({native, native, native, native})));

    QCOMPARE(main.size(), 4);
    for (float s : main)
        QVERIFY2(std::fabs(s - 1.0f) < 1e-5f, qPrintable(QString("EM1 gave %1, expected 1.0").arg(s)));
}

void TestOpusDecoder::em0AndEm1AgreeExactly() {
    // The measured relationship: the K4's EM0 stream carries exactly 256x its EM1 stream for the
    // same sound. Both are integer paths, so equality here is exact, not approximate.
    const qint16 em1Native = 100;
    const qint32 em0Native = qint32(em1Native) * RawAudioFormat::EM0_S16_TO_FULL_SCALE;

    const QVector<float> a = decodeMain(m_decoder, payload(0, 2, em0Data({em0Native, em0Native})));
    const QVector<float> b = decodeMain(m_decoder, payload(1, 2, em1Data({em1Native, em1Native})));

    QCOMPARE(a.size(), 2);
    QCOMPARE(b.size(), 2);
    for (int i = 0; i < 2; i++)
        QCOMPARE(a[i], b[i]);
}

void TestOpusDecoder::allFourModesAgreeOnOneSignal() {
    // One continuous 1 kHz tone, expressed in each mode at its own container scale, must decode
    // to the same level everywhere. This is the calibration stated end to end.
    //
    // Two things this has to get right, both of which produced false failures first time:
    //   - An Opus decoder is STATEFUL. Feeding the same packet to one decoder twice (once as EM2,
    //     once as EM3) makes the second decode a continuation of the first, not a repeat. Each
    //     mode therefore gets its own decoder.
    //   - Opus has encoder lookahead, so the first frames of a stream are a ramp. Measure only
    //     after the codec has settled.
    const int frameLen = 480;
    const int frameCount = 12;
    const int warmupFrames = 4; // discarded - covers Opus lookahead
    const double peak = 300.0;  // in S16 terms, nowhere near any clamp

    QVector<QByteArray> em0Frames, em1Frames, opusFrames;
    OpusEncoder enc;
    QVERIFY(enc.initialize(12000, 1, 64000));

    for (int f = 0; f < frameCount; f++) {
        QVector<qint16> s16(frameLen);
        QVector<qint32> s32(frameLen);
        QByteArray monoPcm(frameLen * static_cast<int>(sizeof(qint16)), Qt::Uninitialized);
        qint16 *pcm = reinterpret_cast<qint16 *>(monoPcm.data());
        for (int i = 0; i < frameLen; i++) {
            const int n = f * frameLen + i; // continuous phase across frames
            const double v = peak * std::sin(2.0 * M_PI * 1000.0 * n / 12000.0);
            s16[i] = static_cast<qint16>(std::lround(v));
            s32[i] = static_cast<qint32>(std::lround(v)) * RawAudioFormat::EM0_S16_TO_FULL_SCALE;
            pcm[i] = s16[i];
        }
        em0Frames.append(em0Data(s32));
        em1Frames.append(em1Data(s16));
        const QByteArray encoded = enc.encode(monoPcm, frameLen);
        QVERIFY(!encoded.isEmpty());
        opusFrames.append(encoded);
    }

    // Decode a whole stream through a dedicated decoder and measure past the warm-up.
    auto streamRms = [&](int mode, const QVector<QByteArray> &frames) {
        OpusDecoder dec;
        if (!dec.initialize(12000, 2))
            return 0.0;
        QVector<float> settled;
        for (int f = 0; f < frames.size(); f++) {
            const QVector<float> main = decodeMain(dec, payload(mode, frameLen, frames[f]));
            if (f >= warmupFrames)
                settled += main;
        }
        return rms(settled);
    };

    const double r0 = streamRms(0, em0Frames);
    const double r1 = streamRms(1, em1Frames);
    const double r2 = streamRms(2, opusFrames);
    const double r3 = streamRms(3, opusFrames);

    for (double r : {r0, r1, r2, r3})
        QVERIFY2(r > 0.0, "a mode decoded to silence");

    // EM0 vs EM1 is exact integer maths. The Opus modes carry codec noise, so they get a wider
    // window - still far tighter than the 12 dB spread the pre-calibration constants produced.
    const double exact = 20.0 * std::log10(r0 / r1);
    QVERIFY2(std::fabs(exact) < 0.05, qPrintable(QString("EM0 vs EM1 differ by %1 dB").arg(exact)));

    for (const auto &pair : {qMakePair(r2, QString("EM2")), qMakePair(r3, QString("EM3"))}) {
        const double diff = 20.0 * std::log10(pair.first / r1);
        QVERIFY2(std::fabs(diff) < 0.5, qPrintable(QString("%1 vs EM1 differ by %2 dB").arg(pair.second).arg(diff)));
    }

    // EM2 and EM3 are the same Opus bitstream read through different decode calls, so they must
    // agree very closely with each other even though each carries codec noise versus EM1.
    const double opusPair = 20.0 * std::log10(r2 / r3);
    QVERIFY2(std::fabs(opusPair) < 0.05, qPrintable(QString("EM2 vs EM3 differ by %1 dB").arg(opusPair)));
}

void TestOpusDecoder::mainAndSubStaySeparate() {
    // Output is interleaved [main, sub, main, sub, ...]. A decoder that merged or swapped the
    // channels would still pass a level test, so assert the split explicitly.
    QByteArray d(4 * static_cast<int>(sizeof(qint16)), Qt::Uninitialized);
    qint16 *w = reinterpret_cast<qint16 *>(d.data());
    w[0] = 1024; // main[0]
    w[1] = 512;  // sub[0]
    w[2] = 1024; // main[1]
    w[3] = 512;  // sub[1]

    const QByteArray out = m_decoder.decodeK4Packet(payload(1, 2, d));
    const float *f = reinterpret_cast<const float *>(out.constData());
    QCOMPARE(static_cast<int>(out.size() / sizeof(float)), 4);
    QVERIFY(std::fabs(f[0] - 1.0f) < 1e-5f); // main at full
    QVERIFY(std::fabs(f[1] - 0.5f) < 1e-5f); // sub at half
    QCOMPARE(f[2], f[0]);
    QCOMPARE(f[3], f[1]);
}

void TestOpusDecoder::rejectsMalformedPackets() {
    QVERIFY(m_decoder.decodeK4Packet(QByteArray()).isEmpty());
    QVERIFY(m_decoder.decodeK4Packet(QByteArray(4, '\0')).isEmpty());         // shorter than the header
    QVERIFY(m_decoder.decodeK4Packet(payload(1, 0, QByteArray())).isEmpty()); // header but no audio

    QByteArray wrongType = payload(1, 2, em1Data({100, 100}));
    wrongType[0] = 0x02; // PAN, not audio
    QVERIFY(m_decoder.decodeK4Packet(wrongType).isEmpty());

    QVERIFY(m_decoder.decodeK4Packet(payload(9, 2, em1Data({100, 100}))).isEmpty()); // unknown mode
}

QTEST_MAIN(TestOpusDecoder)
#include "test_opusdecoder.moc"
