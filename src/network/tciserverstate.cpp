#include "network/tciserver.h"

#include "network/tciserver_internal.h"
#include "network/websocketserver.h"

#include <QSet>

// The state-reporting half of TciServer: what the radio looks like to a client.
//
// Split from tciserver.cpp, which owns the socket, the command dispatch, PTT, the chrono clock and
// the audio. These two files are one class; the division is by subject, so neither passes the
// 800-line limit in CONVENTIONS.md rule 7.
//
// Everything here answers one question in three forms: the init burst (state on connect), the
// broadcasts (state that changed), and answerReadOnly (state on request). They must agree with
// each other, which is what answersQueriesConsistentlyWithTheInitBurst pins.

using namespace TciServerInternal;

void TciServer::setSnapshot(const TciRadioSnapshot &snapshot) {
    const TciRadioSnapshot previous = m_snapshot;
    m_snapshot = snapshot;

    // WHO OWNS THE TRANSMIT STATE depends on whether a TCI client is holding PTT.
    //
    //  * A CLIENT HOLDS IT: its own assertion wins. The K4 keys only once TX audio starts
    //    arriving, so RadioState lags the client by whole packets; taking the radio's value here
    //    broadcast a spurious `trx:0,false;` mid-transmission and WSJT-X stopped sending audio.
    //
    //  * NOBODY HOLDS IT: the radio is the only truth there is, and a change must reach every
    //    client. An operator keying the mic or a footswitch, another CAT client, or the K4's own
    //    keying are all invisible to TCI otherwise - a logger sits showing RX while the radio
    //    transmits.
    if (m_pttOwner != -1) {
        m_snapshot.transmitting = previous.transmitting;
    }

    if (clientCount() == 0) {
        return; // nobody to tell; the init burst will carry it
    }

    for (int r = 0; r < RECEIVER_COUNT; ++r) {
        const TciReceiverState &now = m_snapshot.rx[r];
        const TciReceiverState &was = previous.rx[r];
        const QString trx = QString::number(r);

        if (now.vfoHz != was.vfoHz) {
            broadcast(message(QStringLiteral("vfo"), trx, QString::number(CHANNEL_A), QString::number(now.vfoHz)));
            // dds is an alias for the receive VFO and carries no channel.
            broadcast(message(QStringLiteral("dds"), trx, QString::number(now.vfoHz)));
        }
        if (now.modulation != was.modulation) {
            broadcast(message(QStringLiteral("modulation"), trx, now.modulation));
        }
        if (now.enabled != was.enabled) {
            broadcast(message(QStringLiteral("rx_enable"), trx, boolText(now.enabled)));
            // The sub receiver is also a channel of the main one, for clients that model it that
            // way; see tciradiostate.h.
            if (r == SUB_RECEIVER) {
                broadcast(message(QStringLiteral("rx_channel_enable"), QString::number(MAIN_RECEIVER),
                                  QString::number(CHANNEL_B), boolText(now.enabled)));
            }
        }
        if (now.rit != was.rit) {
            broadcast(message(QStringLiteral("rit_enable"), trx, boolText(now.rit)));
        }
        if (now.xit != was.xit) {
            broadcast(message(QStringLiteral("xit_enable"), trx, boolText(now.xit)));
        }
        if (now.ritXitOffsetHz != was.ritXitOffsetHz) {
            // One register, reported under both names - see TciReceiverState.
            const QString offset = QString::number(now.ritXitOffsetHz);
            broadcast(message(QStringLiteral("rit_offset"), trx, offset));
            broadcast(message(QStringLiteral("xit_offset"), trx, offset));
        }
        if (now.noiseBlankerLevel != was.noiseBlankerLevel ||
            now.noiseBlankerFilterWidth != was.noiseBlankerFilterWidth) {
            broadcast(message(QStringLiteral("rx_nb_param"), trx, QString::number(now.noiseBlankerLevel),
                              QString::number(now.noiseBlankerFilterWidth)));
        }
        if (now.lock != was.lock) {
            broadcast(message(QStringLiteral("vfo_lock"), trx, QString::number(CHANNEL_A), boolText(now.lock)));
        }
        if (now.noiseBlanker != was.noiseBlanker) {
            broadcast(message(QStringLiteral("rx_nb_enable"), trx, boolText(now.noiseBlanker)));
        }
        if (now.noiseReduction != was.noiseReduction) {
            broadcast(message(QStringLiteral("rx_nr_enable"), trx, boolText(now.noiseReduction)));
        }
        if (now.autoNotch != was.autoNotch) {
            broadcast(message(QStringLiteral("rx_anf_enable"), trx, boolText(now.autoNotch)));
        }
        if (now.apf != was.apf) {
            broadcast(message(QStringLiteral("rx_apf_enable"), trx, boolText(now.apf)));
        }
        if (now.volumeDb != was.volumeDb) {
            broadcast(
                message(QStringLiteral("rx_volume"), trx, QString::number(CHANNEL_A), QString::number(now.volumeDb)));
        }
        if (now.notchFilter != was.notchFilter) {
            broadcast(message(QStringLiteral("rx_nf_enable"), trx, boolText(now.notchFilter)));
        }
        if (now.lock != was.lock) {
            broadcast(message(QStringLiteral("lock"), trx, boolText(now.lock)));
        }
        if (now.sqlEnabled != was.sqlEnabled) {
            broadcast(message(QStringLiteral("sql_enable"), trx, boolText(now.sqlEnabled)));
        }
        if (now.agcMode != was.agcMode) {
            broadcast(message(QStringLiteral("agc_mode"), trx, now.agcMode));
        }
        if (now.agcGain != was.agcGain) {
            broadcast(message(QStringLiteral("agc_gain"), trx, QString::number(now.agcGain)));
        }
        if (now.filterLowHz != was.filterLowHz || now.filterHighHz != was.filterHighHz) {
            broadcast(message(QStringLiteral("rx_filter_band"), trx, QString::number(now.filterLowHz),
                              QString::number(now.filterHighHz)));
        }
    }

    const QString mainTrx = QString::number(MAIN_RECEIVER);

    // Channel 1 of the main receiver follows VFO B while split is on and the receive frequency
    // otherwise, so it can move when either changes.
    if (m_snapshot.txChannelHz() != previous.txChannelHz()) {
        broadcast(message(QStringLiteral("vfo"), mainTrx, QString::number(CHANNEL_B),
                          QString::number(m_snapshot.txChannelHz())));
        // tx_frequency is a server-to-client notification with no read form, so a client that
        // wants the transmit frequency has no way to ask for it - it only ever learns by being
        // told. Split is exactly when it matters and when it differs from the RX VFO.
        broadcast(message(QStringLiteral("tx_frequency"), QString::number(m_snapshot.txChannelHz())));
    }
    if (m_snapshot.split != previous.split) {
        broadcast(message(QStringLiteral("split_enable"), mainTrx, boolText(m_snapshot.split)));
    }

    // Radio-driven transmit. While a client owns PTT this can never fire, because the carry-over
    // above makes the value identical; setPtt broadcasts that case instead.
    if (m_snapshot.transmitting != previous.transmitting) {
        broadcast(message(QStringLiteral("trx"), mainTrx, boolText(m_snapshot.transmitting)));
    }
    if (m_snapshot.drive != previous.drive) {
        // Always <trx>,<power>: a bare "drive:0;" crashes ESDR3-mode WSJT-X and JTDX, which index
        // args[1] unconditionally. That rule applies to the broadcast as much as the reply.
        broadcast(message(QStringLiteral("drive"), mainTrx, QString::number(m_snapshot.drive)));
    }
    if (m_snapshot.micLevel != previous.micLevel) {
        broadcast(message(QStringLiteral("mic_level"), QString::number(m_snapshot.micLevel)));
    }
    if (m_snapshot.cwKeyerSpeedWpm != previous.cwKeyerSpeedWpm) {
        // No receiver index: CW_KEYER_SPEED is a single-argument command.
        broadcast(message(QStringLiteral("cw_keyer_speed"), QString::number(m_snapshot.cwKeyerSpeedWpm)));
        // cw_macros_speed is the same setting under the name a contest logger looks for.
        broadcast(message(QStringLiteral("cw_macros_speed"), QString::number(m_snapshot.cwKeyerSpeedWpm)));
    }
    if (m_snapshot.tuneDrive != previous.tuneDrive) {
        broadcast(message(QStringLiteral("tune_drive"), mainTrx, QString::number(m_snapshot.tuneDrive)));
    }
}

