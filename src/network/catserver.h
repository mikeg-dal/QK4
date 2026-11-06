#ifndef CATSERVER_H
#define CATSERVER_H

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>

class RadioState;
class CatPushBroadcaster;

/**
 * @brief TCP server that speaks native K4 CAT protocol
 *
 * Allows external applications (WSJT-X, MacLoggerDX, etc.) to connect
 * using their built-in Elecraft K4 support. Commands are either:
 * - Answered from RadioState cache (GET commands)
 * - Forwarded to the real K4 through catCommandReceived (SET commands)
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

    // Every frame the radio sends, so a query that has to be answered by the radio can be. Several
    // frames may arrive in one string.
    void onRadioResponse(const QString &response);

    // Whether the radio link is up (authenticated). While it is down a client's SET is dropped, not
    // forwarded or applied. Down until set.
    void setRadioLinkUp(bool up) { m_radioLinkUp = up; }

    // How long a client waits for the radio before its query is dropped unanswered.
    static constexpr int kRadioQueryTimeoutMs = 1000;

signals:
    void started(quint16 port);
    void stopped();
    void clientConnected(const QString &address);
    void clientDisconnected(const QString &address);
    void errorOccurred(const QString &error);

    // Emitted when a CAT command needs to be sent to the real K4
    void catCommandReceived(const QString &command);

    // A query only the radio can answer (AG;). Send it as is; the reply comes back through
    // onRadioResponse(). Unlike catCommandReceived this is never applied to RadioState.
    void radioQueryRequested(const QString &command);

    // Emitted when external app requests PTT via TX;/RX; commands
    // This controls the audio input gate, not direct K4 PTT
    void pttRequested(bool on);

private slots:
    void onNewConnection();

private:
    QByteArray handleCommand(const QString &cmd, QTcpSocket *client);

    bool m_radioLinkUp = false;

    struct ClientState {
        QByteArray buffer;
        int aiMode = 0;
    };

    // Answer this client from the radio: queue it under `key` (e.g. "AG", "AG$") and ask the radio,
    // unless a query for the same key is already in flight - one answer serves every waiter.
    void askRadio(const QString &key, QTcpSocket *client);

    struct PendingRadioQuery {
        QList<QPointer<QTcpSocket>> waiters;
        quint64 generation = 0;
    };
    QHash<QString, PendingRadioQuery> m_pendingRadioQueries;
    quint64 m_radioQueryGeneration = 0;

    // Release PTT if this socket is the one that asserted it. Called when a client disconnects
    // and when the server stops.
    void releasePttIfOwner(QTcpSocket *client);

    QTcpServer *m_server;
    RadioState *m_radioState;
    QHash<QTcpSocket *, ClientState> m_clients;

    // The client whose TX; asserted PTT, or null. Tracked only so a client that goes away without
    // sending RX; does not leave the transmitter keyed — any client's RX; still releases,
    // whoever keyed, because that is the fail-safe direction.
    QTcpSocket *m_pttOwner = nullptr;
    CatPushBroadcaster *m_broadcaster = nullptr;
    quint16 m_port = 0;
};

#endif // CATSERVER_H
