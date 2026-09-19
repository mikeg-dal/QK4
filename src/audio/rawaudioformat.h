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

// Bytes of wire payload produced from `monoSamples` captured samples. Both RAW modes duplicate
// mono into a stereo pair (left = Main, right = Sub), as the K4 does in the other direction.
inline constexpr int em0BytesFor(int monoSamples) {
    return monoSamples * 2 * static_cast<int>(sizeof(std::int32_t));
}

inline constexpr int em1BytesFor(int monoSamples) {
    return monoSamples * 2 * static_cast<int>(sizeof(std::int16_t));
}

// S16 full scale (2^15) mapped onto EM0 full scale (2^23) — a left shift of 8.
inline constexpr std::int32_t EM0_S16_TO_FULL_SCALE = 256; // 2^23 / 2^15

// WHY 256, confirmed from two independent directions.
//
// The captured sample arrives already clamped and scaled to S16 peak by bufferAndEmitTxFrames,
// so 32767 is not typical content — it is the loudest sample that can exist. Its maximum must
// therefore land on the format's maximum, and EM0's maximum is 2^23 (see EM0_FULL_SCALE).
// Anything less caps QK4 below full modulation with no way to recover it: setMicGain clamps at
// unity and can only attenuate.
//
//   1. The RX capture of 2026-09-19 measured the K4's own EM0 stream at exactly 256× its EM1
//      stream for the same sound. Sending at 256× puts QK4 on the wire at the same level the
//      radio itself uses.
//   2. The bench ALC table from the same day, from entirely separate data: at one mic setting
//      EM0 reached ALC 2 while EM1/EM2/EM3 pinned at 8 on *less* input. Per unit of mic level
//      EM1 was 64× hotter than EM0-at-×4 — precisely the factor between 4 and 256.
//
// Two shortfalls were found and fixed the same day, so beware the history: the float-vs-S32
// format bug above, and then a ×4 scale that was still 36 dB short because EM0's full scale had
// been assumed to be 2^17. Neither is evidence about the other.
inline constexpr std::int32_t toEm0Sample(std::int16_t mono) {
    return static_cast<std::int32_t>(mono) * EM0_S16_TO_FULL_SCALE;
}

// Write `monoSamples` samples as EM0: S32LE stereo, mono duplicated to both channels.
// `out` must have room for em0BytesFor(monoSamples). Bytes are packed little-endian explicitly
// rather than by casting the buffer, so the wire layout is the same on any host and is what the
// unit test actually asserts.
inline void encodeEm0(const std::int16_t *mono, int monoSamples, unsigned char *out) {
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
inline void encodeEm1(const std::int16_t *mono, int monoSamples, unsigned char *out) {
    for (int i = 0; i < monoSamples; i++) {
        const std::uint16_t v = static_cast<std::uint16_t>(mono[i]);
        for (int ch = 0; ch < 2; ch++) {
            unsigned char *p = out + (i * 2 + ch) * 2;
            p[0] = static_cast<unsigned char>(v & 0xFF);
            p[1] = static_cast<unsigned char>((v >> 8) & 0xFF);
        }
    }
}

} // namespace RawAudioFormat

#endif // RAWAUDIOFORMAT_H
