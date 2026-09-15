#ifndef NETWORK_TCISERVER_H
#define NETWORK_TCISERVER_H

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <vector>

#include "network/tciprotocol.h"

class WebSocketServer;

// Radio state the init burst and broadcasts are built from.
//
// WHY a snapshot struct rather than a RadioState pointer: RadioState is main-thread-only and
// CI-enforced (CONVENTIONS.md rule 4), and its getters carry no locking. The TCI server runs on its
// own thread, so it keeps its own copy, fed by queued signals. Building the init burst from one
// pass over this struct also keeps it self-consistent - RadioState emits per-field signals with no
// batch boundary, so live reads could seed a client with a frequency and mode that never coexisted.
struct TciRadioSnapshot {
    qint64 vfoAHz = 14074000;
    qint64 vfoBHz = 14074000;
    QString modulation = QStringLiteral("usb");
    bool split = false;
    bool transmitting = false;
    bool rit = false;
    bool xit = false;
    int ritOffsetHz = 0;
    int xitOffsetHz = 0;
    int filterLowHz = 100;
    int filterHighHz = 2800;
    int drive = 100;
    int tuneDrive = 100;
    int micLevel = 50;

    // Channel 1 is the transmit VFO. With split off it must still report something coherent - the
    // receive frequency - rather than the 0 a blank VFO B holds, which a client will try to tune to.
    qint64 txChannelHz() const { return (split && vfoBHz > 0) ? vfoBHz : vfoAHz; }
};

// TCI protocol server.
//
// Scope is deliberately the minimum that reaches audio: the init burst, audio_start/audio_stop, the
// sensor echoes, and split_enable accepted without acting. CAT SETs (vfo, modulation, trx) arrive
// in a later phase; an unhandled TCI command is silence, not an error, so deferring them is safe.
// See docs/tci-server-design.md.
//
// Thread affinity: create, start and drive this on the thread that owns it. It is not thread-safe.
class TciServer : public QObject {
    Q_OBJECT

public:
    // The TCI convention, and what WSJT-X defaults to.
    static constexpr quint16 DEFAULT_PORT = 50001;

    // Only receiver 0 exists while the design is scoped to the main VFO. WSJT-X ignores a declared
    // trx_count, so anything addressing receiver 1 has to be refused explicitly rather than steered.
    static constexpr int ONLY_RECEIVER = 0;

    explicit TciServer(QObject *parent = nullptr);
    ~TciServer() override;

    Q_INVOKABLE bool start(quint16 port = DEFAULT_PORT, bool loopbackOnly = true);
    Q_INVOKABLE void stop();

    bool isListening() const;
    quint16 port() const;
    int clientCount() const;
    int audioClientCount() const { return m_audioClients.size(); }

    void setSnapshot(const TciRadioSnapshot &snapshot) { m_snapshot = snapshot; }
    const TciRadioSnapshot &snapshot() const { return m_snapshot; }

    // The commands sent to a freshly connected client, in order, ready-last.
    QStringList initBurst() const;

    // Frame interleaved stereo float samples as RX_AUDIO and send to every client that asked for
    // audio. A no-op when nobody has.
    Q_INVOKABLE void sendRxAudio(const std::vector<float> &interleavedStereo, int sampleRate = 48000);

signals:
    // A client asked for audio, or stopped. The audio source uses these to start and stop work
    // rather than producing frames nobody wants.
    void audioStartRequested(int receiver);
    void audioStopRequested();
    void clientCountChanged(int count);

private slots:
    void onClientConnected(int clientId, const QString &peerAddress);
    void onClientDisconnected(int clientId);
    void onTextMessageReceived(int clientId, const QString &text);

private:
    WebSocketServer *m_socketServer;
    TciRadioSnapshot m_snapshot;
    // Clients that sent audio_start. Per-client because audio is opt-in and a client that never
    // asked must not be sent frames.
    QSet<int> m_audioClients;
    // One parser per client: a command can straddle frames, so buffers must not be shared.
    QHash<int, TciProtocol::Parser> m_parsers;
};

#endif // NETWORK_TCISERVER_H
