#include "network/tciserver.h"

#include <QLoggingCategory>

#include <cmath>

#include "network/tciaudioframe.h"
#include "network/websocketserver.h"

Q_LOGGING_CATEGORY(netTci, "net.tci")

namespace {

using namespace TciProtocol;

// What QK4 claims to support. Sent verbatim as free text because it is legitimately
// comma-separated - scrubbing it would corrupt the value.
const char kModulationsList[] = "usb,lsb,cw,cwr,am,sam,fm,nfm,digu,digl,rtty";
// WSJT-X matches on this string; a mangled one makes it halve transmit amplitude.
const char kProtocolIdentity[] = "ExpertSDR3,1.5";

// The K4's tuning range.
constexpr qint64 kVfoLowHz = 100000;
constexpr qint64 kVfoHighHz = 54000000;
constexpr int kIfLowHz = -48000;
constexpr int kIfHighHz = 48000;

// WSJT-X always treats TCI audio as 48 kHz regardless of what a server declares, so declaring
// anything else is misleading at best. See docs/tci-server-design.md.
constexpr int kAudioSampleRate = 48000;
constexpr int kAudioStreamSamples = TciAudioFrame::CHRONO_FLOATS;

// One chrono asks for CHRONO_FLOATS floats = 1024 stereo frames = 21.333 ms at 48 kHz.
constexpr qint64 kChronoPeriodNs =
    static_cast<qint64>(TciAudioFrame::CHRONO_FLOATS / 2) * 1000000000LL / kAudioSampleRate;
constexpr int kChronoPollMs = 5;

// Audio blocks are far too frequent to log individually - roughly 47 a second each way. Summarise
// instead, so a log can answer "is audio actually moving" without drowning everything else.
constexpr int kRxSummaryEveryBlocks = 200; // ~4.3 s
constexpr int kTxSummaryEveryBlocks = 100; // ~2.1 s

} // namespace

TciServer::TciServer(QObject *parent)
    : QObject(parent), m_socketServer(new WebSocketServer(this)), m_chronoTimer(new QTimer(this)) {
    connect(m_socketServer, &WebSocketServer::clientConnected, this, &TciServer::onClientConnected);
    connect(m_socketServer, &WebSocketServer::clientDisconnected, this, &TciServer::onClientDisconnected);
    connect(m_socketServer, &WebSocketServer::textMessageReceived, this, &TciServer::onTextMessageReceived);
    connect(m_socketServer, &WebSocketServer::binaryMessageReceived, this, &TciServer::onBinaryMessageReceived);

    // Poll faster than the period and emit from an accumulator, so scheduling jitter is absorbed
    // rather than accumulated into a rate error.
    m_chronoTimer->setTimerType(Qt::PreciseTimer);
    m_chronoTimer->setInterval(kChronoPollMs);
    connect(m_chronoTimer, &QTimer::timeout, this, &TciServer::onChronoTick);
}

TciServer::~TciServer() {
    disconnect(this);
    stop();
}

bool TciServer::start(quint16 port, bool loopbackOnly) {
    return m_socketServer->start(port, loopbackOnly);
}

void TciServer::stop() {
    // Unkey before tearing the listener down, so stopping the server can never leave the radio
    // transmitting.
    if (m_pttOwner != -1) {
        stopChrono();
        m_pttOwner = -1;
        m_snapshot.transmitting = false;
        emit pttRequested(false);
    }
    m_socketServer->stop();
    m_audioClients.clear();
    m_parsers.clear();
}

bool TciServer::isListening() const {
    return m_socketServer->isListening();
}

quint16 TciServer::port() const {
    return m_socketServer->port();
}

int TciServer::clientCount() const {
    return m_socketServer->clientCount();
}

