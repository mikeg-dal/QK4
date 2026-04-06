#ifndef OPUSENCODER_H
#define OPUSENCODER_H

#include <QObject>
#include <opus/opus.h>

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

private:
    ::OpusEncoder *m_encoder;
    int m_sampleRate;
    int m_channels;
};

#endif // OPUSENCODER_H
