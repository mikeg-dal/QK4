#include "kpoddevice.h"
#include <hidapi/hidapi.h>
#include <QDateTime>
#include <QLoggingCategory>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>

#ifdef Q_OS_MACOS
#include <CoreFoundation/CoreFoundation.h>
#endif

Q_LOGGING_CATEGORY(hwKpod, "hw.kpod")

// Debug logging helper for KPOD troubleshooting
static void kpodLog(const QString &msg) {
    qCDebug(hwKpod) << "KPOD:" << msg;
#ifdef Q_OS_WIN
    // Write to TEMP directory - guaranteed to exist and be writable
    static QString logPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/kpod_debug.log";

    QFile file(logPath);
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        out << QDateTime::currentDateTime().toString(Qt::ISODate) << " " << msg << "\n";
        file.close();
    }
#endif
}

KpodDevice::KpodDevice(QObject *parent)
    : QObject(parent), m_hidDevice(nullptr), m_pollTimer(new QTimer(this)), m_lastRockerPosition(RockerCenter) {
#ifdef Q_OS_LINUX
    // On Linux (libusb backend), hid_exit() destroys the libusb context globally,
    // crashing any open device handles. Initialize once here, exit once in destructor.
    hid_init();
#endif
    m_deviceInfo = detectDevice();

    // Setup poll timer
    m_pollTimer->setInterval(POLL_INTERVAL_MS);
    connect(m_pollTimer, &QTimer::timeout, this, &KpodDevice::poll);

    // Setup hotplug monitoring for device arrival/removal
    setupHotplugMonitoring();
}

KpodDevice::~KpodDevice() {
    stopPolling();
    teardownHotplugMonitoring();
#ifdef Q_OS_LINUX
    hid_exit();
#endif
}

bool KpodDevice::isDetected() const {
    return m_deviceInfo.detected;
}

KpodDeviceInfo KpodDevice::deviceInfo() const {
    return m_deviceInfo;
}

bool KpodDevice::startPolling() {
    if (m_pollTimer->isActive()) {
        return true; // Already polling
    }

    if (!openDevice()) {
        emit pollError("Failed to open KPOD device");
        return false;
    }

    // Reset state tracking
    m_lastRockerPosition = RockerCenter;
    m_lastButtonState = 0;
    m_holdEmitted = false;

    m_pollTimer->start();
    emit deviceConnected();
    return true;
}

void KpodDevice::stopPolling() {
    if (m_pollTimer->isActive()) {
        m_pollTimer->stop();
    }
    closeDevice();
}

bool KpodDevice::isPolling() const {
    return m_pollTimer->isActive();
}

KpodDevice::RockerPosition KpodDevice::rockerPosition() const {
    return m_lastRockerPosition;
}

bool KpodDevice::openDevice() {
    if (m_hidDevice) {
        return true; // Already open
    }

#ifndef Q_OS_LINUX
    if (hid_init() != 0) {
        qWarning() << "KPOD: Failed to initialize hidapi";
        return false;
    }
#endif

    // Use hid_open_path() on all platforms - hid_open() can fail on Windows (race condition)
    // and Linux (permission errors with libusb backend). Using the path from enumeration is
    // more reliable and works consistently across all hidapi backends.
    if (m_deviceInfo.devicePath.isEmpty()) {
        qWarning() << "KPOD: No device path available";
#ifndef Q_OS_LINUX
        hid_exit();
#endif
        return false;
    }
    m_hidDevice = hid_open_path(m_deviceInfo.devicePath.toUtf8().constData());
    if (!m_hidDevice) {
        qWarning() << "KPOD: Failed to open device";
#ifndef Q_OS_LINUX
        hid_exit();
#endif
        return false;
    }

    // Set non-blocking mode for responsive polling
    hid_set_nonblocking(m_hidDevice, 1);

    return true;
}

void KpodDevice::closeDevice() {
    if (m_hidDevice) {
        hid_close(m_hidDevice);
        m_hidDevice = nullptr;
#ifndef Q_OS_LINUX
        hid_exit();
#endif
        emit deviceDisconnected();
    }
}

