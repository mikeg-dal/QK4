#ifndef TCPCLIENT_H
#define TCPCLIENT_H

#include <QElapsedTimer>
#include <QObject>
#include <QSslSocket>
#include <QThread>
#include <QTimer>
#include <atomic>
#include "protocol.h"

class TcpClient : public QObject {
    Q_OBJECT

public:
    enum ConnectionState { Disconnected, Connecting, Authenticating, Connected };
    Q_ENUM(ConnectionState)

    explicit TcpClient(QObject *parent = nullptr);
    ~TcpClient();

    Q_INVOKABLE void connectToHost(const QString &host, quint16 port, const QString &password, bool useTls = false,
                                   const QString &identity = QString(), int encodeMode = 3, int streamingLatency = 3);
    Q_INVOKABLE void disconnectFromHost();
    bool isConnected() const;
    ConnectionState connectionState() const;
    bool isUsingTls() const { return m_useTls; }

    Q_INVOKABLE void sendCAT(const QString &command);
    Q_INVOKABLE void sendRaw(const QByteArray &data);

    Protocol *protocol() { return m_protocol; }

    int latencyMs() const { return m_latencyMs; }

signals:
    void stateChanged(ConnectionState state);
    void connected();
    void disconnected();
    void errorOccurred(const QString &error);
    void authenticated();
    void authenticationFailed();
    void latencyChanged(int ms);

private slots:
    void onSocketConnected();
    void onSocketEncrypted();
    void onSocketDisconnected();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);
    void onSslErrors(const QList<QSslError> &errors);
    void onPreSharedKeyAuthenticationRequired(QSslPreSharedKeyAuthenticator *authenticator);
    void onConnectTimeout();
    void onAuthTimeout();
    void onPingTimer();
    void onCatResponse(const QString &response);

private:
    void setState(ConnectionState state);
    void sendAuthentication();
    void startPingTimer();
    void stopPingTimer();

    QSslSocket *m_socket;
    Protocol *m_protocol;
    QTimer *m_authTimer;
    QTimer *m_connectTimer;
    QTimer *m_pingTimer;

    QString m_host;
    quint16 m_port;
    QString m_password; // Also used as PSK when TLS enabled
    bool m_useTls;
    QString m_identity;     // TLS-PSK identity (optional)
    int m_encodeMode;       // Audio encode mode (0-3)
    int m_streamingLatency; // Remote streaming audio latency (0-7)
    ConnectionState m_state;
    std::atomic<bool> m_connected{false}; // Thread-safe read for isConnected()
    bool m_authResponseReceived;

    QElapsedTimer m_pingElapsed;
    int m_latencyMs = -1;
};

#endif // TCPCLIENT_H
