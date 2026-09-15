#ifndef TCICONTROLLER_H
#define TCICONTROLLER_H

#include <QObject>
#include <QString>

class AudioController;
class ConnectionController;
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
    TciController(AudioController *audioController, ConnectionController *connectionController, RadioState *radioState,
                  QObject *parent = nullptr);
    ~TciController();

    // Both marshal to the TCI thread. start() is idempotent.
    void start(quint16 port, bool loopbackOnly = true);
    void stop();

    bool isListening() const;
    int clientCount() const;

signals:
    void listeningChanged(bool listening, quint16 port);
    void clientCountChanged(int count);

private:
    // Reads RadioState on the MAIN thread and pushes a whole snapshot across to the TCI thread.
    // RadioState is main-thread-only and CI-enforced with no locking on its getters, so the TCI
    // thread must never touch it - it works from its own copy.
    void publishSnapshot();

    // Send one CatFrames-built command to the radio and echo it into RadioState optimistically,
    // exactly as CatServer's wiring does for an external CAT client.
    void applyCat(const QByteArray &frame);

    AudioController *m_audioController;
    ConnectionController *m_connectionController;
    RadioState *m_radioState;
    TciServer *m_server;
    TciAudioBridge *m_bridge;
    QThread *m_tciThread = nullptr;
};

#endif // TCICONTROLLER_H
