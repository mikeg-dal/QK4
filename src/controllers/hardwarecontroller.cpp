#include "hardwarecontroller.h"
#include "logging/logcategories.h"
#include "logging/logsink.h"
#include "audio/sidetonegenerator.h"
#include "audio/sidetonescheduler.h"
#include "connectioncontroller.h"
#include "hardware/halikeydevice.h"
#include "hardware/keyerruntime.h"
#include "hardware/paddlekeyer.h"
#include "hardware/kpoddevice.h"
#include "hardware/kpodplusdevice.h"
#include "models/radiostate.h"
#include "settings/radiosettings.h"
#include "utils/radioutils.h"
#include "utils/rtscheduling.h"
#include "utils/threadshutdown.h"
#include <QLoggingCategory>
#include <QTimer>
#include <chrono>

namespace {
// The K4 reports no stuck-key timeout; push this fixed default to the KPOD+.
constexpr int kKpodPlusStuckTimeoutSec = 60;

// WHY 1.5 s: close() is queued behind the element the keyer is sending, and an iambic element plus
// its space is up to 600 ms at the 8 WPM floor (RadioUtils::ditMsForWpm). Then run() returns.
constexpr std::chrono::milliseconds kKeyerStopTimeout{1500};
// WHY 2 s: stop() destroys the sidetone QAudioSink, as AudioEngine's does for RX audio.
constexpr std::chrono::milliseconds kSidetoneStopTimeout{2000};
} // namespace

