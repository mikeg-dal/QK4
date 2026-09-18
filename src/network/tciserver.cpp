#include "network/tciserver.h"

#include "network/tciserver_internal.h"

#include <QLoggingCategory>
#include <QSet>

#include <algorithm>
#include <cmath>

#include "network/tciaudioframe.h"
#include "network/websocketserver.h"

Q_LOGGING_CATEGORY(netTci, "net.tci")

using namespace TciServerInternal;

TciServer::TciServer(QObject *parent)
    : QObject(parent), m_socketServer(new WebSocketServer(this)), m_chronoTimer(new QTimer(this)),
      m_sensorTimer(new QTimer(this)) {
    connect(m_socketServer, &WebSocketServer::clientConnected, this, &TciServer::onClientConnected);
    connect(m_socketServer, &WebSocketServer::clientDisconnected, this, &TciServer::onClientDisconnected);
    connect(m_socketServer, &WebSocketServer::textMessageReceived, this, &TciServer::onTextMessageReceived);
    connect(m_socketServer, &WebSocketServer::binaryMessageReceived, this, &TciServer::onBinaryMessageReceived);

    // Poll faster than the period and emit from an accumulator, so scheduling jitter is absorbed
    // rather than accumulated into a rate error.
    m_chronoTimer->setTimerType(Qt::PreciseTimer);
    m_chronoTimer->setInterval(kChronoPollMs);
    connect(m_chronoTimer, &QTimer::timeout, this, &TciServer::onChronoTick);

    // Ordinary timer, unlike the chrono clock: a meter reading a few milliseconds late is
    // invisible, whereas the TX_CHRONO period warps digital-mode tones if its mean rate drifts.
    m_sensorTimer->setInterval(kSensorIntervalDefaultMs);
    connect(m_sensorTimer, &QTimer::timeout, this, &TciServer::onSensorTick);
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
        // Say so before the listener goes away: a client that is about to be disconnected should
        // not be left holding an ON indicator.
        broadcast(message(QStringLiteral("trx"), QString::number(MAIN_RECEIVER), boolText(false)));
        emit pttRequested(false);
    }
    m_socketServer->stop();
    m_audioClients.clear();
    m_parsers.clear();
    m_rxSensorClients.clear();
    m_txSensorClients.clear();
    m_sensorTimer->stop();

    // WebSocketServer::stop() closes the sockets without emitting clientDisconnected for each, so
    // the roster has to be emptied here or the page would keep listing clients of a server that is
    // no longer running.
    m_reportedUnhandled.clear();

    const bool hadClients = !m_clients.isEmpty();
    m_clients.clear();
    if (hadClients) {
        announceClients(/*force=*/true);
    }
}

QVector<TciClientInfo> TciServer::clients() const {
    // Oldest first. Ids are handed out in ascending order by WebSocketServer, so sorting by id
    // sorts by arrival - a row does not jump around the table because a QHash rehashed.
    QVector<TciClientInfo> out;
    out.reserve(m_clients.size());
    for (auto it = m_clients.constBegin(); it != m_clients.constEnd(); ++it) {
        out.append(it.value());
    }
    std::sort(out.begin(), out.end(), [](const TciClientInfo &a, const TciClientInfo &b) { return a.id < b.id; });
    return out;
}

void TciServer::recordClientMessage(int clientId, const QString &message, bool outbound) {
    auto it = m_clients.find(clientId);
    if (it == m_clients.end()) {
        return;
    }
    it->lastMessage = message;
    it->lastMessageOutbound = outbound;
    it->lastMessageTime = QDateTime::currentDateTime();
}

void TciServer::announceClients(bool force) {
    if (!force && m_clientsAnnounceClock.isValid() && m_clientsAnnounceClock.elapsed() < kClientAnnounceMinMs) {
        return;
    }
    m_clientsAnnounceClock.restart();
    emit clientsChanged(clients());
}

