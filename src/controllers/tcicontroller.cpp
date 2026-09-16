#include "controllers/tcicontroller.h"

#include <QThread>

#include "controllers/audiocontroller.h"
#include "controllers/connectioncontroller.h"
#include "network/catframes.h"
#include "models/radiostate.h"
#include "dsp/spectrumscale.h"
#include "network/tciaudiobridge.h"
#include "network/tciserver.h"

namespace {

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
        return QStringLiteral("nfm");
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

} // namespace

TciController::TciController(AudioController *audioController, ConnectionController *connectionController,
                             RadioState *radioState, QObject *parent)
    : QObject(parent), m_audioController(audioController), m_connectionController(connectionController),
      m_radioState(radioState), m_server(new TciServer(nullptr)), m_bridge(new TciAudioBridge(m_server, nullptr)) {
    m_tciThread = new QThread(this);
    m_tciThread->setObjectName(QStringLiteral("TCI"));
    m_server->moveToThread(m_tciThread);
    m_bridge->moveToThread(m_tciThread);
    m_tciThread->start();

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
    connect(m_server, &TciServer::clientCountChanged, this, [this](int count) {
        m_clientCount = count;
        emit clientCountChanged(count);
    });

    // TX. Order matters on both edges and is the reason these are not one connection:
    //  - keying:   select the TCI source BEFORE asserting PTT, or the first frames out are the
    //              microphone picking up the room.
    //  - unkeying: release PTT first, then hand the transmitter back to the microphone.
    if (m_audioController) {
        connect(m_server, &TciServer::pttRequested, this, [this](bool active) {
            if (active) {
                m_audioController->setTxSource(AudioController::TxSource::Tci);
                m_audioController->setPttActive(true);
            } else {
                m_audioController->setPttActive(false);
                m_audioController->setTxSource(AudioController::TxSource::Microphone);
            }
        });

        // Queued: decoding lands on the TCI thread, the encode pipeline lives on the audio thread.
        connect(m_server, &TciServer::txAudioReceived, this, [this](const QByteArray &mono48k) {
            if (m_audioEnabled) {
                m_audioController->feedTciTxAudio(mono48k);
            }
        });
    }

    // CAT sets from a client.
    //
    // WHY these go through CatFrames rather than formatted strings: CatFrames is where K4 command
    // spelling lives (src/network/README.md), and a literal here would be a second place to get it
    // wrong. The TCI layer never spells a K4 command.
    if (m_connectionController) {
        connect(m_server, &TciServer::setFrequencyRequested, this, [this](int channel, qint64 hz) {
            if (hz <= 0) {
                return;
            }
            applyCat(channel == 1 ? CatFrames::frequencyB(static_cast<quint64>(hz))
                                  : CatFrames::frequencyA(static_cast<quint64>(hz)));
        });
        connect(m_server, &TciServer::setModulationRequested, this, [this](const QString &modulation) {
            RadioState::Mode mode = RadioState::USB;
            if (k4ModeFor(modulation, &mode)) {
                applyCat(CatFrames::modeA(mode));
            }
        });
        connect(m_server, &TciServer::setSplitRequested, this,
                [this](bool enabled) { applyCat(CatFrames::split(enabled)); });
        connect(m_server, &TciServer::setSubReceiverRequested, this,
                [this](bool enabled) { applyCat(CatFrames::subReceiver(enabled)); });
    }

    // Keep the server's snapshot in step with the radio. Without this the init burst reports the
    // struct's defaults forever - which showed up immediately as a client stuck on 20m while the
    // K4 was on 40m.
    //
    // A whole snapshot is pushed on every change rather than individual fields: RadioState emits
    // fine-grained signals with no batch boundary, so a client seeded field-by-field could see a
    // frequency and mode that never coexisted.
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
        publishSnapshot();
        publishSensors();
        // Seed the bridge too: the Sub RX may already be on when the controller is constructed,
        // and subRxEnabledChanged only fires on a change.
        const bool subOn = m_radioState->subReceiverEnabled();
        QMetaObject::invokeMethod(
            m_bridge, [this, subOn]() { m_bridge->setSubReceiverEnabled(subOn); }, Qt::QueuedConnection);
    }
}

void TciController::setAudioEnabled(bool enabled) {
    m_audioEnabled = enabled;
    // The bridge lives on the TCI thread; its flag is a plain bool read on that thread only.
    QMetaObject::invokeMethod(m_bridge, [this, enabled]() { m_bridge->setEnabled(enabled); }, Qt::QueuedConnection);
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
    readings.sMeterDbm = SpectrumScale::dbmForSMeterReading(m_radioState->sMeter());
    readings.sMeterSubDbm = SpectrumScale::dbmForSMeterReading(m_radioState->sMeterB());

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
    snapshot.vfoAHz = static_cast<qint64>(m_radioState->vfoA());
    snapshot.vfoBHz = static_cast<qint64>(m_radioState->vfoB());
    snapshot.split = m_radioState->splitEnabled();
    snapshot.modulation = tciModulationFor(m_radioState->mode());

    // RIT/XIT: two enables, ONE offset - the shape the radio and RadioState both use.
    snapshot.rit = m_radioState->ritEnabled();
    snapshot.xit = m_radioState->xitEnabled();
    snapshot.ritXitOffsetHz = m_radioState->ritXitOffset();

    snapshot.agcMode = tciAgcModeFor(m_radioState->agcSpeed());

    // The Sub RX is TCI channel 1 of receiver 0, not a second receiver. See TciRadioSnapshot.
    snapshot.subEnabled = m_radioState->subReceiverEnabled();

    // The radio's own transmit state. TciServer decides whether this or a TCI client's assertion
    // wins - see setSnapshot - but it can only do that if the value actually arrives.
    snapshot.transmitting = m_radioState->isTransmitting();

    // The value QK4 already displays - see tciDriveFor. The K4 has no separate tune power, so
    // tune_drive tracks it rather than claiming a control the radio does not have.
    snapshot.drive = tciDriveFor(m_radioState->rfPower());
    snapshot.tuneDrive = snapshot.drive;
    snapshot.modulationB = tciModulationFor(m_radioState->modeB());

    // Queued: the server reads this from its own thread, so it must be handed over by value
    // through the event loop rather than written under it.
    QMetaObject::invokeMethod(m_server, [this, snapshot]() { m_server->setSnapshot(snapshot); }, Qt::QueuedConnection);
}

TciController::~TciController() {
    // Rule 11: drop queued signals before partial destruction, then stop producers before consumers.
    disconnect(this);
    if (m_tciThread) {
        QMetaObject::invokeMethod(m_server, "stop", Qt::BlockingQueuedConnection);
        m_tciThread->quit();
        m_tciThread->wait(2000);
    }
    delete m_bridge;
    delete m_server;
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
    emit listeningChanged(false, 0);
}
