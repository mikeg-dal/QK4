#include "kpodhidworker.h"
#include "hidapiguard.h"
#include <QLoggingCategory>
#include <QString>
#include <QThread>
#include <QTimer>
#include <hidapi/hidapi.h>

#ifdef Q_OS_LINUX
#include "kpodudevworker.h"
#endif

Q_LOGGING_CATEGORY(hwKpod, "hw.kpod")

static void kpodLog(const QString &msg) {
    qCDebug(hwKpod) << "KPOD:" << msg;
}

// =============================================================================
// Decoder (pure logic, no hidapi dependency at call sites)
// =============================================================================

void KpodHidWorker::resetDecoderState() {
    m_lastRockerPosition = 0;
    m_lastButtonState = 0;
    m_holdEmitted = false;
}

KpodHidWorker::ResponseEvents KpodHidWorker::decodeResponse(const unsigned char buffer[8]) {
    ResponseEvents ev;
    const unsigned char cmd = buffer[0];

    // cmd = 'u' (0x75) means new event; cmd = 0 means no event. An implicit
    // button release is signalled by cmd == 0 while a button was previously
    // pressed.
    if (cmd != 'u') {
        if (m_lastButtonState != 0) {
            if (!m_holdEmitted) {
                ev.emitButtonTap = true;
                ev.buttonNum = m_lastButtonState;
            }
            m_lastButtonState = 0;
            m_holdEmitted = false;
        }
        return ev;
    }

    const qint16 ticks = static_cast<qint16>(buffer[1] | (buffer[2] << 8));
    const quint8 controls = buffer[3];
    const quint8 buttonNum = controls & 0x0F;
    const bool isHold = (controls >> 4) & 0x01;
    const int rocker = (controls >> 5) & 0x03;

    if (ticks != 0) {
        ev.emitEncoder = true;
        ev.encoderTicks = ticks;
    }

    if (buttonNum != 0 && m_lastButtonState == 0) {
        m_lastButtonState = buttonNum;
        m_holdEmitted = false;
        if (isHold) {
            ev.emitButtonHold = true;
            ev.buttonNum = buttonNum;
            m_holdEmitted = true;
        }
    } else if (buttonNum != 0 && m_lastButtonState != 0) {
        if (isHold && !m_holdEmitted) {
            ev.emitButtonHold = true;
            ev.buttonNum = m_lastButtonState;
            m_holdEmitted = true;
        }
    } else if (buttonNum == 0 && m_lastButtonState != 0) {
        if (!m_holdEmitted) {
            ev.emitButtonTap = true;
            ev.buttonNum = m_lastButtonState;
        }
        m_lastButtonState = 0;
        m_holdEmitted = false;
    }

    if (rocker != m_lastRockerPosition && rocker != 3) {
        m_lastRockerPosition = rocker;
        ev.emitRocker = true;
        ev.rockerPosition = rocker;
    }

    return ev;
}

// =============================================================================
// Lifecycle
// =============================================================================

KpodHidWorker::KpodHidWorker(QObject *parent) : QObject(parent) {}

KpodHidWorker::~KpodHidWorker() {
    HidApiGuard::Lock lock;
    if (m_hidDevice || m_hidInitialized) {
        releaseHandle();
        if (m_hidInitialized) {
            HidApiGuard::release();
            m_hidInitialized = false;
        }
    }
}

void KpodHidWorker::start() {
    if (!HidApiGuard::acquire()) {
        qCWarning(hwKpod) << "hid_init failed";
        return;
    }
    m_hidInitialized = true;

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(POLL_INTERVAL_MS);
    m_pollTimer->setTimerType(Qt::PreciseTimer);
    connect(m_pollTimer, &QTimer::timeout, this, &KpodHidWorker::onPollTimer);

#ifdef Q_OS_LINUX
    // udev-based hotplug — kernel-driven netlink monitor on its own thread. Mirrors the
    // existing pattern in KpodPlusUsbWorker.
    m_udevWorker = new KpodUdevWorker(VENDOR_ID, PRODUCT_ID);
    m_udevThread = new QThread(this);
    m_udevThread->setObjectName("KpodUdev");
    m_udevWorker->moveToThread(m_udevThread);
    connect(m_udevThread, &QThread::started, m_udevWorker, &KpodUdevWorker::start);
    connect(m_udevWorker, &KpodUdevWorker::deviceArrived, this, &KpodHidWorker::onDeviceArrivedFromHotplug,
            Qt::QueuedConnection);
    connect(m_udevWorker, &KpodUdevWorker::deviceRemoved, this, &KpodHidWorker::onDeviceRemovedFromHotplug,
            Qt::QueuedConnection);
    connect(m_udevThread, &QThread::finished, m_udevWorker, &QObject::deleteLater);
    m_udevThread->start();
#else
    // macOS / Windows: cheap hid_enumerate-based presence polling.
    m_presenceTimer = new QTimer(this);
    m_presenceTimer->setInterval(PRESENCE_CHECK_INTERVAL_MS);
    connect(m_presenceTimer, &QTimer::timeout, this, &KpodHidWorker::onPresenceTimer);
    m_presenceTimer->start();
#endif

    // Initial detection on the worker thread. The deferred-singleShot main-thread variant
    // from the old KpodDevice constructor used to add a ~400 ms freeze at app launch;
    // running here moves that off main entirely.
    KpodDeviceInfo info = detectDeviceInfo();
    if (info.detected) {
        m_devicePresent = true;
    }
    m_info = info;
    emit deviceInfoReady(m_info);
}

