#include "opusdecoder.h"
#include "logging/logcategories.h"
#include <QDebug>
#include <QtEndian>

OpusDecoder::OpusDecoder() : m_decoder(nullptr), m_sampleRate(12000), m_channels(2) {}

OpusDecoder::~OpusDecoder() {
    if (m_decoder) {
        opus_decoder_destroy(m_decoder);
    }
}

bool OpusDecoder::initialize(int sampleRate, int channels) {
    m_sampleRate = sampleRate;
    m_channels = channels;

    int error;
    m_decoder = opus_decoder_create(sampleRate, channels, &error);

    if (error != OPUS_OK) {
        qCWarning(lcAudio) << "OpusDecoder: Failed to create decoder:" << opus_strerror(error);
        return false;
    }

    return true;
}

QByteArray OpusDecoder::decodeK4Packet(const QByteArray &packet) {
    // K4 Audio Packet Structure:
    // Byte 0: TYPE = 1 (Audio)
    // Byte 1: VER = Version number
    // Byte 2: SEQ = Sequence number
    // Byte 3: Encode Mode (0=RAW S32LE (24-bit), 1=RAW S16LE, 2/3=Opus (same bitstream, int vs float decode))
    // Bytes 4-5: Frame size (little-endian UInt16) - samples per channel
    // Byte 6: Sample rate code (0 = 12000 Hz)
    // Byte 7+: Audio data (format depends on encode mode)
    //
    // Note: EM0 is documented by Elecraft as "RAW 32-bit float" but the K4 actually sends
    // S32LE integers carrying 24-bit audio. Measured 2026-09-19 - see the class comment.

    if (packet.size() < 8) {
        return QByteArray();
    }

    // Verify packet type
    if (static_cast<unsigned char>(packet[0]) != 0x01) {
        return QByteArray();
    }

    unsigned char encodeMode = static_cast<unsigned char>(packet[3]);

    // Audio data starts at byte 7. A view, not a copy: it is read once, within this call.
    const QByteArray audioData = QByteArray::fromRawData(packet.constData() + 7, packet.size() - 7);

    if (audioData.isEmpty()) {
        return QByteArray();
    }

    // Decode based on encode mode — output raw normalized stereo [main, sub, main, sub, ...]
    // Volume/routing/balance is applied later at playback time in AudioEngine::feedAudioDevice()
    switch (encodeMode) {
    case 0x00: // EM0 - 24-bit payload in an S32LE container
    {
        // WHY qFromLittleEndian and not a qint32 pointer: the payload starts at byte 7 of the
        // packet, so its samples are never 4-byte aligned, and reading them through a qint32 * is
        // undefined behaviour (UBSan: "load of misaligned address"). qFromLittleEndian has no
        // alignment constraint and also states the wire's byte order instead of assuming the host's.
        const char *src = audioData.constData();
        const int totalSamples = static_cast<int>(audioData.size() / sizeof(qint32));

        QByteArray out(totalSamples * sizeof(float), Qt::Uninitialized);
        float *dst = reinterpret_cast<float *>(out.data());
        // WHY: EM0 is documented as "RAW 32-bit float" but the K4 actually sends S32LE
        // integers carrying 24-bit audio, so full scale is 2^23 and the same 32× every other
        // mode gets applies here too. See the class comment for the measurement.
        for (int i = 0; i < totalSamples; i++) {
            const qint32 sample = qFromLittleEndian<qint32>(src + i * sizeof(qint32));
            dst[i] = static_cast<float>(sample) * NORMALIZE_K4_RAW * K4_GAIN_BOOST;
        }
        return out;
    }

    case 0x01: // EM1 - RAW 16-bit S16LE stereo PCM
    {
        // Same misalignment as EM0 (byte 7 is odd), so the same byte-wise little-endian read.
        const char *src = audioData.constData();
        const int totalSamples = static_cast<int>(audioData.size() / sizeof(qint16));

        QByteArray out(totalSamples * sizeof(float), Qt::Uninitialized);
        float *dst = reinterpret_cast<float *>(out.data());
        // WHY the same 32× as EM2/EM3: measurement says the K4 ships EM1, EM2 and EM3 at an
        // identical level (16.78 / 16.71 / 16.72 on a 2^15 scale, same signal). The old 16×
        // here was tuned to match an EM0 that was itself 6 dB hot, so it inherited that error.
        for (int i = 0; i < totalSamples; i++) {
            const qint16 sample = qFromLittleEndian<qint16>(src + i * sizeof(qint16));
            dst[i] = static_cast<float>(sample) * NORMALIZE_16BIT * K4_GAIN_BOOST;
        }
        return out;
    }

    case 0x02: // EM2 - Opus encoded, decode with opus_decode() (returns S16LE)
    {
        QByteArray stereoPcm = decode(audioData);
        if (stereoPcm.isEmpty()) {
            return QByteArray();
        }

        const qint16 *stereoSamples = reinterpret_cast<const qint16 *>(stereoPcm.constData());
        int totalSamples = stereoPcm.size() / sizeof(qint16);

        QByteArray out(totalSamples * sizeof(float), Qt::Uninitialized);
        float *dst = reinterpret_cast<float *>(out.data());
        for (int i = 0; i < totalSamples; i++) {
            dst[i] = static_cast<float>(stereoSamples[i]) * NORMALIZE_16BIT * K4_GAIN_BOOST;
        }
        return out;
    }

    case 0x03: // EM3 - Opus encoded, decode with opus_decode_float() (returns float)
    {
        QByteArray stereoPcm = decodeFloat(audioData);
        if (stereoPcm.isEmpty()) {
            return QByteArray();
        }

        const float *stereoFloats = reinterpret_cast<const float *>(stereoPcm.constData());
        int totalSamples = stereoPcm.size() / sizeof(float);

        QByteArray out(totalSamples * sizeof(float), Qt::Uninitialized);
        float *dst = reinterpret_cast<float *>(out.data());
        for (int i = 0; i < totalSamples; i++) {
            dst[i] = stereoFloats[i] * K4_GAIN_BOOST;
        }
        return out;
    }

    default:
        qCWarning(lcAudio) << "OpusDecoder: Unknown encode mode:" << encodeMode;
        return QByteArray();
    }
}

