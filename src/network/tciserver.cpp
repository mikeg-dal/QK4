#include "network/tciserver.h"

#include "network/tciaudioframe.h"
#include "network/websocketserver.h"

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

} // namespace

TciServer::TciServer(QObject *parent) : QObject(parent), m_socketServer(new WebSocketServer(this)) {
    connect(m_socketServer, &WebSocketServer::clientConnected, this, &TciServer::onClientConnected);
    connect(m_socketServer, &WebSocketServer::clientDisconnected, this, &TciServer::onClientDisconnected);
    connect(m_socketServer, &WebSocketServer::textMessageReceived, this, &TciServer::onTextMessageReceived);
}

TciServer::~TciServer() {
    disconnect(this);
    stop();
}

bool TciServer::start(quint16 port, bool loopbackOnly) {
    return m_socketServer->start(port, loopbackOnly);
}

void TciServer::stop() {
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

    // One command per frame, matching what the reference server puts on the wire.
    const QStringList burst = initBurst();
    for (const QString &command : burst) {
        m_socketServer->sendText(clientId, command);
    }
    emit clientCountChanged(clientCount());
}

void TciServer::onClientDisconnected(int clientId) {
    m_parsers.remove(clientId);
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
        } else if (name == QLatin1String("split_enable")) {
            // Accepted without acting while CAT control is out of scope. Confirmed rather than met
            // with silence, because a refused command with no reply reads to a client as a hang.
            m_socketServer->sendText(clientId,
                                     message(name, QString::number(ONLY_RECEIVER), boolText(m_snapshot.split)));
        }
        // Everything else: silence. That is what the protocol specifies for an unknown or refused
        // command, and WSJT-X sends nothing else during a receive session.
    }
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
}