void KpodHidWorker::shutdown() {
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

bool KpodHidWorker::openHandle() {
    HidApiGuard::Lock lock;
    if (m_hidDevice)
        return true;
    if (m_info.devicePath.isEmpty()) {
        qCWarning(hwKpod) << "openHandle: no device path";
        return false;
    }
    m_hidDevice = hid_open_path(m_info.devicePath.toUtf8().constData());
    if (!m_hidDevice) {
        qCWarning(hwKpod) << "openHandle: hid_open_path failed";
        return false;
    }
    hid_set_nonblocking(m_hidDevice, 1);
    return true;
}

void KpodHidWorker::releaseHandle() {
    HidApiGuard::Lock lock;
    if (m_hidDevice) {
        hid_close(m_hidDevice);
        m_hidDevice = nullptr;
    }
}

void KpodHidWorker::openDevice() {
    if (m_hidDevice)
        return;
    if (!openHandle()) {
        emit pollError(QStringLiteral("Failed to open KPOD device"));
        return;
    }
    resetDecoderState();
    if (m_pollTimer && !m_pollTimer->isActive())
        m_pollTimer->start();
    emit deviceArrived();
}

void KpodHidWorker::closeDevice() {
    const bool wasPolling = (m_pollTimer && m_pollTimer->isActive());
    if (m_pollTimer)
        m_pollTimer->stop();
    releaseHandle();
    if (wasPolling)
        emit deviceRemoved();
}

// =============================================================================
// Polling
// =============================================================================

void KpodHidWorker::onPollTimer() {
    HidApiGuard::Lock lock;
    if (!m_hidDevice)
        return;

#ifdef Q_OS_WIN
    // Windows hidapi requires report ID as the first byte (0x00 for unnumbered reports).
    unsigned char cmd[9] = {0x00, 'u', 0, 0, 0, 0, 0, 0, 0};
    int writeResult = hid_write(m_hidDevice, cmd, sizeof(cmd));
#else
    // POSIX hidapi takes the raw 8-byte buffer (no report ID prefix).
    unsigned char cmd[8] = {'u', 0, 0, 0, 0, 0, 0, 0};
    int writeResult = hid_write(m_hidDevice, cmd, sizeof(cmd));
#endif

    if (writeResult < 0) {
        qCWarning(hwKpod) << "hid_write failed; assuming device gone";
        if (m_pollTimer)
            m_pollTimer->stop();
        releaseHandle();
        emit deviceRemoved();
        emit pollError(QStringLiteral("Failed to write to KPOD"));
        return;
    }

    unsigned char buffer[8];
    int readResult = hid_read_timeout(m_hidDevice, buffer, sizeof(buffer), 5);

    if (readResult < 0) {
        qCWarning(hwKpod) << "hid_read failed; assuming device gone";
        if (m_pollTimer)
            m_pollTimer->stop();
        releaseHandle();
        emit deviceRemoved();
        emit pollError(QStringLiteral("Failed to read from KPOD"));
        return;
    }

    if (readResult != 8)
        return; // No data this cycle (non-blocking) — normal.

    const ResponseEvents ev = decodeResponse(buffer);
    if (ev.emitEncoder)
        emit encoderRotated(ev.encoderTicks);
    if (ev.emitButtonHold)
        emit buttonHeld(ev.buttonNum);
    if (ev.emitButtonTap)
        emit buttonTapped(ev.buttonNum);
    if (ev.emitRocker)
        emit rockerPositionChanged(ev.rockerPosition);
}

// =============================================================================
// Presence / hotplug
// =============================================================================

void KpodHidWorker::onPresenceTimer() {
    HidApiGuard::Lock lock;
    // hid_enumerate reads cached USB descriptors on macOS/Windows — cheap. We use this
    // instead of IOHIDManager callbacks because hidapi already owns the IOKit run loop
    // and two managers conflict.
    hid_device_info *devs = hid_enumerate(VENDOR_ID, PRODUCT_ID);
    const bool now = (devs != nullptr);
    hid_free_enumeration(devs);

    if (!m_devicePresent && now) {
        onDeviceArrivedFromHotplug();
    } else if (m_devicePresent && !now) {
        onDeviceRemovedFromHotplug();
    }
}

void KpodHidWorker::onDeviceArrivedFromHotplug() {
    m_info = detectDeviceInfo();
    m_devicePresent = m_info.detected;
    emit deviceInfoReady(m_info);
}

void KpodHidWorker::onDeviceRemovedFromHotplug() {
    m_devicePresent = false;
    if (m_pollTimer && m_pollTimer->isActive())
        m_pollTimer->stop();
    releaseHandle();
    m_info = KpodDeviceInfo{};
    emit deviceInfoReady(m_info);
    emit deviceRemoved();
}

// =============================================================================
// Detection (full open + query for ID & firmware)
// =============================================================================

KpodDeviceInfo KpodHidWorker::detectDeviceInfo() {
    HidApiGuard::Lock lock;
    KpodDeviceInfo info;
    kpodLog("detectDeviceInfo() starting");

    hid_device_info *devs = hid_enumerate(VENDOR_ID, PRODUCT_ID);
    int interfaceCount = 0;
    hid_device_info *selected = nullptr;
    for (hid_device_info *d = devs; d != nullptr; d = d->next) {
        interfaceCount++;
        if (!selected) {
            selected = d;
        } else if (d->usage_page >= 0xFF00 && selected->usage_page < 0xFF00) {
            // Prefer vendor-defined interface
            selected = d;
        }
    }

    if (!selected) {
        hid_free_enumeration(devs);
        kpodLog("No KPOD device found");
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

    // Open + query for device ID / firmware. Retry to handle USB enumeration race.
    hid_device *dev = nullptr;
    const int maxRetries = 3;
    const int retryDelayMs = 200;
    for (int attempt = 1; attempt <= maxRetries && !dev; ++attempt) {
        if (attempt > 1)
            QThread::msleep(retryDelayMs);
        dev = hid_open_path(selected->path);
#ifdef Q_OS_WIN
        if (!dev)
            dev = hid_open(VENDOR_ID, PRODUCT_ID, nullptr);
#endif
    }

    if (dev) {
        unsigned char buf[8];
        unsigned char readBuf[8];
#ifdef Q_OS_WIN
        const int readTimeout = 500;
        unsigned char winBuf[9] = {0};
#else
        const int readTimeout = 100;
#endif

        auto issueQuery = [&](char letter) -> bool {
#ifdef Q_OS_WIN
            memset(winBuf, 0, sizeof(winBuf));
            winBuf[0] = 0x00;
            winBuf[1] = letter;
            return hid_write(dev, winBuf, sizeof(winBuf)) > 0;
#else
            memset(buf, 0, sizeof(buf));
            buf[0] = letter;
            return hid_write(dev, buf, sizeof(buf)) > 0;
#endif
        };

        // Device ID ('=')
        if (issueQuery('=')) {
            if (hid_read_timeout(dev, readBuf, sizeof(readBuf), readTimeout) == 8) {
                QString idStr;
                for (int i = 1; i < 8 && readBuf[i] != 0; ++i)
                    idStr += QChar(readBuf[i]);
                info.deviceId = idStr.trimmed();
            }
        }

        // Firmware version ('v')
        if (issueQuery('v')) {
            if (hid_read_timeout(dev, readBuf, sizeof(readBuf), readTimeout) == 8) {
                qint16 versionBcd = static_cast<qint16>(readBuf[1] | (readBuf[2] << 8));
                int major = versionBcd / 100;
                int minor = versionBcd % 100;
                info.firmwareVersion = QString("%1.%2").arg(major).arg(minor, 2, 10, QChar('0'));
            }
        }

        hid_close(dev);
    }

    hid_free_enumeration(devs);
    kpodLog(QString("detectDeviceInfo() complete — id=%1 fw=%2").arg(info.deviceId).arg(info.firmwareVersion));
    return info;
}
