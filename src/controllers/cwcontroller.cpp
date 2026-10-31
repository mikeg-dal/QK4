#include "cwcontroller.h"
#include "logging/logcategories.h"

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QTimer>
#include <algorithm>
#include <chrono>
#include <utility>

#include "audio/sidetonegenerator.h"
#include "connectioncontroller.h"
#include "hardware/cwgate.h"
#include "hardware/cwmonitormute.h"
#include "hardware/halikeydevice.h"
#include "hardware/keyerruntime.h"
#include "hardware/kpodplusdevice.h"
#include "hardware/paddlekeyer.h"
#include "hardware/vail_sidetone_routing.h"
#include "models/radiostate.h"
#include "network/kzegress.h"
#include "network/tcpclient.h"
#include "settings/radiosettings.h"

namespace {

void restoreMonitorWhenDue(TcpClient *client, const std::shared_ptr<CwMonitorMute::State> &monitor,
                           const std::shared_ptr<QElapsedTimer> &clock, quint64 generation) {
    if (!client->isConnected())
        return;
    const auto action = monitor->pollRestore(generation, clock->elapsed());
    if (!action.command.isEmpty())
        client->sendCAT(action.command);
    if (action.restoreAtMs < 0)
        return;
    // WHY re-arm: a timer wake is not proof that the monitor clock reached its deadline.
    const auto delay = std::max<qint64>(1, action.restoreAtMs - clock->elapsed());
    QTimer::singleShot(std::chrono::milliseconds(delay), Qt::PreciseTimer, client,
                       [client, monitor, clock, generation = action.generation]() {
                           restoreMonitorWhenDue(client, monitor, clock, generation);
                       });
}

} // namespace

