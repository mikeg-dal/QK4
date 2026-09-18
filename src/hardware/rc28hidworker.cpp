#include "rc28hidworker.h"
#include "hidapiguard.h"
#include <QLoggingCategory>
#include <QString>
#include <QThread>
#include <QTimer>
#include <cstring>
#include <hidapi/hidapi.h>

#ifdef Q_OS_LINUX
#include "kpodudevworker.h"
#endif

Q_LOGGING_CATEGORY(hwRc28, "hw.rc28")

static const int RC28_REPORT_LEN = 64;

// =============================================================================
// Decoder (pure logic, no hidapi dependency)
// =============================================================================

Rc28HidWorker::ReportEvents Rc28HidWorker::decodeReport(const unsigned char *buffer, int len) {
    ReportEvents ev;
    if (len <= 0 || buffer == nullptr)
        return ev;

    // Firmware-version response (host asked with a 0x02 report).
    if (buffer[0] == 0x02) {
        ev.isVersion = true;
        // Bytes 1.. are ASCII "102 321..." → version 1.02 in the emulator. We
        // surface the first token as-is; exact real-hardware formatting is not
        // guaranteed, so keep it defensive.
        QString v;
        for (int i = 1; i < len && buffer[i] != 0 && buffer[i] != ' '; ++i) {
            if (buffer[i] >= 0x20 && buffer[i] < 0x7F)
                v += QChar(buffer[i]);
        }
        // "102" → "1.02"
        if (v.size() == 3)
            ev.firmwareVersion = QString("%1.%2").arg(v.left(1)).arg(v.mid(1));
        else
            ev.firmwareVersion = v;
        return ev;
    }

    // Encoder / button frame. byte5 discriminates.
    const int controls = (len > 5) ? buffer[5] : BTN_NONE;

    if (controls == BTN_NONE) {
        // Encoder frame (or idle). byte3 = direction, byte1 = accel/speed.
        const int dir = (len > 3) ? buffer[3] : 0;
        int speed = (len > 1) ? buffer[1] : 1;
        if (speed < 1)
            speed = 1;
        if (speed > 4)
            speed = 4;
        if (dir == 0x01) {
            ev.emitEncoder = true;
            ev.encoderTicks = speed; // Physical RC-28 direction is opposite K-POD
        } else if (dir == 0x02) {
            ev.emitEncoder = true;
            ev.encoderTicks = -speed;
        }
        // dir == 0x00 → no movement
        return ev;
    }

    switch (controls) {
    case BTN_F1:
        ev.buttonDown = 1;
        break;
    case BTN_F2:
        ev.buttonDown = 2;
        break;
    case BTN_TX:
        ev.buttonDown = 3;
        break;
    default:
        ev.buttonDown = 0;
        break;
    }
    return ev;
}

// =============================================================================
// Lifecycle
// =============================================================================

Rc28HidWorker::Rc28HidWorker(QObject *parent) : QObject(parent) {}

Rc28HidWorker::~Rc28HidWorker() {
    HidApiGuard::Lock lock;
    if (m_hidDevice || m_hidInitialized) {
        releaseHandle();
        if (m_hidInitialized) {
            HidApiGuard::release();
            m_hidInitialized = false;
        }
    }
}

void Rc28HidWorker::start() {
    if (!HidApiGuard::acquire()) {
        qCWarning(hwRc28) << "hid_init failed";
        return;
    }
    m_hidInitialized = true;

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(POLL_INTERVAL_MS);
    m_pollTimer->setTimerType(Qt::PreciseTimer);
    connect(m_pollTimer, &QTimer::timeout, this, &Rc28HidWorker::onPollTimer);

#ifdef Q_OS_LINUX
    m_udevWorker = new KpodUdevWorker(VENDOR_ID, PRODUCT_ID);
    m_udevThread = new QThread(this);
    m_udevThread->setObjectName("Rc28Udev");
    m_udevWorker->moveToThread(m_udevThread);
    connect(m_udevThread, &QThread::started, m_udevWorker, &KpodUdevWorker::start);
    connect(m_udevWorker, &KpodUdevWorker::deviceArrived, this, &Rc28HidWorker::onDeviceArrivedFromHotplug,
            Qt::QueuedConnection);
    connect(m_udevWorker, &KpodUdevWorker::deviceRemoved, this, &Rc28HidWorker::onDeviceRemovedFromHotplug,
            Qt::QueuedConnection);
    connect(m_udevThread, &QThread::finished, m_udevWorker, &QObject::deleteLater);
    m_udevThread->start();
#else
    m_presenceTimer = new QTimer(this);
    m_presenceTimer->setInterval(PRESENCE_CHECK_INTERVAL_MS);
    connect(m_presenceTimer, &QTimer::timeout, this, &Rc28HidWorker::onPresenceTimer);
    m_presenceTimer->start();
#endif

    Rc28DeviceInfo info = detectDeviceInfo();
    if (info.detected)
        m_devicePresent = true;
    m_info = info;
    emit deviceInfoReady(m_info);
}

