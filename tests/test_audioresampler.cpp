#include <QtTest>

#include <cmath>
#include <vector>

#include "audio/audioresampler.h"

// The rational resampler that lets QK4 capture from a 44.1 kHz microphone.
//
// This runs in the TRANSMIT path, so the tests are about what goes on the air: the ratio, the
// level, that a voice-band tone survives, that an out-of-band tone is removed rather than folded
// back as a false signal, and that feeding it in arbitrary blocks gives the same answer as feeding
// it all at once. The last one is the one a block-boundary bug would hide behind.
class TestAudioResampler : public QObject {
    Q_OBJECT

private:
    // Peak magnitude of `freq` present in `samples`, by direct quadrature correlation. Simpler to
    // read than an FFT and enough to measure one tone at a known frequency.
    static double amplitudeAt(const std::vector<float> &samples, double freq, double rate) {
        const double pi = 3.14159265358979323846;
        double re = 0.0;
        double im = 0.0;
        for (size_t i = 0; i < samples.size(); ++i) {
            const double t = static_cast<double>(i) / rate;
            re += samples[i] * std::cos(2.0 * pi * freq * t);
            im += samples[i] * std::sin(2.0 * pi * freq * t);
        }
        return 2.0 * std::sqrt(re * re + im * im) / static_cast<double>(samples.size());
    }

    static std::vector<float> tone(double freq, double rate, int count, double amplitude = 1.0) {
        const double pi = 3.14159265358979323846;
        std::vector<float> out(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i)
            out[static_cast<size_t>(i)] = static_cast<float>(amplitude * std::sin(2.0 * pi * freq * i / rate));
        return out;
    }

    // Runs a whole input through, in one call.
    static std::vector<float> runAll(AudioResampler::Resampler &r, const std::vector<float> &in) {
        std::vector<float> out(static_cast<size_t>(r.maxOutputFor(static_cast<int>(in.size()))));
        const int n = r.process(in.data(), static_cast<int>(in.size()), out.data());
        out.resize(static_cast<size_t>(n));
        return out;
    }

private slots:
    void theFieldRatesReduceToTheExpectedRatios();
    void theOutputRateIsTheRequestedOne();
    void aDcInputKeepsItsLevel();
    void aVoiceToneSurvivesAtTheRightFrequency();
    void aToneAboveTheOutputNyquistIsRemovedNotFoldedBack();
    void blockBoundariesDoNotChangeTheResult();
    void resetClearsTheTail();
    void anUpsamplingRateKeepsItsLevelAndPitch();
    void everyRateADeviceActuallyReportsIsAccepted();
    void aNonsenseRateIsRefusedRatherThanBuilt();
    void badRatesAreRefused();
};

void TestAudioResampler::theFieldRatesReduceToTheExpectedRatios() {
    AudioResampler::Resampler r;

    // The Mac Studio / Windows case: 44100 -> 12000 is 40/147.
    QVERIFY(r.configure(44100, 12000));
    QCOMPARE(r.interpolation(), 40);
    QCOMPARE(r.decimation(), 147);

    // Older AirPods, called out in audiodecimator.h as 4:3.
    QVERIFY(r.configure(16000, 12000));
    QCOMPARE(r.interpolation(), 3);
    QCOMPARE(r.decimation(), 4);
}

void TestAudioResampler::theOutputRateIsTheRequestedOne() {
    // One second of input must give one second of output, or the transmission drifts against the
    // radio's clock. Allowed to be off by the couple of samples the phase carries.
    AudioResampler::Resampler r;
    QVERIFY(r.configure(44100, 12000));

    const std::vector<float> in(44100, 0.0f);
    const std::vector<float> out = runAll(r, in);
    QVERIFY2(std::abs(static_cast<int>(out.size()) - 12000) <= 2,
             qUtf8Printable(QStringLiteral("got %1 samples, wanted 12000").arg(out.size())));
}

void TestAudioResampler::aDcInputKeepsItsLevel() {
    // The interpolation gain is the easy thing to get wrong here: without the L factor the output
    // is 40x too quiet at 44.1 kHz, which on the air is a microphone that "does not work".
    AudioResampler::Resampler r;
    QVERIFY(r.configure(44100, 12000));

    const std::vector<float> in(44100, 0.5f);
    const std::vector<float> out = runAll(r, in);
    QVERIFY(out.size() > 1000);

    // Skip the filter's startup transient.
    double sum = 0.0;
    int counted = 0;
    for (size_t i = 200; i < out.size(); ++i, ++counted)
        sum += out[i];
    const double mean = sum / counted;
    QVERIFY2(std::abs(mean - 0.5) < 0.01, qUtf8Printable(QStringLiteral("DC came out at %1").arg(mean)));
}

