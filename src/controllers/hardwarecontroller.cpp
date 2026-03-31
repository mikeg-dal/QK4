#include "hardwarecontroller.h"
#include "connectioncontroller.h"
#include "hardware/halikeydevice.h"
#include "hardware/iambickeyer.h"
#include "hardware/kpoddevice.h"
#include "audio/sidetonegenerator.h"
#include "models/radiostate.h"
#include "settings/radiosettings.h"
#include "utils/radioutils.h"
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(qk4Hardware, "qk4.hardware")

HardwareController::HardwareController(RadioState *radioState, ConnectionController *connController, QObject *parent)
    : QObject(parent), m_radioState(radioState), m_connectionController(connController) {
    // =========================================================================
    // KPOD USB tuning knob
    // =========================================================================
    m_kpodDevice = new KpodDevice(this);

    connect(m_kpodDevice, &KpodDevice::encoderRotated, this, &HardwareController::onKpodEncoderRotated);
    connect(m_kpodDevice, &KpodDevice::rockerPositionChanged, this,
            [this](KpodDevice::RockerPosition pos) { onKpodRockerChanged(static_cast<int>(pos)); });
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

    // Start polling if enabled and detected
    if (RadioSettings::instance()->kpodEnabled() && m_kpodDevice->isDetected()) {
        m_kpodDevice->startPolling();
    }

    // =========================================================================
    // HaliKey CW paddle device
    // =========================================================================
    m_halikeyDevice = new HalikeyDevice(this);

    // =========================================================================
    // Sidetone generator (dedicated thread for low-latency audio feedback)
    // MUST be created BEFORE IambicKeyer signal connections that use it
    // =========================================================================
    m_sidetoneGenerator = new SidetoneGenerator(nullptr);
    m_sidetoneThread = new QThread(this);
    m_sidetoneThread->setObjectName("Sidetone");
    m_sidetoneGenerator->moveToThread(m_sidetoneThread);
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

    // Set initial sidetone frequency from radio state if available
    if (m_radioState->cwPitch() > 0) {
        m_sidetoneGenerator->setFrequency(m_radioState->cwPitch());
    }

    // Update sidetone frequency when CW pitch changes
    connect(m_radioState, &RadioState::cwPitchChanged, this,
            [this](int pitchHz) { m_sidetoneGenerator->setFrequency(pitchHz); });

    // Set initial sidetone volume from RadioSettings (independent of K4's MON level)
    m_sidetoneGenerator->setVolume(RadioSettings::instance()->sidetoneVolume() / 100.0f);

    // Update sidetone volume when changed in Options
    connect(RadioSettings::instance(), &RadioSettings::sidetoneVolumeChanged, this,
            [this](int value) { m_sidetoneGenerator->setVolume(value / 100.0f); });

    // Set initial keyer speed from radio state if available
    if (m_radioState->keyerSpeed() > 0) {
        m_sidetoneGenerator->setKeyerSpeed(m_radioState->keyerSpeed());
    }

    // =========================================================================
    // Iambic keyer state machine (HighPriority thread for CW element timing)
    // =========================================================================
    m_iambicKeyer = new IambicKeyer(nullptr);
    m_keyerThread = new QThread(this);
    m_keyerThread->setObjectName("Keyer");
    m_iambicKeyer->moveToThread(m_keyerThread);
    m_keyerThread->start(QThread::HighPriority);

    // Initialize keyer from RadioState KP settings
    int initWpm = m_radioState->keyerSpeed();
    if (initWpm <= 0)
        initWpm = 20;
    QMetaObject::invokeMethod(m_iambicKeyer, "setSpeed", Qt::QueuedConnection, Q_ARG(int, initWpm));
    QMetaObject::invokeMethod(
        m_iambicKeyer, "setMode", Qt::QueuedConnection,
        Q_ARG(IambicKeyer::Mode, m_radioState->iambicMode() == 'B' ? IambicKeyer::IambicB : IambicKeyer::IambicA));
    QMetaObject::invokeMethod(m_iambicKeyer, "setReversed", Qt::QueuedConnection,
                              Q_ARG(bool, m_radioState->paddleOrientation() == 'R'));

    // Update sidetone and keyer speed when WPM changes
    connect(m_radioState, &RadioState::keyerSpeedChanged, this, [this](int wpm) {
        m_sidetoneGenerator->setKeyerSpeed(wpm);
        QMetaObject::invokeMethod(m_iambicKeyer, "setSpeed", Qt::QueuedConnection, Q_ARG(int, wpm));
        // Sync element length with K4 server
        int ditMs = 1200 / wpm;
        m_connectionController->sendCAT(QString("KZL%1;").arg(ditMs, 2, 10, QChar('0')));
    });

    // Update keyer mode/reversal when KP settings change
    connect(m_radioState, &RadioState::keyerPaddleChanged, this, [this](QChar iambic, QChar paddle, int /*weight*/) {
        QMetaObject::invokeMethod(
            m_iambicKeyer, "setMode", Qt::QueuedConnection,
            Q_ARG(IambicKeyer::Mode, iambic == 'B' ? IambicKeyer::IambicB : IambicKeyer::IambicA));
        QMetaObject::invokeMethod(m_iambicKeyer, "setReversed", Qt::QueuedConnection, Q_ARG(bool, paddle == 'R'));
    });

    // =========================================================================
    // Keyer → CAT commands + sidetone audio
    // =========================================================================

    // Keyer element started — send KZ command to I/O thread + sidetone to sidetone thread
    connect(m_iambicKeyer, &IambicKeyer::elementStarted, this,
            [this](bool isDit) { m_connectionController->sendCAT(isDit ? "KZ.;" : "KZ-;"); });
    connect(m_iambicKeyer, &IambicKeyer::elementStarted, m_sidetoneGenerator,
            [sg = m_sidetoneGenerator](bool isDit) { isDit ? sg->playSingleDit() : sg->playSingleDah(); });

    // Keyer finished — stop local sidetone (K4 unkeys itself after each KZ element)
    connect(m_iambicKeyer, &IambicKeyer::keyingFinished, m_sidetoneGenerator,
            [sg = m_sidetoneGenerator]() { sg->stopElement(); });

    // Character boundary — keyer went idle between elements
    connect(m_iambicKeyer, &IambicKeyer::characterSpace, this, [this]() { m_connectionController->sendCAT("KZ ;"); });

    // Restart after pause — send KZP with elapsed ms before next element
    connect(m_iambicKeyer, &IambicKeyer::restartAfterPause, this,
            [this](int ms) { m_connectionController->sendCAT(QString("KZP%1;").arg(ms, 4, 10, QChar('0'))); });

    // =========================================================================
    // HaliKey paddle → keyer (ZERO-LATENCY DirectConnection)
    // =========================================================================
    // setDitPaddle/setDahPaddle write atomic bools immediately on the calling thread,
    // so onTimerFired() always sees real-time paddle state with zero queue delay.
    connect(m_halikeyDevice, &HalikeyDevice::ditStateChanged, m_iambicKeyer, &IambicKeyer::setDitPaddle,
            Qt::DirectConnection);
    connect(m_halikeyDevice, &HalikeyDevice::dahStateChanged, m_iambicKeyer, &IambicKeyer::setDahPaddle,
            Qt::DirectConnection);

    // Enable keyer when radio connects, disable on disconnect
    connect(m_connectionController, &ConnectionController::radioReady, this, [this]() {
        QMetaObject::invokeMethod(m_iambicKeyer, "setEnabled", Qt::QueuedConnection, Q_ARG(bool, true));
    });
    connect(m_connectionController, &ConnectionController::connectionLost, this, [this]() {
        QMetaObject::invokeMethod(m_iambicKeyer, "setEnabled", Qt::QueuedConnection, Q_ARG(bool, false));
    });

    // Stop keyer when HaliKey disconnects (prevents runaway keying
    // if paddle was held when disconnected — Note Off never arrives)
    connect(m_halikeyDevice, &HalikeyDevice::disconnected, this, [this]() {
        QMetaObject::invokeMethod(m_sidetoneGenerator, "stopElement", Qt::QueuedConnection);
        QMetaObject::invokeMethod(m_iambicKeyer, "stop", Qt::QueuedConnection);
    });
}

