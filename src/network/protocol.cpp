#include "protocol.h"
#include "logging/logcategories.h"
#include <QCryptographicHash>
#include <QtEndian>
#include <QLoggingCategory>

#include <algorithm>
#include <cstring>

Protocol::Protocol(QObject *parent) : QObject(parent) {}

void Protocol::parse(const QByteArray &data) {
    // WHY no size check before parsing: a single read may legitimately carry more than any cap (a
    // burst after a stall), and every complete packet in it must still be delivered. What could
    // grow without bound is only the unconsumed tail, and that is bounded by construction: with no
    // start marker it is 3 bytes, and behind a header it is at most one MAX_PAYLOAD_SIZE frame.
    m_buffer.append(data);

    // Process all complete packets in the buffer
    // All K4 data comes wrapped in binary packets with START/END markers
    while (true) {
        // Look for start marker
        const qsizetype startPos = m_buffer.indexOf(K4Protocol::START_MARKER, m_readOffset);
        if (startPos == -1) {
            // WHY keep the last 3 bytes: START_MARKER is 4 bytes (FE FD FC FB). A TCP read can
            // split the marker across two reads (1–3 bytes now, final byte(s) in the next read).
            // Dropping those trailing bytes would lose sync; retaining 3 guarantees we still find
            // the marker when the remainder arrives.
            m_readOffset = (std::max)(m_readOffset, m_buffer.size() - 3);
            break;
        }

        // Discard any data before the start marker
        m_readOffset = startPos;

        // Check if we have enough data for the header (4 marker + 4 length = 8 bytes)
        const qsizetype available = m_buffer.size() - m_readOffset;
        if (available < 8) {
            break;
        }

        const char *packet = m_buffer.constData() + m_readOffset;

        // Read payload length (big-endian uint32)
        const quint32 payloadLength = qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(packet + 4));
        if (payloadLength > K4Protocol::MAX_PAYLOAD_SIZE) {
            // Not a packet: waiting for this many bytes would stall every packet behind it.
            qCWarning(lcCatRx) << "Invalid K4 packet: announced length" << payloadLength << "exceeds"
                               << K4Protocol::MAX_PAYLOAD_SIZE;
            m_readOffset += 4;
            continue;
        }

        // Calculate total packet size: start(4) + length(4) + payload + end(4)
        const qint64 totalPacketSize = 4 + 4 + qint64(payloadLength) + 4;

        // Check if we have the complete packet
        if (available < totalPacketSize) {
            break;
        }

        // Verify end marker
        if (std::memcmp(packet + totalPacketSize - 4, K4Protocol::END_MARKER.constData(), 4) != 0) {
            // Invalid packet, skip past the start marker and try again
            qCWarning(lcCatRx) << "Invalid K4 packet: bad end marker";
            m_readOffset += 4;
            continue;
        }

        const QByteArray payload = m_buffer.mid(m_readOffset + 8, payloadLength);
        m_readOffset += totalPacketSize;

        // WHY m_readOffset is a member: a slot reached from here may reset() the protocol (a
        // disconnect), and the loop must then see the emptied buffer rather than a stale offset.
        processPacket(payload);
    }

    // One compaction per read, not one deep copy of the tail per packet.
    m_buffer.remove(0, m_readOffset);
    m_readOffset = 0;
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
            qCDebug(lcCatRx) << response;
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

            int binCount = payload.size() - BINS_OFFSET;
            emit spectrumDataReady(receiver, payload, BINS_OFFSET, binCount, centerFreq, sampleRate, noiseFloor);
        }
        break;
    }
    case K4Protocol::MiniPAN: {
        // MiniPAN packet structure - see K4Protocol::MiniPanPacket namespace for offset definitions
        using namespace K4Protocol::MiniPanPacket;
        if (payload.size() <= HEADER_SIZE)
            break;
        const auto *data = reinterpret_cast<const uchar *>(payload.constData());
        const int binCount = qFromLittleEndian<quint16>(data + DATA_LENGTH_OFFSET);
        const qint32 spanKHz = qFromLittleEndian<qint32>(data + SPAN_KHZ_OFFSET);
        // The header states how many bins follow; a frame that does not carry them, or that states
        // no span, cannot be drawn on a frequency axis.
        if (binCount <= 0 || HEADER_SIZE + binCount > payload.size() || spanKHz <= 0 || spanKHz > MAX_SPAN_KHZ) {
            qCDebug(lcCatRx) << "MiniPAN frame dropped: data length" << binCount << "span" << spanKHz << "kHz, payload"
                             << payload.size();
            break;
        }
        const int receiver = data[RECEIVER_OFFSET];
        const qint64 centerFreq = qFromLittleEndian<qint64>(data + CENTER_FREQ_OFFSET);
        emit miniSpectrumDataReady(receiver, payload, BINS_OFFSET, binCount, centerFreq, spanKHz * 1000);
        break;
    }
    default:
        qCDebug(lcCatRx) << "Unknown K4 packet type:" << type;
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

std::size_t Protocol::writeCATPacket(std::string_view command, char *out, std::size_t capacity) {
    const std::size_t payloadLength = 3 + command.size();
    const std::size_t total = kCatPacketOverhead + command.size();
    if (total > capacity)
        return 0;
    char *p = out;
    std::memcpy(p, K4Protocol::START_MARKER.constData(), 4);
    p += 4;
    qToBigEndian(static_cast<quint32>(payloadLength), reinterpret_cast<uchar *>(p));
    p += 4;
    *p++ = static_cast<char>(K4Protocol::CAT);
    *p++ = '\x00';
    *p++ = '\x00';
    std::memcpy(p, command.data(), command.size());
    p += command.size();
    std::memcpy(p, K4Protocol::END_MARKER.constData(), 4);
    return total;
}

QByteArray Protocol::buildAuthData(const QString &password) {
    // SHA-384 hash of password
    QByteArray passwordData = password.toUtf8();
    QByteArray hash = QCryptographicHash::hash(passwordData, QCryptographicHash::Sha384);

    // Convert to lowercase hex string
    QByteArray hexString = hash.toHex().toLower();

    return hexString;
}

QByteArray Protocol::buildAudioPacket(const QByteArray &audioData, quint8 sequence, quint8 encodeMode,
                                      quint16 frameSamples) {
    // K4 TX Audio Packet Structure:
    // Byte 0:    TYPE = 0x01 (Audio)
    // Byte 1:    VER = 0x01 (Version)
    // Byte 2:    SEQ = sequence number (0-255, wrapping)
    // Byte 3:    MODE = encode mode (0=RAW S32LE (24-bit), 1=RAW S16LE, 2/3=Opus (same bitstream, int vs float decode))
    // Bytes 4-5: Frame size (little-endian UInt16) — samples per channel, matches SL tier
    // Byte 6:    Sample rate code = 0x00 (12000 Hz)
    // Byte 7+:   Audio data (format depends on encode mode)

    QByteArray payload;
    payload.reserve(7 + audioData.size());

    payload.append(static_cast<char>(K4Protocol::Audio)); // 0x01
    payload.append(static_cast<char>(0x01));              // Version
    payload.append(static_cast<char>(sequence));          // Sequence
    payload.append(static_cast<char>(encodeMode));        // Encode mode

    // Frame size (little-endian) — matches SL tier
    payload.append(static_cast<char>(frameSamples & 0xFF));
    payload.append(static_cast<char>((frameSamples >> 8) & 0xFF));

    payload.append(static_cast<char>(0x00)); // Sample rate code (0 = 12kHz)

    payload.append(audioData);

    return buildPacket(payload);
}
