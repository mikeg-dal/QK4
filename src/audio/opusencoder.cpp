#include "opusencoder.h"
#include <QDebug>

OpusEncoder::OpusEncoder(QObject *parent) : QObject(parent), m_encoder(nullptr), m_sampleRate(12000), m_channels(1) {}

OpusEncoder::~OpusEncoder() {
    if (m_encoder) {
        opus_encoder_destroy(m_encoder);
    }
}

bool OpusEncoder::initialize(int sampleRate, int channels, int bitrate) {
    m_sampleRate = sampleRate;
    m_channels = channels;

    int error;
    m_encoder = opus_encoder_create(sampleRate, channels, OPUS_APPLICATION_VOIP, &error);
    if (error != OPUS_OK) {
        qWarning() << "OpusEncoder: Failed to create encoder:" << opus_strerror(error);
        return false;
    }

    opus_encoder_ctl(m_encoder, OPUS_SET_BITRATE(bitrate));
    return true;
}

QByteArray OpusEncoder::encode(const QByteArray &pcmData, int frameSamples) {
    if (!m_encoder) {
        return QByteArray();
    }

    // Validate input size matches requested frame size
    int expectedBytes = frameSamples * static_cast<int>(sizeof(opus_int16));
    if (pcmData.size() != expectedBytes) {
        qWarning() << "OpusEncoder: Invalid frame size" << pcmData.size() << "bytes, expected" << expectedBytes;
        return QByteArray();
    }

    QByteArray encoded(MAX_PACKET_SIZE, Qt::Uninitialized);

    const opus_int16 *pcm = reinterpret_cast<const opus_int16 *>(pcmData.constData());
    int bytes =
        opus_encode(m_encoder, pcm, frameSamples, reinterpret_cast<unsigned char *>(encoded.data()), MAX_PACKET_SIZE);

    if (bytes < 0) {
        qWarning() << "OpusEncoder: Encode failed:" << opus_strerror(bytes);
        return QByteArray();
    }

    encoded.resize(bytes);
    return encoded;
}
