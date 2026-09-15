#ifndef DSP_AUDIOUPSAMPLER_H
#define DSP_AUDIOUPSAMPLER_H

#include <vector>

// Integer-factor interpolating resampler for the TCI receive path.
//
// The K4 sends 12 kHz audio; WSJT-X's TCI implementation always consumes 48 kHz and ignores the
// rate a server declares (`audioSampleRate` is assigned once in its constructor and the
// `audio_samplerate` command has no dispatch case). So QK4 has to interpolate 4:1 before framing
// RX_AUDIO. See docs/tci-server-design.md.
//
// WHY a real windowed-sinc rather than the 4-tap boxcar already in AudioEngine::resample48kTo12k:
// zero-stuffing puts images at every multiple of the input rate, and a boxcar's stopband is roughly
// -10 dB. Those images fold straight back into the passband and would corrupt a digital-mode
// signal. The measured rejection of this design is better than -100 dB, which test_audioupsampler
// pins.
//
// Stateful and streaming: history carries across calls so block boundaries are not discontinuities.
// One instance per channel — sharing one across channels would smear filter state between them.
class AudioUpsampler {
public:
    // Taps per polyphase branch. Total filter length is FACTOR * TAPS_PER_PHASE, so the default
    // 4x/64 is a 256-tap linear-phase FIR.
    static constexpr int TAPS_PER_PHASE = 64;

    // Default transition: flat to well past the 2.8 kHz a digital-mode window needs, fully down
    // before the first image at inputRate - 2800.
    static constexpr double DEFAULT_CUTOFF_HZ = 5000.0;

    // factor       — integer interpolation ratio (4 for 12 kHz -> 48 kHz)
    // inputRateHz  — sample rate of the incoming stream
    // cutoffHz     — lowpass corner, in the OUTPUT rate's terms
    AudioUpsampler(int factor, double inputRateHz, double cutoffHz = DEFAULT_CUTOFF_HZ);

    int factor() const { return m_factor; }

    // Constant group delay, in output samples. A linear-phase FIR delays every frequency equally,
    // which is why the decoder sees no dispersion; callers that need to align streams can subtract
    // this.
    int groupDelaySamples() const { return (m_factor * TAPS_PER_PHASE - 1) / 2; }

    // Interpolate `count` input samples, appending `count * factor` samples to `out`.
    // `out` is not cleared, so successive blocks can accumulate into one buffer.
    void process(const float *input, int count, std::vector<float> &out);

    // Drop the filter history. Call on stream discontinuity (reconnect, PTT edge) so audio from
    // before the gap cannot bleed across it.
    void reset();

private:
    int m_factor;
    // m_phases[p][j] is the j-th coefficient of polyphase branch p.
    std::vector<std::vector<float>> m_phases;
    // Ring buffer of the last TAPS_PER_PHASE input samples, newest at m_historyPos.
    std::vector<float> m_history;
    int m_historyPos;
};

#endif // DSP_AUDIOUPSAMPLER_H
