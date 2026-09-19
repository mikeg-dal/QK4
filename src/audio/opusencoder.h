#ifndef OPUSENCODER_H
#define OPUSENCODER_H

#include <QObject>
#include <opus/opus.h>

/**
 * @brief Opus encoder wrapper for outbound TX mic audio. Frame size is dynamic: 240 / 480 / 720 /
 *        1440 samples at 12 kHz, matching the current K4 SL tier. AudioController swaps frame
 *        size in response to `streamingLatencyChanged`.
 */
class OpusEncoder : public QObject {
    Q_OBJECT

public:
    static constexpr int MAX_PACKET_SIZE = 4000; // Max Opus packet size

    explicit OpusEncoder(QObject *parent = nullptr);
    ~OpusEncoder();

    bool initialize(int sampleRate = 12000, int channels = 1, int bitrate = 24000);

    // Encode a PCM frame. frameSamples must match pcmData size (frameSamples * sizeof(opus_int16)).
    // Valid values at 12kHz: 240 (20ms), 480 (40ms), 720 (60ms), 1440 (120ms).
    QByteArray encode(const QByteArray &pcmData, int frameSamples);

    // Encode a Float32 frame directly, without an S16 round trip. This is what the TX path uses:
    // quantising to S16 first discarded bits in proportion to the mic gain, which was audible as
    // tonal distortion on quiet audio (see audio/rawaudioformat.h). Opus takes float natively, so
    // the Opus modes need not quantise at all.
    QByteArray encodeFloat(const QByteArray &floatPcm, int frameSamples);

private:
    ::OpusEncoder *m_encoder;
    int m_sampleRate;
    int m_channels;
};

#endif // OPUSENCODER_H