HardwareController::HardwareController(RadioState *radioState, ConnectionController *connController, QObject *parent)
    : QObject(parent), m_radioState(radioState), m_connectionController(connController) {
    // =========================================================================
    // KPOD USB tuning knob
    // =========================================================================
    m_kpodDevice = new KpodDevice(this);

    connect(m_kpodDevice, &KpodDevice::encoderRotated, this, &HardwareController::onKpodEncoderRotated);
    connect(m_kpodDevice, &KpodDevice::pollError, this, &HardwareController::onKpodPollError);

    // KPOD button signals → macro execution via MainWindow
    connect(m_kpodDevice, &KpodDevice::buttonTapped, this,
            [this](int buttonNum) { emit macroRequested(QString("K-pod.%1T").arg(buttonNum)); });
    connect(m_kpodDevice, &KpodDevice::buttonHeld, this,
            [this](int buttonNum) { emit macroRequested(QString("K-pod.%1H").arg(buttonNum)); });

    // Lifecycle → policy. Detection is a fact the device reports; what to DO about it is decided
    // in one place (hardware/usbdevicelifecycle.h) for both devices.
    //
    // WHY deviceInfoReady rather than deviceConnected for detection: KpodDevice::detectDevice() is
    // deferred to the first event-loop tick (see kpoddevice.cpp) to keep the 400 ms hid_open_path
    // retry off the main thread at startup, so isDetected() is false at construction and this signal
    // is the first honest answer.
    //
    // deviceDisconnected is deliberately NOT mapped to an event. It fires both when the cable is
    // pulled and when WE called stopPolling, with no way to tell them apart — while an actual
    // removal already arrives here as deviceInfoReady with isDetected() false. Feeding it in as a
    // loss would mark a device "gone" that is still plugged in.
    connect(m_kpodDevice, &KpodDevice::deviceInfoReady, this, [this]() {
        applyKpod(m_kpodDevice->isDetected() ? UsbDeviceLifecycle::Event::Detected : UsbDeviceLifecycle::Event::Lost);
    });
    connect(m_kpodDevice, &KpodDevice::deviceConnected, this,
            [this]() { applyKpod(UsbDeviceLifecycle::Event::Opened); });

    // Settings: one checkbox governs both devices.
    m_kpodState.enabled = RadioSettings::instance()->kpodEnabled();
    m_kpodPlusState.enabled = m_kpodState.enabled;
    connect(RadioSettings::instance(), &RadioSettings::kpodEnabledChanged, this, [this](bool enabled) {
        const auto ev = enabled ? UsbDeviceLifecycle::Event::Enabled : UsbDeviceLifecycle::Event::Disabled;
        applyKpod(ev);
        applyKpodPlus(ev);
    });

    // =========================================================================
    // KPOD+ USB keyer device (vendor-specific class, libusb)
    // =========================================================================
    m_kpodPlusDevice = new KpodPlusDevice(this);

    // Encoder/button/rocker signals — KPOD+ encoder reads its own rocker position
    connect(m_kpodPlusDevice, &KpodPlusDevice::encoderRotated, this, [this](int ticks) {
        if (!m_connectionController->isConnected())
            return;
        // Use KPOD+ rocker position (same encoding as KPOD)
        int rocker = static_cast<int>(m_kpodPlusDevice->rockerPosition());
        onKpodEncoderRotatedWithRocker(ticks, rocker);
    });
    connect(m_kpodPlusDevice, &KpodPlusDevice::pollError, this, &HardwareController::onKpodPollError);
    connect(m_kpodPlusDevice, &KpodPlusDevice::buttonTapped, this,
            [this](int buttonNum) { emit macroRequested(QString("K-pod.%1T").arg(buttonNum)); });
    connect(m_kpodPlusDevice, &KpodPlusDevice::buttonHeld, this,
            [this](int buttonNum) { emit macroRequested(QString("K-pod.%1H").arg(buttonNum)); });

    // At KPOD+ plug-in, push the current keyer config to the device. The K4 is
    // the source of truth: keyer speed / CW pitch / iambic mode / paddle
    // orientation come from RadioState (skipped while still at their sentinels,
    // i.e. before the first K4 update arrives). Encode mode has no K4 equivalent
    // and comes from RadioSettings; the K4 reports no stuck timeout so a fixed
    // default is used. Live K4 changes are pushed by CwController.
    auto applyKpodPlusConfig = [this]() {
        if (m_radioState->keyerSpeed() > 0)
            m_kpodPlusDevice->setKeyerSpeed(m_radioState->keyerSpeed());
        if (m_radioState->cwPitch() > 0)
            m_kpodPlusDevice->setCwPitch(m_radioState->cwPitch());
        if (!m_radioState->iambicMode().isNull() && !m_radioState->paddleOrientation().isNull())
            m_kpodPlusDevice->setKeyerParams(m_radioState->iambicMode() == 'B' ? 1 : 0,
                                             m_radioState->paddleOrientation() == 'R');
        m_kpodPlusDevice->setEncodeMode(RadioSettings::instance()->kpodPlusEncodeMode());
        m_kpodPlusDevice->setStuckTimeout(kKpodPlusStuckTimeoutSec);
    };

    // Auto-start polling on device arrival. The KPOD+ keyer-active gate +
    // EP02 keyer-data routing are wired by CwController, which observes the
    // same deviceConnected / deviceInfoReady signals independently.
    m_applyKpodPlusConfig = applyKpodPlusConfig;
    connect(m_kpodPlusDevice, &KpodPlusDevice::deviceInfoReady, this, [this]() {
        applyKpodPlus(m_kpodPlusDevice->isDetected() ? UsbDeviceLifecycle::Event::Detected
                                                     : UsbDeviceLifecycle::Event::Lost);
    });
    connect(m_kpodPlusDevice, &KpodPlusDevice::deviceConnected, this,
            [this]() { applyKpodPlus(UsbDeviceLifecycle::Event::Opened); });
    // The enable handler is shared with the KPOD above — one checkbox, both devices.

    // =========================================================================
    // HaliKey CW paddle device — device type injected here so HalikeyDevice
    // itself doesn't reach into RadioSettings (Phase 3 layering cleanup).
    // =========================================================================
    m_halikeyDevice = new HalikeyDevice(RadioSettings::instance()->halikeyDeviceType(), this);
    connect(RadioSettings::instance(), &RadioSettings::halikeyDeviceTypeChanged, m_halikeyDevice,
            &HalikeyDevice::setDeviceType);

    // =========================================================================
    // Sidetone generator (dedicated thread for low-latency audio feedback)
    // The keyer below refers to the scheduler; the generator owns the clock the scheduler reads.
    // =========================================================================
    m_keyerClock = std::make_unique<SteadyKeyerClock>();
    m_keyerLink = std::make_unique<ForwardingLink>();
    m_sidetoneScheduler = std::make_unique<SidetoneScheduler>(*m_keyerClock, nullptr);
    m_unmuteTimer = new QtOneShotTimer(this);

    m_sidetoneGenerator = new SidetoneGenerator(*m_sidetoneScheduler, *m_keyerClock, nullptr);
    m_sidetoneScheduler->setStreamTimeSource([sg = m_sidetoneGenerator]() { return sg->streamTime(); });
    m_sidetoneThread = new QThread(this);
    m_sidetoneThread->setObjectName("Sidetone");
    Logging::markRealtimeOnStart(m_sidetoneThread);
    m_sidetoneGenerator->moveToThread(m_sidetoneThread);
    // Destroyed on its own thread when that thread finishes (Rule 14). Kept so shutdown can cut it
    // if the keyer, which reads the generator's stream clock, could not be stopped.
    m_sidetoneDeleteConnection =
        connect(m_sidetoneThread, &QThread::finished, m_sidetoneGenerator, &QObject::deleteLater);
    // Promoted to real-time by SidetoneGenerator::start() on the thread itself: sidetone shares the
    // CW perception path with the keyer, and a preempted block write is audible onset jitter.
    m_sidetoneThread->start();
    QMetaObject::invokeMethod(m_sidetoneGenerator, "start", Qt::QueuedConnection);

    // Set sidetone to same output device as AudioEngine
    QString savedSidetoneDevice = RadioSettings::instance()->speakerDevice();
    if (!savedSidetoneDevice.isEmpty()) {
        QMetaObject::invokeMethod(m_sidetoneGenerator, "setOutputDevice", Qt::QueuedConnection,
                                  Q_ARG(QString, savedSidetoneDevice));
    }

    // Follow speaker device changes at runtime
    connect(RadioSettings::instance(), &RadioSettings::speakerDeviceChanged, this, [this](const QString &deviceId) {
        QMetaObject::invokeMethod(m_sidetoneGenerator, "setOutputDevice", Qt::QueuedConnection,
                                  Q_ARG(QString, deviceId));
    });

    // Set initial sidetone volume from RadioSettings (independent of K4's MON level)
    QMetaObject::invokeMethod(m_sidetoneGenerator, "setVolume", Qt::QueuedConnection,
                              Q_ARG(float, RadioSettings::instance()->sidetoneVolume() / 100.0f));

    // Update sidetone volume when changed in Options
    connect(RadioSettings::instance(), &RadioSettings::sidetoneVolumeChanged, this, [this](int value) {
        QMetaObject::invokeMethod(m_sidetoneGenerator, "setVolume", Qt::QueuedConnection, Q_ARG(float, value / 100.0f));
    });

    // Sidetone CW frequency + keyer-speed wiring lives on CwController.

    // =========================================================================
    // Paddle keyer (real-time thread for CW element timing).
    // Constructed + thread-managed here; all CW wiring is on CwController.
    // =========================================================================
    m_paddleKeyer = std::make_unique<PaddleKeyer>(*m_keyerClock, *m_keyerLink, *m_sidetoneScheduler, *m_unmuteTimer);
    // Nothing is keyed until CwController lifts this: the radio must be ready, in CW, and the
    // KPOD+ not owning CW.
    m_paddleKeyer->setInhibited(true);
    m_keyerThread = QThread::create([keyer = m_paddleKeyer.get()]() {
        RtScheduling::promoteCurrentThread(RtScheduling::Threads::kKeyer);
        keyer->run();
    });
    m_keyerThread->setParent(this);
    m_keyerThread->setObjectName("Keyer");
    Logging::markRealtimeOnStart(m_keyerThread);
    // WHY real-time, set by the thread itself: every element boundary is a sleep on this thread,
    // and a preempted wake is a late decision (the keyer learns and subtracts a steady overrun, not
    // a random one). QThread::HighPriority is a no-op on Linux and weaker than this elsewhere, and
    // on macOS the scheduling parameters it sets would opt the thread out of QoS.
    m_keyerThread->start();

    // All CW wiring — keyer configuration from RadioState and settings, HaliKey lines and MIDI →
    // keyer, the keyer's link to the K4, inhibit on mode/connection/KPOD+ — lives on CwController
    // (constructed by MainWindow immediately after this controller). See cwcontroller.h.

    // =========================================================================
    // Device notifications — one policy for all four devices
    // =========================================================================
    // Only the HaliKey used to reach NotificationWidget. The KPOD and KPOD+ announced their arrival
    // and departure to a label on the Options page and nowhere else, so a KPOD+ that vanished
    // mid-session said nothing at all — despite taking the whole CW chain with it, because it owns
    // keying while attached and QK4's keyer is gated off behind it.
    //
    // Each message names the device the operator recognises, and says what STOPPED WORKING rather
    // than what failed internally. "KPOD+ disconnected" is not actionable on its own; "CW keying
    // has returned to QK4's keyer" tells them why the paddle now feels different.
    //
    // WHY pollError is NOT wired to a popup: it comes from a polling loop and can repeat at the
    // poll rate. A popup per failed poll would bury the screen. It stays a log line, now naming
    // which device produced it.

    // connectionError only, NOT disconnected. An unplug raises both - the worker's error handler
    // calls closePort(), which emits disconnected(), and then emits connectionError - so wiring
    // both would pop two notifications for one event. disconnected() also fires on a deliberate
    // close (changing the port, or quitting), which deserves no notification at all.
    // The keyer, sidetone and EP02 threads promote themselves as they start, the HaliKey poller when
    // its port opens; report each once it has had time to.
    QTimer::singleShot(2000, this, &HardwareController::reportThreadScheduling);
    connect(m_halikeyDevice, &HalikeyDevice::connected, this, &HardwareController::reportThreadScheduling);

    connect(m_halikeyDevice, &HalikeyDevice::connectionError, this, [this](const QString &error) {
        emit hardwareError(QStringLiteral("%1: %2 — paddle keying has stopped.").arg(halikeyName(), error));
    });

    // The KPOD/KPOD+ notifications are raised by applyKpod()/applyKpodPlus() from the policy's
    // Effects, not from deviceDisconnected. That signal cannot distinguish a pulled cable from our
    // own stopPolling(), and it arrives twice per unplug on both devices — which is exactly what
    // defeated the one-shot "expected stop" flag this replaces.
}