void TciServer::sendTo(int clientId, const QString &text) {
    m_socketServer->sendText(clientId, text);
    recordClientMessage(clientId, elideForDisplay(text.trimmed()), /*outbound=*/true);
    announceClients(/*force=*/false);
}

void TciServer::sendTelemetry(int clientId, const QString &text) {
    m_socketServer->sendText(clientId, text);
}

void TciServer::broadcast(const QString &text) {
    m_socketServer->broadcastText(text);

    // Recorded against every row, then announced ONCE. Routing this through the per-client path
    // would emit the roster once per client for a message they all got at the same instant.
    const QString shown = elideForDisplay(text.trimmed());
    const QDateTime now = QDateTime::currentDateTime();
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        it->lastMessage = shown;
        it->lastMessageOutbound = true;
        it->lastMessageTime = now;
    }
    announceClients(/*force=*/false);
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

void TciServer::releaseLocalPtt() {
    if (m_pttOwner == -1) {
        return; // nobody was holding it; also the base case that stops a client unkey recursing
    }
    qCInfo(netTci) << "local unkey - releasing PTT held by client" << m_pttOwner;
    stopChrono();
    m_pttOwner = -1;
    m_snapshot.transmitting = false;
    // Every client tracks the transmitter, not just the one that was keying.
    broadcast(message(QStringLiteral("trx"), QString::number(MAIN_RECEIVER), boolText(false)));
}

void TciServer::onClientConnected(int clientId, const QString &peerEndpoint) {
    m_parsers.insert(clientId, TciProtocol::Parser());

    TciClientInfo info;
    info.id = clientId;
    info.address = peerEndpoint;
    // Seeded so the cell is never blank, which would read as a bug. The init burst below overwrites
    // it within microseconds; this shows only if a burst were ever empty.
    info.lastMessage = QStringLiteral("(connected)");
    info.lastMessageTime = QDateTime::currentDateTime();
    m_clients.insert(clientId, info);

    qCInfo(netTci) << "client" << clientId << "connected from" << peerEndpoint << "- sending init burst";

    // One command per frame, matching what the reference server puts on the wire.
    const QStringList burst = initBurst();
    for (const QString &command : burst) {
        sendTo(clientId, command);
    }
    // After the burst, so the new row shows what was actually last sent. Forced, because the burst's
    // own announcements are throttled and a client arriving must not wait half a second to appear.
    announceClients(/*force=*/true);
    emit clientCountChanged(clientCount());
}

