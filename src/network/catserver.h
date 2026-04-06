#ifndef CATSERVER_H
#define CATSERVER_H

#include <QList>
#include <QMap>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>

class RadioState;
class TcpClient;

/**
 * @brief TCP server that speaks native K4 CAT protocol
 *
 * Allows external applications (WSJT-X, MacLoggerDX, etc.) to connect
 * using their built-in Elecraft K4 support. Commands are either:
 * - Answered from RadioState cache (GET commands)
 * - Forwarded to real K4 via TcpClient (SET commands)
 *
 * Native K4 CAT passthrough - no protocol translation needed.
 */
class CatServer : public QObject {
    Q_OBJECT

public:
    explicit CatServer(RadioState *state, QObject *parent = nullptr);
    ~CatServer();

    bool start(quint16 port = 9299);
    void stop();
    bool isListening() const;
    quint16 port() const;
    int clientCount() const;

    // Set the TcpClient for forwarding SET commands to real K4
    void setTcpClient(TcpClient *client);

signals:
    void started(quint16 port);
    void stopped();
    void clientConnected(const QString &address);
    void clientDisconnected(const QString &address);
    void errorOccurred(const QString &error);

    // Emitted when a CAT command needs to be sent to the real K4
    void catCommandReceived(const QString &command);

    // Emitted when external app requests PTT via TX;/RX; commands
    // This controls the audio input gate, not direct K4 PTT
    void pttRequested(bool on);

private slots:
    void onNewConnection();

private:
    QString handleCommand(const QString &cmd);
    QString buildFrequencyResponse(quint64 freq, const QString &prefix) const;
    QString buildModeResponse(int mode) const;

    QTcpServer *m_server;
    RadioState *m_radioState;
    TcpClient *m_tcpClient = nullptr;
    QList<QTcpSocket *> m_clients;
    QMap<QTcpSocket *, QByteArray> m_clientBuffers;
    quint16 m_port = 0;
};

#endif // CATSERVER_H