void HardwareController::applyKpod(UsbDeviceLifecycle::Event event) {
    const UsbDeviceLifecycle::Effects e = UsbDeviceLifecycle::apply(m_kpodState, event);
    if (e.reportArrived)
        qCInfo(lcKpod) << "KPOD available";
    if (e.reportRemoved)
        qCInfo(lcKpod) << "KPOD no longer available";
    if (e.notifyOpenFailed)
        qCWarning(lcKpod) << "KPOD could not be opened";
    if (e.open)
        m_kpodDevice->startPolling();
    if (e.close)
        m_kpodDevice->stopPolling();
    if (e.notifyDisconnected)
        emit hardwareError(QStringLiteral("KPOD disconnected — the tuning knob and its buttons have stopped."));
}

void HardwareController::applyKpodPlus(UsbDeviceLifecycle::Event event) {
    const UsbDeviceLifecycle::Effects e = UsbDeviceLifecycle::apply(m_kpodPlusState, event);
    if (e.notifyOpenFailed)
        qCWarning(lcKpodPlus) << "KPOD+ could not be opened";
    if (e.open) {
        // Raise the CW gate BEFORE the open, not after: claiming the interface takes 10-100 ms and
        // a paddle event landing in that window would reach QK4's own keyer and sidetone.
        emit kpodPlusOwnsCw(true);
        m_kpodPlusDevice->startPolling();
        // WHY the config push belongs HERE and not on deviceConnected: the old handler guarded on
        // !isPolling(), which m_polling had already made false by the time the signal arrived, so
        // the push never ran and the device kept whatever WPM and pitch it was last given
        // (USB-006). It also never ran on re-enable. Pushing from the policy's open decision covers
        // both, and is safe despite the open being asynchronous: startPolling() and every setter
        // queue to the SAME worker thread, and Qt delivers queued events in order, so openDevice
        // has run before the first config command is handled.
        if (m_applyKpodPlusConfig)
            m_applyKpodPlusConfig();
    }
    if (e.close) {
        m_kpodPlusDevice->stopPolling();
        emit kpodPlusOwnsCw(false);
    }
    if (e.notifyConnected)
        emit hardwareError(QStringLiteral("KPOD+ connected — it now owns CW keying."));
    if (e.notifyDisconnected)
        emit hardwareError(QStringLiteral("KPOD+ disconnected — CW keying has returned to QK4's keyer."));
}

