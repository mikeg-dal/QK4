#ifndef AUDIODECIMATOR_H
#define AUDIODECIMATOR_H

// Decimates a captured microphone stream down to the K4's 12 kHz TX rate.
//
// Pure logic with no Qt and no audio device, so the rate arithmetic can be unit-tested without
// hardware — mirroring audio/rawaudioformat.h, hardware/halikey_edge.h and the rest.
//
// WHY the capture rate is not simply 48 kHz any more (INT-005). QK4 opened every microphone at a
// hardcoded 48 kHz and decimated 4:1. That worked on ordinary sound cards and failed completely on
// Bluetooth: an AirPods Pro runs its microphone at 24 kHz in HFP, and asking for 48 kHz produced a
// stream that opened cleanly, reported no error, and delivered *nothing at all*. The operator got a
// lit PTT indicator and dead air.
//
// The trap is that Qt agrees to the wrong format. QAudioDevice::isFormatSupported(48 kHz) returns
// TRUE for that device; only actually opening it and counting bytes reveals otherwise. Measured
// 2026-09-19 with tools/audio_device_probe.cpp, alternating the two formats twice to rule out a
// Bluetooth warm-up effect:
//
//     48000 Hz  ->      0 bytes   (twice, QAudioSource state 2, error 0)
//     24000 Hz  -> 144000 bytes   (twice, exactly the expected count)
//
// So the capture rate must come from the device, and the decimation factor from the rate.
namespace AudioDecimator {

// The K4's TX audio rate. Everything here decimates to this.
inline constexpr int OUTPUT_RATE = 12000;

// Only exact integer multiples of 12 kHz are handled. Every input device observed so far reports
// 48000 or 24000, both exact. A rate like 16000 (older AirPods, 4:3) or 44100 needs rational
// resampling, which is deliberately NOT smuggled in here — see the note on decimate().
inline constexpr bool isSupportedRate(int sampleRate) {
    return sampleRate >= OUTPUT_RATE && (sampleRate % OUTPUT_RATE) == 0;
}

inline constexpr int factorFor(int sampleRate) {
    return isSupportedRate(sampleRate) ? sampleRate / OUTPUT_RATE : 0;
}

inline constexpr int outputSampleCount(int inputSamples, int factor) {
    return factor > 0 ? inputSamples / factor : 0;
}

// Decimate `inputSamples` floats by `factor`, averaging each group into one output sample.
//
// The averaging is a box filter: crude as an anti-aliasing filter, but it is what the 48 kHz path
// has always used and generalising the factor must not quietly change how existing setups sound.
// A proper windowed-sinc decimator — which would also let us accept 16 kHz and 44.1 kHz — is worth
// doing, and worth doing as its own change with its own tests rather than riding along with a
// device-negotiation fix.
//
// `out` must have room for outputSampleCount(inputSamples, factor).
inline void decimate(const float *in, int inputSamples, int factor, float *out) {
    if (factor <= 0 || in == nullptr || out == nullptr)
        return;

    const int outCount = outputSampleCount(inputSamples, factor);
    for (int i = 0; i < outCount; i++) {
        const int src = i * factor;
        float sum = 0.0f;
        int count = 0;
        for (int j = 0; j < factor && (src + j) < inputSamples; j++) {
            sum += in[src + j];
            count++;
        }
        out[i] = (count > 0) ? (sum / static_cast<float>(count)) : 0.0f;
    }
}

} // namespace AudioDecimator

#endif // AUDIODECIMATOR_H