void TestAudioResampler::aVoiceToneSurvivesAtTheRightFrequency() {
    // 1 kHz is squarely in the voice band and must come through at close to its input level, at the
    // same frequency. A resampler that got the ratio wrong would shift the pitch.
    AudioResampler::Resampler r;
    QVERIFY(r.configure(44100, 12000));

    const std::vector<float> in = tone(1000.0, 44100.0, 44100, 0.5);
    const std::vector<float> out = runAll(r, in);

    const std::vector<float> steady(out.begin() + 200, out.end());
    const double at1k = amplitudeAt(steady, 1000.0, 12000.0);
    QVERIFY2(std::abs(at1k - 0.5) < 0.05, qUtf8Printable(QStringLiteral("1 kHz came out at %1").arg(at1k)));
}

void TestAudioResampler::aToneAboveTheOutputNyquistIsRemovedNotFoldedBack() {
    // THE TEST THAT MATTERS. 8 kHz cannot be represented at 12 kHz output; without an anti-alias
    // filter it folds down to 4 kHz and is transmitted as a tone that was never spoken. Removing it
    // is the resampler's real job - the rate arithmetic is the easy half.
    AudioResampler::Resampler r;
    QVERIFY(r.configure(44100, 12000));

    const std::vector<float> in = tone(8000.0, 44100.0, 44100, 0.5);
    const std::vector<float> out = runAll(r, in);
    const std::vector<float> steady(out.begin() + 400, out.end());

    // 12000 - 8000 = 4000: where it would land if it aliased.
    // 0.001 against a 0.5 input is -54 dB. Measured at -78 dB with the tap count in the header, so
    // this has real margin and still fails loudly if the prototype is ever shortened: at 32 taps
    // per phase the same tone comes back at -36 dB.
    const double alias = amplitudeAt(steady, 4000.0, 12000.0);
    QVERIFY2(alias < 0.001, qUtf8Printable(QStringLiteral("8 kHz aliased to 4 kHz at amplitude %1").arg(alias)));

    // And nothing much anywhere else either.
    double worst = 0.0;
    for (double f = 100.0; f < 5900.0; f += 100.0)
        worst = std::max(worst, amplitudeAt(steady, f, 12000.0));
    QVERIFY2(worst < 0.01, qUtf8Printable(QStringLiteral("out-of-band tone left %1 in the passband").arg(worst)));
}

void TestAudioResampler::blockBoundariesDoNotChangeTheResult() {
    // Audio arrives in blocks whose length has nothing to do with L or M. A resampler that resets
    // its history or its phase per block puts a discontinuity at every boundary - a buzz at the
    // block rate, which is exactly the kind of fault that passes a one-shot test.
    const std::vector<float> in = tone(900.0, 44100.0, 20000, 0.4);

    AudioResampler::Resampler whole;
    QVERIFY(whole.configure(44100, 12000));
    const std::vector<float> reference = runAll(whole, in);

    // Deliberately awkward sizes: none is a multiple of 147, and they vary call to call.
    const int sizes[] = {1, 17, 256, 1000, 333, 4096, 61};
    AudioResampler::Resampler streamed;
    QVERIFY(streamed.configure(44100, 12000));

    std::vector<float> collected;
    size_t pos = 0;
    int which = 0;
    while (pos < in.size()) {
        const size_t take = std::min(static_cast<size_t>(sizes[which % 7]), in.size() - pos);
        std::vector<float> chunk(in.begin() + static_cast<long>(pos), in.begin() + static_cast<long>(pos + take));
        std::vector<float> out(static_cast<size_t>(streamed.maxOutputFor(static_cast<int>(take))));
        const int n = streamed.process(chunk.data(), static_cast<int>(take), out.data());
        collected.insert(collected.end(), out.begin(), out.begin() + n);
        pos += take;
        ++which;
    }

    QCOMPARE(collected.size(), reference.size());
    for (size_t i = 0; i < reference.size(); ++i) {
        QVERIFY2(
            std::abs(collected[i] - reference[i]) < 1e-6,
            qUtf8Printable(QStringLiteral("sample %1 differs: %2 vs %3").arg(i).arg(collected[i]).arg(reference[i])));
    }
}