HardwareController::~HardwareController() {
    disconnect(this);
    // Shutdown order: HaliKey → Keyer → Sidetone → KPOD
    // HaliKey stops paddle events first, then keyer (producer of KZ commands) stops
    // before sidetone (the audio consumer) is torn down.

    if (m_halikeyDevice) {
        m_halikeyDevice->closePort();
    }

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

    if (m_kpodDevice) {
        m_kpodDevice->stopPolling();
    }
}

// =============================================================================
// KPOD Event Handlers
// =============================================================================

void HardwareController::onKpodEncoderRotated(int ticks) {
    if (!m_connectionController->isConnected()) {
        return;
    }

    // Action depends on rocker position
    switch (m_kpodDevice->rockerPosition()) {
    case KpodDevice::RockerLeft: // VFO A
    {
        quint64 currentFreq = m_radioState->vfoA();
        int stepHz = RadioUtils::tuningStepToHz(m_radioState->tuningStep());
        qint64 newFreq = static_cast<qint64>(currentFreq) + static_cast<qint64>(ticks) * stepHz;
        if (newFreq > 0) {
            QString cmd = QString("FA%1;").arg(static_cast<quint64>(newFreq));
            m_connectionController->sendCAT(cmd);
            m_radioState->parseCATCommand(cmd);
        }
    } break;

    case KpodDevice::RockerCenter: // VFO B
    {
        quint64 currentFreq = m_radioState->vfoB();
        int stepHz = RadioUtils::tuningStepToHz(m_radioState->tuningStepB());
        qint64 newFreq = static_cast<qint64>(currentFreq) + static_cast<qint64>(ticks) * stepHz;
        if (newFreq > 0) {
            QString cmd = QString("FB%1;").arg(static_cast<quint64>(newFreq));
            m_connectionController->sendCAT(cmd);
            m_radioState->parseCATCommand(cmd);
        }
    } break;

    case KpodDevice::RockerRight: // RIT/XIT
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

void HardwareController::onKpodRockerChanged(int position) {
    QString posName;
    switch (static_cast<KpodDevice::RockerPosition>(position)) {
    case KpodDevice::RockerLeft:
        posName = "VFO A";
        break;
    case KpodDevice::RockerCenter:
        posName = "VFO B";
        break;
    case KpodDevice::RockerRight:
        posName = "XIT/RIT";
        break;
    default:
        posName = "Unknown";
        break;
    }
    Q_UNUSED(posName)
}

void HardwareController::onKpodPollError(const QString &error) {
    qWarning() << "KPOD error:" << error;
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
