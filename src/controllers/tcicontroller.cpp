#include "controllers/tcicontroller.h"

#include <QMetaEnum>

#include <QThread>

#include "controllers/audiocontroller.h"
#include "controllers/connectioncontroller.h"
#include "controllers/menucontroller.h"
#include "network/catframes.h"
#include "models/radiostate.h"
#include <cmath>

#include "dsp/spectrumscale.h"
#include "network/tciaudiobridge.h"
#include "network/tciserver.h"

namespace {

// How long to wait for the TCI thread to finish at shutdown. Long enough for an audio callback in
// flight; short enough that a wedged thread does not hold up quitting. Exceeding it is handled
// rather than ignored - see the destructor.
constexpr int kThreadShutdownMs = 2000;

// K4 mode -> TCI modulation name. Only the modes in modulations_list are legal on the wire; an
// unknown one is reported as usb rather than invented, because a client that cannot parse the
// modulation aborts rather than ignoring the field.
QString tciModulationFor(RadioState::Mode mode) {
    switch (mode) {
    case RadioState::LSB:
        return QStringLiteral("lsb");
    case RadioState::USB:
        return QStringLiteral("usb");
    case RadioState::CW:
        return QStringLiteral("cw");
    case RadioState::CW_R:
        return QStringLiteral("cwr");
    case RadioState::FM:
        // "fm", not "nfm". The K4 has ONE FM mode and no narrow variant, so reporting nfm
        // describes a mode the radio does not have. nfm is still ACCEPTED from a client (see
        // k4ModeFor) because other servers use that name for the same thing - lenient in, exact
        // out.
        return QStringLiteral("fm");
    case RadioState::AM:
        return QStringLiteral("am");
    case RadioState::DATA:
        return QStringLiteral("digu");
    case RadioState::DATA_R:
        return QStringLiteral("digl");
    case RadioState::Unknown:
        break;
    }
    return QStringLiteral("usb");
}

// TCI modulation name -> K4 mode. The inverse of tciModulationFor; an unrecognised name never
// reaches here because TciServer refuses anything outside modulations_list rather than coercing it.
bool k4ModeFor(const QString &modulation, RadioState::Mode *out) {
    static const QHash<QString, RadioState::Mode> kModes{
        {QStringLiteral("lsb"), RadioState::LSB},     {QStringLiteral("usb"), RadioState::USB},
        {QStringLiteral("cw"), RadioState::CW},       {QStringLiteral("cwr"), RadioState::CW_R},
        {QStringLiteral("nfm"), RadioState::FM},      {QStringLiteral("fm"), RadioState::FM},
        {QStringLiteral("am"), RadioState::AM},       {QStringLiteral("sam"), RadioState::AM},
        {QStringLiteral("digu"), RadioState::DATA},   {QStringLiteral("digl"), RadioState::DATA_R},
        {QStringLiteral("rtty"), RadioState::DATA_R},
    };
    const auto it = kModes.constFind(modulation.toLower());
    if (it == kModes.constEnd()) {
        return false;
    }
    if (out) {
        *out = it.value();
    }
    return true;
}

// K4 AGC speed -> TCI AGC mode. TCI defines exactly three: normal, fast, off. QK4 previously
// answered "med", which is not one of them, so a client matching the documented vocabulary could
// not parse it.
QString tciAgcModeFor(RadioState::AGCSpeed speed) {
    switch (speed) {
    case RadioState::AGC_Off:
        return QStringLiteral("off");
    case RadioState::AGC_Fast:
        return QStringLiteral("fast");
    case RadioState::AGC_Slow:
        break;
    }
    // AGC_Slow is the K4's sustained setting and TCI's "normal" is the same idea. It is also the
    // safe default for a value we have not read yet.
    return QStringLiteral("normal");
}

// K4 power -> TCI DRIVE.
//
// RadioState::rfPower() is ALREADY the number QK4 shows the operator: watts in QRP and QRO, mW in
// XVTR, with powerRange() saying which. SideControlPanel::setPower does nothing but pick the unit
// label and the decimal places, so there is no normalisation anywhere in QK4 to reuse - and none
// to invent either.
//
// Reporting that value directly is what keeps the three displays agreeing: the K4 front panel,
// QK4's own PWR button, and a TCI client all show the same number. An earlier version of this
// scaled to a percentage of the range, which is arguably the tidier reading of a protocol field
// documented as "0 to 100" - and it made a client read 82 while the radio said 90 W. Agreement
// with the radio in front of the operator wins.
//
// The one place this loses: QRO runs to 110 W and the protocol field stops at 100, so 101-110 W
// all report 100. Nothing can be done about that without normalising, which costs more than it
// saves. See docs/tci-command-coverage.md.
int tciDriveFor(double power) {
    if (power < 0.0) {
        return 0; // RadioState's sentinel, before the first PC echo
    }
    return qBound(0, static_cast<int>(power + 0.5), 100);
}

// Filter passband edges, in Hz RELATIVE TO THE DIAL, which is what TCI's rx_filter_band wants:
// its own examples are signed, "RX_FILTER_BAND:1,-2900,-70;" for a lower-sideband filter.
//
// The placement rules are lifted from what the panadapter already draws (panadapter_rhi.cpp,
// secondary-VFO passband): the filter is centred on the dial plus or minus the IF shift, with the
// sign following the sideband, and symmetric about the carrier for AM and FM. Using the same
// convention means a TCI client's filter matches the passband QK4 paints, rather than being a
// second opinion about the same radio.
//
// Simplification worth knowing: the panadapter additionally offsets FSK-D and AFSK-A by half the
// RTTY shift. That is not reproduced here, so those sub-modes report the plain data placement.
void tciFilterEdges(RadioState::Mode mode, int bandwidthHz, int shiftHz, int *lowOut, int *highOut) {
    int centre = 0;
    switch (mode) {
    case RadioState::LSB:
    case RadioState::DATA_R:
    case RadioState::CW_R:
        centre = -shiftHz;
        break;
    case RadioState::USB:
    case RadioState::DATA:
    case RadioState::CW:
        centre = shiftHz;
        break;
    case RadioState::AM:
    case RadioState::FM:
    case RadioState::Unknown:
        centre = 0; // symmetric about the carrier, no IF shift applied
        break;
    }
    const int half = bandwidthHz / 2;
    *lowOut = centre - half;
    *highOut = centre + half;
}

// QK4's own playback gain -> TCI rx_volume, in dB.
//
// A TCI client is listening to QK4'S AUDIO STREAM, not to the rig's speaker, so the level that
// means something to it is QK4's mix - the Main and Sub sliders - and not the K4's AF gain. That
// also avoids polling the radio for a value QK4 otherwise has no use for.
//
// The conversion is exact rather than fitted: QK4's sliders are 0-100 scaled to a linear amplitude
// gain of 0.0-1.0, and dB = 20*log10(gain) is the definition of that ratio in dB. Gain 1.0 gives
// 0 dB, 0.5 gives -6 dB, and silence clamps to the -60 the protocol documents as "no sound".
int tciVolumeDbForGain(float gain) {
    if (!(gain > 0.0f)) {
        return -60; // silent, and log10(0) is undefined
    }
    const double db = 20.0 * std::log10(static_cast<double>(gain));
    return qBound(-60, static_cast<int>(std::lround(db)), 0);
}

// The K4's tune power lives in the menu, not in a dedicated command: item 69, "TUNE LP (Low power
// TUNE)", range 1-50 W. Confirmed from the radio's own MEDF dump:
//   MEDF0069,TUNE LP (Low power TUNE),TX,DEC,1,1,50,5,20,1;
constexpr int kTuneDriveMenuId = 69;
constexpr int kTuneDriveMinW = 1;
constexpr int kTuneDriveMaxW = 50;

// TCI's agc_gain is the AGC THRESHOLD - AetherSDR's cmdAgcGain reads and writes AgcThreshold -
// which on a K4 is menu item 10 of eight AGC entries. From the radio's own MEDF dump:
//   MEDF0010,AGC Threshold,RX AGC,DEC,1,2,8,6,6,1;
constexpr int kAgcGainMenuId = 10;
constexpr int kAgcGainMin = 2;
constexpr int kAgcGainMax = 8;

} // namespace

