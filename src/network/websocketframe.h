#ifndef NETWORK_WEBSOCKETFRAME_H
#define NETWORK_WEBSOCKETFRAME_H

#include <QByteArray>
#include <QString>

// RFC 6455 framing, with no sockets anywhere in it.
//
// WHY this is split from websocketserver: framing is where the subtle bugs live — length forms,
// masking direction, fragment reassembly, control frames interleaved mid-message — and none of it
// is reachable in a test if it only exists inside a socket callback. TR4W shipped a WebSocket
// client with the framing fused to its transport and recorded the consequence in its own design
// notes: "today none of the framing is tested". This half is pure, so it is.
//
// Only the subset a TCI server needs is implemented: no extensions, no permessage-deflate, no
// client-side masking of outbound frames.
namespace WebSocketFrame {

enum Opcode : quint8 {
    OpContinuation = 0x0,
    OpText = 0x1,
    OpBinary = 0x2,
    OpClose = 0x8,
    OpPing = 0x9,
    OpPong = 0xA,
};

// Close codes this server emits.
enum CloseCode : quint16 {
    CloseNormal = 1000,
    CloseProtocolError = 1002,
    CloseTooBig = 1009,
};

// Sec-WebSocket-Accept for a client's Sec-WebSocket-Key: base64(sha1(key + GUID)).
// Direction-symmetric, so a client implementation would use the same function to verify.
QString acceptKey(const QString &clientKey);

// Control frames (0x8-0xA) may be interleaved inside a fragmented message and must never be
// fragmented themselves.
bool isControlOpcode(quint8 opcode);

// Encode one unfragmented frame.
//
// WHY the default is unmasked: RFC 6455 5.1 forbids a server from masking. The flag exists only so
// tests can synthesise client traffic, which MUST be masked.
QByteArray encode(quint8 opcode, const QByteArray &payload, bool mask = false);

// Convenience for a CLOSE frame carrying a status code.
QByteArray encodeClose(quint16 code, const QString &reason = QString());

} // namespace WebSocketFrame

// Incremental decoder: bytes in, whole messages out.
//
// Handles the three length forms, unmasking, fragment reassembly, and the size limits that keep a
// hostile peer from allocating unbounded memory (CONVENTIONS.md rule 5). Control frames are
// delivered immediately even when they arrive between fragments of a data message.
class WebSocketDecoder {
public:
    enum class Status {
        NeedMoreData, // nothing complete yet; feed more
        Ready,        // `out` holds a complete message or control frame
        Error,        // protocol violation; the session must be closed
    };

    // 64 KiB each, mirroring what AetherSDR's TCI server accepts. A TCI text command is tens of
    // bytes and an audio frame is ~8 KiB, so this is generous.
    static constexpr int DEFAULT_MAX_FRAME_BYTES = 64 * 1024;
    static constexpr int DEFAULT_MAX_MESSAGE_BYTES = 64 * 1024;

    // requireMask: true for a server (clients MUST mask), false when decoding server->client
    // traffic in a test.
    explicit WebSocketDecoder(bool requireMask = true, int maxFrameBytes = DEFAULT_MAX_FRAME_BYTES,
                              int maxMessageBytes = DEFAULT_MAX_MESSAGE_BYTES);

    void append(const QByteArray &bytes);

    struct Message {
        quint8 opcode = 0;
        QByteArray payload;
    };

    // Pops one message. Call repeatedly until it returns NeedMoreData — a single append() can
    // complete several.
    Status next(Message &out);

    // Set when next() returns Error. Also carries the close code to send back.
    QString errorString() const { return m_error; }
    quint16 closeCode() const { return m_closeCode; }

    void reset();

private:
    Status fail(const QString &why, quint16 code);

    bool m_requireMask;
    int m_maxFrameBytes;
    int m_maxMessageBytes;

    QByteArray m_buffer;
    // Reassembly state for a fragmented data message.
    bool m_inFragment = false;
    quint8 m_fragmentOpcode = 0;
    QByteArray m_fragment;

    QString m_error;
    quint16 m_closeCode = 0;
};

#endif // NETWORK_WEBSOCKETFRAME_H