QStringList TciServer::receiverBurst(int receiver) const {
    const QString trx = QString::number(receiver);
    const TciReceiverState &r = m_snapshot.rx[receiver];

    QStringList burst;
    burst << message(QStringLiteral("vfo"), trx, QString::number(CHANNEL_A), QString::number(r.vfoHz))
          << message(QStringLiteral("dds"), trx, QString::number(r.vfoHz))
          << message(QStringLiteral("modulation"), trx, r.modulation)
          << message(QStringLiteral("rx_enable"), trx, boolText(r.enabled))
          << message(QStringLiteral("rx_filter_band"), trx, QString::number(r.filterLowHz),
                     QString::number(r.filterHighHz))
          << message(QStringLiteral("rit_enable"), trx, boolText(r.rit))
          << message(QStringLiteral("xit_enable"), trx, boolText(r.xit))
          << message(QStringLiteral("rit_offset"), trx, QString::number(r.ritXitOffsetHz))
          << message(QStringLiteral("xit_offset"), trx, QString::number(r.ritXitOffsetHz))
          << message(QStringLiteral("lock"), trx, boolText(r.lock))
          << message(QStringLiteral("sql_enable"), trx, boolText(r.sqlEnabled))
          << message(QStringLiteral("sql_level"), trx, QString::number(r.sqlLevelDbm))
          << message(QStringLiteral("agc_mode"), trx, r.agcMode)
          << message(QStringLiteral("agc_gain"), trx, QString::number(r.agcGain))
          << message(QStringLiteral("rx_nb_enable"), trx, boolText(r.noiseBlanker))
          << message(QStringLiteral("rx_nb_param"), trx, QString::number(r.noiseBlankerLevel),
                     QString::number(r.noiseBlankerFilterWidth))
          << message(QStringLiteral("vfo_lock"), trx, QString::number(CHANNEL_A), boolText(r.lock))
          << message(QStringLiteral("rx_nr_enable"), trx, boolText(r.noiseReduction))
          << message(QStringLiteral("rx_anf_enable"), trx, boolText(r.autoNotch))
          << message(QStringLiteral("rx_apf_enable"), trx, boolText(r.apf))
          << message(QStringLiteral("rx_nf_enable"), trx, boolText(r.notchFilter))
          // rx_volume carries a channel index; the receiver's own channel is A.
          << message(QStringLiteral("rx_volume"), trx, QString::number(CHANNEL_A), QString::number(r.volumeDb))
          << message(QStringLiteral("mute"), trx, boolText(false))
          << message(QStringLiteral("tx_enable"), trx, boolText(receiver == MAIN_RECEIVER));

    // Drive belongs to the TRANSMITTER, and the K4 has exactly one, behind the main receiver.
    // Reporting it for the sub receiver as well was duplication describing a control that does
    // not exist there - a client could reasonably read drive:1,45 as a second power setting.
    //
    // Always <trx>,<power> when it IS sent: a bare "drive:0;" crashes ESDR3-mode WSJT-X and JTDX,
    // which index args[1] unconditionally.
    if (receiver == MAIN_RECEIVER) {
        burst << message(QStringLiteral("drive"), trx, QString::number(m_snapshot.drive))
              << message(QStringLiteral("tune_drive"), trx, QString::number(m_snapshot.tuneDrive));
    }
    return burst;
}

