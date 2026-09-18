#include "hardwarecontroller.h"
#include "audio/sidetonegenerator.h"
#include "connectioncontroller.h"
#include "hardware/halikeydevice.h"
#include "hardware/iambickeyer.h"
#include "hardware/kpoddevice.h"
#include "hardware/kpodplusdevice.h"
#include "hardware/ctr2mididevice.h"
#include "hardware/midiinputrouter.h"
#include "hardware/midimapping.h"
#include "models/radiostate.h"
#include "settings/radiosettings.h"
#include "utils/radioutils.h"
#include <QLoggingCategory>
#include <QJsonDocument>
#include <QSettings>

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

    // Auto-start polling when device arrives
    connect(m_kpodDevice, &KpodDevice::deviceConnected, this, [this]() {
        if (RadioSettings::instance()->kpodEnabled() && !m_kpodDevice->isPolling()) {
            m_kpodDevice->startPolling();
        }
    });

    // Settings: KPOD enable/disable
    connect(RadioSettings::instance(), &RadioSettings::kpodEnabledChanged, this,
            &HardwareController::onKpodEnabledChanged);

    // WHY: KpodDevice::detectDevice() is deferred to the first event-loop tick (see
    // kpoddevice.cpp constructor note) to keep the 400ms hid_open_path retry off the
    // main thread at startup. isDetected() is false at this point; the deviceInfoReady
    // signal fires after detection completes and is the correct point to auto-start
    // polling if the user had KPOD enabled.
    connect(m_kpodDevice, &KpodDevice::deviceInfoReady, this, [this]() {
        if (RadioSettings::instance()->kpodEnabled() && m_kpodDevice->isDetected() && !m_kpodDevice->isPolling()) {
            m_kpodDevice->startPolling();
        }
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
    connect(m_kpodPlusDevice, &KpodPlusDevice::deviceConnected, this, [this, applyKpodPlusConfig]() {
        if (RadioSettings::instance()->kpodEnabled() && !m_kpodPlusDevice->isPolling()) {
            m_kpodPlusDevice->startPolling();
            applyKpodPlusConfig();
        }
    });

    // Auto-start on detection at startup
    connect(m_kpodPlusDevice, &KpodPlusDevice::deviceInfoReady, this, [this, applyKpodPlusConfig]() {
        if (RadioSettings::instance()->kpodEnabled() && m_kpodPlusDevice->isDetected() &&
            !m_kpodPlusDevice->isPolling()) {
            m_kpodPlusDevice->startPolling();
            applyKpodPlusConfig();
        }
    });

    // KPOD enable/disable also controls KPOD+
    connect(RadioSettings::instance(), &RadioSettings::kpodEnabledChanged, this, [this](bool enabled) {
        if (enabled) {
            if (m_kpodPlusDevice->isDetected() && !m_kpodPlusDevice->isPolling()) {
                m_kpodPlusDevice->startPolling();
            }
        } else {
            m_kpodPlusDevice->stopPolling();
        }
    });

    // =========================================================================
    // HaliKey CW paddle device — device type injected here so HalikeyDevice
    // itself doesn't reach into RadioSettings (Phase 3 layering cleanup).
    // =========================================================================
    m_halikeyDevice = new HalikeyDevice(RadioSettings::instance()->halikeyDeviceType(), this);
    connect(RadioSettings::instance(), &RadioSettings::halikeyDeviceTypeChanged, m_halikeyDevice,
            &HalikeyDevice::setDeviceType);

    // Independent CTR2-MIDI input. It does not borrow the HaliKey MIDI port,
    // so both controllers can remain attached at the same time.
    m_ctr2MidiDevice = new Ctr2MidiDevice(this);
    m_midiInputRouter = new MidiInputRouter(this);
    QSettings ctr2Settings;
    MidiMapping::DeviceMapping ctr2Map = MidiMapping::ctr2Default();
    const auto document = QJsonDocument::fromJson(ctr2Settings.value(QStringLiteral("ctr2/mapping")).toByteArray());
    QString mappingError;
    if (!document.isObject() || !MidiMapping::fromJson(document.object(), &ctr2Map, &mappingError))
        ctr2Map = MidiMapping::ctr2Default();
    m_midiInputRouter->setMapping(QStringLiteral("ctr2-midi"), ctr2Map);
    connect(m_ctr2MidiDevice, &Ctr2MidiDevice::rawMidiEvent, this, [this](int status, int data1, int data2) {
        m_midiInputRouter->processEvent(QStringLiteral("ctr2-midi"), status, data1, data2);
    });
    connect(m_ctr2MidiDevice, &Ctr2MidiDevice::disconnected, this,
            [this] { m_midiInputRouter->clearSourceState(QStringLiteral("ctr2-midi")); });
    connect(m_ctr2MidiDevice, &Ctr2MidiDevice::connectionError, this,
            [this](const QString &error) { emit hardwareError(QStringLiteral("CTR2: %1").arg(error)); });
    connect(m_midiInputRouter, &MidiInputRouter::knobActionRequested, this,
            &HardwareController::ctr2KnobActionRequested);
    connect(m_midiInputRouter, &MidiInputRouter::buttonActionRequested, this,
            &HardwareController::ctr2ButtonActionRequested);
    connect(m_midiInputRouter, &MidiInputRouter::macroRequested, this, [this](const QString &, const QString &command) {
        if (m_connectionController->isConnected())
            m_connectionController->sendCAT(command);
    });
    const QString savedCtr2 = ctr2Settings.value(QStringLiteral("ctr2/port")).toString();
    if (!savedCtr2.isEmpty())
        QTimer::singleShot(0, this, [this, savedCtr2] { connectCtr2(savedCtr2); });

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

    // Surface HaliKey port-open failures to the user via NotificationWidget. Without
    // this connect, openPort() failures (nonexistent serial port, busy MIDI device,
    // permission denied) were silently swallowed.
    connect(m_halikeyDevice, &HalikeyDevice::connectionError, this,
            [this](const QString &error) { emit hardwareError(QStringLiteral("HaliKey: %1").arg(error)); });
}

void HardwareController::shutdownSidetone() {
    // Synchronous sidetone sink teardown — required from MainWindow::closeEvent
    // so QAudioSink is destroyed while the event loop is still alive, not
    // during libc atexit (which races PipeWire's RT worker on Linux).
    if (m_sidetoneGenerator && m_sidetoneThread && m_sidetoneThread->isRunning()) {
        QMetaObject::invokeMethod(m_sidetoneGenerator, "stop", Qt::BlockingQueuedConnection);
    }
}

HardwareController::~HardwareController() {
    disconnect(this);
    // Shutdown order: HaliKey → Keyer → Sidetone → KPOD/KPOD+
    // HaliKey stops paddle events first, then keyer (producer of KZ commands) stops
    // before sidetone (the audio consumer) is torn down.

    if (m_halikeyDevice) {
        m_halikeyDevice->closePort();
    }
    if (m_ctr2MidiDevice)
        m_ctr2MidiDevice->closePort();

    if (m_keyerThread) {
        QMetaObject::invokeMethod(m_iambicKeyer, "stop", Qt::BlockingQueuedConnection);
        m_keyerThread->quit();
        m_keyerThread->wait(2000);
    }
    delete m_iambicKeyer; // No parent, must delete manually

    if (m_sidetoneThread) {
        QMetaObject::invokeMethod(m_sidetoneGenerator, "stop", Qt::BlockingQueuedConnection);
        m_sidetoneThread->quit();
        m_sidetoneThread->wait(2000);
    }
    delete m_sidetoneGenerator; // No parent, must delete manually

    if (m_kpodPlusDevice) {
        m_kpodPlusDevice->stopPolling();
    }

    if (m_kpodDevice) {
        m_kpodDevice->stopPolling();
    }
}

MidiMapping::DeviceMapping HardwareController::ctr2Mapping() const {
    return m_midiInputRouter ? m_midiInputRouter->mapping(QStringLiteral("ctr2-midi")) : MidiMapping::ctr2Default();
}

void HardwareController::setCtr2Mapping(const MidiMapping::DeviceMapping &mapping) {
    if (!m_midiInputRouter)
        return;
    m_midiInputRouter->setMapping(QStringLiteral("ctr2-midi"), mapping);
    QSettings().setValue(QStringLiteral("ctr2/mapping"),
                         QJsonDocument(MidiMapping::toJson(mapping)).toJson(QJsonDocument::Compact));
}

bool HardwareController::connectCtr2(const QString &portName) {
    if (!m_ctr2MidiDevice)
        return false;
    if (portName.isEmpty()) {
        m_ctr2MidiDevice->closePort();
        QSettings().remove(QStringLiteral("ctr2/port"));
        return true;
    }
    if (!m_ctr2MidiDevice->openPort(portName))
        return false;
    QSettings().setValue(QStringLiteral("ctr2/port"), portName);
    return true;
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
    qCWarning(qk4Hardware) << "KPOD error:" << error;
}

void HardwareController::onKpodEnabledChanged(bool enabled) {
    if (enabled) {
        if (m_kpodDevice->isDetected()) {
            m_kpodDevice->startPolling();
        }
    } else {
        m_kpodDevice->stopPolling();
    }
}