void TestAudioResampler::resetClearsTheTail() {
    // A new transmission must not open with the end of the last one.
    AudioResampler::Resampler r;
    QVERIFY(r.configure(44100, 12000));

    const std::vector<float> loud = tone(1000.0, 44100.0, 4410, 1.0);
    runAll(r, loud);
    r.reset();

    const std::vector<float> silence(4410, 0.0f);
    const std::vector<float> out = runAll(r, silence);
    for (float s : out)
        QVERIFY2(std::abs(s) < 1e-6, qUtf8Printable(QStringLiteral("tail leaked: %1").arg(s)));
}

void TestAudioResampler::anUpsamplingRateKeepsItsLevelAndPitch() {
    // 11025 Hz is the worst rate real hardware reports: L=160, M=147, so it is the only common case
    // where the ratio goes UP and the filter is sized by L rather than M. Every other signal test
    // here downsamples, which exercises the opposite branch of the cutoff calculation - a sign error
    // there would pass all of them and ruin this one.
    AudioResampler::Resampler r;
    QVERIFY(r.configure(11025, 12000));
    QCOMPARE(r.interpolation(), 160);
    QCOMPARE(r.decimation(), 147);

    const std::vector<float> dc(11025, 0.5f);
    const std::vector<float> flat = runAll(r, dc);
    QVERIFY(flat.size() > 1000);
    double sum = 0.0;
    int counted = 0;
    for (size_t i = 400; i < flat.size(); ++i, ++counted)
        sum += flat[i];
    const double mean = sum / counted;
    QVERIFY2(std::abs(mean - 0.5) < 0.01, qUtf8Printable(QStringLiteral("DC came out at %1").arg(mean)));

    r.reset();
    const std::vector<float> in = tone(1000.0, 11025.0, 11025, 0.5);
    const std::vector<float> out = runAll(r, in);
    const std::vector<float> steady(out.begin() + 400, out.end());
    const double at1k = amplitudeAt(steady, 1000.0, 12000.0);
    QVERIFY2(std::abs(at1k - 0.5) < 0.05, qUtf8Printable(QStringLiteral("1 kHz came out at %1").arg(at1k)));
}

void TestAudioResampler::everyRateADeviceActuallyReportsIsAccepted() {
    // The whole point of the MAX_INTERPOLATION guard is that it must never turn away a real device.
    // These are the rates sound hardware reports; all of them have to configure.
    const int rates[] = {8000, 11025, 16000, 22050, 32000, 44100, 48000, 88200, 96000, 176400, 192000};
    for (int rate : rates) {
        AudioResampler::Resampler r;
        QVERIFY2(r.configure(rate, 12000), qUtf8Printable(QStringLiteral("refused %1 Hz").arg(rate)));
        QVERIFY2(r.interpolation() <= AudioResampler::MAX_INTERPOLATION,
                 qUtf8Printable(QStringLiteral("%1 Hz needs L=%2").arg(rate).arg(r.interpolation())));
    }
}

void TestAudioResampler::aNonsenseRateIsRefusedRatherThanBuilt() {
    // 44101 Hz shares no useful factors with 12000, so it would need L=12000: a 768,000-tap filter
    // and 3 MB of coefficients, built on the first PTT press. No device reports such a rate, and
    // stalling the transmit path to build a filter for one is worse than declining it.
    AudioResampler::Resampler r;
    QVERIFY(!r.configure(44101, 12000));
    QVERIFY(!r.isConfigured());

    // And an unconfigured resampler is inert, not dangerous.
    float out[8] = {};
    const std::vector<float> in(8, 1.0f);
    QCOMPARE(r.process(in.data(), 8, out), 0);
    QCOMPARE(r.maxOutputFor(8), 0);
}

void TestAudioResampler::badRatesAreRefused() {
    AudioResampler::Resampler r;
    QVERIFY(!r.configure(0, 12000));
    QVERIFY(!r.configure(44100, 0));
    QVERIFY(!r.configure(-44100, 12000));
    QVERIFY(!r.isConfigured());

    // And an unconfigured resampler produces nothing rather than reading uninitialised taps.
    float out[16] = {};
    const std::vector<float> in(16, 1.0f);
    QCOMPARE(r.process(in.data(), 16, out), 0);
}

QTEST_MAIN(TestAudioResampler)
#include "test_audioresampler.moc"