QStringList TciServer::initBurst() const {
    const QString mainTrx = QString::number(MAIN_RECEIVER);

    QStringList burst;
    burst << message(QStringLiteral("vfo_limits"), QString::number(kVfoLowHz), QString::number(kVfoHighHz))
          << message(QStringLiteral("if_limits"), QString::number(kIfLowHz), QString::number(kIfHighHz))
          // Two receivers: the K4's Main and Sub. See tciradiostate.h for why the Sub RX is a
          // receiver and not a channel.
          << message(QStringLiteral("trx_count"), QString::number(RECEIVER_COUNT))
          // Plural. The published PDF says CHANNEL_COUNT and the reference parser aborts on it.
          << message(QStringLiteral("channels_count"), QStringLiteral("2"))
          << message(QStringLiteral("device"), deviceIdentity())
          << message(QStringLiteral("receive_only"), boolText(false))
          << messageFreeText(QStringLiteral("modulations_list"), QLatin1String(kModulationsList))
          << messageFreeText(QStringLiteral("protocol"), QLatin1String(kProtocolIdentity));

    for (int r = 0; r < RECEIVER_COUNT; ++r) {
        burst << receiverBurst(r);
    }

    // Channel 1 of the main receiver is the transmit VFO, and the sub receiver doubles as that
    // channel for clients that model it the ExpertSDR3 way.
    burst << message(QStringLiteral("vfo"), mainTrx, QString::number(CHANNEL_B),
                     QString::number(m_snapshot.txChannelHz()))
          << message(QStringLiteral("rx_channel_enable"), mainTrx, QString::number(CHANNEL_A), boolText(true))
          << message(QStringLiteral("rx_channel_enable"), mainTrx, QString::number(CHANNEL_B),
                     boolText(m_snapshot.rx[SUB_RECEIVER].enabled))
          << message(QStringLiteral("tx_frequency"), QString::number(m_snapshot.txChannelHz()))
          // Not decoration: the RF2K-S amplifier uses split_enable:0,false as its signal that VFO
          // 0 is active, and reports "No TCI available" until it arrives.
          << message(QStringLiteral("split_enable"), mainTrx, boolText(m_snapshot.split))
          // mic_level, trx and volume are global single-argument commands - no receiver index.
          << message(QStringLiteral("mic_level"), QString::number(m_snapshot.micLevel))
          << message(QStringLiteral("cw_keyer_speed"), QString::number(m_snapshot.cwKeyerSpeedWpm))
          << message(QStringLiteral("cw_macros_speed"), QString::number(m_snapshot.cwKeyerSpeedWpm))
          << message(QStringLiteral("trx"), mainTrx, boolText(m_snapshot.transmitting))
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

void TciServer::applySet(int receiver, const QString &name, const TciProtocol::Command &command, int valueIndex) {
    // Only the values QK4 can actually send are acted on. Everything else in the group is still
    // answered from the snapshot, so a client is never left waiting - it simply does not move the
    // radio. See docs/tci-command-coverage.md section 8 for what is still missing a builder.
    //
    // SETs address the MAIN receiver only for now: the K4's sub-receiver forms are $-suffixed
    // (RT$, GT$, NB$, NR$, BW$) and CatFrames has no builders for them yet. Refusing is better
    // than silently moving the main receiver when a client asked for the sub.
    if (receiver != MAIN_RECEIVER) {
        return;
    }

    static const QSet<QString> boolSets{
        QStringLiteral("rit_enable"),
        QStringLiteral("xit_enable"),
        QStringLiteral("rx_nb_enable"),
        QStringLiteral("rx_nr_enable"),
    };
    // tune_drive goes to the K4's MENU ITEM 69 ("TUNE LP", 1-50 W), not to PC. It briefly shared
    // drive's handler and therefore sent PC - the OPERATING power - so asking for tune power
    // changed the wrong control. Found on the bench, and the reason these are separate now.
    static const QSet<QString> intSets{
        QStringLiteral("rit_offset"), QStringLiteral("xit_offset"), QStringLiteral("drive"),
        QStringLiteral("tune_drive"), QStringLiteral("agc_gain"),
    };

    if (boolSets.contains(name)) {
        bool value = false;
        if (command.argAsBool(valueIndex, &value)) {
            emit setBoolRequested(receiver, name, value);
        }
        return;
    }
    if (intSets.contains(name)) {
        int value = 0;
        if (command.argAsInt(valueIndex, &value)) {
            emit setIntRequested(receiver, name, value);
        }
        return;
    }
    if (name == QLatin1String("agc_mode")) {
        // A string value, and only the three the spec defines. An unknown one is refused rather
        // than coerced - coercing is how a radio ends up in a mode nobody asked for.
        const QString mode = command.arg(valueIndex).toLower();
        if (mode == QLatin1String("normal") || mode == QLatin1String("fast") || mode == QLatin1String("off")) {
            emit setIntRequested(receiver, name,
                                 mode == QLatin1String("off")    ? 0
                                 : mode == QLatin1String("fast") ? 2
                                                                 : 1);
        }
        return;
    }
    if (name == QLatin1String("rx_filter_band")) {
        // rx_filter_band:<trx>,<low>,<high> - both edges required, and low must be below high.
        int low = 0;
        int high = 0;
        if (command.argAsInt(valueIndex, &low) && command.argAsInt(valueIndex + 1, &high) && high > low) {
            emit setFilterBandRequested(receiver, low, high);
        }
        return;
    }
}

bool TciServer::answerReadOnly(int clientId, const TciProtocol::Command &command) {
    // Answers a query from the snapshot without touching the radio.
    //
    // WHY answer at all rather than stay silent: silence is what the protocol specifies for an
    // unknown command, but these are commands this server does declare in its init burst. A client
    // that polls one and gets nothing back can sit waiting - the failure TR4W recorded for an
    // unexpanded split_enable. Reporting the state we hold is honest and cheap.
    //
    // WHY read-only: the matching SETs move the radio, and none of them has been bench-tested
    // against a K4. They are deliberately deferred rather than shipped untested - see
    // docs/tci-server-design.md, phase 8.
    const QString &name = command.name;
    const TciRadioSnapshot &s = m_snapshot;

    // Per-receiver values. Every one of these is answered for the receiver the client asked about,
    // which is the point of modelling the Sub RX as receiver 1: as a channel it had no way to
    // report its own mode, filter, AGC or RIT at all.
    //
    // rx_enable IS NOT IN THIS SET, deliberately - do not re-add it. answerReadOnly runs ahead of
    // the rx_channel_enable/rx_enable branch in handleCommand, so listing it here consumed the
    // command first. A set then reached applySet, which refuses anything addressed at the sub
    // receiver, and the client got back an unchanged value with no sign its request had been
    // dropped: `rx_enable:1,true;` was answerable but not actionable, while the
    // `rx_channel_enable` spelling for the same K4 control worked. Left out, it falls through to
    // that branch, which answers queries from the snapshot exactly as this block did and also
    // emits setSubReceiverRequested.
    static const QSet<QString> perReceiver{
        QStringLiteral("rit_enable"),    QStringLiteral("xit_enable"),     QStringLiteral("rit_offset"),
        QStringLiteral("xit_offset"),    QStringLiteral("rx_filter_band"), QStringLiteral("drive"),
        QStringLiteral("tune_drive"),    QStringLiteral("agc_mode"),       QStringLiteral("tx_enable"),
        QStringLiteral("lock"),          QStringLiteral("sql_enable"),     QStringLiteral("sql_level"),
        QStringLiteral("mute"),          QStringLiteral("rx_nb_enable"),   QStringLiteral("rx_nr_enable"),
        QStringLiteral("rx_anf_enable"), QStringLiteral("rx_apf_enable"),  QStringLiteral("rx_nf_enable"),
        QStringLiteral("agc_gain"),
    };
    if (perReceiver.contains(name)) {
        // A first argument that PARSES AS AN INTEGER is the receiver index - that is the TCI
        // convention for every command in this group. One that does not parse is a value in the
        // global form (`rit_enable:true;`), which addresses the main receiver by definition.
        // Refusing on a failed parse instead would drop that form silently.
        int receiver = MAIN_RECEIVER;
        if (command.argCount() > 0 && command.argAsInt(0, &receiver) && !s.validReceiver(receiver)) {
            return true; // addressed a receiver that does not exist
        }
        if (!s.validReceiver(receiver)) {
            receiver = MAIN_RECEIVER;
        }
        const QString trx = QString::number(receiver);
        const TciReceiverState &r = s.rx[receiver];

        // Where the VALUE sits, if the client sent one. Normally arg 1, after the receiver index;
        // in the global form (`rit_enable:true;`) arg 0 is the value and there is no index.
        int valueIndex = -1;
        if (command.argCount() >= 2) {
            valueIndex = 1;
        } else if (command.argCount() == 1) {
            int probe = 0;
            if (!command.argAsInt(0, &probe)) {
                valueIndex = 0; // not an index, so it is a value
            }
        }
        if (valueIndex >= 0) {
            applySet(receiver, name, command, valueIndex);
        }

        QString reply;
        if (name == QLatin1String("rit_enable")) {
            reply = message(name, trx, boolText(r.rit));
        } else if (name == QLatin1String("xit_enable")) {
            reply = message(name, trx, boolText(r.xit));
        } else if (name == QLatin1String("rit_offset") || name == QLatin1String("xit_offset")) {
            // Deliberately the same value for both: the radio has one offset register.
            reply = message(name, trx, QString::number(r.ritXitOffsetHz));
        } else if (name == QLatin1String("rx_filter_band")) {
            reply = message(name, trx, QString::number(r.filterLowHz), QString::number(r.filterHighHz));
        } else if (name == QLatin1String("drive") || name == QLatin1String("tune_drive")) {
            // Only the main receiver has a transmitter behind it, so only it has a drive level.
            // Answering for the sub receiver would invent a second power control - and the reply
            // has to match the burst, which no longer carries one.
            if (receiver != MAIN_RECEIVER) {
                return true; // recognised, deliberately unanswered
            }
            // Always <trx>,<power>: a bare "drive:0;" crashes ESDR3-mode WSJT-X and JTDX.
            reply = message(name, trx, QString::number(name == QLatin1String("drive") ? s.drive : s.tuneDrive));
        } else if (name == QLatin1String("agc_mode")) {
            reply = message(name, trx, r.agcMode);
        } else if (name == QLatin1String("agc_gain")) {
            reply = message(name, trx, QString::number(r.agcGain));
        } else if (name == QLatin1String("tx_enable")) {
            // Only the main receiver has a transmitter behind it.
            reply = message(name, trx, boolText(receiver == MAIN_RECEIVER));
        } else if (name == QLatin1String("sql_level")) {
            reply = message(name, trx, QString::number(r.sqlLevelDbm));
        } else if (name == QLatin1String("sql_enable")) {
            reply = message(name, trx, boolText(r.sqlEnabled));
        } else if (name == QLatin1String("lock")) {
            reply = message(name, trx, boolText(r.lock));
        } else if (name == QLatin1String("rx_nb_enable")) {
            reply = message(name, trx, boolText(r.noiseBlanker));
        } else if (name == QLatin1String("rx_nr_enable")) {
            reply = message(name, trx, boolText(r.noiseReduction));
        } else if (name == QLatin1String("rx_anf_enable")) {
            reply = message(name, trx, boolText(r.autoNotch));
        } else if (name == QLatin1String("rx_apf_enable")) {
            reply = message(name, trx, boolText(r.apf));
        } else if (name == QLatin1String("rx_nf_enable")) {
            reply = message(name, trx, boolText(r.notchFilter));
        } else {
            // mute is the only one left, and QK4 does not model it for TCI. Claiming otherwise
            // would be a lie a client could act on.
            reply = message(name, trx, boolText(false));
        }
        sendTo(clientId, reply);
        return true;
    }

    if (name == QLatin1String("rx_nb_param")) {
        // rx_nb_param:<trx>[,<level>,<width>] - two values, so not the perReceiver shape.
        int receiver = MAIN_RECEIVER;
        if (command.argCount() > 0 && command.argAsInt(0, &receiver) && !s.validReceiver(receiver)) {
            return true;
        }
        if (!s.validReceiver(receiver)) {
            receiver = MAIN_RECEIVER;
        }
        int level = 0;
        int width = 0;
        if (receiver == MAIN_RECEIVER && command.argAsInt(1, &level) && command.argAsInt(2, &width)) {
            emit setNoiseBlankerParamRequested(level, width);
        }
        const TciReceiverState &r = s.rx[receiver];
        sendTo(clientId, message(name, QString::number(receiver), QString::number(r.noiseBlankerLevel),
                                 QString::number(r.noiseBlankerFilterWidth)));
        return true;
    }

    if (name == QLatin1String("vfo_lock")) {
        // vfo_lock:<trx>,<channel>[,<bool>] - a channel index, unlike plain lock.
        int receiver = MAIN_RECEIVER;
        int channel = CHANNEL_A;
        bool wanted = false;
        if (!command.argAsInt(0, &receiver) || !s.validReceiver(receiver) || !command.argAsInt(1, &channel)) {
            return true;
        }
        if (channel != CHANNEL_A && channel != CHANNEL_B) {
            return true;
        }
        // Channel B of the main receiver is VFO B, which is the sub receiver's own VFO.
        const int targetReceiver = (receiver == MAIN_RECEIVER && channel == CHANNEL_B) ? SUB_RECEIVER : receiver;
        if (command.argAsBool(2, &wanted)) {
            emit setVfoLockRequested(targetReceiver, wanted);
        }
        sendTo(clientId,
               message(name, QString::number(receiver), QString::number(channel), boolText(s.rx[targetReceiver].lock)));
        return true;
    }

    if (name == QLatin1String("rx_volume")) {
        // rx_volume:<trx>,<channel>[,<dB>] - a read carries two arguments, not one.
        int receiver = MAIN_RECEIVER;
        if (command.argCount() > 0 && command.argAsInt(0, &receiver) && !s.validReceiver(receiver)) {
            return true;
        }
        if (!s.validReceiver(receiver)) {
            receiver = MAIN_RECEIVER;
        }
        int channel = CHANNEL_A;
        if (command.argCount() > 1 && command.argAsInt(1, &channel) && channel != CHANNEL_A) {
            // Each receiver's audio is its own channel A; there is no separate level for the
            // main receiver's channel B, which is the transmit VFO rather than a receiver.
            return true;
        }
        sendTo(clientId, message(name, QString::number(receiver), QString::number(CHANNEL_A),
                                 QString::number(s.rx[receiver].volumeDb)));
        return true;
    }

    // Global values carry no receiver index.
    if (name == QLatin1String("cw_keyer_speed") || name == QLatin1String("cw_macros_speed")) {
        // Both names, one setting. The spec marks CW_KEYER_SPEED as client-to-server, but a client
        // that asks is better answered than ignored, and QK4 knows the value (RadioState::
        // keyerSpeed, the K4's KS command).
        sendTo(clientId, message(name, QString::number(s.cwKeyerSpeedWpm)));
        return true;
    }
    if (name == QLatin1String("mic_level")) {
        sendTo(clientId, message(name, QString::number(s.micLevel)));
        return true;
    }
    if (name == QLatin1String("volume")) {
        sendTo(clientId, message(name, QStringLiteral("0")));
        return true;
    }
    if (name == QLatin1String("trx_count")) {
        // Must agree with the burst, or a client that re-reads it concludes the sub receiver
        // vanished. answersQueriesConsistentlyWithTheInitBurst exists to catch exactly this.
        sendTo(clientId, message(name, QString::number(RECEIVER_COUNT)));
        return true;
    }
    if (name == QLatin1String("channels_count")) {
        sendTo(clientId, message(name, QStringLiteral("2")));
        return true;
    }
    if (name == QLatin1String("device")) {
        // The same string the burst carried. One source, because a query answering something other
        // than the greeting is the kind of drift nobody notices until a client trusts one of them.
        sendTo(clientId, message(name, deviceIdentity()));
        return true;
    }
    if (name == QLatin1String("receive_only")) {
        sendTo(clientId, message(name, boolText(false)));
        return true;
    }
    if (name == QLatin1String("protocol")) {
        sendTo(clientId, messageFreeText(name, QLatin1String(kProtocolIdentity)));
        return true;
    }
    if (name == QLatin1String("modulations_list")) {
        sendTo(clientId, messageFreeText(name, QLatin1String(kModulationsList)));
        return true;
    }
    if (name == QLatin1String("audio_samplerate") || name == QLatin1String("iq_samplerate")) {
        sendTo(clientId, message(name, QString::number(kAudioSampleRate)));
        return true;
    }
    if (name == QLatin1String("audio_stream_samples")) {
        sendTo(clientId, message(name, QString::number(kAudioStreamSamples)));
        return true;
    }
    if (name == QLatin1String("tx_stream_audio_buffering")) {
        sendTo(clientId, message(name, QStringLiteral("50")));
        return true;
    }
    if (name == QLatin1String("audio_stream_channels")) {
        sendTo(clientId, message(name, QStringLiteral("2")));
        return true;
    }
    if (name == QLatin1String("audio_stream_sample_type")) {
        sendTo(clientId, message(name, QStringLiteral("float32")));
        return true;
    }
    return false;
}
