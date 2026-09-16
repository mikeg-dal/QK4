#ifndef TCICONTROLLER_H
#define TCICONTROLLER_H

#include <QObject>
#include <QString>
#include <QVector>

#include "network/tciclientinfo.h"

class AudioController;
class ConnectionController;
class MenuController;
class QThread;
class RadioState;
class TciAudioBridge;
class TciServer;

/**
 * @brief Owns the TCI thread, the TCI server and the audio bridge. Task-level API only.
 *
 * TCI lets an external program (WSJT-X and friends) reach the K4 through QK4 for both control and
 * audio, replacing the virtual serial port plus loopback sound card arrangement. This controller is
 * the seam between that server and the rest of the app.
 *
 * Threading:
 *   - TciServer and TciAudioBridge are moved to `m_tciThread` at construction and never touched
 *     directly from another thread; every public method here marshals.
 *   - RX audio arrives from AudioController::rxAudioAvailable on the I/O thread and crosses to the
 *     TCI thread by queued connection, so resampling is paid off the I/O thread - which also
 *     carries the K4 control stream.
 *   - RadioState is NOT read from the TCI thread. It is main-thread-only and CI-enforced
 *     (CONVENTIONS.md rule 4); the server keeps its own snapshot instead.
 *
 * The listener is off unless started. An always-on port would change behaviour for every user of
 * the app, and nothing else in QK4 opens a socket without being asked.
 *
 * See docs/tci-server-design.md.
 */
class TciController : public QObject {
    Q_OBJECT

public:
    // menuController may be null; without it tune power falls back to the drive level, since the
    // K4 keeps tune power in the menu rather than in a command of its own.
    TciController(AudioController *audioController, ConnectionController *connectionController, RadioState *radioState,
                  MenuController *menuController = nullptr, QObject *parent = nullptr);
    ~TciController();

    // Both marshal to the TCI thread. start() is idempotent.
    void start(quint16 port, bool loopbackOnly = true);
    void stop();

    // Both answer from a MAIN-THREAD CACHE, never by reaching into the server.
    //
    // WHY: the server and its sessions live on the TCI thread. clientCount() used to return
    // WebSocketServer::m_sessions.size(), and the options page calls it from the main thread while
    // that QHash is being inserted into and erased from by socket events on the TCI thread. That is
    // a data race, not a stale read - QHash::size() dereferences d, and an insert that rehashes
    // frees the old d. The cache is fed by clientCountChanged, which is the same signal that
    // prompted the re-read, so it carries no less information.
    bool isListening() const { return m_listening; }
    int clientCount() const { return m_clientCount; }

    // Same rule, same reason: a copy pushed across by clientsChanged, never a read of the server's
    // QHash from here.
    QVector<TciClientInfo> clients() const { return m_clients; }

    // Carrying audio is separable from carrying CAT. Off means no RX frames are sent and no TX
    // audio is accepted; the control half keeps working.
    // Call when QK4's own audio levels move. rx_volume reports QK4's mix, and nothing in
    // RadioState changes when a slider does, so there is no radio event to ride on.
    void audioLevelsChanged();

    void setAudioEnabled(bool enabled);
    bool audioEnabled() const { return m_audioEnabled; }

signals:
    void listeningChanged(bool listening, quint16 port);
    void clientCountChanged(int count);
    void clientsChanged(const QVector<TciClientInfo> &clients);

private:
    // Reads RadioState on the MAIN thread and pushes a whole snapshot across to the TCI thread.
    // RadioState is main-thread-only and CI-enforced with no locking on its getters, so the TCI
    // thread must never touch it - it works from its own copy.
    void publishSnapshot();

    // Reads the meters on the MAIN thread and pushes them across. Separate from publishSnapshot
    // because meters move continuously and state does not: the server stores these and emits them
    // on its own timer, rather than broadcasting every reading.
    void publishSensors();

    // Send one CatFrames-built command to the radio and echo it into RadioState optimistically,
    // exactly as CatServer's wiring does for an external CAT client.
    void applyCat(const QByteArray &frame);

    AudioController *m_audioController;
    ConnectionController *m_connectionController;
    bool m_audioEnabled = true;
    RadioState *m_radioState;
    TciServer *m_server;
    TciAudioBridge *m_bridge;
    QThread *m_tciThread = nullptr;

    MenuController *m_menuController;

    // Main-thread only. See isListening()/clientCount().
    bool m_listening = false;
    int m_clientCount = 0;
    QVector<TciClientInfo> m_clients;
};

#endif // TCICONTROLLER_H