void TciServer::setSnapshot(const TciRadioSnapshot &snapshot) {
    const TciRadioSnapshot previous = m_snapshot;
    m_snapshot = snapshot;

    if (clientCount() == 0) {
        return; // nobody to tell; the init burst will carry it
    }
    const QString trx = QString::number(ONLY_RECEIVER);

    if (snapshot.vfoAHz != previous.vfoAHz) {
        m_socketServer->broadcastText(
            message(QStringLiteral("vfo"), trx, QStringLiteral("0"), QString::number(snapshot.vfoAHz)));
        m_socketServer->broadcastText(message(QStringLiteral("dds"), trx, QString::number(snapshot.vfoAHz)));
    }
    // Channel 1 follows VFO B while split is on and the receive frequency otherwise, so it can move
    // when either changes.
    if (snapshot.txChannelHz() != previous.txChannelHz()) {
        m_socketServer->broadcastText(
            message(QStringLiteral("vfo"), trx, QStringLiteral("1"), QString::number(snapshot.txChannelHz())));
    }
    if (snapshot.modulation != previous.modulation) {
        m_socketServer->broadcastText(message(QStringLiteral("modulation"), trx, snapshot.modulation));
    }
    if (snapshot.split != previous.split) {
        m_socketServer->broadcastText(message(QStringLiteral("split_enable"), trx, boolText(snapshot.split)));
    }
    if (snapshot.transmitting != previous.transmitting) {
        m_socketServer->broadcastText(message(QStringLiteral("trx"), trx, boolText(snapshot.transmitting)));
    }
}

QStringList TciServer::initBurst() const {
    const QString trx = QString::number(ONLY_RECEIVER);
    const TciRadioSnapshot &s = m_snapshot;

    QStringList burst;
    burst << message(QStringLiteral("vfo_limits"), QString::number(kVfoLowHz), QString::number(kVfoHighHz))
          << message(QStringLiteral("if_limits"), QString::number(kIfLowHz), QString::number(kIfHighHz))
          // trx_count is 1 while only the main VFO is in scope. WSJT-X ignores it, so refusing
          // trx:1 explicitly is what actually keeps a misconfigured client honest.
          << message(QStringLiteral("trx_count"), QStringLiteral("1"))
          // Plural. The published PDF says CHANNEL_COUNT and the reference parser aborts on it.
          << message(QStringLiteral("channels_count"), QStringLiteral("2"))
          << message(QStringLiteral("device"), QStringLiteral("QK4"))
          << message(QStringLiteral("receive_only"), boolText(false))
          << messageFreeText(QStringLiteral("modulations_list"), QLatin1String(kModulationsList))
          << messageFreeText(QStringLiteral("protocol"), QLatin1String(kProtocolIdentity))
          << message(QStringLiteral("vfo"), trx, QStringLiteral("0"), QString::number(s.vfoAHz))
          << message(QStringLiteral("vfo"), trx, QStringLiteral("1"), QString::number(s.txChannelHz()))
          << message(QStringLiteral("dds"), trx, QString::number(s.vfoAHz))
          << message(QStringLiteral("modulation"), trx, s.modulation)
          << message(QStringLiteral("rx_enable"), trx, boolText(true))
          << message(QStringLiteral("rx_filter_band"), trx, QString::number(s.filterLowHz),
                     QString::number(s.filterHighHz))
          << message(QStringLiteral("rit_enable"), trx, boolText(s.rit))
          << message(QStringLiteral("xit_enable"), trx, boolText(s.xit))
          << message(QStringLiteral("rit_offset"), trx, QString::number(s.ritOffsetHz))
          << message(QStringLiteral("xit_offset"), trx, QString::number(s.xitOffsetHz))
          // Not decoration: the RF2K-S amplifier uses split_enable:0,false as its signal that VFO 0
          // is active, and reports "No TCI available" until it arrives.
          << message(QStringLiteral("split_enable"), trx, boolText(s.split))
          << message(QStringLiteral("lock"), trx, boolText(false))
          << message(QStringLiteral("sql_enable"), trx, boolText(false))
          << message(QStringLiteral("sql_level"), trx, QStringLiteral("20"))
          << message(QStringLiteral("agc_mode"), trx, QStringLiteral("med"))
          << message(QStringLiteral("rx_nb_enable"), trx, boolText(false))
          << message(QStringLiteral("rx_nr_enable"), trx, boolText(false))
          << message(QStringLiteral("rx_anf_enable"), trx, boolText(false))
          << message(QStringLiteral("rx_apf_enable"), trx, boolText(false))
          << message(QStringLiteral("mute"), trx, boolText(false))
          << message(QStringLiteral("tx_enable"), trx, boolText(true))
          // drive and tune_drive must always carry <trx>,<power>: a bare "drive:0;" crashes
          // ESDR3-mode WSJT-X and JTDX, which index args[1] unconditionally.
          << message(QStringLiteral("drive"), trx, QString::number(s.drive))
          << message(QStringLiteral("tune_drive"), trx, QString::number(s.tuneDrive))
          // mic_level, trx, volume are global single-argument commands - no receiver index.
          << message(QStringLiteral("mic_level"), QString::number(s.micLevel))
          << message(QStringLiteral("trx"), trx, boolText(s.transmitting))
          << message(QStringLiteral("volume"), QStringLiteral("0"))
          << message(QStringLiteral("audio_samplerate"), QString::number(kAudioSampleRate))
          << message(QStringLiteral("audio_stream_sample_type"), QStringLiteral("float32"))
          << message(QStringLiteral("audio_stream_channels"), QStringLiteral("2"))
          << message(QStringLiteral("audio_stream_samples"), QString::number(kAudioStreamSamples))
          << message(QStringLiteral("tx_stream_audio_buffering"), QStringLiteral("50"))
          << message(QStringLiteral("iq_samplerate"), QString::number(kAudioSampleRate))
          << message(QStringLiteral("start"))
          // ready LAST, after everything. SDC and CW Skimmer latch cached settings the instant it
          // arrives, and audio_start must never appear in the greeting - it is client-owned.
          << message(QStringLiteral("ready"));
    return burst;
}

