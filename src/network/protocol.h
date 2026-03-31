#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <QObject>
#include <QByteArray>

// K4 Protocol Constants
namespace K4Protocol {

// Packet markers (inline to avoid ODR violations - single definition across all TUs)
inline const QByteArray START_MARKER = QByteArray::fromHex("FEFDFCFB");
inline const QByteArray END_MARKER = QByteArray::fromHex("FBFCFDFE");

// Payload types (first byte of payload)
enum PayloadType : quint8 {
    CAT = 0x00,    // CAT command (ASCII)
    Audio = 0x01,  // Audio data (Opus)
    PAN = 0x02,    // Panadapter/spectrum data
    MiniPAN = 0x03 // Mini panadapter
};

// Default K4 ports
constexpr quint16 DEFAULT_PORT = 9205; // Unencrypted (SHA-384 auth)
constexpr quint16 TLS_PORT = 9204;     // TLS/PSK encrypted

// Timing constants
constexpr int PING_INTERVAL_MS = 1000;       // 1 second (matches SIRC update interval)
constexpr int CONNECTION_TIMEOUT_MS = 10000; // 10 seconds
constexpr int AUTH_TIMEOUT_MS = 5000;        // 5 seconds for auth response

// Buffer limits
constexpr int MAX_BUFFER_SIZE = 1024 * 1024; // 1MB max buffer before reset

// PAN packet byte offsets (Type 0x02)
namespace PanPacket {
constexpr int TYPE_OFFSET = 0;
constexpr int VERSION_OFFSET = 1;
constexpr int SEQUENCE_OFFSET = 2;
constexpr int PAN_TYPE_OFFSET = 3;
constexpr int RECEIVER_OFFSET = 4;     // 0=Main, 1=Sub
constexpr int DATA_LENGTH_OFFSET = 5;  // uint16 LE
constexpr int RESERVED_OFFSET = 7;     // 4 bytes reserved
constexpr int CENTER_FREQ_OFFSET = 11; // int64 LE, Hz
constexpr int SAMPLE_RATE_OFFSET = 19; // int32 LE
constexpr int NOISE_FLOOR_OFFSET = 23; // int32 LE, divide by 10 for dB
constexpr int BINS_OFFSET = 27;        // Compressed bin data starts here
constexpr int HEADER_SIZE = 27;        // Minimum packet size before bins
} // namespace PanPacket

// MiniPAN packet byte offsets (Type 0x03)
namespace MiniPanPacket {
constexpr int TYPE_OFFSET = 0;
constexpr int VERSION_OFFSET = 1;
constexpr int SEQUENCE_OFFSET = 2;
constexpr int RESERVED_OFFSET = 3;
constexpr int RECEIVER_OFFSET = 4; // 0=Main, 1=Sub
constexpr int BINS_OFFSET = 5;     // Mini PAN data starts here
constexpr int HEADER_SIZE = 5;
} // namespace MiniPanPacket

// Audio packet byte offsets (Type 0x01)
namespace AudioPacket {
constexpr int TYPE_OFFSET = 0;
constexpr int VERSION_OFFSET = 1;
constexpr int SEQUENCE_OFFSET = 2;
constexpr int MODE_OFFSET = 3;
constexpr int FRAME_SIZE_OFFSET = 4; // uint16 LE
constexpr int SAMPLE_RATE_OFFSET = 6;
constexpr int DATA_OFFSET = 7;
constexpr int HEADER_SIZE = 7;
} // namespace AudioPacket

// CAT command strings
namespace Commands {
constexpr const char *READY = "RDY;";
constexpr const char *ENABLE_K4_MODE = "K41;";
constexpr const char *ENABLE_LONG_ERRORS = "ER1;";
constexpr const char *PING = "PING;";
constexpr const char *DISCONNECT = "RRN;";
} // namespace Commands

} // namespace K4Protocol

class Protocol : public QObject {
    Q_OBJECT

public:
    explicit Protocol(QObject *parent = nullptr);

    // Parse incoming raw data, extracts complete K4 packets
    void parse(const QByteArray &data);

    // Build a CAT command packet
    static QByteArray buildCATPacket(const QString &command);

    // Build authentication data (SHA-384 hash as hex string)
    static QByteArray buildAuthData(const QString &password);

    // Build TX audio packet (Opus encoded data with K4 audio header)
    // sequence: 0-255, wrapping counter for packet ordering
    // encodeMode: 0=RAW32, 1=RAW16, 2=Opus Int, 3=Opus Float (default)
    static QByteArray buildAudioPacket(const QByteArray &audioData, quint8 sequence, quint8 encodeMode = 0x03);

signals:
    void audioDataReady(const QByteArray &opusData);
    void audioSequenceReceived(quint8 seq);
    // receiver: 0 = Main (VFO A), 1 = Sub (VFO B)
    void spectrumDataReady(int receiver, const QByteArray &payload, int binsOffset, int binCount, qint64 centerFreq,
                           qint32 sampleRate, float noiseFloor);
    void miniSpectrumDataReady(int receiver, const QByteArray &payload, int binsOffset, int binCount);
    void catResponseReceived(const QString &response);
    void packetReceived(quint8 type, const QByteArray &payload);

private:
    void processPacket(const QByteArray &packet);

    // Build a K4 packet from payload (used internally by buildCATPacket/buildAudioPacket)
    static QByteArray buildPacket(const QByteArray &payload);

    QByteArray m_buffer;
};

#endif // PROTOCOL_H