CwController::CwController(RadioState *radioState, ConnectionController *connection, PaddleKeyer *keyer,
                           ForwardingLink *link, SidetoneGenerator *sidetone, HalikeyDevice *halikey,
                           KpodPlusDevice *kpodPlus, QObject *parent)
    : QObject(parent), m_radioState(radioState), m_connection(connection), m_keyer(keyer), m_link(link),
      m_sidetone(sidetone), m_halikey(halikey), m_kpodPlus(kpodPlus) {

    // =========================================================================
    // Keyer → K4
    // =========================================================================
    m_link->setTarget(m_connection->tcpClient()->kzEgress()->softwareLink());

    // =========================================================================
    // Configuration from the K4 (the source of truth) and from settings
    // =========================================================================
    auto *settings = RadioSettings::instance();
    m_cachedIsV14.store(settings->halikeyDeviceType() == HalikeyDevice::Serial, std::memory_order_release);
    m_vailSelected->store(settings->halikeyDeviceType() == HalikeyDevice::Vail, std::memory_order_release);
    m_vailComputerSidetone->store(settings->vailComputerSidetone(), std::memory_order_release);
    m_vailSidetoneRoute = std::make_shared<VailSidetoneRouting::Route>();
    wireKeyingRoutes();
    connect(settings, &RadioSettings::vailComputerSidetoneChanged, this, [this](bool computer) {
        m_vailComputerSidetone->store(computer, std::memory_order_release);
        if (m_vailSelected->load(std::memory_order_acquire)) {
            updateInhibit();
            updateVailSidetone(true);
        }
    });
    m_keyer->setStraightKeying(settings->halikeyStraightKey());
    m_keyer->setDitMemoryDelayPercent(settings->halikeyDitMemoryDelay());
    // WHY: the shared I/O policy owns monitor muting for both keyers; a second restore timer
    // could unmute the radio after switching from HaliKey to Vail mid-burst.
    m_keyer->setMuteRadioMonitor(false);
    if (m_radioState->keyDownInitialDelayMs() >= 0)
        m_keyer->setKeyDownInitialDelayMs(m_radioState->keyDownInitialDelayMs());
    applySpeed();
    applyMode();
    applyReversal();
    if (m_radioState->cwPitch() > 0)
        QMetaObject::invokeMethod(m_sidetone, "setFrequency", Qt::QueuedConnection,
                                  Q_ARG(int, m_radioState->cwPitch()));

    connect(m_radioState, &RadioState::keyerSpeedChanged, this, [this](int wpm) {
        applySpeed();
        configureVailKeyer();
        // K4 is the source of truth — mirror the speed onto the KPOD+ keyer.
        if (m_kpodPlus->isPolling())
            m_kpodPlus->setKeyerSpeed(wpm);
    });

    connect(m_radioState, &RadioState::keyerPaddleChanged, this, [this](QChar iambic, QChar paddle, int /*weight*/) {
        applySpeed(); // the weight
        applyMode();
        applyReversal();
        configureVailKeyer();
        if (m_kpodPlus->isPolling())
            m_kpodPlus->setKeyerParams(iambic == 'B' ? 1 : 0, paddle == 'R');
    });

    connect(m_radioState, &RadioState::cwPitchChanged, this, [this](int pitchHz) {
        configureVailKeyer();
        QMetaObject::invokeMethod(m_sidetone, "setFrequency", Qt::QueuedConnection, Q_ARG(int, pitchHz));
        // K4 is the source of truth — mirror the CW pitch onto the KPOD+ keyer.
        if (m_kpodPlus->isPolling())
            m_kpodPlus->setCwPitch(pitchHz);
    });

    connect(m_radioState, &RadioState::keyDownInitialDelayChanged, this,
            [this](int ms) { m_keyer->setKeyDownInitialDelayMs(ms); });

    connect(settings, &RadioSettings::halikeyDeviceTypeChanged, this, [this](int type) {
        m_cachedIsV14.store(type == HalikeyDevice::Serial, std::memory_order_release);
        m_vailSelected->store(type == HalikeyDevice::Vail, std::memory_order_release);
        m_keyer->releasePaddles();
        updateVailSidetone(true);
        configureVailKeyer();
        updateInhibit();
    });
    connect(settings, &RadioSettings::halikeyKeyerModeChanged, this, [this](int) { applyMode(); });
    m_halikey->setStraightKey(settings->halikeyStraightKey());
    connect(settings, &RadioSettings::halikeyStraightKeyChanged, this, [this](bool straight) {
        m_keyer->setStraightKeying(straight);
        m_halikey->setStraightKey(straight);
        applyReversal();
    });
    connect(settings, &RadioSettings::halikeyDitMemoryDelayChanged, this,
            [this](int percent) { m_keyer->setDitMemoryDelayPercent(percent); });
    // =========================================================================
    // Inhibit: radio ready, in CW, KPOD+ not owning CW
    // =========================================================================
    connect(m_radioState, &RadioState::modeChanged, this, [this](RadioState::Mode) { updateInhibit(); });
    connect(m_connection, &ConnectionController::radioReady, this, [this]() {
        m_radioReady = true;
        configureVailKeyer();
        updateInhibit();
    });
    connect(m_connection, &ConnectionController::connectionStateChanged, this,
            [this](TcpClient::ConnectionState state) {
                if (state != TcpClient::Connected) {
                    m_radioReady = false;
                    updateInhibit();
                }
            });
    updateInhibit();

    // =========================================================================
    // HaliKey → keyer
    // =========================================================================
    // V1.4 serial: the worker's poller has already applied rest state, straight-key debounce and
    // the per-line delta before delivering each edge on its own thread.
    m_paddleConnection = connect(
        m_halikey, &HalikeyDevice::paddleChanged, this,
        [this](bool dah, bool asserted, double deltaTime) { onPaddle(dah, asserted, deltaTime); },
        Qt::DirectConnection);

    // MIDI: raw messages with RtMidi's delta, on RtMidi's thread. MoMIDI's own timestamps are
    // decoded by the keyer.
    m_halikey->setMidiSink([this](int status, int note, int velocity, double deltaTime) {
        if (!m_cachedIsV14.load(std::memory_order_acquire) && !m_vailSelected->load(std::memory_order_acquire))
            m_keyer->midiMessage(status, note, velocity, deltaTime);
    });

    // A lost device releases the key: a held paddle's release would otherwise never arrive.
    connect(m_halikey, &HalikeyDevice::disconnected, this, [this]() {
        m_keyer->releasePaddles();
        updateVailSidetone(true);
        updateInhibit(); // the sidetone stream closes with the last keying source
    });
    connect(m_halikey, &HalikeyDevice::connected, this, [this]() {
        configureVailKeyer();
        updateInhibit();
    });

    // =========================================================================
    // KPOD+ EP02 keyer data
    // =========================================================================
    // The KPOD+ delivers complete KZ/KX strings in 32-byte transfers, zero-padded after the last
    // ';'. The EP02 reader trims the padding and posts each batch to the same KZ egress, from its
    // own thread: no Qt event, no copy into a QByteArray, and the main thread is not on the path.
    m_kpodPlus->setKeyerLink(m_connection->tcpClient()->kzEgress());
    configureVailKeyer();
    if (m_vailSelected->load(std::memory_order_acquire))
        updateVailSidetone(true);
}

