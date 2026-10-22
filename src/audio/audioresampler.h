#ifndef AUDIORESAMPLER_H
#define AUDIORESAMPLER_H

#include <cmath>
#include <cstddef>
#include <vector>

// Rational resampling to the K4's 12 kHz TX rate, for the capture rates AudioDecimator cannot take.
//
// Pure logic with no Qt and no audio device, so the arithmetic and the filter response can be
// unit-tested without hardware - the same shape as audio/audiodecimator.h.
//
// WHY THIS EXISTS. AudioDecimator handles only exact integer multiples of 12 kHz, so a microphone
// at 44.1 kHz was refused outright: no capture, no TX audio, and - because a local PTT press keys
// the K4 by STREAMING AUDIO rather than by sending TX; - no transmission at all. Reproduced on a
// Mac Studio on 2026-10-18: four PTT presses, the transmitter taken and released each time, and
// nothing whatever on the wire. 44.1 kHz is the default for a great deal of consumer audio, so this
// was not an edge case.
//
// WHY THE INTEGER PATH IS LEFT ALONE. AudioDecimator's box filter is crude, but it is what every
// working 48 kHz and 24 kHz setup has always sounded like, and that header says plainly that
// generalising the factor must not quietly change them. So 48000 and 24000 still go through
// AudioDecimator untouched; this runs only for the rates that previously did not work at all.
// Nobody's audio changes - some people's starts existing.
//
// THE METHOD. Upsample by L, low-pass, decimate by M, with L/M = outRate/inRate reduced by their
// GCD, evaluated polyphase so only the taps that land on a real sample are computed. 44.1 kHz ->
// 12 kHz is 40/147; 16 kHz -> 12 kHz is 3/4. The prototype is a Blackman-windowed sinc: ~74 dB
// stopband, which is far more than voice through a 12 kHz channel needs, and no Bessel function to
// get wrong.
namespace AudioResampler {

// Taps per polyphase branch. CHOSEN BY MEASUREMENT, not by a rule of thumb: the 44.1 kHz case
// needs L=40, M=147, and a cutoff that narrow needs a long prototype. Rejection of an 8 kHz tone
// folding to 4 kHz, measured by test_audioresampler:
//
//     32 taps/phase  ->  -36 dB     far too little; the tone is plainly audible
//     48 taps/phase  ->  -77 dB     the knee - this is the Blackman window's own floor
//     64 taps/phase  ->  -78 dB
//     96 taps/phase  ->  -91 dB
//
// 64 sits comfortably past the knee with margin and costs 64 multiply-adds per OUTPUT sample -
// 768k/second at 12 kHz, which is nothing beside the Opus encode already on this thread. Going
// further buys numbers nobody can hear.
inline constexpr int TAPS_PER_PHASE = 64;

// WHY the branch can be longer than TAPS_PER_PHASE: the transition band's width, measured against
// the OUTPUT rate, shrinks with branch length and grows with M/L - each output sample steps M/L
// input samples. A fixed branch is therefore ample at 44.1 kHz (M/L = 3.7) and far too short at
// 176.4 kHz (M/L = 14.7), where an 8 kHz tone came back at -14 dB. Spanning this many output
// periods keeps the transition the same width at every rate; at 44.1 kHz and below it asks for
// fewer than TAPS_PER_PHASE, so every rate already measured above is unchanged.
inline constexpr int OUTPUT_PERIODS_SPANNED = 16;

// Bounds the per-output cost the same way MAX_INTERPOLATION bounds the prototype: 1024 covers
// rates up to ~750 kHz, and a rate past that is refused rather than run on the transmit path.
inline constexpr int MAX_TAPS_PER_PHASE = 1024;

// The passband edge, as a fraction of the output Nyquist. The remaining 10% is transition band, so
// the stopband is fully attenuated by the time aliasing could fold anything back. For a 12 kHz
// output that puts the corner at 5.4 kHz - well above the ~3 kHz a voice channel uses.
inline constexpr double PASSBAND_FRACTION = 0.9;

inline constexpr int gcd(int a, int b) {
    return b == 0 ? a : gcd(b, a % b);
}

// The largest interpolation factor worth building a filter for. The prototype is TAPS_PER_PHASE * L
// taps long, so L is what decides the cost, and L is 12000 divided by the GCD of the two rates - it
// explodes when a rate shares almost no factors with 12000.
//
// Every rate a sound device actually reports is cheap. Measured: 48000 and 96000 give L=1, 16000
// and 32000 give L=3, 44100 gives L=40, 22050 gives L=80, and the worst standard rate, 11025, gives
// L=160. A rate like 44101 Hz, which no device reports, gives L=12000 - a 768,000-tap filter, 3 MB
// of coefficients, and a sin/cos loop that long running on the first PTT press.
//
// 320 is twice the worst real rate, so nothing a device can report is refused, and a nonsense rate
// is turned away instead of stalling the transmit path to build a filter for it.
inline constexpr int MAX_INTERPOLATION = 320;

inline constexpr int tapsPerPhaseFor(int l, int m) {
    const int spanned = (OUTPUT_PERIODS_SPANNED * m + l - 1) / l;
    return spanned > TAPS_PER_PHASE ? spanned : TAPS_PER_PHASE;
}

// The one rule for whether configure() will accept a pair of rates, so a caller can ask without
// building a filter - MicRate decides what to tell the operator from this.
inline constexpr bool canConvert(int inputRate, int outputRate) {
    if (inputRate <= 0 || outputRate <= 0)
        return false;
    const int g = gcd(inputRate, outputRate);
    const int l = outputRate / g;
    return l <= MAX_INTERPOLATION && tapsPerPhaseFor(l, inputRate / g) <= MAX_TAPS_PER_PHASE;
}

// Resamples a stream from one rate to another, holding the filter state ACROSS CALLS.
//
// Streaming state is the whole point: audio arrives in blocks whose length has no relationship to
// L or M, and a resampler that restarted per block would put a discontinuity at every boundary -
// a buzz at the block rate. It also means process() may return a different count for the same
// input length on consecutive calls, which is correct and which callers must not assume away.
class Resampler {
public:
    // False when canConvert() refuses the pair. Leaves the resampler unconfigured, so process()
    // produces nothing and the caller reports the rate as unusable.
    bool configure(int inputRate, int outputRate) {
        if (!canConvert(inputRate, outputRate)) {
            m_l = 0;
            m_m = 0;
            return false;
        }

        const int g = gcd(inputRate, outputRate);
        m_l = outputRate / g;
        m_m = inputRate / g;
        m_tapsPerPhase = tapsPerPhaseFor(m_l, m_m);
        m_inputRate = inputRate;
        m_outputRate = outputRate;

        buildPrototype();
        reset();
        return true;
    }