QString HardwareController::halikeyName() const {
    // RadioSettings is the only place that knows which transport is configured; HalikeyDevice is
    // constructed from the same value. Named for the operator, matching the Options dropdown, so a
    // notification and the settings page cannot disagree about what the device is called.
    switch (RadioSettings::instance()->halikeyDeviceType()) {
    case HalikeyDevice::Midi:
        return QStringLiteral("HaliKey MIDI");
    case HalikeyDevice::Vail:
        return QStringLiteral("Vail Adapter");
    default:
        return QStringLiteral("HaliKey V1.4");
    }
}

void HardwareController::reportThreadScheduling() {
    for (const RtScheduling::Report &report : RtScheduling::takeUnreported()) {
        const RtScheduling::Result &r = report.result;
        QString detail;
        // Only the RLIMIT_NICE fallback leaves the value out of the mechanism; -10 names itself.
        if (r.nice != 0 && r.nice != RtScheduling::kLinuxHighNice)
            detail += QStringLiteral(" (nice %1)").arg(r.nice);
        if (r.error != 0)
            detail += QStringLiteral(" (OS error %1)").arg(r.error);
        qCInfo(lcHardware).nospace().noquote()
            << report.thread << " thread: asked for " << RtScheduling::className(r.requested) << ", got "
            << RtScheduling::outcomeName(r.outcome) << " - " << r.mechanism << detail;
    }
}

