#ifndef RAWAUDIOFORMAT_H
#define RAWAUDIOFORMAT_H

#include <cstdint>

// Builds the wire payload for the K4's RAW TX audio modes, EM0 and EM1, from a captured
// S16LE mono microphone frame.
//
// Pure logic with no Qt, no audio device and no socket, so the wire format can be unit-tested
// without a radio — mirroring hardware/halikey_edge.h, hardware/usbdevicelifecycle.h,
// network/connect_failure.h and models/transmitowner.h.
//
// WHY this exists at all (AUD-003). EM0 is documented as "RAW 32-bit float", and QK4's TX path
// took the documentation at its word: it wrote IEEE-754 float32 in ±1.0. The K4's own EM0 stream
// is S32LE integers — that is what OpusDecoder's RX path was calibrated against, empirically, and
// the two halves never agreed.
//
// Handing float32 to a reader that expects S32 is not a gain error, it is a total loss of signal:
// the float's exponent field lands in the integer's high bits, so magnitude barely moves across
// the whole input range while the sign bit flips with the waveform.
//
//     mic  +1 LSB  → float +0.0000305 → read as S32:   +939,524,096   (112× full scale)
//     mic  -1 LSB  → float -0.0000305 → read as S32: -1,207,959,552   (144× full scale)
//     mic full scale → float ~1.0     → read as S32: ~1,065,352,713   (127× full scale)
//
// 90 dB of microphone dynamic range compresses into a 1.13:1 amplitude ratio and only the sign
// bit survives — a 1-bit hard limit well over full scale. On a quiet mic, whose dither flips
// sign at random, that is full-scale white noise. Confirmed on hardware 2026-09-19: EM0
// transmitted static while the mic peaked at 1 LSB. EM0 RX being correct was no evidence for TX;
// they are independent paths and only the RX one had ever been calibrated against the radio.
namespace RawAudioFormat {

// EM0 full scale is 2^23 — EM0 is 24-bit audio in a 32-bit container.
//
// MEASURED 2026-09-19 by tools/k4_audio_capture.py (capture bench-logs/audiocal-20260919-0741)
// against a stationary reference: dummy load, AGC OFF, fixed AF gain, 20 s of receiver noise
// captured in each of the four modes. The K4's EM0 stream carries exactly 256× (2^8) the values
// of its EM1 stream for the same sound — a bit shift, which is why it is exact rather than
// approximate. The previous value of 2^17, guessed by ear from quiet signals as "the S16 range
// with ~4× headroom", was low by a factor of 64.
//
// OpusDecoder derives its EM0 normalisation from this constant, so RX and TX cannot drift apart.
inline constexpr float EM0_FULL_SCALE = 8388608.0f; // 2^23
inline constexpr float EM1_FULL_SCALE = 32768.0f;   // 2^15

// EM0's container is 256× EM1's. Kept as a named constant because it is the measured relationship
// between the two RAW formats, and the tests assert against it.
inline constexpr std::int32_t EM0_S16_TO_FULL_SCALE = 256; // 2^23 / 2^15

// Bytes of wire payload produced from `monoSamples` captured samples. Both RAW modes duplicate
// mono into a stereo pair (left = Main, right = Sub), as the K4 does in the other direction.
inline constexpr int em0BytesFor(int monoSamples) {
    return monoSamples * 2 * static_cast<int>(sizeof(std::int32_t));
}

inline constexpr int em1BytesFor(int monoSamples) {
    return monoSamples * 2 * static_cast<int>(sizeof(std::int16_t));
}

// WHY these take float, not S16. The TX path used to apply mic gain in float and then quantise to
// S16 before choosing a wire format, which threw away bits in proportion to the attenuation: at a
// 30% mic slider (gain 0.027) a full-scale input survived as ~10.8 bits, and quiet audio occupied
// a handful of codes. Undithered quantisation error at that depth is correlated with the signal,
// so it is harmonic distortion rather than noise — confirmed on hardware 2026-09-19, where dead
// air produced a tonal buzz at low mic gain that vanished completely at 50% and above.
//
// Quantising once, at the wire format, removes it: EM0 gets its full 24 bits instead of 16 shifted
// left by 8, and the Opus modes never quantise at all (see OpusEncoder::encodeFloat). EM1 is 16-bit
// by definition and is the one mode that cannot be improved this way.
//
// Input is ±1.0; values outside that range are clamped rather than allowed to wrap.
inline std::int32_t toEm0Sample(float mono) {
    const float scaled = mono * (EM0_FULL_SCALE - 1.0f);
    if (scaled >= EM0_FULL_SCALE - 1.0f)
        return static_cast<std::int32_t>(EM0_FULL_SCALE) - 1;
    if (scaled <= -EM0_FULL_SCALE)
        return -static_cast<std::int32_t>(EM0_FULL_SCALE);
    return static_cast<std::int32_t>(scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f);
}

inline std::int16_t toEm1Sample(float mono) {
    const float scaled = mono * (EM1_FULL_SCALE - 1.0f);
    if (scaled >= EM1_FULL_SCALE - 1.0f)
        return 32767;
    if (scaled <= -EM1_FULL_SCALE)
        return -32768;
    return static_cast<std::int16_t>(scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f);
}

// Write `monoSamples` samples as EM0: S32LE stereo, mono duplicated to both channels.
// `out` must have room for em0BytesFor(monoSamples). Bytes are packed little-endian explicitly
// rather than by casting the buffer, so the wire layout is the same on any host and is what the
// unit test actually asserts.
inline void encodeEm0(const float *mono, int monoSamples, unsigned char *out) {
    for (int i = 0; i < monoSamples; i++) {
        const std::uint32_t v = static_cast<std::uint32_t>(toEm0Sample(mono[i]));
        for (int ch = 0; ch < 2; ch++) {
            unsigned char *p = out + (i * 2 + ch) * 4;
            p[0] = static_cast<unsigned char>(v & 0xFF);
            p[1] = static_cast<unsigned char>((v >> 8) & 0xFF);
            p[2] = static_cast<unsigned char>((v >> 16) & 0xFF);
            p[3] = static_cast<unsigned char>((v >> 24) & 0xFF);
        }
    }
}

// Write `monoSamples` samples as EM1: S16LE stereo, mono duplicated to both channels.
// `out` must have room for em1BytesFor(monoSamples).
inline void encodeEm1(const float *mono, int monoSamples, unsigned char *out) {
    for (int i = 0; i < monoSamples; i++) {
        const std::uint16_t v = static_cast<std::uint16_t>(toEm1Sample(mono[i]));
        for (int ch = 0; ch < 2; ch++) {
            unsigned char *p = out + (i * 2 + ch) * 2;
            p[0] = static_cast<unsigned char>(v & 0xFF);
            p[1] = static_cast<unsigned char>((v >> 8) & 0xFF);
        }
    }
}

} // namespace RawAudioFormat

#endif // RAWAUDIOFORMAT_H
