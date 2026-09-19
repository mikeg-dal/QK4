#include <QtTest/QtTest>
#include <cmath>
#include <vector>

#include "audio/audiodecimator.h"

// Capture-rate decimation to the K4's 12 kHz — the gate on INT-005.
//
// WHY this suite exists. QK4 opened every microphone at a hardcoded 48 kHz and decimated 4:1.
// Bluetooth microphones do not run at 48 kHz: an AirPods Pro is 24 kHz in HFP. Asking for 48 kHz
// produced a stream that opened cleanly, reported QAudioSource error 0, and delivered ZERO bytes,
// so the operator got a lit PTT indicator and dead air with nothing in the log.
//
// The trap worth remembering is that Qt agreed to the wrong format: isFormatSupported(48 kHz)
// returned TRUE for a device that could not produce it. Measured with tools/audio_device_probe.cpp,
// alternating formats twice to rule out a Bluetooth warm-up effect:
//
//     48000 Hz ->      0 bytes  (twice)
//     24000 Hz -> 144000 bytes  (twice, exactly the expected count)
//
// So the rate must come from the device and the factor from the rate, which is what these pin.
class TestAudioDecimator : public QObject {
    Q_OBJECT

private slots:
    void acceptsMultiplesOfTwelveKilohertz();
    void rejectsRatesItCannotResample();
    void factorMatchesTheRate();
    void twoToOnePreservesASignal();
    void fourToOneMatchesTheOldFixedPath();
    void passthroughAtTwelveKilohertz();
    void outputCountIsExact();
    void handlesPartialAndEmptyInput();

private:
    static std::vector<float> tone(int count, double freqHz, double rateHz, float amp = 0.5f) {
        std::vector<float> v(count);
        for (int i = 0; i < count; i++)
            v[i] = static_cast<float>(amp * std::sin(2.0 * M_PI * freqHz * i / rateHz));
        return v;
    }

    static double rms(const std::vector<float> &v) {
        if (v.empty())
            return 0.0;
        double acc = 0.0;
        for (float s : v)
            acc += double(s) * double(s);
        return std::sqrt(acc / v.size());
    }
};

void TestAudioDecimator::acceptsMultiplesOfTwelveKilohertz() {
    // Every input device observed in the field so far.
    QVERIFY(AudioDecimator::isSupportedRate(12000));
    QVERIFY(AudioDecimator::isSupportedRate(24000)); // AirPods Pro in HFP
    QVERIFY(AudioDecimator::isSupportedRate(48000)); // ordinary sound cards
    QVERIFY(AudioDecimator::isSupportedRate(96000));
}

void TestAudioDecimator::rejectsRatesItCannotResample() {
    // These need rational resampling, which this deliberately does not attempt. They must be
    // REFUSED rather than silently mangled - a wrong-sounding transmission is worse than a
    // device the operator is told to change.
    QVERIFY(!AudioDecimator::isSupportedRate(16000)); // older AirPods, 4:3
    QVERIFY(!AudioDecimator::isSupportedRate(44100)); // 147:40
    QVERIFY(!AudioDecimator::isSupportedRate(22050));
    QVERIFY(!AudioDecimator::isSupportedRate(32000)); // 8:3
    QVERIFY(!AudioDecimator::isSupportedRate(8000));  // below the output rate entirely
    QVERIFY(!AudioDecimator::isSupportedRate(0));
}

void TestAudioDecimator::factorMatchesTheRate() {
    QCOMPARE(AudioDecimator::factorFor(12000), 1);
    QCOMPARE(AudioDecimator::factorFor(24000), 2);
    QCOMPARE(AudioDecimator::factorFor(48000), 4);
    QCOMPARE(AudioDecimator::factorFor(96000), 8);
    QCOMPARE(AudioDecimator::factorFor(44100), 0); // unsupported reports 0, never a wrong factor
}

void TestAudioDecimator::twoToOnePreservesASignal() {
    // The AirPods case. A 1 kHz tone at 24 kHz must survive 2:1 decimation at roughly its own
    // amplitude - well inside the 6 kHz Nyquist of the 12 kHz output, so the box filter does not
    // meaningfully attenuate it.
    const auto in = tone(2400, 1000.0, 24000.0);
    std::vector<float> out(AudioDecimator::outputSampleCount(int(in.size()), 2));
    AudioDecimator::decimate(in.data(), int(in.size()), 2, out.data());

    QCOMPARE(int(out.size()), 1200);
    const double ratio = rms(out) / rms(in);
    QVERIFY2(ratio > 0.9 && ratio < 1.1, qPrintable(QString("1 kHz survived 2:1 at %1 of its input level").arg(ratio)));
}

void TestAudioDecimator::fourToOneMatchesTheOldFixedPath() {
    // Generalising the factor must not change how existing 48 kHz setups sound, so reproduce the
    // previous hardcoded implementation exactly and compare sample for sample.
    const auto in = tone(4800, 800.0, 48000.0);
    std::vector<float> out(AudioDecimator::outputSampleCount(int(in.size()), 4));
    AudioDecimator::decimate(in.data(), int(in.size()), 4, out.data());

    for (size_t i = 0; i < out.size(); i++) {
        float sum = 0.0f;
        int count = 0;
        for (int j = 0; j < 4 && (int(i) * 4 + j) < int(in.size()); j++) {
            sum += in[i * 4 + j];
            count++;
        }
        QCOMPARE(out[i], sum / count);
    }
}

void TestAudioDecimator::passthroughAtTwelveKilohertz() {
    // A device already at the K4's rate must come through untouched, not averaged into itself.
    const auto in = tone(120, 1000.0, 12000.0);
    std::vector<float> out(AudioDecimator::outputSampleCount(int(in.size()), 1));
    AudioDecimator::decimate(in.data(), int(in.size()), 1, out.data());

    QCOMPARE(int(out.size()), int(in.size()));
    for (size_t i = 0; i < in.size(); i++)
        QCOMPARE(out[i], in[i]);
}

void TestAudioDecimator::outputCountIsExact() {
    QCOMPARE(AudioDecimator::outputSampleCount(4800, 4), 1200);
    QCOMPARE(AudioDecimator::outputSampleCount(4800, 2), 2400);
    QCOMPARE(AudioDecimator::outputSampleCount(4800, 1), 4800);
    QCOMPARE(AudioDecimator::outputSampleCount(0, 4), 0);
    // A partial group at the end is dropped, not emitted half-formed.
    QCOMPARE(AudioDecimator::outputSampleCount(4803, 4), 1200);
    // An unsupported rate yields factor 0, which must produce nothing rather than divide by zero.
    QCOMPARE(AudioDecimator::outputSampleCount(4800, 0), 0);
}

void TestAudioDecimator::handlesPartialAndEmptyInput() {
    std::vector<float> out(16, -1.0f);

    // Factor 0 (an unsupported rate reaching here) must be inert, not a crash.
    AudioDecimator::decimate(nullptr, 0, 0, out.data());
    QCOMPARE(out[0], -1.0f);

    const std::vector<float> in = {1.0f, 1.0f, 1.0f};
    AudioDecimator::decimate(in.data(), int(in.size()), 4, out.data());
    QCOMPARE(out[0], -1.0f); // fewer samples than one group: nothing written

    AudioDecimator::decimate(in.data(), int(in.size()), 2, out.data());
    QCOMPARE(out[0], 1.0f); // one complete pair averaged; the odd sample dropped
}

QTEST_APPLESS_MAIN(TestAudioDecimator)
#include "test_audiodecimator.moc"
