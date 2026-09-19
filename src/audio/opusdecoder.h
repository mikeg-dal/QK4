#ifndef OPUSDECODER_H
#define OPUSDECODER_H

#include <QObject>
#include <array>
#include "audio/rawaudioformat.h" // EM0_FULL_SCALE
#include <opus/opus.h>

/**
 * @brief Opus decoder wrapper for inbound K4 audio packets. Handles K4's audio-packet framing
 *        (EM0..EM3) and emits stereo Float32 PCM. Volume/mix/balance is applied later in
 *        AudioEngine at playback time, not here.
 *
 *        All four modes follow ONE rule:  out = native × K4_GAIN_BOOST / full_scale
 *
 *          EM0 (S32LE, 24-bit payload) — full scale 2^23  (RawAudioFormat::EM0_FULL_SCALE)
 *          EM1 (S16LE)                 — full scale 2^15
 *          EM2 (Opus → S16)            — full scale 2^15
 *          EM3 (Opus → float)          — full scale 1.0
 *
 *        MEASURED 2026-09-19 by tools/k4_audio_capture.py against a stationary reference
 *        (dummy load, AGC OFF, fixed AF gain, 20 s of receiver noise per mode) — capture
 *        bench-logs/audiocal-20260919-0741. Under this rule all four modes land within
 *        0.42% (0.04 dB) of each other. The K4 ships EM1, EM2 and EM3 at an identical level
 *        (16.78 / 16.71 / 16.72 on a 2^15 scale) and EM0 at exactly 256× that.
 *
 *        This replaced four unrelated by-ear constants, two of which were wrong: EM0 was
 *        6 dB hot (its full scale had been assumed 2^17, not 2^23) and EM1 was 6 dB quiet
 *        (a 16× boost where the measurement says 32×, the same as every other mode). The
 *        32× itself is independently corroborated — K4-Companion, a separate implementation,
 *        applies the same 32× to the same stream.
 */
class OpusDecoder : public QObject {
    Q_OBJECT

public:
    explicit OpusDecoder(QObject *parent = nullptr);
    ~OpusDecoder();

    // Initialize decoder (K4 uses 12000Hz stereo)
    bool initialize(int sampleRate = 12000, int channels = 2);

    // Decode K4 audio packet payload, returns raw normalized stereo Float32 PCM
    // Output is interleaved [main, sub, main, sub, ...] with gain boost applied
    // Volume/routing/balance is NOT applied here — that happens at playback time
    QByteArray decodeK4Packet(const QByteArray &packet);

    // Raw decode for testing (returns S16LE stereo PCM)
    QByteArray decode(const QByteArray &opusData);

    // Decode to float (returns float32 stereo PCM)
    QByteArray decodeFloat(const QByteArray &opusData);

    // S16 -> float32 normalization factor. Public because AudioController's TX
    // mic path (EM0 RAW float) uses the same conversion and was previously
    // duplicating this value locally.
    static constexpr float NORMALIZE_16BIT = 1.0f / 32768.0f;

private:
    ::OpusDecoder *m_decoder;
    int m_sampleRate;
    int m_channels;

    // Max frame size for 12kHz audio = 120ms * 12000 = 1440 samples per channel.
    // Stereo is the only configured channel count (m_channels = 2 from initialize).
    static constexpr int MAX_FRAME_SAMPLES_PER_CHANNEL = 1440;
    static constexpr int MAX_SCRATCH_SAMPLES = MAX_FRAME_SAMPLES_PER_CHANNEL * 2; // stereo

    // Pre-allocated scratch buffers for opus_decode / opus_decode_float. Previously
    // each call allocated a per-frame QVector — at SL0 (50 Hz RX), that was 50+
    // heap allocations per second on the IO thread just for scratch space. Fixed-
    // size members reuse the same memory each frame; we still copy into the
    // returned QByteArray (callers depend on QByteArray ownership semantics).
    std::array<opus_int16, MAX_SCRATCH_SAMPLES> m_pcmIntScratch{};
    std::array<float, MAX_SCRATCH_SAMPLES> m_pcmFloatScratch{};

    // The single gain every mode shares. The K4 ships all four encode modes ~32× below the
    // full scale of whatever container they use, so this is a property of the radio, not of
    // any one codec. Measured 2026-09-19; independently corroborated by K4-Companion, which
    // applies the same 32× to the same stream.
    static constexpr float K4_GAIN_BOOST = 32.0f;

    // EM0's container full scale, shared with the TX side so the two directions of EM0 cannot
    // drift apart again — that divergence is what AUD-003 was.
    static constexpr float NORMALIZE_K4_RAW = 1.0f / RawAudioFormat::EM0_FULL_SCALE;
};

#endif // OPUSDECODER_H