void Rc28HidWorker::shutdown() {
    HidApiGuard::Lock lock;
    if (m_pollTimer)
        m_pollTimer->stop();
#ifndef Q_OS_LINUX
    if (m_presenceTimer)
        m_presenceTimer->stop();
#else
    if (m_udevWorker)
        m_udevWorker->stop();
    if (m_udevThread) {
        m_udevThread->quit();
        m_udevThread->wait(2000);
    }
#endif
    releaseHandle();
    if (m_hidInitialized) {
        HidApiGuard::release();
        m_hidInitialized = false;
    }
}

// =============================================================================
// Open / close
// =============================================================================

bool Rc28HidWorker::openHandle() {
    HidApiGuard::Lock lock;
    if (m_hidDevice)
        return true;
    if (m_info.devicePath.isEmpty()) {
        qCWarning(hwRc28) << "openHandle: no device path";
        return false;
    }
    m_hidDevice = hid_open_path(m_info.devicePath.toUtf8().constData());
    if (!m_hidDevice) {
        qCWarning(hwRc28) << "openHandle: hid_open_path failed";
        return false;
    }
    hid_set_nonblocking(m_hidDevice, 1);

    // Match the RS-BA1 handshake: ask for the firmware version. This is what
    // switches the real RC-28 into its 0x01 report mode. Response (if any) is
    // read during normal polling and ignored.
    unsigned char req[RC28_REPORT_LEN];
    memset(req, 0, sizeof(req));
    req[0] = 0x02;
    hid_write(m_hidDevice, req, sizeof(req));
    return true;
}

void Rc28HidWorker::releaseHandle() {
    HidApiGuard::Lock lock;
    if (m_hidDevice) {
        hid_close(m_hidDevice);
        m_hidDevice = nullptr;
    }
}

void Rc28HidWorker::openDevice() {
    if (m_hidDevice)
        return;
    if (!openHandle()) {
        emit pollError(QStringLiteral("Failed to open RC-28 device"));
        return;
    }
    m_pressedButton = 0;
    m_holdEmitted = false;
    if (m_pollTimer && !m_pollTimer->isActive())
        m_pollTimer->start();
    emit deviceArrived();
}

void Rc28HidWorker::closeDevice() {
    const bool wasPolling = (m_pollTimer && m_pollTimer->isActive());
    if (m_pollTimer)
        m_pollTimer->stop();
    releaseHandle();
    if (wasPolling)
        emit deviceRemoved();
}

void Rc28HidWorker::setLeds(bool f1, bool f2, bool tx) {
    HidApiGuard::Lock lock;
    if (!m_hidDevice)
        return;
    // Host LED command: report type 0x01, byte1 = active-low bitfield.
    // bit0=TX, bit1=F1, bit2=F2, bit3=LINK. Keep LINK lit while connected.
    unsigned char cmd[RC28_REPORT_LEN];
    memset(cmd, 0, sizeof(cmd));
    cmd[0] = 0x01;
    unsigned char bits = 0x07; // function LEDs off, LINK on
    if (tx)
        bits &= ~0x01;
    if (f1)
        bits &= ~0x02;
    if (f2)
        bits &= ~0x04;
    cmd[1] = bits;
    hid_write(m_hidDevice, cmd, sizeof(cmd));
}

// =============================================================================
// Polling
// =============================================================================