CwController::~CwController() {
    // WHY the handle and not m_halikey->disconnect(this): the HaliKey may already be destroyed
    // (HardwareController goes first), and disconnecting a handle is safe either way. What actually
    // protects the device-thread handlers is MainWindow::closeEvent calling
    // HardwareController::shutdownDevices() while ConnectionController is still alive; a disconnect
    // cannot stop a call the worker has already started.
    disconnect(m_paddleConnection);
    disconnect(m_vailSidetoneConnection);
    // WHY the MIDI sink is NOT cleared here: HardwareController, and with it the HaliKey, is
    // destroyed before this (construction order), so m_halikey dangles by now. Clearing it here
    // crashed QK4 on every quit. Nothing needs it cleared: shutdownDevices() closes the HaliKey,
    // which stops RtMidi's callbacks, before either object goes away, and the sink dies with the
    // device.
}

void CwController::applySpeed() {
    int wpm = m_radioState->keyerSpeed();
    if (wpm <= 0)
        wpm = 20;
    const int weight = m_radioState->keyingWeight();
    m_keyer->setSpeed(wpm, weight > 0 ? weight / 100.0 : 1.0);
}

void CwController::applyMode() {
    switch (RadioSettings::instance()->halikeyKeyerMode()) {
    case 1:
        m_keyer->setMode(PaddleKeyer::Mode::PlainIambic);
        break;
    case 2:
        m_keyer->setMode(PaddleKeyer::Mode::Ultimatic);
        break;
    default:
        m_keyer->setMode(m_radioState->iambicMode() == 'A' ? PaddleKeyer::Mode::IambicA : PaddleKeyer::Mode::IambicB);
        break;
    }
}

void CwController::applyReversal() {
    // A straight key is on the dit line whatever the paddle orientation.
    const bool reversed = m_radioState->paddleOrientation() == 'R' && !RadioSettings::instance()->halikeyStraightKey();
    const bool changed = m_reversed.exchange(reversed, std::memory_order_acq_rel) != reversed;
    m_keyer->setDitNotes({reversed ? 21 : 20});
    m_keyer->setDahNotes({reversed ? 20 : 21});
    // WHY only on a change: KP arrives again whenever the weight or iambic mode moves, which must
    // not cut a burst in progress. A real swap must, or a held lever would change meaning mid-key.
    if (changed)
        m_keyer->releasePaddles();
}