TciController::TciController(AudioController *audioController, ConnectionController *connectionController,
                             RadioState *radioState, MenuController *menuController, QObject *parent)
    : QObject(parent), m_audioController(audioController), m_connectionController(connectionController),
      m_radioState(radioState), m_menuController(menuController), m_server(new TciServer(nullptr)),
      m_bridge(new TciAudioBridge(m_server, nullptr)) {
    m_tciThread = new QThread(this);
    m_tciThread->setObjectName(QStringLiteral("TCI"));
    m_server->moveToThread(m_tciThread);
    m_bridge->moveToThread(m_tciThread);
    m_tciThread->start();

    // The wiring lives in helpers rather than one block - see each for what it covers.
    wireAudioAndClients();
    wireTransmit();
    wireCatSets();
    wireSnapshot();
}

// RX audio into the server, and the client roster back out. Split from the constructor,
// which had 46 connect() calls in one 287-line block - banned shape #3 in
// src/controllers/README.md, and the shape the other controllers avoid.
void TciController::wireAudioAndClients() {
    // RX audio: I/O thread -> TCI thread. Queued, so the resampling never runs on the I/O thread.
    if (m_audioController) {
        connect(m_audioController, &AudioController::rxAudioAvailable, m_bridge, &TciAudioBridge::onRxAudio,
                Qt::QueuedConnection);
    }

    // A listener arriving after an idle stretch must not hear samples from before the gap: the
    // bridge skips work entirely while nobody is subscribed, so the filter history is stale.
    connect(
        m_server, &TciServer::audioStartRequested, m_bridge, [this](int) { m_bridge->reset(); }, Qt::QueuedConnection);

    // Queued (m_server is on the TCI thread, this is not), so the cache is written on the main
    // thread and read there too. Re-emitted rather than forwarded directly so no consumer can
    // observe the signal before the cache it is expected to read.
    // The roster crosses threads by queued connection, so the element type has to be known to the
    // metatype system. Qt 6 registers most fully-defined types on its own; saying so explicitly
    // costs nothing and turns a silent "cannot queue argument" warning at runtime into a
    // compile-time requirement.
    qRegisterMetaType<QVector<TciClientInfo>>("QVector<TciClientInfo>");
    connect(m_server, &TciServer::clientsChanged, this, [this](const QVector<TciClientInfo> &clients) {
        m_clients = clients;
        emit clientsChanged(m_clients);
    });
    connect(m_server, &TciServer::clientCountChanged, this, [this](int count) {
        m_clientCount = count;
        emit clientCountChanged(count);
    });
}

