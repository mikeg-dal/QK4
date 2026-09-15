#ifndef NETWORK_TCISERVER_H
#define NETWORK_TCISERVER_H

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

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

    // The K4's Sub RX, which tunes VFO B. In TCI terms this is CHANNEL 1 of receiver 0, not a
    // second receiver: the spec's RX_CHANNEL_ENABLE is described as "enable additional receive
    // channel (VFO B)" and says channel A is always on. That matches the radio exactly, and it is
    // why trx_count stays 1 while channels_count is 2.
    bool subEnabled = false;
    QString modulationB = QStringLiteral("usb");
    bool rit = false;
    bool xit = false;

    // ONE offset, matching the radio. The K4 has a single RO register shared by RIT and XIT, with
    // RT and XT as independent enables, and RadioState models it the same way
    // (ritXitOffset(), ritXitChanged(rit, xit, offset)). TCI defines RIT_OFFSET and XIT_OFFSET as
    // two values; they are reported from this one and can never be set independently.
    // See docs/tci-command-coverage.md section 4.1.
    int ritXitOffsetHz = 0;

    int filterLowHz = 100;
    int filterHighHz = 2800;
    int drive = 100;
    int tuneDrive = 100;
    int micLevel = 50;

    // TCI defines exactly three AGC modes: normal, fast, off. Anything else is unparseable to a
    // client matching the documented vocabulary.
    QString agcMode = QStringLiteral("normal");

    bool sqlEnabled = false;
    // TCI squelch is an ABSOLUTE threshold in dBm, range -140..0. QK4 does not know the K4's
    // squelch threshold in dBm - the radio reports SQ as an arbitrary integer scale - so no
    // truthful conversion exists. -140 is "opens on anything", which is both in range and the
    // least misleading thing to claim. See docs/tci-command-coverage.md.
    int sqlLevelDbm = -140;

    // Channel 1 is the transmit VFO. With split off it must still report something coherent - the
    // receive frequency - rather than the 0 a blank VFO B holds, which a client will try to tune to.
    qint64 txChannelHz() const { return (split && vfoBHz > 0) ? vfoBHz : vfoAHz; }
};

// TCI protocol server.
//
// Implemented: the init burst, audio_start/audio_stop, RX and TX audio with TX_CHRONO pacing, the
// sensor echoes, and the CAT sets a digital-mode client needs - vfo, dds, modulation, trx (PTT) and
// split_enable. Everything else the burst declares is answered READ-ONLY from the snapshot by
// answerReadOnly: the matching SETs move real hardware and are deferred until they can be benched.
// An unhandled TCI command is silence, not an error, so deferring them is safe. See
// docs/tci-server-design.md, phase 8.
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

    // Channel 0 is VFO A (Main), channel 1 is VFO B (Sub, and the split transmit VFO). Both are
    // the same VFO B on a K4, which is why one channel index serves both jobs.
    static constexpr int CHANNEL_A = 0;
    static constexpr int CHANNEL_B = 1;

    explicit TciServer(QObject *parent = nullptr);
    ~TciServer() override;

    Q_INVOKABLE bool start(quint16 port = DEFAULT_PORT, bool loopbackOnly = true);
    Q_INVOKABLE void stop();

    bool isListening() const;
    quint16 port() const;
    int clientCount() const;
    int audioClientCount() const { return m_audioClients.size(); }

    // Replaces the snapshot and broadcasts whatever actually moved.
    //
    // WHY diff rather than broadcast everything: a chatty radio would otherwise flood every client
    // on every CAT echo. A message that arrives should mean something changed.
    void setSnapshot(const TciRadioSnapshot &snapshot);
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

    // A client asserted or released PTT via `trx:<n>,<bool>`.
    //
    // WHY this is a gate and not a CAT command: the K4 keys when TX audio starts arriving, which is
    // what CatServer's TX/RX handling already relies on ("Don't forward to K4 - the audio stream
    // itself triggers K4 TX", catserver.cpp:317-330). Sending a PTT command as well would fight it.
    void pttRequested(bool active);

    // One block of client transmit audio, already reduced to 48 kHz mono.
    void txAudioReceived(const QByteArray &f32Mono48k);

    // CAT sets from a client. channel 0 is the receive VFO, 1 the transmit VFO.
    void setFrequencyRequested(int channel, qint64 hz);
    void setModulationRequested(const QString &modulation);
    void setSplitRequested(bool enabled);

    // A client asked to turn the Sub RX (VFO B) on or off.
    void setSubReceiverRequested(bool enabled);

private slots:
    void onClientConnected(int clientId, const QString &peerAddress);
    void onClientDisconnected(int clientId);
    void onTextMessageReceived(int clientId, const QString &text);
    void onBinaryMessageReceived(int clientId, const QByteArray &payload);
    void onChronoTick();

private:
    // Answers a query from the snapshot without touching the radio. Returns true if the command was
    // recognised and answered. Read-only by design - the matching SETs move the radio and are
    // deferred until they can be bench-tested. See docs/tci-server-design.md, phase 8.
    bool answerReadOnly(int clientId, const TciProtocol::Command &command);

    void setPtt(int clientId, bool active);
    void startChrono(int clientId);
    void stopChrono();
    WebSocketServer *m_socketServer;
    TciRadioSnapshot m_snapshot;
    // Clients that sent audio_start. Per-client because audio is opt-in and a client that never
    // asked must not be sent frames.
    QSet<int> m_audioClients;
    // One parser per client: a command can straddle frames, so buffers must not be shared.
    QHash<int, TciProtocol::Parser> m_parsers;

    // PTT is owned by exactly one client at a time. -1 means nobody holds it.
    //
    // WHY ownership matters: losing the client that keyed must unkey (fail closed), while an
    // UNOWNED `trx:<n>,false` is a status report and must never unkey the operator or another
    // client.
    int m_pttOwner = -1;

    // TX_CHRONO pacing. WSJT-X sends no audio until asked and answers exactly one block per
    // request, so this clock is both the pacing and the flow control.
    //
    // WHY an accumulator rather than a fixed interval: the period is 21.333 ms and a 21 ms timer
    // runs ~1.6% fast, which warps digital-mode tones. Measured client tolerance is wide (65 ms
    // instantaneous jitter, 9% of frames back-to-back) but the long-run MEAN RATE must be right.
    QTimer *m_chronoTimer;
    QElapsedTimer m_chronoClock;
    qint64 m_chronoAccumNs = 0;
    int m_chronoClient = -1;

    // Counters behind the periodic log summaries. Blocks are far too frequent to log individually.
    qint64 m_rxBlocks = 0;
    qint64 m_txBlocks = 0;
    qint64 m_chronoSent = 0;
};

#endif // NETWORK_TCISERVER_H