void CwController::updateInhibit() {
    const auto mode = m_radioState->mode();
    CwGate::Inputs in;
    in.radioReady = m_radioReady;
    in.inCwMode = mode == RadioState::CW || mode == RadioState::CW_R;
    in.kpodPlusOwnsCw = m_kpodPlusGate;
    in.halikeyOpen = m_halikey->isConnected();
    const bool vail = m_vailSelected->load(std::memory_order_acquire);
    const bool inhibited = CwGate::keyerInhibited(in) || vail;
    m_connection->tcpClient()->kzEgress()->setSoftwareKeyingAllowed(!inhibited);
    m_keyer->setInhibited(inhibited);
    const bool wanted =
        CwGate::sidetoneStreamWanted(in) && (!vail || m_vailComputerSidetone->load(std::memory_order_acquire));
    if (wanted != m_sidetoneStreamWanted) {
        m_sidetoneStreamWanted = wanted;
        QMetaObject::invokeMethod(m_sidetone, "setStreamWanted", Qt::QueuedConnection, Q_ARG(bool, wanted));
    }
    updateVailKeying();
}

void CwController::onPaddle(bool dah, bool asserted, double deltaTime) {
    if (!m_cachedIsV14.load(std::memory_order_acquire))
        return; // MIDI reaches the keyer through the raw sink
    if (m_reversed.load(std::memory_order_acquire))
        dah = !dah;
    if (dah)
        m_keyer->paddleEvent(asserted ? PaddleKeyer::Event::DahDown : PaddleKeyer::Event::DahUp, deltaTime);
    else
        m_keyer->paddleEvent(asserted ? PaddleKeyer::Event::DitDown : PaddleKeyer::Event::DitUp, deltaTime);
}

void CwController::setKeyDownInitialDelay(int ms) {
    m_connection->sendCAT(QString("KZL%1;").arg(qBound(0, ms, 1000), 4, 10, QChar('0')));
    m_connection->sendCAT(QStringLiteral("KZL;"));
}

void CwController::stopKeying() {
    m_keyer->releasePaddles();
    if (m_vailSelected->load(std::memory_order_acquire)) {
        m_halikey->setVailKeyingEnabled(false);
        updateVailSidetone(true);
        updateVailKeying();
    }
}

void CwController::setKpodPlusGate(bool active) {
    // The single most useful line in a CW bench log: it says who is generating the elements.
    qCInfo(lcCwKeyer) << "KPOD+ keyer gate"
                      << (active ? "UP - the KPOD+ owns CW; QK4's keyer keys nothing"
                                 : "DOWN - QK4's own keyer owns CW again");
    m_kpodPlusGate = active;
    m_kpodPlusOwnsCw->store(active, std::memory_order_release);
    updateInhibit();
}