// Everything that keys or unkeys the transmitter, in both directions: a client asking, and
// QK4 itself asking. Kept together because the ORDER within each edge is the subtle part.
void TciController::wireTransmit() {
    // TX. Order matters on both edges and is the reason these are not one connection:
    //  - keying:   select the TCI source BEFORE asserting PTT, or the first frames out are the
    //              microphone picking up the room.
    //  - unkeying: release PTT first, then hand the transmitter back to the microphone.
    if (m_audioController) {
        connect(m_server, &TciServer::pttRequested, this, [this](bool active) {
            // Guarded: setPttActive emits pttActiveChanged, and the handler below would otherwise
            // read this controller's own request as the operator taking the transmitter.
            m_drivingPtt = true;
            if (active) {
                m_audioController->setTxSource(AudioController::TxSource::Tci);
                m_audioController->setPttActive(true);
            } else {
                m_audioController->setPttActive(false);
                m_audioController->setTxSource(AudioController::TxSource::Microphone);
            }
            m_drivingPtt = false;
            emit transmittingChanged(active);
        });

        // QK4 ITSELF KEYED OR UNKEYED - Esc, the PTT button, the HaliKey PTT line, the side panel,
        // CatServer. All of them funnel through AudioController::setPttActive.
        //
        // Before this, none of them told the TCI side, so the server went on believing the client
        // held the transmitter until the client's own transmit period ended - up to ~15 s for FT8.
        // Pressing QK4's PTT inside that window sent the client's tones instead of the operator's
        // microphone, because AudioEngine discards mic audio unless the source is Microphone.
        //
        // WHY TAKE OVER RATHER THAN REFUSE, for a local PRESS while a client holds PTT: the
        // operator is at the radio and the client is not. Refusing would repeat the defect being
        // fixed here in the other direction - the operator acts, nothing happens, and nothing
        // anywhere says why. Taking over is also the only option that leaves the audio path
        // matching what the operator can hear.
        connect(m_audioController, &AudioController::pttActiveChanged, this, [this](bool active) {
            if (m_drivingPtt) {
                return; // this controller asked for it, not the operator
            }
            QMetaObject::invokeMethod(m_server, "releaseLocalPtt", Qt::QueuedConnection);
            // Either way the microphone is the source now: on a release because transmit is over,
            // on a press because it is the operator transmitting.
            m_audioController->setTxSource(AudioController::TxSource::Microphone);
            emit transmittingChanged(active);
        });

        // Queued: decoding lands on the TCI thread, the encode pipeline lives on the audio thread.
        connect(m_server, &TciServer::txAudioReceived, this, [this](const QByteArray &mono48k) {
            if (m_audioEnabled) {
                m_audioController->feedTciTxAudio(mono48k);
            }
        });
    }
}