    bool isConfigured() const { return m_l > 0 && m_m > 0; }
    int interpolation() const { return m_l; }
    int decimation() const { return m_m; }
    int inputRate() const { return m_inputRate; }
    int outputRate() const { return m_outputRate; }
    int tapsPerPhase() const { return m_tapsPerPhase; }

    // Clears the filter history and the phase. Call when the stream restarts - a new transmission,
    // a device change - so the tail of the last one cannot bleed into the start of the next.
    void reset() {
        m_history.assign(static_cast<size_t>(m_tapsPerPhase), 0.0f);
        m_writePos = 0;
        m_offset = 0;
    }

    // An upper bound on what process() can write for `inputSamples`, for sizing the output buffer.
    // Deliberately generous by two: the exact count depends on the phase carried in from the last
    // call, and a caller that under-allocates by one corrupts memory for a saving of 8 bytes.
    int maxOutputFor(int inputSamples) const {
        if (!isConfigured() || inputSamples <= 0)
            return 0;
        return (inputSamples * m_l) / m_m + 2;
    }

    // Writes up to maxOutputFor(inputSamples) samples and returns how many. `out` must have that
    // much room.
    int process(const float *in, int inputSamples, float *out) {
        if (!isConfigured() || in == nullptr || out == nullptr || inputSamples <= 0)
            return 0;

        int produced = 0;
        for (int i = 0; i < inputSamples; ++i) {
            m_history[static_cast<size_t>(m_writePos)] = in[i];
            m_writePos = (m_writePos + 1) % m_tapsPerPhase;

            // Every output whose position on the upsampled grid falls within this input sample's
            // span of L. m_offset is that position measured from the current sample's base, so the
            // branch index IS the offset - see the derivation in the class comment.
            while (m_offset < m_l) {
                out[produced++] = filterAt(m_offset);
                m_offset += m_m;
            }
            m_offset -= m_l; // rebase onto the next input sample
        }
        return produced;
    }

private:
    // One polyphase branch: every L-th tap, starting at `branch`, against the newest m_tapsPerPhase
    // input samples. Newest sample pairs with the lowest tap index.
    float filterAt(int branch) const {
        double acc = 0.0;
        int readPos = (m_writePos - 1 + m_tapsPerPhase) % m_tapsPerPhase;
        for (int j = 0; j < m_tapsPerPhase; ++j) {
            const int tap = branch + j * m_l;
            if (tap < static_cast<int>(m_taps.size()))
                acc += static_cast<double>(m_taps[static_cast<size_t>(tap)]) * m_history[static_cast<size_t>(readPos)];
            readPos = (readPos - 1 + m_tapsPerPhase) % m_tapsPerPhase;
        }
        return static_cast<float>(acc);
    }

