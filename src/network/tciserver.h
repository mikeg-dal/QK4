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

#include "network/tciclientinfo.h"
#include "network/tciprotocol.h"
#include "network/tciradiostate.h"

class WebSocketServer;

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

    // Receiver 0 is the K4's Main RX, receiver 1 its Sub RX. See tciradiostate.h for why the Sub
    // RX is a receiver rather than a channel.
    static constexpr int MAIN_RECEIVER = TciRadio::MAIN_RECEIVER;
    static constexpr int SUB_RECEIVER = TciRadio::SUB_RECEIVER;
    static constexpr int RECEIVER_COUNT = TciRadio::RECEIVER_COUNT;

    // Channel 0 is a receiver's own VFO; channel 1 of the MAIN receiver is the split transmit VFO.
    static constexpr int CHANNEL_A = TciRadio::CHANNEL_A;
    static constexpr int CHANNEL_B = TciRadio::CHANNEL_B;

    explicit TciServer(QObject *parent = nullptr);
    ~TciServer() override;

    Q_INVOKABLE bool start(quint16 port = DEFAULT_PORT, bool loopbackOnly = true);
    Q_INVOKABLE void stop();

    bool isListening() const;
    quint16 port() const;
    int clientCount() const;
    int audioClientCount() const { return m_audioClients.size(); }

    // The connected clients, ordered oldest first. Returned by value: the caller is on another
    // thread in the only case that matters, and a reference into a QHash the socket events are
    // erasing from is exactly the data race clientCount() was fixed for.
    QVector<TciClientInfo> clients() const;

    // Replaces the snapshot and broadcasts whatever actually moved.
    //
    // WHY diff rather than broadcast everything: a chatty radio would otherwise flood every client
    // on every CAT echo. A message that arrives should mean something changed.
    void setSnapshot(const TciRadioSnapshot &snapshot);
    const TciRadioSnapshot &snapshot() const { return m_snapshot; }

    // The commands sent to a freshly connected client, in order, ready-last.
    QStringList initBurst() const;

    // Replaces the stored telemetry. Never broadcasts - the sensor timer decides when anything
    // goes out, and to whom. Cheap enough to call on every meter update.
    void setSensors(const TciSensorReadings &readings);
    const TciSensorReadings &sensors() const { return m_sensors; }

    // Frame interleaved stereo float samples as RX_AUDIO and send to every client that asked for
    // audio. A no-op when nobody has.
    Q_INVOKABLE void sendRxAudio(const std::vector<float> &interleavedStereo, int sampleRate = 48000);

signals:
    // A client asked for audio, or stopped. The audio source uses these to start and stop work
    // rather than producing frames nobody wants.
    void audioStartRequested(int receiver);
    void audioStopRequested();
    void clientCountChanged(int count);

    // The roster, whenever it changes - a client arriving or leaving, or one of them saying
    // something. Throttled for traffic; see noteClientActivity.
    void clientsChanged(const QVector<TciClientInfo> &clients);

    // A client asserted or released PTT via `trx:<n>,<bool>`.
    //
    // WHY this is a gate and not a CAT command: the K4 keys when TX audio starts arriving, which is
    // what CatServer's TX/RX handling already relies on ("Don't forward to K4 - the audio stream
    // itself triggers K4 TX", catserver.cpp:317-330). Sending a PTT command as well would fight it.
    void pttRequested(bool active);

    // One block of client transmit audio, already reduced to 48 kHz mono.
    void txAudioReceived(const QByteArray &f32Mono48k);

    // CAT sets from a client, addressed by receiver. For the main receiver, channel 0 is its own
    // VFO and channel 1 the transmit VFO; the sub receiver has only channel 0.
    void setFrequencyRequested(int receiver, int channel, qint64 hz);
    void setModulationRequested(int receiver, const QString &modulation);
    void setSplitRequested(bool enabled);

    // A client asked to turn the Sub RX (receiver 1) on or off.
    void setSubReceiverRequested(bool enabled);

    // Generic SETs, normalised. The server validates arity, receiver and range; the controller
    // maps the name to a CatFrames builder. Kept as three typed signals rather than one variant
    // carrier so a wrong-typed value cannot reach the radio, and so the TCI layer still never
    // spells a K4 command - see docs/tci-server-design.md.
    void setBoolRequested(int receiver, const QString &name, bool value);
    void setIntRequested(int receiver, const QString &name, int value);
    void setFilterBandRequested(int receiver, int lowHz, int highHz);
    void setNoiseBlankerParamRequested(int level, int filterWidth);
    void setVfoLockRequested(int receiver, bool locked);

private slots:
    void onClientConnected(int clientId, const QString &peerAddress);
    void onClientDisconnected(int clientId);
    void onTextMessageReceived(int clientId, const QString &text);
    void onBinaryMessageReceived(int clientId, const QByteArray &payload);
    void onChronoTick();
    void onSensorTick();

private:
    // Answers a query from the snapshot without touching the radio. Returns true if the command was
    // recognised and answered. Read-only by design - the matching SETs move the radio and are
    // deferred until they can be bench-tested. See docs/tci-server-design.md, phase 8.
    bool answerReadOnly(int clientId, const TciProtocol::Command &command);

    // Acts on a SET inside the per-receiver group, when QK4 has a way to send it.
    void applySet(int receiver, const QString &name, const TciProtocol::Command &command, int valueIndex);

    // The per-receiver half of the init burst, so the burst builder stays readable with two.
    QStringList receiverBurst(int receiver) const;

    // Starts or stops the sensor timer to match the current subscriptions.
    void updateSensorTimer();

    // Every text message OUT goes through these two, so the roster sees it. Calling
    // m_socketServer->sendText directly would still reach the client and silently skip the record -
    // which is the whole reason the wrappers exist rather than a note at each of the ~75 call
    // sites. Binary audio is NOT routed through them; see TciClientInfo.
    void sendTo(int clientId, const QString &text);
    void broadcast(const QString &text);

    // Records what passed between QK4 and one client, and which way. No signal: announcing is
    // separate so a broadcast can update every row and announce once.
    void recordClientMessage(int clientId, const QString &message, bool outbound);

    // Emits clientsChanged, subject to the throttle. `force` skips it, for the changes that alter
    // the ROW SET rather than a cell: a client connecting or disconnecting must show up at once.
    void announceClients(bool force);

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

    // What the options page lists. Keyed by client id, ordered on the way out.
    QHash<int, TciClientInfo> m_clients;
    // Throttles clientsChanged. TX audio arrives ~47 times a second and the table shows a time to
    // the second, so announcing every frame would be work nobody can see.
    QElapsedTimer m_clientsAnnounceClock;

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

    // Sensor subscriptions, per client and per direction: a client asks for RX levels and TX
    // readings separately, and must not be sent the one it did not ask for.
    QSet<int> m_rxSensorClients;
    QSet<int> m_txSensorClients;
    TciSensorReadings m_sensors;
    QTimer *m_sensorTimer;
    int m_sensorIntervalMs = 200;

    // Counters behind the periodic log summaries. Blocks are far too frequent to log individually.
    qint64 m_rxBlocks = 0;
    qint64 m_txBlocks = 0;
    qint64 m_chronoSent = 0;
};

#endif // NETWORK_TCISERVER_H