// Client SETs turned into K4 commands. The largest group, and the only place a TCI name
// becomes a CatFrames builder.
void TciController::wireCatSets() {
    // CAT sets from a client.
    //
    // WHY these go through CatFrames rather than formatted strings: CatFrames is where K4 command
    // spelling lives (src/network/README.md), and a literal here would be a second place to get it
    // wrong. The TCI layer never spells a K4 command.
    if (m_connectionController) {
        // LOSING THE RADIO MID-TRANSMIT is a local unkey too. The transmitter is not keyed any more
        // whatever the server believes, so holding ownership would leave the next connection
        // starting with a client still nominally in charge of a transmitter it cannot reach.
        connect(m_connectionController, &ConnectionController::connectionStateChanged, this,
                [this](TcpClient::ConnectionState state) {
                    if (state == TcpClient::Disconnected) {
                        QMetaObject::invokeMethod(m_server, "releaseLocalPtt", Qt::QueuedConnection);
                        emit transmittingChanged(false);
                    }
                });

        connect(m_server, &TciServer::setFrequencyRequested, this, [this](int receiver, int channel, qint64 hz) {
            if (hz <= 0) {
                return;
            }
            // VFO B is addressed two ways and both mean the same register: as the SUB receiver's
            // own VFO, and as channel 1 of the main receiver (the split transmit VFO). On a K4
            // they are one piece of hardware.
            const bool isVfoB = (receiver == TciRadio::SUB_RECEIVER) ||
                                (receiver == TciRadio::MAIN_RECEIVER && channel == TciRadio::CHANNEL_B);
            applyCat(isVfoB ? CatFrames::frequencyB(static_cast<quint64>(hz))
                            : CatFrames::frequencyA(static_cast<quint64>(hz)));
        });
        connect(m_server, &TciServer::setModulationRequested, this, [this](int receiver, const QString &modulation) {
            RadioState::Mode mode = RadioState::USB;
            if (!k4ModeFor(modulation, &mode)) {
                return;
            }
            // The whole reason the Sub RX is a receiver rather than a channel: modulation has no
            // channel argument, so this could not be addressed at all before.
            applyCat(receiver == TciRadio::SUB_RECEIVER ? CatFrames::modeB(mode) : CatFrames::modeA(mode));
        });
        connect(m_server, &TciServer::setSplitRequested, this,
                [this](bool enabled) { applyCat(CatFrames::split(enabled)); });
        connect(m_server, &TciServer::setSubReceiverRequested, this,
                [this](bool enabled) { applyCat(CatFrames::subReceiver(enabled)); });

        // The normalised SETs. This is the ONLY place a TCI name becomes a K4 command, which is
        // the rule the design doc states: CatFrames owns K4 spelling and the TCI layer never
        // writes one. TciServer has already validated arity, receiver and vocabulary.
        connect(m_server, &TciServer::setBoolRequested, this, [this](int, const QString &name, bool value) {
            if (name == QLatin1String("rit_enable")) {
                applyCat(CatFrames::ritEnabled(value));
            } else if (name == QLatin1String("xit_enable")) {
                applyCat(CatFrames::xitEnabled(value));
            } else if (name == QLatin1String("rx_nb_enable")) {
                // Preserve the level: TCI asks for on/off and nothing more, so changing the level
                // here would be a setting the client never requested.
                const int level = m_radioState ? m_radioState->noiseBlankerLevel() : 0;
                applyCat(CatFrames::setNoiseBlanker(level < 0 ? 0 : level, value));
            } else if (name == QLatin1String("rx_nr_enable")) {
                applyCat(CatFrames::noiseReduction(value));
            }
        });

        connect(m_server, &TciServer::setIntRequested, this, [this](int, const QString &name, int value) {
            if (name == QLatin1String("rit_offset") || name == QLatin1String("xit_offset")) {
                // ONE register on the radio, so either TCI name writes the same RO. Setting the
                // "XIT offset" necessarily moves RIT's too - see docs/tci-command-coverage.md 4.1.
                applyCat(CatFrames::ritOffset(value));
            } else if (name == QLatin1String("agc_mode")) {
                applyCat(CatFrames::agcSpeed(value));
            } else if (name == QLatin1String("agc_gain")) {
                // Menu item 10, raw 2-8. One control on the radio, so a write from either
                // receiver moves both - the same shape as the shared RIT/XIT offset.
                applyCat(CatFrames::setMenuValue(kAgcGainMenuId, qBound(kAgcGainMin, value, kAgcGainMax)));
            } else if (name == QLatin1String("tune_drive")) {
                // MENU ITEM 69, "TUNE LP (Low power TUNE)", 1-50 W - NOT PC. Sending PC here is
                // the bug this branch exists to prevent: it changes the operating power.
                const int watts = qBound(kTuneDriveMinW, value, kTuneDriveMaxW);
                applyCat(CatFrames::setMenuValue(kTuneDriveMenuId, watts));
                // No optimistic write: MenuModel is updated by the ME echo the radio sends back,
                // and menuValueChanged republishes the snapshot from it.
            } else if (name == QLatin1String("drive")) {
                // drive ONLY - tune_drive is handled above and must not fall through to PC.
                // PCnnnr, with the range letter - the same form QK4's UI sends. The PCX variant
                // is the extended QUERY and the radio ignores it as a set, which is why drive
                // failed on the bench until this changed.
                applyCat(CatFrames::setRfPower(value, m_radioState && m_radioState->isQrpMode()));
            }
        });

        connect(m_server, &TciServer::setNoiseBlankerParamRequested, this, [this](int level, int width) {
            // The K4 carries level, on/off and filter width in ONE command, so the current
            // enabled state has to be preserved - sending the level alone would switch NB off.
            const bool on = m_radioState && m_radioState->noiseBlankerEnabled();
            applyCat(CatFrames::setNoiseBlanker(level, on, width));
        });

        connect(m_server, &TciServer::cwMacroRequested, this,
                [this](const QVector<CwMacroSegment> &segments) { sendCwMacro(segments); });
        connect(m_server, &TciServer::setKeyerSpeedRequested, this, [this](int wpm) {
            // CatFrames::keyerSpeed clamps to the K4's documented 8..100, so a client asking for
            // something unkeyable gets the nearest speed the radio will actually do rather than a
            // command it ignores.
            applyCat(CatFrames::keyerSpeed(wpm));
        });
        connect(m_server, &TciServer::cwAbortRequested, this, [this]() {
            // Straight out, with no optimistic RadioState echo: there is no state to update, and
            // the frame carries a control character plus two commands.
            m_connectionController->sendCAT(QString::fromLatin1(CatFrames::cwAbort()));
        });
        connect(m_server, &TciServer::setVfoLockRequested, this, [this](int receiver, bool locked) {
            applyCat(CatFrames::setVfoLock(locked, receiver == TciRadio::SUB_RECEIVER));
        });

        connect(m_server, &TciServer::setFilterBandRequested, this, [this](int, int lowHz, int highHz) {
            // TCI gives two edges; the K4 takes a WIDTH. The conversion is lossy in one direction
            // and that is unavoidable - the centre is set by the mode and the IF shift, not by
            // this command, so only the width survives.
            applyCat(CatFrames::setFilterBandwidth(highHz - lowHz));
        });
    }

    // Keep the server's snapshot in step with the radio. Without this the init burst reports the
    // struct's defaults forever - which showed up immediately as a client stuck on 20m while the
    // K4 was on 40m.
    //
    // A whole snapshot is pushed on every change rather than individual fields: RadioState emits
    // fine-grained signals with no batch boundary, so a client seeded field-by-field could see a
    // frequency and mode that never coexisted.
    if (m_menuController) {
        // Covers the front panel as well as our own writes: the radio echoes an ME for both.
        connect(m_menuController, &MenuController::menuValueChanged, this, [this](int menuId, int) {
            if (menuId == kTuneDriveMenuId || menuId == kAgcGainMenuId) {
                publishSnapshot();
            }
        });
    }
}

