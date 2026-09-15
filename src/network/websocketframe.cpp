#include "network/websocketframe.h"

#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QtEndian>

namespace {

// RFC 6455 1.3. Not a secret; it exists so a cached HTTP proxy cannot be tricked into completing a
// handshake it did not understand.
const char kWebSocketGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

constexpr int kMaxControlPayload = 125; // RFC 6455 5.5

} // namespace

namespace WebSocketFrame {

QString acceptKey(const QString &clientKey) {
    QByteArray concatenated = clientKey.trimmed().toUtf8() + kWebSocketGuid;
    return QString::fromLatin1(QCryptographicHash::hash(concatenated, QCryptographicHash::Sha1).toBase64());
}

bool isControlOpcode(quint8 opcode) {
    return (opcode & 0x08) != 0;
}

QByteArray encode(quint8 opcode, const QByteArray &payload, bool mask) {
    QByteArray frame;
    frame.append(static_cast<char>(0x80 | (opcode & 0x0F))); // FIN set; never fragment on send

    const int len = payload.size();
    const char maskBit = mask ? static_cast<char>(0x80) : static_cast<char>(0x00);
    if (len < 126) {
        frame.append(static_cast<char>(maskBit | len));
    } else if (len <= 0xFFFF) {
        frame.append(static_cast<char>(maskBit | 126));
        char be[2];
        qToBigEndian<quint16>(static_cast<quint16>(len), be);
        frame.append(be, 2);
    } else {
        frame.append(static_cast<char>(maskBit | 127));
        char be[8];
        qToBigEndian<quint64>(static_cast<quint64>(len), be);
        frame.append(be, 8);
    }

    if (!mask) {
        frame.append(payload);
        return frame;
    }

    quint8 key[4];
    const quint32 r = QRandomGenerator::global()->generate();
    key[0] = static_cast<quint8>(r & 0xFF);
    key[1] = static_cast<quint8>((r >> 8) & 0xFF);
    key[2] = static_cast<quint8>((r >> 16) & 0xFF);
    key[3] = static_cast<quint8>((r >> 24) & 0xFF);
    frame.append(reinterpret_cast<const char *>(key), 4);

    QByteArray masked = payload;
    for (int i = 0; i < masked.size(); ++i) {
        masked[i] = static_cast<char>(static_cast<quint8>(masked[i]) ^ key[i % 4]);
    }
    frame.append(masked);
    return frame;
}

QByteArray encodeClose(quint16 code, const QString &reason) {
    QByteArray payload;
    char be[2];
    qToBigEndian<quint16>(code, be);
    payload.append(be, 2);
    if (!reason.isEmpty()) {
        payload.append(reason.toUtf8());
    }
    return encode(OpClose, payload);
}

} // namespace WebSocketFrame

WebSocketDecoder::WebSocketDecoder(bool requireMask, int maxFrameBytes, int maxMessageBytes)
    : m_requireMask(requireMask), m_maxFrameBytes(maxFrameBytes), m_maxMessageBytes(maxMessageBytes) {}

void WebSocketDecoder::append(const QByteArray &bytes) {
    m_buffer.append(bytes);
}

void WebSocketDecoder::reset() {
    m_buffer.clear();
    m_inFragment = false;
    m_fragmentOpcode = 0;
    m_fragment.clear();
    m_error.clear();
    m_closeCode = 0;
}

WebSocketDecoder::Status WebSocketDecoder::fail(const QString &why, quint16 code) {
    m_error = why;
    m_closeCode = code;
    return Status::Error;
}