void KpodDevice::poll() {
    if (!m_hidDevice) {
        stopPolling();
        emit pollError("Device handle invalid");
        return;
    }

    // Send update request command
    // Windows hidapi requires report ID as first byte (0x00 for devices without numbered reports)
#ifdef Q_OS_WIN
    unsigned char cmd[9] = {0x00, 'u', 0, 0, 0, 0, 0, 0, 0};
    int writeResult = hid_write(m_hidDevice, cmd, sizeof(cmd));
#else
    unsigned char cmd[8] = {'u', 0, 0, 0, 0, 0, 0, 0};
    int writeResult = hid_write(m_hidDevice, cmd, sizeof(cmd));
#endif

    if (writeResult < 0) {
        // Write failed - device may be disconnected
        qWarning() << "KPOD: Write failed, device disconnected?";
        stopPolling();
        emit pollError("Failed to write to KPOD");
        return;
    }

    // Read response (short timeout to avoid blocking event loop)
    unsigned char buffer[8];
    int readResult = hid_read_timeout(m_hidDevice, buffer, sizeof(buffer), 5);

    if (readResult < 0) {
        // Read error - device may be disconnected
        qWarning() << "KPOD: Read failed, device disconnected?";
        stopPolling();
        emit pollError("Failed to read from KPOD");
        return;
    }

    // Process response if we got data
    // Note: According to spec, cmd='u' only for new events, but we always
    // check controls byte for button/rocker state changes
    if (readResult == 8) {
        processResponse(buffer);
    }
    // readResult == 0 means no data available (normal for non-blocking)
}

void KpodDevice::processResponse(const unsigned char *buffer) {
    // KPOD Protocol (from spec):
    // - buffer[0] = cmd: 'u' (0x75) if new event, 0 if no event
    // - buffer[1-2] = ticks: signed 16-bit encoder count (little-endian)
    // - buffer[3] = controls: button/tap-hold/rocker state (ONLY valid when cmd='u')
    // - buffer[4-7] = spare

    unsigned char cmd = buffer[0];

    // Protocol: cmd='u' means new event, cmd=0 means no event
    // However, if we have a pending button press and see cmd=0, that's a release
    if (cmd != 'u') {
        // Check for implicit button release (button was pressed, now no event)
        if (m_lastButtonState != 0) {
            if (!m_holdEmitted) {
                emit buttonTapped(m_lastButtonState);
            }
            m_lastButtonState = 0;
            m_holdEmitted = false;
        }
        return;
    }

    // Extract encoder ticks (signed int16, little-endian)
    qint16 encoderTicks = static_cast<qint16>(buffer[1] | (buffer[2] << 8));

    // Extract controls byte (now guaranteed to be valid since cmd == 'u')
    quint8 controls = buffer[3];

    // Parse controls byte:
    // bit 7: unused
    // bit 6-5: Rocker (00=center/VFO B, 01=right/RIT-XIT, 10=left/VFO A, 11=error)
    // bit 4: Tap/Hold (0=tap, 1=hold)
    // bit 3-0: Button (0x01=btn1, 0x02=btn2, ..., 0x08=btn8)
    quint8 buttonNum = controls & 0x0F;
    bool isHold = (controls >> 4) & 0x01;
    RockerPosition rocker = static_cast<RockerPosition>((controls >> 5) & 0x03);

    // Emit encoder signal if there was rotation
    if (encoderTicks != 0) {
        emit encoderRotated(encoderTicks);
    }

    // Button state machine:
    // - KPOD sends hold=false initially, then hold=true after hold threshold
    // - If hold becomes true while pressed, emit buttonHeld
    // - If released while hold=false, emit buttonTapped
    if (buttonNum != 0 && m_lastButtonState == 0) {
        // Button just pressed - start tracking
        m_lastButtonState = buttonNum;
        m_holdEmitted = false;
        if (isHold) {
            // Already a hold at first press (unusual but handle it)
            emit buttonHeld(buttonNum);
            m_holdEmitted = true;
        }
    } else if (buttonNum != 0 && m_lastButtonState != 0) {
        // Button still pressed - check if hold flag became true
        if (isHold && !m_holdEmitted) {
            emit buttonHeld(m_lastButtonState);
            m_holdEmitted = true;
        }
    } else if (buttonNum == 0 && m_lastButtonState != 0) {
        // Button released - emit tap only if it wasn't a hold
        if (!m_holdEmitted) {
            emit buttonTapped(m_lastButtonState);
        }
        m_lastButtonState = 0;
        m_holdEmitted = false;
    }

    // Rocker position - update on change (rocker=3 is error state)
    if (rocker != m_lastRockerPosition && rocker != 3) {
        m_lastRockerPosition = rocker;
        emit rockerPositionChanged(rocker);
    }
}

