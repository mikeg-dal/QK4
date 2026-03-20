#include "protocol.h"
#include <QCryptographicHash>
#include <QtEndian>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(catRx, "CAT.RX")

Protocol::Protocol(QObject *parent) : QObject(parent) {}

void Protocol::parse(const QByteArray &data) {
    m_buffer.append(data);

    // Prevent unbounded buffer growth from malformed data
    if (m_buffer.size() > K4Protocol::MAX_BUFFER_SIZE) {
        qWarning() << "Protocol buffer overflow (" << m_buffer.size() << "bytes), clearing";
        m_buffer.clear();
        return;
    }

    // Process all complete packets in the buffer
    // All K4 data comes wrapped in binary packets with START/END markers
    while (true) {
        // Look for start marker
        int startPos = m_buffer.indexOf(K4Protocol::START_MARKER);
        if (startPos == -1) {
            // No start marker found, clear buffer up to last 3 bytes
            // (in case partial marker is at the end)
            if (m_buffer.size() > 3) {
                m_buffer = m_buffer.right(3);
            }
            break;
        }

        // Discard any data before the start marker
        if (startPos > 0) {
            m_buffer = m_buffer.mid(startPos);
        }

        // Check if we have enough data for the header (4 marker + 4 length = 8 bytes)
        if (m_buffer.size() < 8) {
            break;
        }

        // Read payload length (big-endian uint32)
        quint32 payloadLength = qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(m_buffer.constData() + 4));

        // Calculate total packet size: start(4) + length(4) + payload + end(4)
        int totalPacketSize = 4 + 4 + payloadLength + 4;

        // Check if we have the complete packet
        if (m_buffer.size() < totalPacketSize) {
            break;
        }

        // Verify end marker
        QByteArray endMarker = m_buffer.mid(totalPacketSize - 4, 4);
        if (endMarker != K4Protocol::END_MARKER) {
            // Invalid packet, skip past the start marker and try again
            qWarning() << "Invalid K4 packet: bad end marker";
            m_buffer = m_buffer.mid(4);
            continue;
        }

        // Extract payload
        QByteArray payload = m_buffer.mid(8, payloadLength);

        // Remove processed packet from buffer
        m_buffer = m_buffer.mid(totalPacketSize);

        // Process the complete packet
        processPacket(payload);
    }
}

void Protocol::processPacket(const QByteArray &payload) {
    if (payload.isEmpty()) {
        return;
    }

    quint8 type = static_cast<quint8>(payload[0]);
    emit packetReceived(type, payload);

    switch (type) {
    case K4Protocol::CAT: {
        // CAT response: [0x00][0x00][0x00][ASCII data]
        if (payload.size() > 3) {
            QString response = QString::fromLatin1(payload.mid(3));
            qCDebug(catRx) << response;
            emit catResponseReceived(response);
        }
        break;
    }
    case K4Protocol::Audio: {
        // Audio packet structure - see K4Protocol::AudioPacket namespace for offset definitions
        if (payload.size() > K4Protocol::AudioPacket::HEADER_SIZE) {
            emit audioDataReady(payload);
            quint8 audioSeq = static_cast<quint8>(payload[K4Protocol::AudioPacket::SEQUENCE_OFFSET]);
            emit audioSequenceReceived(audioSeq);
        }
        break;
    }
    case K4Protocol::PAN: {
        // PAN packet structure - see K4Protocol::PanPacket namespace for offset definitions
        using namespace K4Protocol::PanPacket;
        if (payload.size() > HEADER_SIZE) {
            int receiver = static_cast<quint8>(payload[RECEIVER_OFFSET]);
            qint64 centerFreq =
                qFromLittleEndian<qint64>(reinterpret_cast<const uchar *>(payload.constData() + CENTER_FREQ_OFFSET));
            qint32 sampleRate =
                qFromLittleEndian<qint32>(reinterpret_cast<const uchar *>(payload.constData() + SAMPLE_RATE_OFFSET));
            qint32 noiseFloorRaw =
                qFromLittleEndian<qint32>(reinterpret_cast<const uchar *>(payload.constData() + NOISE_FLOOR_OFFSET));
            float noiseFloor = noiseFloorRaw / 10.0f;

            QByteArray bins = payload.mid(BINS_OFFSET);
            emit spectrumDataReady(receiver, bins, centerFreq, sampleRate, noiseFloor);
        }
        break;
    }
    case K4Protocol::MiniPAN: {
        // MiniPAN packet structure - see K4Protocol::MiniPanPacket namespace for offset definitions
        using namespace K4Protocol::MiniPanPacket;
        if (payload.size() > HEADER_SIZE) {
            int receiver = static_cast<quint8>(payload[RECEIVER_OFFSET]);
            QByteArray bins = payload.mid(BINS_OFFSET);
            emit miniSpectrumDataReady(receiver, bins);
        }
        break;
    }
    default:
        qCDebug(catRx) << "Unknown K4 packet type:" << type;
        break;
    }
}

QByteArray Protocol::buildPacket(const QByteArray &payload) {
    QByteArray packet;
    packet.reserve(payload.size() + 12);

    // Start marker
    packet.append(K4Protocol::START_MARKER);

    // Payload length (big-endian)
    quint32 length = payload.size();
    char lengthBytes[4];
    qToBigEndian(length, reinterpret_cast<uchar *>(lengthBytes));
    packet.append(lengthBytes, 4);

    // Payload
    packet.append(payload);

    // End marker
    packet.append(K4Protocol::END_MARKER);

    return packet;
}

QByteArray Protocol::buildCATPacket(const QString &command) {
    QByteArray payload;
    payload.append(static_cast<char>(K4Protocol::CAT));
    payload.append('\x00');
    payload.append('\x00');
    payload.append(command.toLatin1());

    return buildPacket(payload);
}

QByteArray Protocol::buildAuthData(const QString &password) {
    // SHA-384 hash of password
    QByteArray passwordData = password.toUtf8();
    QByteArray hash = QCryptographicHash::hash(passwordData, QCryptographicHash::Sha384);

    // Convert to lowercase hex string
    QByteArray hexString = hash.toHex().toLower();

    return hexString;
}

QByteArray Protocol::buildAudioPacket(const QByteArray &audioData, quint8 sequence, quint8 encodeMode) {
    // K4 TX Audio Packet Structure:
    // Byte 0:    TYPE = 0x01 (Audio)
    // Byte 1:    VER = 0x01 (Version)
    // Byte 2:    SEQ = sequence number (0-255, wrapping)
    // Byte 3:    MODE = encode mode (0=RAW32, 1=RAW16, 2=Opus Int, 3=Opus Float)
    // Bytes 4-5: Frame size (little-endian UInt16) = 240 samples
    // Byte 6:    Sample rate code = 0x00 (12000 Hz)
    // Byte 7+:   Audio data (format depends on encode mode)

    QByteArray payload;
    payload.reserve(7 + audioData.size());

    payload.append(static_cast<char>(K4Protocol::Audio)); // 0x01
    payload.append(static_cast<char>(0x01));              // Version
    payload.append(static_cast<char>(sequence));          // Sequence
    payload.append(static_cast<char>(encodeMode));        // Encode mode

    // Frame size: 240 samples (little-endian)
    quint16 frameSize = 240;
    payload.append(static_cast<char>(frameSize & 0xFF));
    payload.append(static_cast<char>((frameSize >> 8) & 0xFF));

    payload.append(static_cast<char>(0x00)); // Sample rate code (0 = 12kHz)

    payload.append(audioData);

    return buildPacket(payload);
}