QByteArray OpusDecoder::decode(const QByteArray &opusData) {
    if (!m_decoder)
        return QByteArray();

    int samples = opus_decode(m_decoder, reinterpret_cast<const unsigned char *>(opusData.constData()), opusData.size(),
                              m_pcmIntScratch.data(), MAX_FRAME_SAMPLES_PER_CHANNEL, 0);

    if (samples < 0) {
        qCWarning(lcAudio) << "OpusDecoder: decode failed:" << opus_strerror(samples);
        return QByteArray();
    }

    // WHY the widening: samples and m_channels are both int, so the byte count was computed as int
    // and only then widened to QByteArray's qsizetype. 1440 samples x 2 channels cannot overflow,
    // but the cast says so independently of MAX_FRAME_SAMPLES_PER_CHANNEL, and it is what CodeQL's
    // cpp/integer-multiplication-cast-to-long asks for.
    return QByteArray(reinterpret_cast<const char *>(m_pcmIntScratch.data()),
                      static_cast<qsizetype>(samples) * m_channels * static_cast<qsizetype>(sizeof(opus_int16)));
}

QByteArray OpusDecoder::decodeFloat(const QByteArray &opusData) {
    if (!m_decoder)
        return QByteArray();

    int samples = opus_decode_float(m_decoder, reinterpret_cast<const unsigned char *>(opusData.constData()),
                                    opusData.size(), m_pcmFloatScratch.data(), MAX_FRAME_SAMPLES_PER_CHANNEL, 0);

    if (samples < 0) {
        qCWarning(lcAudio) << "OpusDecoder: decodeFloat failed:" << opus_strerror(samples);
        return QByteArray();
    }

    // Widened for the same reason as decode() above.
    return QByteArray(reinterpret_cast<const char *>(m_pcmFloatScratch.data()),
                      static_cast<qsizetype>(samples) * m_channels * static_cast<qsizetype>(sizeof(float)));
}