KpodDeviceInfo KpodDevice::detectDevice() {
    KpodDeviceInfo info;

    kpodLog("detectDevice() starting");

#ifndef Q_OS_LINUX
    if (hid_init() != 0) {
        kpodLog("hid_init() failed");
        return info;
    }
#endif

    kpodLog("hid_init() succeeded, enumerating devices...");
    struct hid_device_info *devs = hid_enumerate(VENDOR_ID, PRODUCT_ID);
    struct hid_device_info *cur_dev = devs;

    // Log ALL enumerated interfaces (KPOD may have multiple)
    int interfaceCount = 0;
    struct hid_device_info *selected_dev = nullptr;
    for (struct hid_device_info *d = devs; d != nullptr; d = d->next) {
        interfaceCount++;
        kpodLog(QString("Interface %1: usage_page=0x%2, usage=0x%3, interface=%4, path=%5")
                    .arg(interfaceCount)
                    .arg(d->usage_page, 4, 16, QChar('0'))
                    .arg(d->usage, 4, 16, QChar('0'))
                    .arg(d->interface_number)
                    .arg(d->path ? QString::fromUtf8(d->path) : "null"));

        // Prefer vendor-defined usage page (0xFF00) or the first valid interface
        if (!selected_dev) {
            selected_dev = d;
        } else if (d->usage_page >= 0xFF00 && selected_dev->usage_page < 0xFF00) {
            // Prefer vendor-defined interface
            selected_dev = d;
            kpodLog(QString("Selecting interface %1 (vendor-defined usage page)").arg(interfaceCount));
        }
    }
    kpodLog(QString("Total interfaces found: %1").arg(interfaceCount));

    cur_dev = selected_dev;

    if (cur_dev) {
        info.detected = true;
        info.vendorId = cur_dev->vendor_id;
        info.productId = cur_dev->product_id;

        if (cur_dev->product_string) {
            info.productName = QString::fromWCharArray(cur_dev->product_string);
        }
        if (cur_dev->manufacturer_string) {
            info.manufacturer = QString::fromWCharArray(cur_dev->manufacturer_string);
        }
        if (cur_dev->path) {
            info.devicePath = QString::fromUtf8(cur_dev->path);
        }

        kpodLog(QString("Selected device: %1 (VID=%2, PID=%3, usage_page=0x%4)")
                    .arg(info.productName)
                    .arg(info.vendorId, 4, 16, QChar('0'))
                    .arg(info.productId, 4, 16, QChar('0'))
                    .arg(cur_dev->usage_page, 4, 16, QChar('0')));
        kpodLog(QString("Device path: %1").arg(info.devicePath));

        // Try to open device to get firmware version and device ID
        kpodLog("Attempting to open device...");
        hid_device *dev = nullptr;

        // Use hid_open_path() on all platforms for consistency.
        // hid_open() fails on Linux (hidraw permission errors) and Windows (race conditions).
        // Retry with delays to handle device settling after enumeration.
        const int maxRetries = 3;
        const int retryDelayMs = 200;

        for (int attempt = 1; attempt <= maxRetries && !dev; ++attempt) {
            if (attempt > 1) {
                kpodLog(
                    QString("Retry attempt %1/%2 after %3ms delay...").arg(attempt).arg(maxRetries).arg(retryDelayMs));
                QThread::msleep(retryDelayMs);
            }

            dev = hid_open_path(cur_dev->path);
            if (dev) {
                kpodLog(QString("hid_open_path succeeded on attempt %1").arg(attempt));
                break;
            }

            const wchar_t *pathErr = hid_error(nullptr);
            kpodLog(QString("hid_open_path failed: %1").arg(pathErr ? QString::fromWCharArray(pathErr) : "unknown"));

#ifdef Q_OS_WIN
            // Fallback: try hid_open with VID/PID (lets hidapi find a fresh path)
            dev = hid_open(VENDOR_ID, PRODUCT_ID, nullptr);
            if (dev) {
                kpodLog(QString("hid_open (VID/PID) succeeded on attempt %1").arg(attempt));
                break;
            }

            const wchar_t *vidErr = hid_error(nullptr);
            kpodLog(QString("hid_open (VID/PID) failed: %1").arg(vidErr ? QString::fromWCharArray(vidErr) : "unknown"));
#endif
        }
        if (dev) {
            kpodLog("Device opened successfully");
            unsigned char buf[8];
            unsigned char readBuf[8];

            // Use longer timeout on Windows (HID driver latency)
#ifdef Q_OS_WIN
            const int readTimeout = 500;
            // Windows hidapi requires report ID as first byte (0x00 for devices without numbered reports)
            unsigned char winBuf[9] = {0};
#else
            const int readTimeout = 100;
#endif
            kpodLog(QString("Using read timeout: %1ms").arg(readTimeout));

            // Get Device ID (command '=')
#ifdef Q_OS_WIN
            memset(winBuf, 0, sizeof(winBuf));
            winBuf[0] = 0x00; // Report ID
            winBuf[1] = '=';
            kpodLog("Sending Device ID command '=' (with report ID)...");
            int writeResult = hid_write(dev, winBuf, sizeof(winBuf));
#else
            memset(buf, 0, sizeof(buf));
            buf[0] = '=';
            kpodLog("Sending Device ID command '='...");
            int writeResult = hid_write(dev, buf, sizeof(buf));
#endif
            kpodLog(QString("hid_write returned: %1").arg(writeResult));
            if (writeResult > 0) {
                int readResult = hid_read_timeout(dev, readBuf, sizeof(readBuf), readTimeout);
                kpodLog(QString("hid_read_timeout returned: %1").arg(readResult));
                if (readResult == 8) {
                    QString idStr;
                    for (int i = 1; i < 8 && readBuf[i] != 0; ++i) {
                        idStr += QChar(readBuf[i]);
                    }
                    info.deviceId = idStr.trimmed();
                    kpodLog(QString("Device ID: '%1'").arg(info.deviceId));
                } else if (readResult < 0) {
                    const wchar_t *err = hid_error(dev);
                    kpodLog(QString("Read error: %1").arg(err ? QString::fromWCharArray(err) : "unknown"));
                } else {
                    kpodLog(QString("Read returned %1 bytes (expected 8)").arg(readResult));
                }
            } else {
                const wchar_t *err = hid_error(dev);
                kpodLog(QString("Write error: %1").arg(err ? QString::fromWCharArray(err) : "unknown"));
            }

            // Get Firmware Version (command 'v')
#ifdef Q_OS_WIN
            memset(winBuf, 0, sizeof(winBuf));
            winBuf[0] = 0x00; // Report ID
            winBuf[1] = 'v';
            kpodLog("Sending Firmware Version command 'v' (with report ID)...");
            writeResult = hid_write(dev, winBuf, sizeof(winBuf));
#else
            memset(buf, 0, sizeof(buf));
            buf[0] = 'v';
            kpodLog("Sending Firmware Version command 'v'...");
            writeResult = hid_write(dev, buf, sizeof(buf));
#endif
            kpodLog(QString("hid_write returned: %1").arg(writeResult));
            if (writeResult > 0) {
                int readResult = hid_read_timeout(dev, readBuf, sizeof(readBuf), readTimeout);
                kpodLog(QString("hid_read_timeout returned: %1").arg(readResult));
                if (readResult == 8) {
                    qint16 versionBcd = static_cast<qint16>(readBuf[1] | (readBuf[2] << 8));
                    int major = versionBcd / 100;
                    int minor = versionBcd % 100;
                    info.firmwareVersion = QString("%1.%2").arg(major).arg(minor, 2, 10, QChar('0'));
                    kpodLog(QString("Firmware version: %1 (raw BCD: %2)").arg(info.firmwareVersion).arg(versionBcd));
                } else if (readResult < 0) {
                    const wchar_t *err = hid_error(dev);
                    kpodLog(QString("Read error: %1").arg(err ? QString::fromWCharArray(err) : "unknown"));
                } else {
                    kpodLog(QString("Read returned %1 bytes (expected 8)").arg(readResult));
                }
            } else {
                const wchar_t *err = hid_error(dev);
                kpodLog(QString("Write error: %1").arg(err ? QString::fromWCharArray(err) : "unknown"));
            }

            hid_close(dev);
            kpodLog("Device closed");
        } else {
            const wchar_t *err = hid_error(nullptr);
            kpodLog(QString("Failed to open device: %1").arg(err ? QString::fromWCharArray(err) : "unknown"));
        }
    } else {
        kpodLog("No KPOD device found in enumeration");
    }

    hid_free_enumeration(devs);
#ifndef Q_OS_LINUX
    hid_exit();
#endif

    kpodLog(QString("detectDevice() complete - detected=%1, version=%2, id=%3")
                .arg(info.detected)
                .arg(info.firmwareVersion)
                .arg(info.deviceId));

    return info;
}