// RadioState changes pushed to the server as whole snapshots. Reads RadioState on the MAIN
// thread, which is the rule this wiring exists to keep (CONVENTIONS.md rule 4).
void TciController::wireSnapshot() {
    if (m_radioState) {
        connect(m_radioState, &RadioState::frequencyChanged, this, [this](quint64) { publishSnapshot(); });
        connect(m_radioState, &RadioState::frequencyBChanged, this, [this](quint64) { publishSnapshot(); });
        connect(m_radioState, &RadioState::modeChanged, this, [this](RadioState::Mode) { publishSnapshot(); });
        connect(m_radioState, &RadioState::splitChanged, this, [this](bool) { publishSnapshot(); });
        connect(m_radioState, &RadioState::ritXitChanged, this, [this](bool, bool, int) { publishSnapshot(); });
        // Without this a transmit started anywhere other than a TCI client - the mic, a
        // footswitch, another CAT client - never reaches TCI clients at all.
        connect(m_radioState, &RadioState::transmitStateChanged, this, [this](bool) { publishSnapshot(); });
        connect(m_radioState, &RadioState::rfPowerChanged, this,
                [this](double, LevelsState::PowerRange) { publishSnapshot(); });

        // Meters. Frequent by nature: the server stores these and emits on its own timer at the
        // interval the client asked for, so a fast radio cannot flood the link.
        connect(m_radioState, &RadioState::sMeterChanged, this, [this](double) { publishSensors(); });
        connect(m_radioState, &RadioState::sMeterBChanged, this, [this](double) { publishSensors(); });
        connect(m_radioState, &RadioState::swrChanged, this, [this](double) { publishSensors(); });
        connect(m_radioState, &RadioState::txMeterChanged, this,
                [this](int, int, double, double) { publishSensors(); });
        connect(m_radioState, &RadioState::modeBChanged, this, [this](RadioState::Mode) { publishSnapshot(); });
        connect(m_radioState, &RadioState::filterBandwidthChanged, this, [this](int) { publishSnapshot(); });
        connect(m_radioState, &RadioState::filterBandwidthBChanged, this, [this](int) { publishSnapshot(); });
        connect(m_radioState, &RadioState::keyerSpeedChanged, this, [this](int) { publishSnapshot(); });
        connect(m_radioState, &RadioState::micGainChanged, this, [this](int) { publishSnapshot(); });
        connect(m_radioState, &RadioState::subRxEnabledChanged, this, [this](bool enabled) {
            // The bridge decides what goes in the right audio channel, and it lives on the TCI
            // thread, so this has to be marshalled rather than written from here.
            QMetaObject::invokeMethod(
                m_bridge, [this, enabled]() { m_bridge->setSubReceiverEnabled(enabled); }, Qt::QueuedConnection);
            publishSnapshot();
        });
        // RadioState has no per-field AGC signal; processingChanged is the coarse one it emits from
        // handleGT, and it also covers the NB/NR fields when those get wired.
        connect(m_radioState, &RadioState::processingChanged, this, [this]() { publishSnapshot(); });

        // EVERY snapshot field needs a signal, or it reports a value that can change and never
        // announces the change. That gap is not theoretical: vfo_lock moved on the radio and in
        // QK4's own UI while TCI kept reporting false, because nothing here listened for it.
        //
        // The DSP flags are spread across three state files with separate signals, which is why
        // subscribing to processingChanged alone was not enough:
        //   processingstate  -> processingChanged, processingChangedB, notchChanged, notchBChanged
        //   audioeffectsstate -> apfChanged, apfBChanged
        //   frequencyvfostate -> lockAChanged, lockBChanged
        connect(m_radioState, &RadioState::processingChangedB, this, [this]() { publishSnapshot(); });
        connect(m_radioState, &RadioState::notchChanged, this, [this]() { publishSnapshot(); });
        connect(m_radioState, &RadioState::notchBChanged, this, [this]() { publishSnapshot(); });
        connect(m_radioState, &RadioState::apfChanged, this, [this]() { publishSnapshot(); });
        connect(m_radioState, &RadioState::apfBChanged, this, [this]() { publishSnapshot(); });
        connect(m_radioState, &RadioState::lockAChanged, this, [this](bool) { publishSnapshot(); });
        connect(m_radioState, &RadioState::lockBChanged, this, [this](bool) { publishSnapshot(); });
        publishSnapshot();
        publishSensors();
        // Seed the bridge too: the Sub RX may already be on when the controller is constructed,
        // and subRxEnabledChanged only fires on a change.
        const bool subOn = m_radioState->subReceiverEnabled();
        QMetaObject::invokeMethod(
            m_bridge, [this, subOn]() { m_bridge->setSubReceiverEnabled(subOn); }, Qt::QueuedConnection);
    }
}