void CwController::wireKeyingRoutes() {
    auto *tc = m_connection->tcpClient();
    auto *egress = tc->kzEgress();
    const auto monitor = std::make_shared<CwMonitorMute::State>();
    const auto monitorClock = std::make_shared<QElapsedTimer>();
    monitorClock->start();
    monitor->setEnabled(RadioSettings::instance()->halikeyMuteRadioMonitor(), 0);
    monitor->observeLevel(m_radioState->monitorLevelCW(), 0);
    monitor->setKeyDownDelay(m_radioState->keyDownInitialDelayMs(), 0);
    const auto scheduledRestore = std::make_shared<std::pair<quint64, qint64>>(0, -1);
    const auto applyMonitor = [tc, monitor, monitorClock, scheduledRestore](const CwMonitorMute::Action &action) {
        if (!tc->isConnected())
            return;
        if (!action.command.isEmpty())
            tc->sendCAT(action.command);
        const auto timer = std::make_pair(action.generation, action.restoreAtMs);
        if (*scheduledRestore == timer)
            return;
        *scheduledRestore = timer;
        if (action.restoreAtMs < 0)
            return;
        restoreMonitorWhenDue(tc, monitor, monitorClock, action.generation);
    };
    const auto sendKeying = [tc, monitor, monitorClock, applyMonitor](const QString &command) {
        applyMonitor(monitor->accept(command, monitorClock->elapsed()));
        tc->sendCAT(command);
    };
    // WHY: these callbacks own their state and use only I/O objects. They must survive this
    // controller until TcpClient's graceful disconnect has restored a monitor we muted.
    connect(RadioSettings::instance(), &RadioSettings::halikeyMuteRadioMonitorChanged, tc,
            [monitor, monitorClock, applyMonitor](bool enabled) {
                applyMonitor(monitor->setEnabled(enabled, monitorClock->elapsed()));
            });
    connect(tc->protocol(), &Protocol::catResponseReceived, tc,
            [monitor, monitorClock, applyMonitor](const QString &response) {
                applyMonitor(monitor->observeResponse(response, monitorClock->elapsed()));
            });
    connect(tc, &TcpClient::authenticated, tc, [tc]() { tc->sendCAT(QStringLiteral("ML0;KZL;")); });
    connect(tc, &TcpClient::aboutToDisconnect, tc, [tc, monitor]() {
        const auto command = monitor->restoreBeforeDisconnect();
        if (!command.isEmpty())
            tc->sendCAT(command);
    });
    connect(m_halikey, &HalikeyDevice::vailCleanupFailed, tc, &TcpClient::disconnectFromHost, Qt::QueuedConnection);
    const auto vailSelected = m_vailSelected;
    const auto vailControl = m_halikey->vailRoutingControl();
    const auto vailRoute = std::make_shared<VailCatRouting::Route>();
    tc->setKzFrameFilter([egress, vailRoute, monitor, monitorClock, applyMonitor](
                             std::string_view frame, KzEgress::Source source, quint64 epoch, bool eligibleAtPost) {
        if (source == KzEgress::Source::Kpod)
            return true;
        const auto command = QString::fromLatin1(frame.data(), static_cast<qsizetype>(frame.size()));
        const bool eligible = eligibleAtPost && egress->softwareKeyingAllowed() && epoch == egress->softwareEpoch();
        if (!vailRoute->acceptSoftware(command, epoch, eligible))
            return false;
        applyMonitor(monitor->accept(command, monitorClock->elapsed()));
        return true;
    });
    const auto computerSidetone = m_vailComputerSidetone;
    const auto sidetoneGeneration = m_vailSidetoneGeneration;
    const auto sidetoneGate = m_vailSidetoneGate;
    const auto sidetoneRoute = m_vailSidetoneRoute;
    const auto kpodPlusOwnsCw = m_kpodPlusOwnsCw;
    auto *sg = m_sidetone;
    m_vailSidetoneConnection = connect(
        m_halikey, &HalikeyDevice::routedVailKeyingStateChanged, sg,
        [sg, kpodPlusOwnsCw, vailSelected, computerSidetone, vailControl, sidetoneGeneration, sidetoneGate,
         sidetoneRoute](bool down, quint64 epoch, qint64 timestampNs) {
            // Capture the feedback generation on the emitting worker thread, before
            // audio queuing. A rapid source switch cannot revive an old queued DOWN.
            const quint64 generation = sidetoneGeneration->load(std::memory_order_acquire);
            const bool eligibleAtEmission = sidetoneGate->load(std::memory_order_acquire);
            QMetaObject::invokeMethod(
                sg,
                [sg, kpodPlusOwnsCw, vailSelected, computerSidetone, vailControl, sidetoneGeneration, sidetoneGate,
                 sidetoneRoute, down, epoch, generation, timestampNs, eligibleAtEmission]() {
                    const bool eligible = eligibleAtEmission && sidetoneGate->load(std::memory_order_acquire) &&
                                          vailSelected->load(std::memory_order_acquire) &&
                                          computerSidetone->load(std::memory_order_acquire) &&
                                          !kpodPlusOwnsCw->load(std::memory_order_acquire);
                    if (sidetoneRoute->accept(down, epoch, generation,
                                              sidetoneGeneration->load(std::memory_order_acquire), *vailControl,
                                              eligible))
                        sg->setKeyDownAt(down, timestampNs);
                },
                Qt::QueuedConnection);
        },
        Qt::DirectConnection);
    connect(
        m_halikey, &HalikeyDevice::routedVailKeyingCommand, tc,
        [tc, kpodPlusOwnsCw, vailSelected, vailControl, vailRoute, sendKeying](const QString &command, quint64 epoch) {
            const bool eligible =
                vailSelected->load(std::memory_order_acquire) && !kpodPlusOwnsCw->load(std::memory_order_acquire);
            if (tc->isConnected() && vailRoute->accept(command, epoch, *vailControl, eligible))
                sendKeying(command);
        },
        Qt::QueuedConnection);
    // Reset on the I/O thread itself, before a blocked GUI can process disconnect.
    // Old UP/RX cleanup must never target a newly authenticated radio connection.
    connect(
        tc, &TcpClient::stateChanged, tc,
        [egress, vailControl, vailRoute, sidetoneGeneration, sidetoneGate, monitor](TcpClient::ConnectionState state) {
            if (state != TcpClient::Connected) {
                egress->setSoftwareKeyingAllowed(false);
                vailControl->invalidate();
                vailRoute->reset();
                monitor->reset();
                sidetoneGate->store(false, std::memory_order_release);
                sidetoneGeneration->fetch_add(1, std::memory_order_acq_rel);
            }
        },
        Qt::DirectConnection);
    // A queued receiver context keeps teardown safe after the audio object is
    // destroyed. The I/O-side gate above closes before a stalled GUI can react.
    connect(
        tc, &TcpClient::stateChanged, sg,
        [sg, vailSelected, sidetoneGate, sidetoneRoute](TcpClient::ConnectionState state) {
            if (state != TcpClient::Connected && vailSelected->load(std::memory_order_acquire) &&
                !sidetoneGate->load(std::memory_order_acquire)) {
                sidetoneRoute->reset();
                sg->resetKeyedTone();
                sg->setKeyDown(false);
            }
        },
        Qt::QueuedConnection);
}