void TciServer::onClientConnected(int clientId, const QString &peerAddress) {
    Q_UNUSED(peerAddress);
    m_parsers.insert(clientId, TciProtocol::Parser());
    qCInfo(netTci) << "client" << clientId << "connected from" << peerAddress << "- sending init burst";

    // One command per frame, matching what the reference server puts on the wire.
    const QStringList burst = initBurst();
    for (const QString &command : burst) {
        m_socketServer->sendText(clientId, command);
    }
    emit clientCountChanged(clientCount());
}

void TciServer::onClientDisconnected(int clientId) {
    m_parsers.remove(clientId);

    // Fail closed: losing the client that keyed the transmitter must unkey it. A stuck PTT after a
    // crashed client is the worst failure this server can have.
    if (m_pttOwner == clientId) {
        stopChrono();
        m_pttOwner = -1;
        m_snapshot.transmitting = false;
        emit pttRequested(false);
    }

    const bool hadAudio = m_audioClients.remove(clientId);
    // Fail closed: if the last audio consumer vanished, stop producing.
    if (hadAudio && m_audioClients.isEmpty()) {
        emit audioStopRequested();
    }
    emit clientCountChanged(clientCount());
}

void TciServer::onTextMessageReceived(int clientId, const QString &text) {
    auto it = m_parsers.find(clientId);
    if (it == m_parsers.end()) {
        return;
    }
    const QVector<Command> commands = it->feed(text);
    for (const Command &raw : commands) {
        const Command command = expandGlobalForm(raw);
        const QString &name = command.name;

        if (name == QLatin1String("audio_start")) {
            int receiver = ONLY_RECEIVER;
            command.argAsInt(0, &receiver);
            const bool first = m_audioClients.isEmpty();
            m_audioClients.insert(clientId);
            // Echo the request back, as the reference server does; WSJT-X waits for it.
            m_socketServer->sendText(clientId, message(name, QString::number(receiver)));
            qCInfo(netTci) << "client" << clientId << "requested audio on receiver" << receiver;
            if (first) {
                emit audioStartRequested(receiver);
            }
        } else if (name == QLatin1String("audio_stop")) {
            int receiver = ONLY_RECEIVER;
            command.argAsInt(0, &receiver);
            const bool had = m_audioClients.remove(clientId);
            m_socketServer->sendText(clientId, message(name, QString::number(receiver)));
            if (had && m_audioClients.isEmpty()) {
                emit audioStopRequested();
            }
        } else if (name == QLatin1String("rx_sensors_enable") || name == QLatin1String("tx_sensors_enable")) {
            // Echo only: acknowledged so the client does not wait, but nothing is measured yet.
            m_socketServer->sendText(clientId, message(name, command.args));
        } else if (name == QLatin1String("vfo") || name == QLatin1String("dds")) {
            // vfo:<trx>,<channel>,<hz> sets; vfo:<trx>,<channel> reads. dds is an alias for the
            // receive VFO and carries no channel.
            const bool isDds = (name == QLatin1String("dds"));
            int receiver = ONLY_RECEIVER;
            int channel = 0;
            qint64 hz = 0;
            const bool haveReceiver = command.argAsInt(0, &receiver);
            const bool haveChannel = isDds ? true : command.argAsInt(1, &channel);
            const bool haveHz = command.argAsLongLong(isDds ? 1 : 2, &hz);

            if (!haveReceiver || receiver != ONLY_RECEIVER || !haveChannel) {
                break; // an unknown receiver produces no request at all
            }
            // Range-check the channel: "vfo:0,2,..." must not be treated as channel 0.
            if (!isDds && channel != 0 && channel != 1) {
                break;
            }
            const qint64 current = (channel == 1) ? m_snapshot.txChannelHz() : m_snapshot.vfoAHz;
            if (haveHz) {
                emit setFrequencyRequested(isDds ? 0 : channel, hz);
            }
            // Confirm with what the model currently holds, never with silence. The radio's own
            // change comes back as a broadcast from setSnapshot, which is the authoritative echo -
            // a stale confirmation is what made WSJT-X transmit out of band in the reference server.
            m_socketServer->sendText(clientId, message(QStringLiteral("vfo"), QString::number(ONLY_RECEIVER),
                                                       QString::number(isDds ? 0 : channel), QString::number(current)));
        } else if (name == QLatin1String("modulation") || name == QLatin1String("mode")) {
            int receiver = ONLY_RECEIVER;
            const bool haveReceiver = command.argAsInt(0, &receiver);
            const QString wanted = command.arg(1).toLower();

            if (!haveReceiver || receiver != ONLY_RECEIVER) {
                break;
            }
            if (!wanted.isEmpty()) {
                // An unknown modulation is refused rather than coerced. The reference server's
                // coercion to usb puts the radio in a mode nobody asked for, silently.
                if (QString::fromLatin1(kModulationsList).split(QLatin1Char(',')).contains(wanted)) {
                    emit setModulationRequested(wanted);
                }
            }
            m_socketServer->sendText(
                clientId, message(QStringLiteral("modulation"), QString::number(ONLY_RECEIVER), m_snapshot.modulation));
        } else if (name == QLatin1String("trx")) {
            int receiver = ONLY_RECEIVER;
            bool keyed = false;
            const bool haveReceiver = command.argAsInt(0, &receiver);
            const bool haveState = command.argAsBool(1, &keyed);

            // A GET, or a malformed argument: report, never guess. Coercing a non-boolean is how
            // the reference server turns "trx:0,yes" into a silent unkey.
            if (!haveReceiver || !haveState) {
                m_socketServer->sendText(
                    clientId, message(name, QString::number(ONLY_RECEIVER), boolText(m_snapshot.transmitting)));
            } else if (receiver != ONLY_RECEIVER) {
                // Only the main VFO exists. Decline explicitly - silence surfaces in WSJT-X as
                // "TCI failed to set ptt" with no cause, and PTT must never fall back to trx 0.
                m_socketServer->sendText(clientId, message(name, QString::number(receiver), boolText(false)));
            } else {
                setPtt(clientId, keyed);
            }
        } else if (name == QLatin1String("split_enable")) {
            int receiver = ONLY_RECEIVER;
            bool wanted = false;
            const bool haveReceiver = command.argAsInt(0, &receiver);
            const bool haveState = command.argAsBool(1, &wanted);

            if (haveReceiver && receiver == ONLY_RECEIVER && haveState) {
                // A STEADY false IS NOT AN EDGE. WSJT-X sends split_enable:<n>,false as part of its
                // normal sequence BEFORE programming channel 1; acting on it every time would tear
                // down a split the operator had just set up. Only a real transition does anything.
                if (wanted != m_snapshot.split) {
                    emit setSplitRequested(wanted);
                }
            }
            m_socketServer->sendText(clientId,
                                     message(name, QString::number(ONLY_RECEIVER), boolText(m_snapshot.split)));
        }
        // Everything else: silence. That is what the protocol specifies for an unknown or refused
        // command, and WSJT-X sends nothing else during a receive session.
    }
}