void TciController::audioLevelsChanged() {
    publishSnapshot();
}

void TciController::setAudioEnabled(bool enabled) {
    m_audioEnabled = enabled;
    // The bridge lives on the TCI thread; its flag is a plain bool read on that thread only.
    QMetaObject::invokeMethod(m_bridge, [this, enabled]() { m_bridge->setEnabled(enabled); }, Qt::QueuedConnection);
}

void TciController::sendCwMacro(const QVector<CwMacroSegment> &segments) {
    if (!m_connectionController || segments.isEmpty()) {
        return;
    }

    // SAY SO WHEN NOTHING IS GOING TO HAPPEN. The K4 keys KY in CW and the DATA modes and discards
    // it in silence otherwise - no keying, no error - so an operator whose radio is in SSB presses
    // a macro key and gets nothing, with no explanation available anywhere: not from the logger,
    // not from the radio. Confirmed with QLog, which never sets the mode itself.
    //
    // It is STILL SENT. The mode is the operator's to manage and switching it here would be a
    // surprising side effect of a text command; and in the DATA modes the radio sends the text as
    // data, which is a legitimate use this must not refuse.
    if (m_radioState && !CatFrames::modeKeysCwText(m_radioState->mode())) {
        qWarning() << "TCI: CW text sent while the radio is in"
                   << QMetaEnum::fromType<RadioState::Mode>().valueToKey(m_radioState->mode())
                   << "- the K4 keys KY only in CW and DATA modes, so this will be silently ignored";
    }

    // What to send is decided in CwMacro::plan, which is tested; this only carries it out.
    const int baseWpm = m_radioState ? m_radioState->keyerSpeed() : 0;

    for (const CwMacroStep &step : CwMacro::plan(segments, baseWpm)) {
        if (step.setWpm >= 0) {
            applyCat(CatFrames::keyerSpeed(step.setWpm));
        }
        for (const QByteArray &frame : CatFrames::cwText(step.text, step.wait)) {
            // No optimistic echo for KY: nothing in RadioState describes text in the keyer buffer,
            // and feeding CW text to the CAT parser could only misfire.
            m_connectionController->sendCAT(QString::fromLatin1(frame));
        }
    }
}

void TciController::applyCat(const QByteArray &frame) {
    const QString command = QString::fromLatin1(frame);

    // Mirrors what CatServer's wiring does for an external client (mainwindow.cpp:352-375): send it,
    // then parse it locally so the panadapter passband tracks immediately. K4 spectrum packets
    // arrive BEFORE the CAT echo, so without the optimistic parse the passband goes off-screen
    // until the echo lands.
    m_connectionController->sendCAT(command);

    // parseCATCommand is main-thread-only and CI-enforced (CONVENTIONS.md rule 4). This lambda runs
    // on the main thread already, but the queue keeps that true if the signal is ever reconnected
    // from the TCI thread.
    if (m_radioState) {
        QMetaObject::invokeMethod(
            m_radioState, [this, command]() { m_radioState->parseCATCommand(command); }, Qt::QueuedConnection);
    }
}

