#ifndef NETWORK_WEBSOCKETSERVER_H
#define NETWORK_WEBSOCKETSERVER_H

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>

#include "network/websocketframe.h"

class QTcpServer;
class QTcpSocket;

// RFC 6455 server transport. Knows nothing about TCI.
//
// Accepts connections, completes the HTTP upgrade, and turns the byte stream into whole messages
// using WebSocketDecoder. Answers PING with PONG and echoes CLOSE itself; everything else is
// surfaced as a signal.
//
// WHY hand-rolled instead of Qt6::WebSockets: that module ships with Homebrew Qt but is absent from
// the Linux, Windows and flatpak dependency sets, so adopting it means editing five upstream-owned
// files for one feature. The server subset is small and Qt6::Network is already a dependency. See
// docs/tci-server-design.md.
//
// Thread affinity: create and start() this on the thread that will own it. QTcpServer and its
// sockets are not thread-safe, so every public method must run on that thread — the TCI thread,
// per the design. Callers on other threads marshal with QMetaObject::invokeMethod.
class WebSocketServer : public QObject {
    Q_OBJECT

public:
    // Mirrors AetherSDR's limits. A TCI text command is tens of bytes and an audio frame ~8 KiB,
    // so these are generous rather than tight.
    static constexpr int MAX_CLIENTS = 8;
    // An HTTP upgrade request is a few hundred bytes. Capped so a peer that never sends the
    // terminating blank line cannot grow the buffer without bound (CONVENTIONS.md rule 5).
    static constexpr int MAX_HANDSHAKE_BYTES = 8 * 1024;

    // OUTBOUND BACKPRESSURE. Qt buffers whatever the peer has not read, in our process, without
    // limit. RX audio runs about 384 kB/s per subscriber, so a client that stops reading grows
    // QK4's memory for as long as it stays connected. CONVENTIONS.md rule 5 requires an explicit
    // limit on any buffer fed by an external source; this is the one direction that had none.
    //
    // Two limits, because audio and control must not be treated alike. Past the soft limit audio
    // frames are DROPPED: for a live stream, late audio is worthless and dropping is the correct
    // response, where queueing is not. Control frames are small and carry state a client cannot
    // re-derive, so they are never shed. Past the hard limit the peer is not reading anything at
    // all, and the session goes.
    static constexpr qint64 SEND_QUEUE_AUDIO_DROP_BYTES = 256 * 1024;  // ~0.7 s of RX audio
    static constexpr qint64 SEND_QUEUE_HARD_LIMIT_BYTES = 1024 * 1024; // rule 5's default cap

    // The decision, separated from the socket so it can be tested without one - the same reason
    // TransmitOwner is split out of TransmitController. Provoking the real thing needs a peer that
    // connects and then never reads, which a test client cannot be.
    enum class SendDecision { Send, DropFrame, DropSession };
    static SendDecision decideSend(qint64 queuedBytes, bool sheddable);

    explicit WebSocketServer(QObject *parent = nullptr);
    ~WebSocketServer() override;

    // loopbackOnly binds 127.0.0.1. WHY that is the default: CatServer binds every interface
    // unconditionally, which exposes radio control to the whole LAN; this listener does not repeat
    // that without the operator opting in.
    Q_INVOKABLE bool start(quint16 port, bool loopbackOnly = true);
    Q_INVOKABLE void stop();

    bool isListening() const;
    quint16 port() const;
    int clientCount() const { return m_sessions.size(); }
    QString errorString() const { return m_errorString; }

    // Ignored for an unknown id, so a send racing a disconnect is not an error.
    Q_INVOKABLE void sendText(int clientId, const QString &text);
    Q_INVOKABLE void sendBinary(int clientId, const QByteArray &payload);
    Q_INVOKABLE void broadcastText(const QString &text);

    Q_INVOKABLE void closeClient(int clientId, quint16 code = WebSocketFrame::CloseNormal,
                                 const QString &reason = QString());

signals:
    void started(quint16 port);
    void stopped();
    // peerEndpoint is "host:port" ("[host]:port" for IPv6) - the port included because it is what
    // distinguishes two connections from the same host.
    void clientConnected(int clientId, const QString &peerEndpoint);
    void clientDisconnected(int clientId);
    void textMessageReceived(int clientId, const QString &text);
    void binaryMessageReceived(int clientId, const QByteArray &payload);
    void errorOccurred(const QString &error);

private slots:
    void onNewConnection();

private:
    struct Session {
        QTcpSocket *socket = nullptr;
        bool upgraded = false;
        QByteArray handshakeBuffer;
        WebSocketDecoder decoder{/*requireMask=*/true};
        // Reported once per episode rather than per frame: a wedged peer would otherwise produce a
        // log line every audio block, burying the one line that matters.
        qint64 droppedAudioFrames = 0;
        bool reportedShedding = false;
    };

    // These take an id, never a Session& : a held reference cannot survive a signal emission,
    // because a consumer's slot can erase from or insert into m_sessions. See websocketserver.cpp.
    void onReadyRead(int clientId);
    void onDisconnected(int clientId);
    bool tryUpgrade(int clientId);
    void pumpFrames(int clientId);
    void dropSession(int clientId, quint16 code, const QString &why);
    void sendFrame(int clientId, quint8 opcode, const QByteArray &payload);

    // The single write path, so backpressure cannot be bypassed by adding another sender.
    // `sheddable` marks a frame that may be dropped rather than queued - true for audio only.
    // Returns false if the frame was not written, for any reason.
    bool writeFrame(int clientId, const QByteArray &frame, bool sheddable);

    QTcpServer *m_server;
    QHash<int, Session> m_sessions;
    int m_nextClientId = 1;
    QString m_errorString;
};

#endif // NETWORK_WEBSOCKETSERVER_H