void TciServer::onClientDisconnected(int clientId) {
    m_parsers.remove(clientId);
    if (m_clients.remove(clientId) > 0) {
        announceClients(/*force=*/true);
    }

    // Fail closed: losing the client that keyed the transmitter must unkey it. A stuck PTT after a
    // crashed client is the worst failure this server can have.
    if (m_pttOwner == clientId) {
        stopChrono();
        m_pttOwner = -1;
        m_snapshot.transmitting = false;
        emit pttRequested(false);
    }

    // A vanished client cannot be sent anything; leaving it subscribed would keep the timer
    // running for nobody.
    m_rxSensorClients.remove(clientId);
    m_txSensorClients.remove(clientId);
    updateSensorTimer();

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
    // Recorded BEFORE dispatch: a command that drops the client (or unkeys and disconnects) would
    // otherwise never reach the roster, and the last thing a client said before it went away is
    // exactly what the table is for.
    recordClientMessage(clientId, elideForDisplay(text.trimmed()), /*outbound=*/false);
    announceClients(/*force=*/false);

    const QVector<Command> commands = it->feed(text);
    for (const Command &raw : commands) {
        const Command command = expandGlobalForm(raw);
        const QString &name = command.name;

        if (name == QLatin1String("audio_start")) {
            int receiver = MAIN_RECEIVER;
            command.argAsInt(0, &receiver);
            const bool first = m_audioClients.isEmpty();
            m_audioClients.insert(clientId);
            // Echo the request back, as the reference server does; WSJT-X waits for it.
            sendTo(clientId, message(name, QString::number(receiver)));
            qCInfo(netTci) << "client" << clientId << "requested audio on receiver" << receiver;
            if (first) {
                emit audioStartRequested(receiver);
            }
        } else if (name == QLatin1String("audio_stop")) {
            int receiver = MAIN_RECEIVER;
            command.argAsInt(0, &receiver);
            const bool had = m_audioClients.remove(clientId);
            sendTo(clientId, message(name, QString::number(receiver)));
            if (had && m_audioClients.isEmpty()) {
                emit audioStopRequested();
            }
        } else if (name == QLatin1String("cw_keyer_speed") || name == QLatin1String("cw_macros_speed")) {
            // BEFORE answerReadOnly, which reports this pair and would otherwise answer the query
            // and swallow the set.
            //
            // Both names, one setting: the spec marks CW_KEYER_SPEED client-to-server and
            // CW_MACROS_SPEED as the contest-logger name for the same thing. Global, one argument,
            // no receiver index - the K4 has one keyer.
            int wpm = 0;
            if (command.argAsInt(0, &wpm)) {
                emit setKeyerSpeedRequested(wpm);
            }
            // Confirm with what the MODEL holds, not with the value just asked for. The radio's own
            // change arrives as a broadcast from setSnapshot and is the authoritative echo; a
            // confirmation of an unapplied value is the lie that made WSJT-X transmit out of band
            // in the reference server.
            sendTo(clientId, message(name, QString::number(m_snapshot.cwKeyerSpeedWpm)));
        } else if (answerReadOnly(clientId, command)) {
            // Handled: a query answered from the snapshot. See answerReadOnly.
        } else if (name == QLatin1String("rx_sensors_enable") || name == QLatin1String("tx_sensors_enable")) {
            // rx/tx_sensors_enable:<bool>[,<interval ms>] - per client, per direction.
            bool wanted = false;
            if (!command.argAsBool(0, &wanted)) {
                continue; // a malformed enable is not a disable
            }
            const bool isRx = (name == QLatin1String("rx_sensors_enable"));
            QSet<int> &subscribers = isRx ? m_rxSensorClients : m_txSensorClients;
            if (wanted) {
                subscribers.insert(clientId);
            } else {
                subscribers.remove(clientId);
            }

            int interval = 0;
            if (command.argAsInt(1, &interval)) {
                // Clamp rather than refuse: the spec gives 30..1000 ms, and a client asking for
                // 1 ms wants "as fast as you can", not a rejection.
                m_sensorIntervalMs = qBound(kSensorIntervalMinMs, interval, kSensorIntervalMaxMs);
            }
            updateSensorTimer();
            // Echo, as before - the client waits for it.
            sendTo(clientId, message(name, command.args));
            qCInfo(netTci) << "client" << clientId << (isRx ? "RX" : "TX") << "sensors" << (wanted ? "on" : "off")
                           << "every" << m_sensorIntervalMs << "ms";
        } else if (name == QLatin1String("vfo") || name == QLatin1String("dds")) {
            // vfo:<trx>,<channel>,<hz> sets; vfo:<trx>,<channel> reads. dds is an alias for the
            // receive VFO and carries no channel.
            const bool isDds = (name == QLatin1String("dds"));
            int receiver = MAIN_RECEIVER;
            int channel = CHANNEL_A;
            qint64 hz = 0;
            const bool haveReceiver = command.argAsInt(0, &receiver);
            const bool haveChannel = isDds ? true : command.argAsInt(1, &channel);
            const bool haveHz = command.argAsLongLong(isDds ? 1 : 2, &hz);

            if (!haveReceiver || !m_snapshot.validReceiver(receiver) || !haveChannel) {
                continue; // an unknown receiver produces no request at all
            }
            if (isDds) {
                channel = CHANNEL_A;
            }
            // Channel B exists only on the main receiver, where it is the transmit VFO.
            if (channel != CHANNEL_A && !(receiver == MAIN_RECEIVER && channel == CHANNEL_B)) {
                continue;
            }
            const qint64 current = (receiver == MAIN_RECEIVER && channel == CHANNEL_B) ? m_snapshot.txChannelHz()
                                                                                       : m_snapshot.rx[receiver].vfoHz;
            if (haveHz) {
                emit setFrequencyRequested(receiver, channel, hz);
            }
            // Confirm with what the model currently holds, never with silence. The radio's own
            // change comes back as a broadcast from setSnapshot, which is the authoritative echo -
            // a stale confirmation is what made WSJT-X transmit out of band in the reference
            // server.
            sendTo(clientId, message(QStringLiteral("vfo"), QString::number(receiver), QString::number(channel),
                                     QString::number(current)));
        } else if (name == QLatin1String("modulation") || name == QLatin1String("mode")) {
            // Per RECEIVER, with no channel argument - which is exactly why the Sub RX is modelled
            // as receiver 1. As channel 1 of receiver 0 its mode had nowhere to live at all.
            int receiver = MAIN_RECEIVER;
            const bool haveReceiver = command.argAsInt(0, &receiver);
            const QString wanted = command.arg(1).toLower();

            if (!haveReceiver || !m_snapshot.validReceiver(receiver)) {
                continue;
            }
            if (!wanted.isEmpty()) {
                // An unknown modulation is refused rather than coerced. The reference server's
                // coercion to usb puts the radio in a mode nobody asked for, silently.
                if (QString::fromLatin1(kModulationsList).split(QLatin1Char(',')).contains(wanted)) {
                    emit setModulationRequested(receiver, wanted);
                }
            }
            sendTo(clientId, message(QStringLiteral("modulation"), QString::number(receiver),
                                     m_snapshot.rx[receiver].modulation));
        } else if (name == QLatin1String("trx")) {
            int receiver = MAIN_RECEIVER;
            bool keyed = false;
            const bool haveReceiver = command.argAsInt(0, &receiver);
            const bool haveState = command.argAsBool(1, &keyed);

            // A GET, or a malformed argument: report, never guess. Coercing a non-boolean is how
            // the reference server turns "trx:0,yes" into a silent unkey.
            if (!haveReceiver || !haveState) {
                sendTo(clientId, message(name, QString::number(MAIN_RECEIVER), boolText(m_snapshot.transmitting)));
            } else if (receiver != MAIN_RECEIVER) {
                // ONLY THE MAIN RECEIVER TRANSMITS. The K4 has one transmitter, and the sub
                // receiver is receive-only however it is addressed. Decline explicitly - silence
                // surfaces in WSJT-X as "TCI failed to set ptt" with no cause, and PTT must never
                // fall back to another receiver.
                sendTo(clientId, message(name, QString::number(receiver), boolText(false)));
            } else {
                setPtt(clientId, keyed);
            }
        } else if (name == QLatin1String("rx_channel_enable") || name == QLatin1String("rx_enable")) {
            // Two spellings for one thing, because clients disagree about how a second receiver is
            // addressed:
            //   rx_channel_enable:<trx>,<channel>[,<bool>]  - ExpertSDR3's channel B of receiver 0
            //   rx_enable:<trx>[,<bool>]                    - receiver 1 directly
            // Both land on the K4's Sub RX. See tciradiostate.h.
            const bool byChannel = (name == QLatin1String("rx_channel_enable"));
            int receiver = MAIN_RECEIVER;
            int channel = CHANNEL_A;
            bool wanted = false;
            const bool haveReceiver = command.argAsInt(0, &receiver);
            const bool haveChannel = byChannel ? command.argAsInt(1, &channel) : true;
            const bool haveState = command.argAsBool(byChannel ? 2 : 1, &wanted);

            if (!haveReceiver || !m_snapshot.validReceiver(receiver) || !haveChannel) {
                continue;
            }

            // Which receiver is actually being addressed, whichever spelling was used.
            int target = receiver;
            if (byChannel) {
                if (receiver != MAIN_RECEIVER) {
                    continue; // only the main receiver has a second channel
                }
                if (channel == CHANNEL_B) {
                    target = SUB_RECEIVER;
                } else if (channel != CHANNEL_A) {
                    continue; // no such channel
                }
            }

            if (target == MAIN_RECEIVER) {
                // The main receiver cannot be switched off. Report it rather than staying silent:
                // a client that believed it had turned the main receiver off would stop asking
                // for audio.
                if (byChannel) {
                    sendTo(clientId,
                           message(name, QString::number(MAIN_RECEIVER), QString::number(CHANNEL_A), boolText(true)));
                } else {
                    sendTo(clientId, message(name, QString::number(MAIN_RECEIVER), boolText(true)));
                }
                continue;
            }

            // A steady value is not an edge, for the same reason split_enable checks: repeating
            // the state the radio already holds must not generate CAT traffic.
            if (haveState && wanted != m_snapshot.rx[SUB_RECEIVER].enabled) {
                emit setSubReceiverRequested(wanted);
            }
            // Confirm with what the model holds; the radio's own change arrives as a broadcast.
            const bool held = m_snapshot.rx[SUB_RECEIVER].enabled;
            if (byChannel) {
                sendTo(clientId,
                       message(name, QString::number(MAIN_RECEIVER), QString::number(CHANNEL_B), boolText(held)));
            } else {
                sendTo(clientId, message(name, QString::number(SUB_RECEIVER), boolText(held)));
            }
        } else if (name == QLatin1String("cw_macros")) {
            // TWO DIALECTS, both accepted.
            //
            // The spec is `cw_macros:<trx>,<text>;`. But TR4W sends `cw_macros:<text>;` with no
            // receiver index, and its own source notes that AetherSDR takes the raw text and that
            // the receiver-index form was never verified. Reading the spec strictly makes a whole
            // logger's CW silently do nothing against QK4 - which is exactly the failure this
            // server keeps being bitten by - so a lone argument is taken as the text.
            //
            // Args from index 1 are REJOINED with the comma that split them. TCI escapes its own
            // separators (',' travels as '~'), so a well-behaved client sends one argument - but a
            // client that forgets would otherwise have its message truncated at the first comma,
            // and half an exchange going out is worse than none.
            const QString text =
                (command.args.size() == 1) ? command.args.first() : command.args.mid(1).join(QLatin1Char(','));
            const QVector<CwMacroSegment> segments = CwMacro::parse(text, m_snapshot.cwKeyerSpeedWpm);
            if (segments.isEmpty()) {
                // Say so. A macro that carries nothing keyable used to be dropped in silence, which
                // gives an operator hearing no CW nothing anywhere to look at.
                qCInfo(netTci) << "client" << clientId << "sent cw_macros with nothing keyable in it:" << command.args;
                continue;
            }
            qCInfo(netTci) << "client" << clientId << "CW:" << text << "in" << segments.size() << "speed segment(s)";
            emit cwMacroRequested(segments);
            // No echo. The spec defines no confirmation for CW_MACROS, and the radio's own keying
            // is the acknowledgement.
        } else if (name == QLatin1String("cw_macros_stop")) {
            qCInfo(netTci) << "client" << clientId << "asked to stop CW";
            emit cwAbortRequested();
        } else if (name == QLatin1String("split_enable")) {
            int receiver = MAIN_RECEIVER;
            bool wanted = false;
            const bool haveReceiver = command.argAsInt(0, &receiver);
            const bool haveState = command.argAsBool(1, &wanted);

            if (haveReceiver && receiver == MAIN_RECEIVER && haveState) {
                // A STEADY false IS NOT AN EDGE. WSJT-X sends split_enable:<n>,false as part of its
                // normal sequence BEFORE programming channel 1; acting on it every time would tear
                // down a split the operator had just set up. Only a real transition does anything.
                if (wanted != m_snapshot.split) {
                    emit setSplitRequested(wanted);
                }
            }
            sendTo(clientId, message(name, QString::number(MAIN_RECEIVER), boolText(m_snapshot.split)));
        } else {
            // SILENT ON THE WIRE, but not invisible here.
            //
            // Silence is what the protocol specifies for an unknown or refused command, and that
            // does not change. What changes is that QK4 now says so in its own log: a command it
            // does not implement is precisely what somebody adding support for it needs to see,
            // and there was previously no way to learn what a client actually sends short of a
            // packet capture. CW is the open case - see docs/tci-command-coverage.md section 5.
            //
            // First sighting of each name at info, repeats at debug: a client may poll an
            // unimplemented command indefinitely, and a flooded log hides the thing it was
            // supposed to reveal.
            if (!m_reportedUnhandled.contains(name)) {
                m_reportedUnhandled.insert(name);
                qCInfo(netTci) << "client" << clientId << "sent" << name << "with args" << command.args
                               << "- NOT PROCESSED (no QK4 implementation); further occurrences at debug level";
            } else {
                qCDebug(netTci) << "client" << clientId << "sent" << name << "with args" << command.args
                                << "- not processed";
            }
        }
    }
}