void HardwareController::shutdownDevices() {
    if (m_devicesShutDown) {
        return;
    }
    m_devicesShutDown = true;

    // Shutdown order: HaliKey → Keyer → Sidetone → KPOD/KPOD+
    // HaliKey stops paddle events first, then keyer (producer of KZ commands) stops
    // before sidetone (the audio consumer) is torn down.
    //
    // The sidetone teardown is synchronous and has to happen while the event loop is still alive:
    // destroying a QAudioSink from libc atexit races PipeWire's RT worker on Linux and segfaults
    // in pw_stream_dequeue_buffer.

    if (m_halikeyDevice) {
        m_halikeyDevice->closePort();
    }

    // close() releases whatever is keyed (an open straight-key KZD gets its KZU, queued for the
    // keyer thread ahead of Close) and posts Close, which ends run(). quit() would do nothing: the
    // keyer thread has no event loop.
    //
    // WHY the link is cleared only after the join: the keyer may still have a KZU to send (a
    // straight key held at quit). Dropping it would leave the K4 keyed. ConnectionController is
    // still alive here (CONC-001), so the send is safe. A keyer that would not stop has it cleared
    // anyway: it would otherwise send into the KZ egress after ConnectionController destroys it.
    if (m_keyerThread) {
        m_paddleKeyer->close();
        m_keyerDisowned = ThreadShutdown::join(m_keyerThread, QDeadlineTimer(kKeyerStopTimeout), lcCwKeyer()) ==
                          ThreadShutdown::Outcome::Disowned;
        m_keyerLink->setTarget(nullptr);
    }

    // WHY a disowned keyer keeps the generator alive: the keyer's SidetoneScheduler reads the
    // generator's stream clock (setStreamTimeSource), so the generator must outlive any keyer
    // still running. It is leaked with it rather than deleted when the sidetone thread finishes.
    if (m_keyerDisowned) {
        disconnect(m_sidetoneDeleteConnection);
    }
    if (SidetoneGenerator *generator = m_sidetoneGenerator) {
        const QDeadlineTimer deadline(kSidetoneStopTimeout);
        ThreadShutdown::callBlocking(generator, [generator]() { generator->stop(); }, deadline);
        ThreadShutdown::retire(m_sidetoneThread, deadline, lcAudioSidetone());
    }
    m_sidetoneGenerator = nullptr;

    // Through the policy, so the close is silent: the window is going away and there is nobody
    // left to tell that a device disconnected.
    applyKpodPlus(UsbDeviceLifecycle::Event::ShuttingDown);
    applyKpod(UsbDeviceLifecycle::Event::ShuttingDown);
    // The KZ egress belongs to the connection, which is destroyed before this controller.
    m_kpodPlusDevice->setKeyerLink(nullptr);
}

