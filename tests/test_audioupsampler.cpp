#include <QtTest>

#include <cmath>
#include <vector>

#include "dsp/audioupsampler.h"

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kInRate = 12000.0;  // K4 receive audio
constexpr double kOutRate = 48000.0; // what WSJT-X's TCI always assumes
constexpr int kFactor = 4;

// Amplitude of one frequency in a buffer, via a single-bin DFT with a Hann window.
// Windowed so an off-bin tone does not smear into the stopband measurement; the 2.0 corrects the
// half-spectrum and the extra 2.0 the window's 0.5 coherent gain.
double amplitudeAt(const std::vector<float> &x, double freqHz, double rateHz, int skip = 0) {
    const int n = static_cast<int>(x.size()) - skip;
    if (n <= 0) {
        return 0.0;
    }
    double re = 0.0;
    double im = 0.0;
    double winSum = 0.0;
    for (int i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * i / (n - 1));
        const double a = 2.0 * kPi * freqHz * i / rateHz;
        re += x[skip + i] * w * std::cos(a);
        im -= x[skip + i] * w * std::sin(a);
        winSum += w;
    }
    return 2.0 * std::sqrt(re * re + im * im) / winSum;
}

std::vector<float> sine(double freqHz, double rateHz, int count, float amplitude = 0.5f) {
    std::vector<float> v(count);
    for (int i = 0; i < count; ++i) {
        v[i] = static_cast<float>(amplitude * std::sin(2.0 * kPi * freqHz * i / rateHz));
    }
    return v;
}

std::vector<float> upsample(const std::vector<float> &in) {
    AudioUpsampler up(kFactor, kInRate);
    std::vector<float> out;
    up.process(in.data(), static_cast<int>(in.size()), out);
    return out;
}

double db(double v) {
    return 20.0 * std::log10(std::max(v, 1e-12));
}

} // namespace

// 12 kHz -> 48 kHz interpolation for the TCI receive path.
//
// WSJT-X ignores a declared audio_samplerate and always treats TCI audio as 48 kHz, so the K4's
// 12 kHz stream has to be interpolated rather than simply announced. The thresholds below are the
// acceptance criteria measured on the Python prototype during design, which was separately proven
// to preserve all 14 jt9 decodes of the stock FT8 sample with unchanged SNR, DT and frequency.
// See docs/tci-server-design.md.
class TestAudioUpsampler : public QObject {
    Q_OBJECT

private slots:
    // ---- shape ---------------------------------------------------------------------------

    void producesFactorTimesAsManySamples() {
        const std::vector<float> in(1000, 0.0f);
        QCOMPARE(static_cast<int>(upsample(in).size()), 1000 * kFactor);
    }

    void blockBoundariesAreNotDiscontinuities() {
        // Streaming in two halves must equal one whole call, or every TCI frame boundary would
        // inject a click.
        const std::vector<float> in = sine(1500.0, kInRate, 2048);

        const std::vector<float> whole = upsample(in);

        AudioUpsampler up(kFactor, kInRate);
        std::vector<float> split;
        up.process(in.data(), 1000, split);
        up.process(in.data() + 1000, 1048, split);

        QCOMPARE(split.size(), whole.size());
        for (size_t i = 0; i < whole.size(); ++i) {
            QVERIFY2(std::fabs(split[i] - whole[i]) < 1e-6f, qPrintable(QString("sample %1 differs: %2 vs %3")
                                                                            .arg(i)
                                                                            .arg(static_cast<double>(split[i]))
                                                                            .arg(static_cast<double>(whole[i]))));
        }
    }

    void resetClearsHistory() {
        const std::vector<float> in = sine(1500.0, kInRate, 512);
        AudioUpsampler up(kFactor, kInRate);

        std::vector<float> first;
        up.process(in.data(), static_cast<int>(in.size()), first);

        up.reset();
        std::vector<float> second;
        up.process(in.data(), static_cast<int>(in.size()), second);

        QCOMPARE(first.size(), second.size());
        for (size_t i = 0; i < first.size(); ++i) {
            QCOMPARE(first[i], second[i]);
        }
    }

    // ---- gain and phase ------------------------------------------------------------------

    void dcGainIsUnity() {
        // Zero-stuffing costs exactly `factor` in amplitude; the prototype is scaled to put it
        // back. A regression here would silently attenuate every TCI listener by 12 dB.
        const std::vector<float> in(2048, 1.0f);
        const std::vector<float> out = upsample(in);

        double sum = 0.0;
        const int settle = 512; // past the filter's transient
        for (size_t i = settle; i < out.size(); ++i) {
            sum += out[i];
        }
        const double mean = sum / (out.size() - settle);
        QVERIFY2(std::fabs(mean - 1.0) < 0.001, qPrintable(QString("DC gain %1").arg(mean)));
    }