void TciServer::setSensors(const TciSensorReadings &readings) {
    // Stored only. Broadcasting here would put a message on the wire for every meter packet the
    // radio sends, which is several a second per meter and is exactly what the interval argument
    // exists to prevent.
    m_sensors = readings;
}

void TciServer::updateSensorTimer() {
    const bool wanted = !m_rxSensorClients.isEmpty() || !m_txSensorClients.isEmpty();
    if (!wanted) {
        m_sensorTimer->stop();
        return;
    }
    if (m_sensorTimer->interval() != m_sensorIntervalMs) {
        m_sensorTimer->setInterval(m_sensorIntervalMs);
    }
    if (!m_sensorTimer->isActive()) {
        m_sensorTimer->start();
    }
}

void TciServer::onSensorTick() {
    // WHY a snapshot of the id sets rather than iterating them directly: sendText can surface a
    // disconnect, whose handler removes from these very sets. Same hazard as the session table.
    if (!m_rxSensorClients.isEmpty()) {
        // Built once per tick, not once per client.
        QStringList readings;
        // rx_sensors is deprecated in TCI 2.0 in favour of rx_channel_sensors, but older clients
        // only understand the former. It has no receiver index in practice, so it carries the
        // main receiver.
        readings << message(QStringLiteral("rx_sensors"), QString::number(MAIN_RECEIVER),
                            QString::number(m_sensors.sMeterDbm[MAIN_RECEIVER], 'f', 1));
        for (int r = 0; r < RECEIVER_COUNT; ++r) {
            // Only claim a level for a receiver that is switched on.
            if (!m_snapshot.rx[r].enabled) {
                continue;
            }
            readings << message(QStringLiteral("rx_channel_sensors"), QString::number(r), QString::number(CHANNEL_A),
                                QString::number(m_sensors.sMeterDbm[r], 'f', 1));
        }
        // The sub receiver doubles as channel B of the main one, for clients that model it the
        // ExpertSDR3 way rather than as a second receiver.
        if (m_snapshot.rx[SUB_RECEIVER].enabled) {
            readings << message(QStringLiteral("rx_channel_sensors"), QString::number(MAIN_RECEIVER),
                                QString::number(CHANNEL_B), QString::number(m_sensors.sMeterDbm[SUB_RECEIVER], 'f', 1));
        }

        const QList<int> targets = m_rxSensorClients.values();
        for (int id : targets) {
            if (!m_rxSensorClients.contains(id)) {
                continue; // dropped while we were sending to an earlier client
            }
            for (const QString &line : readings) {
                sendTelemetry(id, line);
            }
        }
    }

    if (!m_txSensorClients.isEmpty()) {
        // Five arguments, so the QStringList form: trx, mic dBm, RMS power W, peak power W, SWR.
        // Transmit belongs to the main receiver; the K4 has one transmitter.
        const QString reading =
            message(QStringLiteral("tx_sensors"),
                    QStringList{QString::number(MAIN_RECEIVER), QString::number(m_sensors.micLevelDbm, 'f', 1),
                                QString::number(m_sensors.forwardPowerW, 'f', 1),
                                QString::number(m_sensors.peakPowerW, 'f', 1), QString::number(m_sensors.swr, 'f', 2)});
        const QList<int> targets = m_txSensorClients.values();
        for (int id : targets) {
            if (!m_txSensorClients.contains(id)) {
                continue;
            }
            sendTelemetry(id, reading);
        }
    }
}