// ============== Hotplug Monitoring ==============
//
// Note: We use periodic hid_enumerate() instead of IOKit's IOHIDManager callbacks
// because hidapi internally uses IOHIDManager on macOS. Having two IOHIDManagers
// for the same device causes crashes due to resource conflicts.
//
// The periodic check is very lightweight - hid_enumerate() only reads USB
// descriptors from the OS kernel, no device I/O.

void KpodDevice::setupHotplugMonitoring() {
    // Create a timer for periodic device presence checking
    m_presenceTimer = new QTimer(this);
    m_presenceTimer->setInterval(PRESENCE_CHECK_INTERVAL_MS);
    connect(m_presenceTimer, &QTimer::timeout, this, &KpodDevice::checkDevicePresence);
    m_presenceTimer->start();
}

void KpodDevice::teardownHotplugMonitoring() {
    if (m_presenceTimer) {
        m_presenceTimer->stop();
        delete m_presenceTimer;
        m_presenceTimer = nullptr;
    }
}

void KpodDevice::checkDevicePresence() {
    // Quick check using hid_enumerate - very lightweight, no device I/O
#ifndef Q_OS_LINUX
    if (hid_init() != 0) {
        return;
    }
#endif

    struct hid_device_info *devs = hid_enumerate(VENDOR_ID, PRODUCT_ID);
    bool nowDetected = (devs != nullptr);
    hid_free_enumeration(devs);
#ifndef Q_OS_LINUX
    hid_exit();
#endif

    bool wasDetected = m_deviceInfo.detected;

    if (!wasDetected && nowDetected) {
        // Device just arrived
        onDeviceArrived();
    } else if (wasDetected && !nowDetected) {
        // Device just departed
        onDeviceRemoved();
    }
}

void KpodDevice::onDeviceArrived() {
    // Refresh device info
    m_deviceInfo = detectDevice();

    // Emit signal for UI update
    emit deviceConnected();
}

void KpodDevice::onDeviceRemoved() {
    // Stop polling if active
    if (m_pollTimer->isActive()) {
        m_pollTimer->stop();
    }

    // Close device handle if open
    if (m_hidDevice) {
        hid_close(m_hidDevice);
        m_hidDevice = nullptr;
#ifndef Q_OS_LINUX
        hid_exit();
#endif
    }

    // Update device info
    m_deviceInfo.detected = false;

    // Emit signal for UI update
    emit deviceDisconnected();
}