void Rc28HidWorker::onPollTimer() {
    HidApiGuard::Lock lock;
    if (!m_hidDevice)
        return;

    // The RC-28 pushes interrupt-IN reports; drain everything queued this cycle.
    unsigned char buffer[RC28_REPORT_LEN];
    int readResult = 0;
    int iterations = 0;
    do {
        readResult = hid_read_timeout(m_hidDevice, buffer, sizeof(buffer), 0);
        if (readResult < 0) {
            qCWarning(hwRc28) << "hid_read failed; assuming device gone";
            if (m_pollTimer)
                m_pollTimer->stop();
            releaseHandle();
            emit deviceRemoved();
            emit pollError(QStringLiteral("Failed to read from RC-28"));
            return;
        }
        if (readResult == 0)
            break; // no more data this cycle

        const ReportEvents ev = decodeReport(buffer, readResult);
        if (ev.isVersion) {
            if (!ev.firmwareVersion.isEmpty() && m_info.firmwareVersion != ev.firmwareVersion) {
                m_info.firmwareVersion = ev.firmwareVersion;
                emit deviceInfoReady(m_info);
            }
            continue;
        }
        if (ev.emitEncoder)
            emit encoderRotated(ev.encoderTicks);
        updateButtonState(ev.buttonDown);
    } while (++iterations < 32);

    // Hold detection while a button is held but no fresh report arrived.
    if (m_pressedButton != 0 && !m_holdEmitted && m_pressTimer.isValid() &&
        m_pressTimer.elapsed() >= HOLD_THRESHOLD_MS) {
        emit buttonHeld(m_pressedButton);
        m_holdEmitted = true;
    }
}

void Rc28HidWorker::updateButtonState(int buttonDown) {
    if (buttonDown != m_pressedButton) {
        // Release the previous control first. Real hardware can transition
        // directly between button masks without an intervening idle report.
        if (m_pressedButton != 0) {
            const int released = m_pressedButton;
            emit buttonReleased(released);
            if (!m_holdEmitted)
                emit buttonTapped(released);
        }

        m_pressedButton = buttonDown;
        m_holdEmitted = false;
        if (buttonDown != 0) {
            m_pressTimer.restart();
            emit buttonPressed(buttonDown);
        } else {
            m_pressTimer.invalidate();
        }
    } else if (buttonDown != 0) {
        // Still held — hold emission handled by the timer check in onPollTimer.
        if (!m_holdEmitted && m_pressTimer.isValid() && m_pressTimer.elapsed() >= HOLD_THRESHOLD_MS) {
            emit buttonHeld(m_pressedButton);
            m_holdEmitted = true;
        }
    }
}

// =============================================================================
// Presence / hotplug
// =============================================================================

void Rc28HidWorker::onPresenceTimer() {
    HidApiGuard::Lock lock;
    hid_device_info *devs = hid_enumerate(VENDOR_ID, PRODUCT_ID);
    const bool now = (devs != nullptr);
    hid_free_enumeration(devs);

    if (!m_devicePresent && now) {
        onDeviceArrivedFromHotplug();
    } else if (m_devicePresent && !now) {
        onDeviceRemovedFromHotplug();
    }
}

void Rc28HidWorker::onDeviceArrivedFromHotplug() {
    m_info = detectDeviceInfo();
    m_devicePresent = m_info.detected;
    emit deviceInfoReady(m_info);
}

void Rc28HidWorker::onDeviceRemovedFromHotplug() {
    m_devicePresent = false;
    if (m_pollTimer && m_pollTimer->isActive())
        m_pollTimer->stop();
    releaseHandle();
    m_info = Rc28DeviceInfo{};
    emit deviceInfoReady(m_info);
    emit deviceRemoved();
}

// =============================================================================
// Detection
// =============================================================================

Rc28DeviceInfo Rc28HidWorker::detectDeviceInfo() {
    HidApiGuard::Lock lock;
    Rc28DeviceInfo info;

    hid_device_info *devs = hid_enumerate(VENDOR_ID, PRODUCT_ID);
    hid_device_info *selected = nullptr;
    for (hid_device_info *d = devs; d != nullptr; d = d->next) {
        if (!selected) {
            selected = d;
        } else if (d->usage_page >= 0xFF00 && selected->usage_page < 0xFF00) {
            selected = d; // prefer the vendor-defined interface
        }
    }

    if (!selected) {
        hid_free_enumeration(devs);
        return info;
    }

    info.detected = true;
    info.vendorId = selected->vendor_id;
    info.productId = selected->product_id;
    if (selected->product_string)
        info.productName = QString::fromWCharArray(selected->product_string);
    if (selected->manufacturer_string)
        info.manufacturer = QString::fromWCharArray(selected->manufacturer_string);
    if (selected->path)
        info.devicePath = QString::fromUtf8(selected->path);

    hid_free_enumeration(devs);
    return info;
}