WebSocketDecoder::Status WebSocketDecoder::next(Message &out) {
    for (;;) {
        if (!m_error.isEmpty()) {
            return Status::Error;
        }
        if (m_buffer.size() < 2) {
            return Status::NeedMoreData;
        }

        const quint8 b0 = static_cast<quint8>(m_buffer[0]);
        const quint8 b1 = static_cast<quint8>(m_buffer[1]);
        const bool fin = (b0 & 0x80) != 0;
        const quint8 rsv = b0 & 0x70;
        const quint8 opcode = b0 & 0x0F;
        const bool masked = (b1 & 0x80) != 0;
        quint64 len = b1 & 0x7F;
        int pos = 2;

        // WHY reject reserved bits: they only carry meaning for a negotiated extension, and this
        // server negotiates none. Ignoring them would silently mis-parse a deflate-compressed frame.
        if (rsv != 0) {
            return fail(QStringLiteral("reserved bits set without a negotiated extension"),
                        WebSocketFrame::CloseProtocolError);
        }

        if (len == 126) {
            if (m_buffer.size() < pos + 2) {
                return Status::NeedMoreData;
            }
            len = qFromBigEndian<quint16>(m_buffer.constData() + pos);
            pos += 2;
        } else if (len == 127) {
            if (m_buffer.size() < pos + 8) {
                return Status::NeedMoreData;
            }
            len = qFromBigEndian<quint64>(m_buffer.constData() + pos);
            pos += 8;
            if (len > static_cast<quint64>(INT32_MAX)) {
                return fail(QStringLiteral("frame length exceeds addressable size"), WebSocketFrame::CloseTooBig);
            }
        }

        if (static_cast<qint64>(len) > m_maxFrameBytes) {
            return fail(QStringLiteral("frame of %1 bytes exceeds the %2 byte limit").arg(len).arg(m_maxFrameBytes),
                        WebSocketFrame::CloseTooBig);
        }

        // RFC 6455 5.1: a server MUST close the connection on an unmasked client frame.
        if (m_requireMask && !masked) {
            return fail(QStringLiteral("client frame was not masked"), WebSocketFrame::CloseProtocolError);
        }

        quint8 key[4] = {0, 0, 0, 0};
        if (masked) {
            if (m_buffer.size() < pos + 4) {
                return Status::NeedMoreData;
            }
            for (int i = 0; i < 4; ++i) {
                key[i] = static_cast<quint8>(m_buffer[pos + i]);
            }
            pos += 4;
        }

        if (static_cast<quint64>(m_buffer.size()) < static_cast<quint64>(pos) + len) {
            return Status::NeedMoreData;
        }

        QByteArray payload = m_buffer.mid(pos, static_cast<int>(len));
        if (masked) {
            for (int i = 0; i < payload.size(); ++i) {
                payload[i] = static_cast<char>(static_cast<quint8>(payload[i]) ^ key[i % 4]);
            }
        }
        m_buffer.remove(0, pos + static_cast<int>(len));

        if (WebSocketFrame::isControlOpcode(opcode)) {
            // RFC 6455 5.5: control frames are never fragmented and carry at most 125 bytes.
            if (!fin) {
                return fail(QStringLiteral("fragmented control frame"), WebSocketFrame::CloseProtocolError);
            }
            if (payload.size() > kMaxControlPayload) {
                return fail(QStringLiteral("control frame payload over 125 bytes"), WebSocketFrame::CloseProtocolError);
            }
            if (opcode != WebSocketFrame::OpClose && opcode != WebSocketFrame::OpPing &&
                opcode != WebSocketFrame::OpPong) {
                return fail(QStringLiteral("unknown control opcode %1").arg(opcode),
                            WebSocketFrame::CloseProtocolError);
            }
            // Delivered immediately, even mid-message: reassembly state is untouched.
            out.opcode = opcode;
            out.payload = payload;
            return Status::Ready;
        }

        if (opcode == WebSocketFrame::OpContinuation) {
            if (!m_inFragment) {
                return fail(QStringLiteral("continuation frame with nothing to continue"),
                            WebSocketFrame::CloseProtocolError);
            }
        } else if (opcode == WebSocketFrame::OpText || opcode == WebSocketFrame::OpBinary) {
            if (m_inFragment) {
                return fail(QStringLiteral("new data frame while a fragmented message is open"),
                            WebSocketFrame::CloseProtocolError);
            }
        } else {
            return fail(QStringLiteral("unknown data opcode %1").arg(opcode), WebSocketFrame::CloseProtocolError);
        }

        if (!m_inFragment) {
            m_fragmentOpcode = opcode;
            m_fragment.clear();
        }

        if (m_fragment.size() + payload.size() > m_maxMessageBytes) {
            return fail(QStringLiteral("reassembled message exceeds the %1 byte limit").arg(m_maxMessageBytes),
                        WebSocketFrame::CloseTooBig);
        }
        m_fragment.append(payload);

        if (!fin) {
            m_inFragment = true;
            continue; // more fragments to come; try to parse the next frame already buffered
        }

        out.opcode = m_fragmentOpcode;
        out.payload = m_fragment;
        m_inFragment = false;
        m_fragment.clear();
        return Status::Ready;
    }
}