void TciServer::setPtt(int clientId, bool active) {
    if (active) {
        // One owner at a time. A second client keying while another holds the transmitter is
        // refused rather than silently stealing it.
        if (m_pttOwner != -1 && m_pttOwner != clientId) {
            m_socketServer->sendText(clientId,
                                     message(QStringLiteral("trx"), QString::number(ONLY_RECEIVER), boolText(false)));
            return;
        }
        m_pttOwner = clientId;
        m_snapshot.transmitting = true;
        qCInfo(netTci) << "PTT ON from client" << clientId;
        // Confirm before starting the clock: the client waits for this echo, and WSJT-X drops the
        // link if a PTT request is not reflected quickly.
        m_socketServer->sendText(clientId,
                                 message(QStringLiteral("trx"), QString::number(ONLY_RECEIVER), boolText(true)));
        emit pttRequested(true);
        startChrono(clientId);
        return;
    }

    // An unkey from a client that does not hold PTT is a status report, not a command. Acting on it
    // would let any client unkey the operator.
    if (m_pttOwner != clientId) {
        m_socketServer->sendText(clientId, message(QStringLiteral("trx"), QString::number(ONLY_RECEIVER),
                                                   boolText(m_snapshot.transmitting)));
        return;
    }

    qCInfo(netTci) << "PTT OFF from client" << clientId;
    stopChrono();
    m_pttOwner = -1;
    m_snapshot.transmitting = false;
    m_socketServer->sendText(clientId, message(QStringLiteral("trx"), QString::number(ONLY_RECEIVER), boolText(false)));
    emit pttRequested(false);
}

