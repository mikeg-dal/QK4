#include "hardwarecontroller.h"
#include "audio/sidetonegenerator.h"
#include "connectioncontroller.h"
#include "hardware/halikeydevice.h"
#include "hardware/iambickeyer.h"
#include "hardware/kpoddevice.h"
#include "hardware/kpodplusdevice.h"
#include "models/radiostate.h"
#include "settings/radiosettings.h"
#include "utils/radioutils.h"
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(qk4Hardware, "qk4.hardware")

namespace {
// The K4 reports no stuck-key timeout; push this fixed default to the KPOD+.
constexpr int kKpodPlusStuckTimeoutSec = 60;
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
    // MUST be created BEFORE IambicKeyer signal connections that use it
    // =========================================================================
    m_sidetoneGenerator = new SidetoneGenerator(nullptr);
    m_sidetoneThread = new QThread(this);
    m_sidetoneThread->setObjectName("Sidetone");
    m_sidetoneGenerator->moveToThread(m_sidetoneThread);
    // WHY HighPriority: sidetone shares the real-time CW perception path with the keyer
    // thread (also HighPriority); at default priority the OS could preempt the per-element
    // synthesis + sink write under load, adding audible onset jitter.
    m_sidetoneThread->start(QThread::HighPriority);
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
    // Iambic keyer state machine (HighPriority thread for CW element timing).
    // Constructed + thread-managed here; all CW signal wiring is on CwController.
    // =========================================================================
    m_iambicKeyer = new IambicKeyer(nullptr);
    m_keyerThread = new QThread(this);
    m_keyerThread->setObjectName("Keyer");
    m_iambicKeyer->moveToThread(m_keyerThread);
    m_keyerThread->start(QThread::HighPriority);

    // All CW signal wiring — keyer init, RadioState observers, IambicKeyer →
    // CAT/sidetone, HaliKey paddle/PTT demux, keyer enable/disable on
    // connect/disconnect — lives on CwController (constructed by MainWindow
    // immediately after this controller). See cwcontroller.h.

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
    if (e.open)
        m_kpodDevice->startPolling();
    if (e.close)
        m_kpodDevice->stopPolling();
    if (e.notifyDisconnected)
        emit hardwareError(QStringLiteral("KPOD disconnected — the tuning knob and its buttons have stopped."));
}

void HardwareController::applyKpodPlus(UsbDeviceLifecycle::Event event) {
    const UsbDeviceLifecycle::Effects e = UsbDeviceLifecycle::apply(m_kpodPlusState, event);
    if (e.open) {
        m_kpodPlusDevice->startPolling();
        if (m_applyKpodPlusConfig)
            m_applyKpodPlusConfig();
    }
    if (e.close)
        m_kpodPlusDevice->stopPolling();
    if (e.notifyConnected)
        emit hardwareError(QStringLiteral("KPOD+ connected — it now owns CW keying."));
    if (e.notifyDisconnected)
        emit hardwareError(QStringLiteral("KPOD+ disconnected — CW keying has returned to QK4's keyer."));
}

QString HardwareController::halikeyName() const {
    // RadioSettings is the only place that knows which transport is configured; HalikeyDevice is
    // constructed from the same value. Named for the operator, matching the Options dropdown, so a
    // notification and the settings page cannot disagree about what the device is called.
    return RadioSettings::instance()->halikeyDeviceType() == 1 ? QStringLiteral("HaliKey MIDI")
                                                               : QStringLiteral("HaliKey V1.4");
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

    if (m_keyerThread) {
        QMetaObject::invokeMethod(m_iambicKeyer, "stop", Qt::BlockingQueuedConnection);
        m_keyerThread->quit();
        m_keyerThread->wait(2000);
    }
    delete m_iambicKeyer; // No parent, must delete manually
    m_iambicKeyer = nullptr;
    m_keyerThread = nullptr; // child of this; Qt deletes it, but never join it twice

    if (m_sidetoneThread) {
        QMetaObject::invokeMethod(m_sidetoneGenerator, "stop", Qt::BlockingQueuedConnection);
        m_sidetoneThread->quit();
        m_sidetoneThread->wait(2000);
    }
    delete m_sidetoneGenerator; // No parent, must delete manually
    m_sidetoneGenerator = nullptr;
    m_sidetoneThread = nullptr;

    // Through the policy, so the close is silent: the window is going away and there is nobody
    // left to tell that a device disconnected.
    applyKpodPlus(UsbDeviceLifecycle::Event::ShuttingDown);
    applyKpod(UsbDeviceLifecycle::Event::ShuttingDown);
}

HardwareController::~HardwareController() {
    disconnect(this);
    // Normally a no-op: MainWindow::closeEvent has already run this, which is the whole point —
    // doing it here for the first time is what CONC-001 was. Still called, because a fatal error
    // or a window that was never shown reaches the destructor without a closeEvent.
    shutdownDevices();
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
    switch (rockerPos) {
    case 2: // RockerLeft — VFO A
    {
        if (m_radioState->lockA())
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
        if (m_radioState->lockB())
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
    qCWarning(qk4Hardware) << "KPOD/KPOD+ poll error:" << error;
}