void TciController::publishSensors() {
    if (!m_radioState) {
        return;
    }
    TciSensorReadings readings;

    // The protocol wants an absolute level in dBm. RadioState carries the K4's S-meter in its own
    // encoding, so the conversion goes through the one place that owns the S-unit convention.
    readings.sMeterDbm[TciRadio::MAIN_RECEIVER] = SpectrumScale::dbmForSMeterReading(m_radioState->sMeter());
    readings.sMeterDbm[TciRadio::SUB_RECEIVER] = SpectrumScale::dbmForSMeterReading(m_radioState->sMeterB());

    readings.forwardPowerW = m_radioState->forwardPower();
    // The K4 reports ONE forward-power figure, not an RMS/peak pair. Reporting it as both is the
    // honest reading of what we have; inventing a peak by holding a maximum here would be a
    // measurement QK4 never made. See docs/tci-command-coverage.md.
    readings.peakPowerW = m_radioState->forwardPower();
    readings.swr = m_radioState->swrMeter();

    // micLevelDbm is deliberately left at its floor - see TciSensorReadings. The K4 reports ALC
    // deflection, which is a drive indicator, not a calibrated microphone level in dBm.

    QMetaObject::invokeMethod(m_server, [this, readings]() { m_server->setSensors(readings); }, Qt::QueuedConnection);
}

void TciController::publishSnapshot() {
    if (!m_radioState) {
        return;
    }
    TciRadioSnapshot snapshot;

    // MAIN receiver: the K4's VFO A side.
    TciReceiverState &main = snapshot.rx[TciRadio::MAIN_RECEIVER];
    main.vfoHz = static_cast<qint64>(m_radioState->vfoA());
    main.modulation = tciModulationFor(m_radioState->mode());
    main.enabled = true; // always on
    main.rit = m_radioState->ritEnabled();
    main.xit = m_radioState->xitEnabled();
    main.ritXitOffsetHz = m_radioState->ritXitOffset();
    main.agcMode = tciAgcModeFor(m_radioState->agcSpeed());
    main.noiseBlanker = m_radioState->noiseBlankerEnabled();
    main.noiseReduction = m_radioState->noiseReductionEnabled();
    main.autoNotch = m_radioState->autoNotchEnabled();
    main.apf = m_radioState->apfEnabled();
    main.notchFilter = m_radioState->manualNotchEnabled();
    main.noiseBlankerLevel = qMax(0, m_radioState->noiseBlankerLevel());
    main.noiseBlankerFilterWidth = qMax(0, m_radioState->noiseBlankerFilterWidth());
    main.volumeDb = m_audioController ? tciVolumeDbForGain(m_audioController->mainVolume()) : 0;
    main.lock = m_radioState->lockA();
    tciFilterEdges(m_radioState->mode(), m_radioState->filterBandwidth(), m_radioState->shiftHz(), &main.filterLowHz,
                   &main.filterHighHz);

    // SUB receiver: the K4's VFO B side. Every one of these has a *B counterpart in RadioState,
    // and none of them could be expressed at all while the Sub RX was modelled as a channel -
    // modulation, rx_filter_band and agc_mode carry no channel argument. See tciradiostate.h.
    TciReceiverState &sub = snapshot.rx[TciRadio::SUB_RECEIVER];
    sub.vfoHz = static_cast<qint64>(m_radioState->vfoB());
    sub.modulation = tciModulationFor(m_radioState->modeB());
    sub.enabled = m_radioState->subReceiverEnabled();
    sub.rit = m_radioState->ritEnabledB();
    sub.ritXitOffsetHz = m_radioState->ritXitOffsetB();
    sub.agcMode = tciAgcModeFor(m_radioState->agcSpeedB());
    sub.noiseBlanker = m_radioState->noiseBlankerEnabledB();
    sub.noiseReduction = m_radioState->noiseReductionEnabledB();
    sub.autoNotch = m_radioState->autoNotchEnabledB();
    sub.apf = m_radioState->apfEnabledB();
    sub.notchFilter = m_radioState->manualNotchEnabledB();
    sub.noiseBlankerLevel = qMax(0, m_radioState->noiseBlankerLevelB());
    sub.noiseBlankerFilterWidth = qMax(0, m_radioState->noiseBlankerFilterWidthB());
    sub.volumeDb = m_audioController ? tciVolumeDbForGain(m_audioController->subVolume()) : 0;
    sub.lock = m_radioState->lockB();
    tciFilterEdges(m_radioState->modeB(), m_radioState->filterBandwidthB(), m_radioState->shiftBHz(), &sub.filterLowHz,
                   &sub.filterHighHz);

    snapshot.split = m_radioState->splitEnabled();
    snapshot.transmitting = m_radioState->isTransmitting();

    // The value QK4 already displays - see tciDriveFor. The K4 has no separate tune power, so
    // tune_drive tracks it rather than claiming a control the radio does not have.
    snapshot.drive = tciDriveFor(m_radioState->rfPower());
    // Tune power comes from the radio, not from a local guess. The K4 pushes the full MEDF menu
    // definitions at connect and individual ME updates afterwards, so MenuModel already holds the
    // live value for item 69 and reading it costs no extra traffic.
    //
    // Reporting a remembered value instead would have been actively harmful: a client that reads,
    // changes and restores would write back a number the radio never had, silently clobbering a
    // TUNE LP set on the front panel.
    // AGC threshold is a single radio-wide menu item, so both receivers report the same number.
    int agcThreshold = 0;
    if (m_menuController && m_menuController->menuValue(kAgcGainMenuId, &agcThreshold) && agcThreshold > 0) {
        main.agcGain = agcThreshold;
        sub.agcGain = agcThreshold;
    }

    int tuneWatts = 0;
    if (m_menuController && m_menuController->menuValue(kTuneDriveMenuId, &tuneWatts) && tuneWatts > 0) {
        snapshot.tuneDrive = tuneWatts;
    } else {
        // No menu yet - no radio, or it has not sent MEDF. Falling back to drive keeps the field
        // plausible rather than reporting a zero a client might write back.
        snapshot.tuneDrive = snapshot.drive;
    }
    // RadioState uses -1 as "not read from the radio yet", and a negative WPM is nonsense on the
    // wire. Report the K4 default until the radio says otherwise.
    const int wpm = m_radioState->keyerSpeed();
    snapshot.cwKeyerSpeedWpm = (wpm > 0) ? wpm : 20;

    // mic_level is not in the published 2.0 spec - it is an ExpertSDR3 extension real clients
    // expect - so there is no documented range to map onto. RadioState::micGain is the K4's own
    // 0-80 MG value; reporting it directly beats the constant that was here before. -1 is the
    // not-read-yet sentinel.
    const int mic = m_radioState->micGain();
    snapshot.micLevel = (mic >= 0) ? mic : 0;

    // Queued: the server reads this from its own thread, so it must be handed over by value
    // through the event loop rather than written under it.
    QMetaObject::invokeMethod(m_server, [this, snapshot]() { m_server->setSnapshot(snapshot); }, Qt::QueuedConnection);
}