void TciServer::startChrono(int clientId) {
    m_chronoClient = clientId;
    m_chronoAccumNs = 0;
    m_chronoClock.start();
    qCInfo(netTci) << "TX_CHRONO started for client" << clientId << "- period" << (double(kChronoPeriodNs) / 1.0e6)
                   << "ms, poll" << kChronoPollMs << "ms";
    m_chronoTimer->start();
    // Prime it: the client sends nothing at all until the first request arrives.
    m_socketServer->sendBinary(clientId, TciAudioFrame::encodeTxChrono(ONLY_RECEIVER, kAudioSampleRate));
}

void TciServer::stopChrono() {
    if (m_chronoTimer->isActive()) {
        qCInfo(netTci) << "TX_CHRONO stopped after" << m_chronoSent << "requests," << m_txBlocks << "blocks received";
    }
    m_chronoTimer->stop();
    m_chronoClient = -1;
    m_chronoAccumNs = 0;
    m_chronoClock.invalidate();
}

void TciServer::onChronoTick() {
    if (m_chronoClient < 0) {
        m_chronoTimer->stop();
        return;
    }
    if (!m_chronoClock.isValid()) {
        m_chronoClock.start();
        return;
    }
    m_chronoAccumNs += m_chronoClock.nsecsElapsed();
    m_chronoClock.restart();

    // Drain the backlog rather than emitting one per tick: a late timer must not cost rate. This is
    // what produces the bursty cadence measured on the reference server, which clients tolerate.
    const QByteArray frame = TciAudioFrame::encodeTxChrono(ONLY_RECEIVER, kAudioSampleRate);
    while (m_chronoAccumNs >= kChronoPeriodNs) {
        m_chronoAccumNs -= kChronoPeriodNs;
        m_socketServer->sendBinary(m_chronoClient, frame);
        ++m_chronoSent;
    }
}

void TciServer::onBinaryMessageReceived(int clientId, const QByteArray &payload) {
    // Only the client holding PTT may put audio on the transmitter.
    if (clientId != m_pttOwner) {
        return;
    }
    std::vector<float> mono;
    if (!TciAudioFrame::decodeTxAudioToMono(payload, &mono) || mono.empty()) {
        qCDebug(netTci) << "TX audio: dropped an undecodable binary frame of" << payload.size() << "bytes";
        return;
    }

    if (++m_txBlocks % kTxSummaryEveryBlocks == 0) {
        float peak = 0.0f;
        for (float v : mono) {
            peak = std::max(peak, std::fabs(v));
        }
        qCInfo(netTci) << "TX audio:" << m_txBlocks << "blocks from client" << clientId << "," << mono.size()
                       << "samples, peak" << peak << "- chrono sent" << m_chronoSent;
    }

    emit txAudioReceived(
        QByteArray(reinterpret_cast<const char *>(mono.data()), static_cast<int>(mono.size() * sizeof(float))));
}

void TciServer::sendRxAudio(const std::vector<float> &interleavedStereo, int sampleRate) {
    if (m_audioClients.isEmpty() || interleavedStereo.empty()) {
        return;
    }
    const QByteArray frame = TciAudioFrame::encodeRxAudio(ONLY_RECEIVER, sampleRate, interleavedStereo.data(),
                                                          static_cast<int>(interleavedStereo.size()));
    for (int clientId : m_audioClients) {
        m_socketServer->sendBinary(clientId, frame);
    }

    if (++m_rxBlocks % kRxSummaryEveryBlocks == 0) {
        float peak = 0.0f;
        for (float v : interleavedStereo) {
            peak = std::max(peak, std::fabs(v));
        }
        qCInfo(netTci) << "RX audio:" << m_rxBlocks << "blocks sent to" << m_audioClients.size() << "client(s),"
                       << interleavedStereo.size() / 2 << "frames at" << sampleRate << "Hz, peak" << peak;
    }
}
