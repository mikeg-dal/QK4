// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from WSJT-X b4f9a4: get_spectrum_baseline, baseline, ft8_downsample,
// sync8d, ft8b, getcandidates4, ft4_baseline and ft4_decode.
// Attribution, equations and validation: docs/FT8_SNR_REFERENCE.md.
#include "ft8snr.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numeric>
extern "C" {
#include <ft8/encode.h>
#include <ft8/constants.h>
#include <fft/kiss_fftr.h>
}
namespace {
using Complex = std::complex<float>;
constexpr double Pi = 3.14159265358979323846;
struct RealFft {
    kiss_fftr_cfg plan;
    QVector<float> input;
    QVector<kiss_fft_cpx> output;
    explicit RealFft(int n) : plan(kiss_fftr_alloc(n, 0, nullptr, nullptr)), input(n), output(n / 2 + 1) {}
    ~RealFft() { kiss_fftr_free(plan); }
    void run() { kiss_fftr(plan, input.constData(), output.data()); }
};
QVector<double> baseline(const QVector<double> &spectrum, int low, int high) {
    // Same ten-segment tenth-percentile lower envelope and fourth-order fit.
    // Scale x to [-1,1] to condition the normal equations; the fitted polynomial
    // is algebraically unchanged from WSJT-X's unweighted polyfit.
    const int length = (high - low + 1) / 10;
    std::array<std::array<double, 6>, 5> a{};
    const double mid = (low + high) / 2.0, scale = (high - low) / 2.0;
    QVector<double> db(spectrum.size()), result(spectrum.size());
    for (int i = low; i <= high; ++i)
        db[i] = 10 * std::log10(std::max(spectrum[i], 1e-30));
    for (int segment = 0; segment < 10; ++segment) {
        const int begin = low + segment * length;
        auto sorted = db.mid(begin, length);
        std::sort(sorted.begin(), sorted.end());
        const double threshold = sorted[qMax(0, qRound(length * .1) - 1)];
        for (int i = begin; i < begin + length; ++i) {
            if (db[i] > threshold)
                continue;
            std::array<double, 9> powers{1};
            for (int j = 1; j < 9; ++j)
                powers[j] = powers[j - 1] * (i - mid) / scale;
            for (int row = 0; row < 5; ++row) {
                for (int col = 0; col < 5; ++col)
                    a[row][col] += powers[row + col];
                a[row][5] += powers[row] * db[i];
            }
        }
    }
    for (int col = 0; col < 5; ++col) {
        int pivot = col;
        for (int row = col + 1; row < 5; ++row)
            if (std::abs(a[row][col]) > std::abs(a[pivot][col]))
                pivot = row;
        std::swap(a[col], a[pivot]);
        if (std::abs(a[col][col]) < 1e-12)
            return {};
        const double divisor = a[col][col];
        for (int j = col; j <= 5; ++j)
            a[col][j] /= divisor;
        for (int row = 0; row < 5; ++row) {
            if (row == col)
                continue;
            const double factor = a[row][col];
            for (int j = col; j <= 5; ++j)
                a[row][j] -= factor * a[col][j];
        }
    }
    for (int i = low; i <= high; ++i) {
        const double x = (i - mid) / scale;
        double value = a[4][5];
        for (int j = 3; j >= 0; --j)
            value = value * x + a[j][5];
        result[i] = std::pow(10.0, (value + .65) / 10.0);
    }
    return result;
}
} // namespace
struct Ft8Snr::Impl {
    Ft8::Mode mode;
    QVector<double> base, smoothed;
    QVector<kiss_fft_cpx> spectrum;
    bool valid = false;
    explicit Impl(const QVector<float> &samples, Ft8::Mode protocol) : mode(protocol) {
        const bool ft4 = mode == Ft8::Mode::FT4;
        const int nfft = ft4 ? 2304 : 3840;
        const int step = ft4 ? 576 : 1920;
        const int count = ft4 ? 122 : 93;
        const int limit = ft4 ? 72576 : 180000;
        RealFft fft(nfft);
        QVector<double> average(nfft / 2 + 1, 0);
        QVector<float> window(nfft);
        for (int i = 0; i < nfft; ++i) {
            const double p = 2 * Pi * i / nfft;
            window[i] =
                float(.3635819 - .4891775 * std::cos(p) + .1365995 * std::cos(2 * p) - .0106411 * std::cos(3 * p));
        }
        const double norm = ft4 ? 1.0 / 300 : 3840.0 / (300 * std::accumulate(window.begin(), window.end(), 0.0));
        double energy = 0;
        for (float v : samples)
            if (std::isfinite(v))
                energy += double(v) * v;
        if (energy <= 0)
            return;
        for (int j = 0; j < count; ++j) {
            const int begin = j * step;
            if (begin + nfft > limit)
                break;
            for (int i = 0; i < nfft; ++i) {
                const float sample = begin + i < samples.size() ? samples[begin + i] : 0;
                fft.input[i] = std::isfinite(sample) ? float(sample * window[i] * norm) : 0;
            }
            fft.run();
            for (int i = 1; i < average.size(); ++i)
                average[i] += double(fft.output[i].r) * fft.output[i].r + double(fft.output[i].i) * fft.output[i].i;
        }
        if (ft4)
            for (auto &v : average)
                v /= count;
        const double df = 12000.0 / nfft;
        // Match the application's 100–3300 Hz decoded passband; FT4's reference
        // estimator starts at 200 Hz. Do not include the K4's out-of-band zeros.
        const int low = ft4 ? qRound(200 / df) : qRound(100 / df);
        const int high = ft4 ? int(3300 / df) : qRound(3300 / df);
        if (ft4) {
            smoothed.fill(0, average.size());
            for (int i = 8; i < average.size() - 7; ++i)
                for (int k = i - 7; k <= i + 7; ++k)
                    smoothed[i] += average[k] / 15;
        }
        base = baseline(average, low, high);
        if (base.isEmpty())
            return;
        if (ft4) {
            for (int i = low; i <= high; ++i)
                smoothed[i] /= base[i];
        } else {
            RealFft big(192000);
            for (int i = 0; i < qMin(limit, int(samples.size())); ++i)
                big.input[i] = std::isfinite(samples[i]) ? samples[i] : 0;
            big.run();
            spectrum = std::move(big.output);
        }
        valid = true;
    }
    QVector<Complex> downsample(double hz) const {
        constexpr int n = 3200;
        constexpr double df = 12000.0 / 192000;
        const int center = qRound(hz / df);
        const int low = qMax(1, qRound((hz - 1.5 * 6.25) / df));
        const int high = qMin(96000, qRound((hz + 8.5 * 6.25) / df));
        QVector<kiss_fft_cpx> input(n), output(n);
        for (int i = low; i <= high; ++i) {
            float taper = 1;
            if (i - low <= 100)
                taper *= float(.5 * (1 + std::cos((100 - (i - low)) * Pi / 100)));
            if (high - i <= 100)
                taper *= float(.5 * (1 + std::cos((100 - (high - i)) * Pi / 100)));
            const int bin = (i - center + n) % n;
            input[bin] = {spectrum[i].r * taper, spectrum[i].i * taper};
        }
        auto plan = kiss_fft_alloc(n, 1, nullptr, nullptr);
        kiss_fft(plan, input.constData(), output.data());
        kiss_fft_free(plan);
        QVector<Complex> result(n);
        const float scale = 1.0f / std::sqrt(192000.0f * 3200);
        for (int i = 0; i < n; ++i)
            result[i] = Complex(output[i].r, output[i].i) * scale;
        return result;
    }
    static double sync(const QVector<Complex> &audio, int start, double hzOffset = 0) {
        double power = 0;
        constexpr int costas[] = {3, 1, 4, 0, 6, 5, 2};
        for (int i = 0; i < 7; ++i) {
            const Complex delta = std::polar(1.0f, float(-2 * Pi * (costas[i] / 32.0 + hzOffset / 200)));
            for (int block : {0, 36, 72}) {
                const int begin = start + (i + block) * 32;
                if (begin < 0 || begin + 31 >= 2812)
                    continue;
                Complex sum{}, phase{1, 0};
                for (int j = 0; j < 32; ++j) {
                    sum += audio[begin + j] * phase;
                    phase *= delta;
                }
                power += std::norm(sum);
            }
        }
        return power;
    }
};
Ft8Snr::Ft8Snr(const QVector<float> &samples, Ft8::Mode mode) : m(std::make_unique<Impl>(samples, mode)) {}
Ft8Snr::~Ft8Snr() = default;
std::optional<int> Ft8Snr::estimate(const uint8_t *payload, double hz, double startSeconds) {
    if (!m->valid || !std::isfinite(hz) || !std::isfinite(startSeconds))
        return {};
    if (m->mode == Ft8::Mode::FT4) {
        // getcandidates4: find the coarse spectral peak associated with this
        // successfully decoded station, not ft8_lib's unrelated sync score.
        const int center = qRound((hz + 1.5 * 12000 / 576.0) / (12000.0 / 2304));
        double score = 0;
        for (int i = qMax(39, center - 3); i <= qMin(632, center + 3); ++i)
            if (m->smoothed[i] >= m->smoothed[i - 1] && m->smoothed[i] >= m->smoothed[i + 1]) {
                const double denominator = m->smoothed[i - 1] - 2 * m->smoothed[i] + m->smoothed[i + 1];
                const double delta =
                    denominator != 0 ? .5 * (m->smoothed[i - 1] - m->smoothed[i + 1]) / denominator : 0;
                const double peak = m->smoothed[i] - .25 * (m->smoothed[i - 1] - m->smoothed[i + 1]) * delta;
                score = std::max(score, peak);
            }
        if (score <= 0)
            return {};
        const double snr = score > 1 ? 10 * std::log10(score - 1) - 14.8 : -21;
        return qBound(-21, qRound(std::max(-21.0, snr)), 49);
    }
    auto audio = m->downsample(hz);
    int best = qRound(startSeconds * 200);
    double maximum = -1;
    // ft8_lib's half-symbol coarse timing is refined in the same 200 Hz
    // domain used by ft8b/sync8d, before measuring decoded symbol powers.
    const int guess = best;
    for (int i = guess - 16; i <= guess + 16; ++i) {
        const auto p = Impl::sync(audio, i);
        if (p > maximum) {
            maximum = p;
            best = i;
        }
    }
    double correction = 0;
    maximum = -1;
    for (int i = -5; i <= 5; ++i) {
        const auto p = Impl::sync(audio, best, i * .5);
        if (p > maximum) {
            maximum = p;
            correction = i * .5;
        }
    }
    hz += correction;
    audio = m->downsample(hz);
    const int refined = best;
    maximum = -1;
    for (int i = refined - 4; i <= refined + 4; ++i) {
        const auto p = Impl::sync(audio, i);
        if (p > maximum) {
            maximum = p;
            best = i;
        }
    }
    uint8_t tones[FT8_NN];
    ft8_encode(payload, tones);
    double signal = 0;
    for (int i = 0; i < FT8_NN; ++i) {
        const int begin = best + i * 32;
        if (begin < 0 || begin + 31 >= 2812)
            continue;
        const auto delta = std::polar(1.0f, float(-2 * Pi * tones[i] / 32));
        Complex sum{}, phase{1, 0};
        for (int j = 0; j < 32; ++j) {
            sum += audio[begin + j] * phase;
            phase *= delta;
        }
        signal += std::norm(sum);
    }
    const int bin = qRound(hz / 3.125);
    if (bin < 0 || bin >= m->base.size() || m->base[bin] <= 0)
        return {};
    // ft8_decode xbase=10^((baseline_dB-40)/10), then ft8b's normal
    // (nagain=false) xsnr2. Preserve its empirical calibration and -24 floor.
    const double ratio = signal / (m->base[bin] * .0001) / 3e6 - 1;
    const double snr = 10 * std::log10(ratio > .1 ? ratio : .001) - 27;
    return qBound(-24, qRound(std::max(-24.0, snr)), 49);
}