void CwController::configureVailKeyer() {
    m_halikey->configureVailKeyer(m_radioState->keyerSpeed() > 0 ? m_radioState->keyerSpeed() : 20,
                                  m_radioState->cwPitch() > 0 ? m_radioState->cwPitch() : 600,
                                  m_radioState->iambicMode() == 'B' ? 1 : 0);
}

void CwController::updateVailKeying() {
    const bool inCw = m_radioState->mode() == RadioState::CW || m_radioState->mode() == RadioState::CW_R;
    // Queue audio reset/priming before a newly enabled worker can emit DOWN.
    updateVailSidetone();
    m_halikey->setVailKeyingEnabled(m_radioReady && inCw && !m_kpodPlusGate);
}

void CwController::updateVailSidetone(bool forceReset) {
    const bool inCw = m_radioState->mode() == RadioState::CW || m_radioState->mode() == RadioState::CW_R;
    const bool allowed = m_vailSelected->load(std::memory_order_acquire) &&
                         m_vailComputerSidetone->load(std::memory_order_acquire) && m_halikey->isConnected() &&
                         m_radioReady && inCw && !m_kpodPlusGate;
    if (!forceReset && allowed == m_vailSidetoneAllowed &&
        allowed == m_vailSidetoneGate->load(std::memory_order_acquire))
        return;
    m_vailSidetoneAllowed = allowed;
    // Close eligibility until the reset is enqueued. An edge racing a source
    // change cannot enter the audio queue ahead of the reset for its generation.
    m_vailSidetoneGate->store(false, std::memory_order_release);
    const quint64 generation = m_vailSidetoneGeneration->fetch_add(1, std::memory_order_acq_rel) + 1;
    QMetaObject::invokeMethod(
        m_sidetone,
        [sg = m_sidetone, route = m_vailSidetoneRoute, generations = m_vailSidetoneGeneration,
         selected = m_vailSelected, generation]() {
            if (!route->resetIfCurrent(generation, generations->load(std::memory_order_acquire)))
                return;
            sg->resetKeyedTone();
            if (selected->load(std::memory_order_acquire))
                sg->setKeyDown(false);
        },
        Qt::QueuedConnection);
    m_vailSidetoneGate->store(allowed, std::memory_order_release);
}