void TciServer::setPtt(int clientId, bool active) {
    if (active) {
        // One owner at a time. A second client keying while another holds the transmitter is
        // refused rather than silently stealing it.
        if (m_pttOwner != -1 && m_pttOwner != clientId) {
            sendTo(clientId, message(QStringLiteral("trx"), QString::number(MAIN_RECEIVER), boolText(false)));
            return;
        }
        m_pttOwner = clientId;
        m_snapshot.transmitting = true;
        qCInfo(netTci) << "PTT ON from client" << clientId;
        // BROADCAST, not a reply to the asker. The protocol makes the server a synchroniser: a
        // state change reaches every client, so a second program (a logger showing an ON
        // indicator, an amplifier controller) sees the transmitter key even though someone else
        // asked for it. The requester is included, so this is still the echo it waits for -
        // and WSJT-X drops the link if a PTT request is not reflected quickly, so it goes out
        // before the chrono clock starts.
        broadcast(message(QStringLiteral("trx"), QString::number(MAIN_RECEIVER), boolText(true)));
        emit pttRequested(true);
        startChrono(clientId);
        return;
    }

    // An unkey from a client that does not hold PTT is a status report, not a command. Acting on it
    // would let any client unkey the operator.
    if (m_pttOwner != clientId) {
        sendTo(clientId,
               message(QStringLiteral("trx"), QString::number(MAIN_RECEIVER), boolText(m_snapshot.transmitting)));
        return;
    }

    qCInfo(netTci) << "PTT OFF from client" << clientId;
    stopChrono();
    m_pttOwner = -1;
    m_snapshot.transmitting = false;
    // Broadcast for the same reason as the key: every client tracks the transmitter.
    broadcast(message(QStringLiteral("trx"), QString::number(MAIN_RECEIVER), boolText(false)));
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
    m_socketServer->sendBinary(clientId, TciAudioFrame::encodeTxChrono(MAIN_RECEIVER, kAudioSampleRate));
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
    const QByteArray frame = TciAudioFrame::encodeTxChrono(MAIN_RECEIVER, kAudioSampleRate);
    while (m_chronoAccumNs >= kChronoPeriodNs) {
        m_chronoAccumNs -= kChronoPeriodNs;
        m_socketServer->sendBinary(m_chronoClient, frame);
        ++m_chronoSent;
    }
}

void TciServer::onBinaryMessageReceived(int clientId, const QByteArray &payload) {
    // Counted as activity even though it is not a command: a client in the middle of a 15-second
    // WSJT-X transmission sends no text at all, and a roster that showed it last heard from half a
    // minute ago would be reporting it as stale at the one moment it is busiest. Throttled, so the
    // ~47 frames a second cost one announcement every half second.
    recordClientMessage(clientId, QStringLiteral("(tx audio)"), /*outbound=*/false);
    announceClients(/*force=*/false);

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
    const QByteArray frame = TciAudioFrame::encodeRxAudio(MAIN_RECEIVER, sampleRate, interleavedStereo.data(),
                                                          static_cast<int>(interleavedStereo.size()));
    // A SNAPSHOT of the ids, not the set itself, and membership rechecked per client. sendBinary
    // can surface a disconnect, whose handler removes from m_audioClients - mutating the container
    // this loop is walking. Same hazard, and the same shape, as onSensorTick.
    const QList<int> targets = m_audioClients.values();
    for (int clientId : targets) {
        if (!m_audioClients.contains(clientId)) {
            continue; // dropped while we were sending to an earlier client
        }
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
