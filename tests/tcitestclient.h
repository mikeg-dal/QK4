#ifndef TESTS_TCITESTCLIENT_H
#define TESTS_TCITESTCLIENT_H

#include <QtTest>

#include <QTcpSocket>

#include "network/websocketframe.h"

// A TCI client over a real socket. Reads drive the shared event loop, because the server under
// test lives in this same thread — waitForReadyRead would pump only this socket and deadlock.
class TciTestClient {
public:
    static constexpr int kTimeoutMs = 5000;

    bool connectTo(quint16 port) {
        m_socket.connectToHost(QHostAddress::LocalHost, port);
        if (!m_socket.waitForConnected(kTimeoutMs)) {
            return false;
        }
        m_socket.write("GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\n"
                       "Connection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                       "Sec-WebSocket-Version: 13\r\n\r\n");
        m_socket.flush();

        QByteArray header;
        QElapsedTimer timer;
        timer.start();
        while (!header.contains("\r\n\r\n") && timer.elapsed() < kTimeoutMs) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            header.append(m_socket.readAll());
        }
        const int end = header.indexOf("\r\n\r\n");
        if (end < 0) {
            return false;
        }
        const QByteArray leftover = header.mid(end + 4);
        if (!leftover.isEmpty()) {
            m_decoder.append(leftover);
        }
        return header.startsWith("HTTP/1.1 101");
    }

    void sendBinary(const QByteArray &payload) {
        m_socket.write(WebSocketFrame::encode(WebSocketFrame::OpBinary, payload, /*mask=*/true));
        m_socket.flush();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }

    void send(const QByteArray &payload) {
        m_socket.write(WebSocketFrame::encode(WebSocketFrame::OpText, payload, /*mask=*/true));
        m_socket.flush();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }

    bool next(WebSocketDecoder::Message &out, int ms = kTimeoutMs) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < ms) {
            if (m_decoder.next(out) == WebSocketDecoder::Status::Ready) {
                return true;
            }
            if (m_socket.bytesAvailable() > 0) {
                m_decoder.append(m_socket.readAll());
                continue;
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        return m_decoder.next(out) == WebSocketDecoder::Status::Ready;
    }

    // Collects text messages until one of them is `terminator`.
    QStringList collectUntil(const QByteArray &terminator, int ms = kTimeoutMs) {
        QStringList out;
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < ms) {
            WebSocketDecoder::Message m;
            if (!next(m, 500)) {
                continue;
            }
            if (m.opcode != WebSocketFrame::OpText) {
                continue;
            }
            out << QString::fromUtf8(m.payload);
            if (m.payload == terminator) {
                break;
            }
        }
        return out;
    }

    void close() { m_socket.abort(); }

private:
    QTcpSocket m_socket;
    WebSocketDecoder m_decoder{/*requireMask=*/false};
};

#endif // TESTS_TCITESTCLIENT_H