TciController::~TciController() {
    // Rule 11: drop queued signals before partial destruction, then stop producers before consumers.
    disconnect(this);

    if (!m_tciThread) {
        delete m_bridge;
        delete m_server;
        return;
    }

    QMetaObject::invokeMethod(m_server, "stop", Qt::BlockingQueuedConnection);

    // DESTROYED ON THEIR OWN THREAD, not from here.
    //
    // Both objects live on m_tciThread. Deleting one from another thread is undefined - it may be
    // inside a slot at the time - and this destructor used to do exactly that whenever the join
    // below timed out, because it deleted them regardless of the result. A deferred delete posted
    // to a thread is run when that thread's event loop finishes (Qt guarantees it even for a thread
    // with no loop left running), so these are gone before the thread exits, and nothing here
    // touches the pointers again either way.
    m_bridge->deleteLater();
    m_server->deleteLater();
    m_bridge = nullptr;
    m_server = nullptr;

    m_tciThread->quit();
    if (m_tciThread->wait(kThreadShutdownMs)) {
        return;
    }

    // THE TIMEOUT PATH, which is the one that crashed.
    //
    // m_tciThread is a child of this controller, so simply returning would have ~QObject destroy a
    // QThread that is still running - which aborts. Disown it instead. The thread, and whatever it
    // still holds, is leaked; at application exit that costs nothing, where the abort cost the user
    // a crash on every quit slow enough to reach this line.
    qWarning() << "TCI thread did not stop within" << kThreadShutdownMs
               << "ms - leaving it running rather than destroying it underneath itself";
    m_tciThread->setParent(nullptr);
    m_tciThread = nullptr;
}

void TciController::start(quint16 port, bool loopbackOnly) {
    bool ok = false;
    QMetaObject::invokeMethod(m_server, "start", Qt::BlockingQueuedConnection, Q_RETURN_ARG(bool, ok),
                              Q_ARG(quint16, port), Q_ARG(bool, loopbackOnly));
    // Blocking, so the result is authoritative by the time it lands here.
    m_listening = ok;
    emit listeningChanged(ok, ok ? port : quint16(0));
}

void TciController::stop() {
    QMetaObject::invokeMethod(m_server, "stop", Qt::BlockingQueuedConnection);
    m_listening = false;
    // The server emits clientDisconnected for each session it tears down, so the queued
    // clientCountChanged would converge on its own - but not before this function returns, and a
    // page repainting on listeningChanged would show a listener that is down with clients still
    // attached to it.
    m_clientCount = 0;
    m_clients.clear();
    emit clientsChanged(m_clients);
    emit listeningChanged(false, 0);
}
