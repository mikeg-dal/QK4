#include "dsp/audioupsampler.h"

#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;

double sincNormalized(double x) {
    if (std::fabs(x) < 1e-12) {
        return 1.0;
    }
    const double a = kPi * x;
    return std::sin(a) / a;
}

// Blackman window. Chosen over Hamming for its much deeper stopband (~-74 dB sidelobes vs -41):
// the images this filter has to kill sit directly on top of the passband once they fold.
double blackman(int n, int length) {
    const double t = 2.0 * kPi * n / (length - 1);
    return 0.42 - 0.5 * std::cos(t) + 0.08 * std::cos(2.0 * t);
}

} // namespace

AudioUpsampler::AudioUpsampler(int factor, double inputRateHz, double cutoffHz)
    : m_factor(factor > 1 ? factor : 1), m_history(TAPS_PER_PHASE, 0.0f), m_historyPos(0) {
    const int numTaps = m_factor * TAPS_PER_PHASE;
    const double outputRateHz = inputRateHz * m_factor;

    // Normalised cutoff in cycles per OUTPUT sample. Clamped below Nyquist so a caller passing a
    // nonsensical cutoff cannot produce an unstable-looking (aliased) prototype.
    double fc = cutoffHz / outputRateHz;
    if (fc > 0.5) {
        fc = 0.5;
    }

    std::vector<double> taps(numTaps);
    const double centre = (numTaps - 1) / 2.0;
    double sum = 0.0;
    for (int n = 0; n < numTaps; ++n) {
        taps[n] = 2.0 * fc * sincNormalized(2.0 * fc * (n - centre)) * blackman(n, numTaps);
        sum += taps[n];
    }

    // WHY scale to `factor` and not 1: only one input sample in every `factor` is non-zero after
    // zero-stuffing, so an unscaled filter would attenuate the signal by exactly that ratio.
    const double gain = (sum != 0.0) ? (m_factor / sum) : 1.0;

    m_phases.assign(m_factor, std::vector<float>(TAPS_PER_PHASE, 0.0f));
    for (int n = 0; n < numTaps; ++n) {
        m_phases[n % m_factor][n / m_factor] = static_cast<float>(taps[n] * gain);
    }
}

void AudioUpsampler::process(const float *input, int count, std::vector<float> &out) {
    if (!input || count <= 0) {
        return;
    }
    out.reserve(out.size() + static_cast<size_t>(count) * m_factor);

    for (int i = 0; i < count; ++i) {
        m_history[m_historyPos] = input[i];

        for (int p = 0; p < m_factor; ++p) {
            const std::vector<float> &phase = m_phases[p];
            float acc = 0.0f;
            int h = m_historyPos;
            for (int j = 0; j < TAPS_PER_PHASE; ++j) {
                acc += phase[j] * m_history[h];
                if (--h < 0) {
                    h = TAPS_PER_PHASE - 1;
                }
            }
            out.push_back(acc);
        }

        if (++m_historyPos >= TAPS_PER_PHASE) {
            m_historyPos = 0;
        }
    }
}

void AudioUpsampler::reset() {
    std::fill(m_history.begin(), m_history.end(), 0.0f);
    m_historyPos = 0;
}