    void buildPrototype() {
        const int taps = m_tapsPerPhase * m_l;
        m_taps.assign(static_cast<size_t>(taps), 0.0f);

        // Cutoff on the UPSAMPLED grid, whose rate is inputRate * L. The filter has to remove
        // everything above the lower of the two Nyquists, which for any downward conversion is the
        // output's - hence max(L, M).
        const double cutoff = PASSBAND_FRACTION * 0.5 / static_cast<double>(m_l > m_m ? m_l : m_m);
        const double centre = (taps - 1) / 2.0;
        const double pi = 3.14159265358979323846;

        double sum = 0.0;
        for (int i = 0; i < taps; ++i) {
            const double x = i - centre;
            // sinc, with the removable singularity at the centre tap handled explicitly.
            const double sinc = (std::fabs(x) < 1e-9) ? 2.0 * cutoff : std::sin(2.0 * pi * cutoff * x) / (pi * x);
            // Blackman: -74 dB stopband, and computable without a Bessel function.
            const double w =
                0.42 - 0.5 * std::cos(2.0 * pi * i / (taps - 1)) + 0.08 * std::cos(4.0 * pi * i / (taps - 1));
            const double h = sinc * w;
            m_taps[static_cast<size_t>(i)] = static_cast<float>(h);
            sum += h;
        }

        // Normalise so a DC input comes out at the same level. The L factor is the interpolation
        // gain: only one in every L upsampled samples is non-zero, so without it the output sits L
        // times too quiet - which on a transmit path reads as a microphone that barely works.
        if (sum != 0.0) {
            const double scale = static_cast<double>(m_l) / sum;
            for (float &t : m_taps)
                t = static_cast<float>(t * scale);
        }
    }

    int m_l = 0;
    int m_m = 0;
    int m_inputRate = 0;
    int m_outputRate = 0;
    int m_tapsPerPhase = TAPS_PER_PHASE;
    int m_writePos = 0;
    int m_offset = 0;
    std::vector<float> m_taps;
    std::vector<float> m_history;
};

} // namespace AudioResampler

#endif // AUDIORESAMPLER_H