    void impulseResponseIsSymmetric() {
        // Linear phase is why the decoder sees no dispersion. Symmetry is the whole property.
        //
        // An impulse at input index k emerges at output index k * factor, because out[i*L+p]
        // sums phase[p][j] * x[i-j] and only j = i-k contributes. So out[impulseAt*factor + n]
        // is tap n of the prototype filter.
        constexpr int impulseAt = 8;
        std::vector<float> impulse(impulseAt + AudioUpsampler::TAPS_PER_PHASE + 8, 0.0f);
        impulse[impulseAt] = 1.0f;
        const std::vector<float> out = upsample(impulse);

        const int numTaps = kFactor * AudioUpsampler::TAPS_PER_PHASE;
        const int start = impulseAt * kFactor;
        QVERIFY(static_cast<int>(out.size()) >= start + numTaps);

        for (int i = 0; i < numTaps / 2; ++i) {
            const float lo = out[start + i];
            const float hi = out[start + numTaps - 1 - i];
            QVERIFY2(std::fabs(lo - hi) < 1e-6f, qPrintable(QString("tap %1 vs %2: %3 vs %4")
                                                                .arg(i)
                                                                .arg(numTaps - 1 - i)
                                                                .arg(static_cast<double>(lo))
                                                                .arg(static_cast<double>(hi))));
        }
    }

    // ---- frequency response --------------------------------------------------------------

    void passbandIsFlatAcrossTheDigitalModeWindow() {
        // FT8 lives around 200-2800 Hz. Any tilt here shows up as unequal tone amplitudes.
        double minDb = 1e9;
        double maxDb = -1e9;
        for (double f : {200.0, 500.0, 1000.0, 1500.0, 2000.0, 2500.0, 2800.0}) {
            const std::vector<float> out = upsample(sine(f, kInRate, 4096));
            const double a = amplitudeAt(out, f, kOutRate, 1024);
            minDb = std::min(minDb, db(a / 0.5));
            maxDb = std::max(maxDb, db(a / 0.5));
        }
        const double ripple = maxDb - minDb;
        QVERIFY2(ripple < 0.1, qPrintable(QString("passband ripple %1 dB").arg(ripple)));
        QVERIFY2(std::fabs(maxDb) < 0.1, qPrintable(QString("passband gain %1 dB").arg(maxDb)));
    }

    void toneFrequencyIsPreserved() {
        // A tone must come out where it went in. Getting this wrong is how a mis-declared rate
        // manifests, and it is the failure that cost a long debugging session during design.
        const std::vector<float> out = upsample(sine(1500.0, kInRate, 4096));
        const double wanted = amplitudeAt(out, 1500.0, kOutRate, 1024);
        for (double wrong : {375.0, 750.0, 3000.0, 6000.0}) {
            const double other = amplitudeAt(out, wrong, kOutRate, 1024);
            QVERIFY2(db(other / wanted) < -60.0,
                     qPrintable(QString("energy at %1 Hz is %2 dBc").arg(wrong).arg(db(other / wanted))));
        }
    }

    void imagesAreRejected() {
        // Zero-stuffing replicates the spectrum at every multiple of the INPUT rate. For a 1500 Hz
        // tone at 12 kHz those land at 10500, 13500 and 22500 Hz. A boxcar leaves them ~10 dB down;
        // they must be inaudible here or a digital signal is corrupted.
        const std::vector<float> out = upsample(sine(1500.0, kInRate, 4096));
        const double fundamental = amplitudeAt(out, 1500.0, kOutRate, 1024);

        for (double image : {10500.0, 13500.0, 22500.0}) {
            const double a = amplitudeAt(out, image, kOutRate, 1024);
            const double rejection = db(a / fundamental);
            QVERIFY2(rejection < -100.0,
                     qPrintable(QString("image at %1 Hz only %2 dBc down").arg(image).arg(rejection)));
        }
    }

    void stopbandIsDeepAcrossTheWholeImageRegion() {
        // Sweep rather than spot-check: a design error can put a lobe between the named images.
        const std::vector<float> out = upsample(sine(1500.0, kInRate, 4096));
        const double fundamental = amplitudeAt(out, 1500.0, kOutRate, 1024);

        double worst = -1e9;
        double worstAt = 0.0;
        for (double f = 7000.0; f <= 23000.0; f += 250.0) {
            const double r = db(amplitudeAt(out, f, kOutRate, 1024) / fundamental);
            if (r > worst) {
                worst = r;
                worstAt = f;
            }
        }
        QVERIFY2(worst < -100.0, qPrintable(QString("worst stopband %1 dBc at %2 Hz").arg(worst).arg(worstAt)));
    }
};

QTEST_MAIN(TestAudioUpsampler)
#include "test_audioupsampler.moc"