HardwareController::~HardwareController() {
    // Normally a no-op: MainWindow::closeEvent has already run this, which is the whole point —
    // doing it here for the first time is what CONC-001 was. Still called, because a fatal error
    // or a window that was never shown reaches the destructor without a closeEvent.
    shutdownDevices();

    // A keyer thread that would not stop still runs PaddleKeyer::run() against these. Leaked, not
    // destroyed underneath it; the process is exiting.
    if (m_keyerDisowned) {
        (void)m_paddleKeyer.release();
        (void)m_sidetoneScheduler.release();
        (void)m_keyerLink.release();
        (void)m_keyerClock.release();
        m_unmuteTimer->setParent(nullptr);
    }
}

// =============================================================================
// KPOD Event Handlers
// =============================================================================

void HardwareController::onKpodEncoderRotated(int ticks) {
    if (!m_connectionController->isConnected()) {
        return;
    }
    onKpodEncoderRotatedWithRocker(ticks, static_cast<int>(m_kpodDevice->rockerPosition()));
}

void HardwareController::onKpodEncoderRotatedWithRocker(int ticks, int rockerPos) {
    // Action depends on rocker position (shared by KPOD and KPOD+)
    //
    // USB-007. isConnected() goes true when the socket authenticates, which is BEFORE the RDY dump
    // that carries the radio's actual frequencies. A knob turn in that window read the VFO at its 0
    // sentinel and sent FA/FB for a near-zero frequency - the radio obeyed and jumped off band. The
    // `newFreq > 0` guards below do not catch it, because ticks x step is itself positive.
    //
    // Waiting for a real frequency is the honest condition: until the K4 has told us where the VFO
    // being tuned is, QK4 has nothing to tune relative to.
    switch (rockerPos) {
    case 2: // RockerLeft — VFO A
    {
        if (m_radioState->lockA() || m_radioState->vfoA() == 0)
            break;
        quint64 currentFreq = m_radioState->vfoA();
        int stepHz = RadioUtils::tuningStepToHz(m_radioState->tuningStep());
        qint64 newFreq =
            RadioUtils::snapFreqToStep(static_cast<qint64>(currentFreq), stepHz) + static_cast<qint64>(ticks) * stepHz;
        if (newFreq > 0) {
            QString cmd = QString("FA%1;").arg(static_cast<quint64>(newFreq), 11, 10, QChar('0'));
            m_connectionController->sendCAT(cmd);
            m_radioState->parseCATCommand(cmd);
        }
    } break;

    case 0: // RockerCenter — VFO B
    {
        if (m_radioState->lockB() || m_radioState->vfoB() == 0)
            break;
        quint64 currentFreq = m_radioState->vfoB();
        int stepHz = RadioUtils::tuningStepToHz(m_radioState->tuningStepB());
        qint64 newFreq =
            RadioUtils::snapFreqToStep(static_cast<qint64>(currentFreq), stepHz) + static_cast<qint64>(ticks) * stepHz;
        if (newFreq > 0) {
            QString cmd = QString("FB%1;").arg(static_cast<quint64>(newFreq), 11, 10, QChar('0'));
            m_connectionController->sendCAT(cmd);
            m_radioState->parseCATCommand(cmd);
        }
    } break;

    case 1: // RockerRight — RIT/XIT
        // K4 routes RU;/RD; based on active mode: RIT → RO (VFO A), XIT → RO$ (VFO B)
        // BSET + RIT: use RU$/RD$ to force VFO B's RIT offset
        {
            bool bSet = m_radioState->bSetEnabled();
            bool adjustB = bSet && !m_radioState->xitEnabled();
            QString cmd = (ticks > 0) ? (adjustB ? "RU$;" : "RU;") : (adjustB ? "RD$;" : "RD;");
            int count = qAbs(ticks);
            for (int i = 0; i < count; i++) {
                m_connectionController->sendCAT(cmd);
            }
        }
        break;
    }
}

void HardwareController::onKpodPollError(const QString &error) {
    // Deliberately a log line and not a notification: this arrives from a polling loop and can
    // repeat at the poll rate, so a popup per occurrence would bury the screen. The user-visible
    // signal for "the device is gone" is the deviceDisconnected notification, which fires once.
    qCWarning(lcHardware) << "KPOD/KPOD+ poll error:" << error;
}
