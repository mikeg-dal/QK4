#include "kpodplususbworker.h"
#include <QLoggingCategory>
#include <QString>
#include <QThread>
#include <QTime>
#include <QTimer>
#include <cstring>
#include <libusb-1.0/libusb.h>

// WHY: libusb 1.0.21 introduced reliable hotplug on macOS and is the floor
// every other capability we depend on (interrupt event handler, multi-context
// safety). The Linux pkg-config check in CMakeLists.txt enforces this at
// configure time on POSIX; this compile-time gate covers Windows where
// pkg-config is unavailable.
static_assert(LIBUSB_API_VERSION >= 0x01000105, "libusb >= 1.0.21 required");

Q_LOGGING_CATEGORY(hwKpodPlus, "hw.kpodplus")

// =============================================================================
// Pure helpers
// =============================================================================

void KpodPlusUsbWorker::buildKeyerSpeedCmd(int wpm, unsigned char out[8]) {
    wpm = qBound(8, wpm, 100);
    std::memset(out, 0, 8);
    out[0] = 'K';
    out[1] = 0x03;
    out[2] = static_cast<unsigned char>(wpm);
}

void KpodPlusUsbWorker::buildCwPitchCmd(int freqHz, unsigned char out[8]) {
    int tenHz = qBound(40, freqHz / 10, 100);
    std::memset(out, 0, 8);
    out[0] = 'K';
    out[1] = 0x04;
    out[2] = static_cast<unsigned char>(tenHz);
}

void KpodPlusUsbWorker::buildKeyerParamsCmd(int iambicMode, bool reversed, unsigned char out[8]) {
    std::memset(out, 0, 8);
    out[0] = 'K';
    out[1] = 0x01;
    out[2] = static_cast<unsigned char>(qBound(0, iambicMode, 1));
    out[3] = reversed ? 1 : 0;
}

void KpodPlusUsbWorker::buildEncodeModeCmd(int mode, unsigned char out[8]) {
    std::memset(out, 0, 8);
    out[0] = 'K';
    out[1] = 0x02;
    out[2] = static_cast<unsigned char>(qBound(0, mode, 1));
}

void KpodPlusUsbWorker::buildStuckTimeoutCmd(int seconds, unsigned char out[8]) {
    seconds = qBound(5, seconds, 600);
    std::memset(out, 0, 8);
    out[0] = 'K';
    out[1] = 0x05;
    out[2] = static_cast<unsigned char>(seconds & 0xFF);
    out[3] = static_cast<unsigned char>((seconds >> 8) & 0xFF);
}

QByteArray KpodPlusUsbWorker::trimEp02Buffer(const QByteArray &raw) {
    int len = raw.size();
    while (len > 0 && raw.at(len - 1) == '\0')
        --len;
    return raw.left(len);
}

// =============================================================================
// EP01 response decoder
// =============================================================================

void KpodPlusUsbWorker::resetDecoderState() {
    m_lastRockerPosition = 0;
    m_lastButtonState = 0;
    m_holdEmitted = false;
}

KpodPlusUsbWorker::ResponseEvents KpodPlusUsbWorker::decodeResponse(const unsigned char buffer[8]) {
    ResponseEvents ev;
    const unsigned char cmd = buffer[0];

    if (cmd != 'u') {
        // No event reported this cycle. If a button was held down on the
        // previous cycle, treat the implicit no-op as a release (tap).
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

KpodPlusUsbWorker::KpodPlusUsbWorker(QObject *parent) : QObject(parent) {}

KpodPlusUsbWorker::~KpodPlusUsbWorker() {
    // The façade is expected to invoke shutdown() via BlockingQueuedConnection
    // before destroying the thread. If it didn't, fall back here.
    if (m_handle || m_ctx) {
        releaseHandle();
        if (m_ctx) {
            libusb_exit(m_ctx);
            m_ctx = nullptr;
        }
    }
}

void KpodPlusUsbWorker::start() {
    const int rc = libusb_init(&m_ctx);
    if (rc != LIBUSB_SUCCESS) {
        qCWarning(hwKpodPlus) << "libusb_init failed:" << libusb_error_name(rc);
        m_ctx = nullptr;
        return;
    }

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(POLL_INTERVAL_MS);
    m_pollTimer->setTimerType(Qt::PreciseTimer);
    connect(m_pollTimer, &QTimer::timeout, this, &KpodPlusUsbWorker::onPollTimer);

    m_presenceTimer = new QTimer(this);
    m_presenceTimer->setInterval(PRESENCE_CHECK_INTERVAL_MS);
    connect(m_presenceTimer, &QTimer::timeout, this, &KpodPlusUsbWorker::onPresenceTimer);
    m_presenceTimer->start();

    // Initial detection on the worker thread (formerly a deferred singleShot
    // on the main thread that could stall the GUI).
    KpodPlusDeviceInfo info;
    if (detectDeviceLocation(&info)) {
        m_devicePresent = true;
    }
    m_info = info;
    emit deviceInfoReady(m_info);
}

void KpodPlusUsbWorker::shutdown() {
    if (m_pollTimer) {
        m_pollTimer->stop();
    }
    if (m_presenceTimer) {
        m_presenceTimer->stop();
    }
    releaseHandle();
    if (m_ctx) {
        libusb_exit(m_ctx);
        m_ctx = nullptr;
    }
}

// =============================================================================
// Detection / open
// =============================================================================

bool KpodPlusUsbWorker::detectDeviceLocation(KpodPlusDeviceInfo *info) {
    if (!m_ctx)
        return false;

    libusb_device **devList = nullptr;
    const ssize_t cnt = libusb_get_device_list(m_ctx, &devList);
    if (cnt < 0)
        return false;

    bool found = false;
    for (ssize_t i = 0; i < cnt; ++i) {
        libusb_device *dev = devList[i];
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(dev, &desc) != LIBUSB_SUCCESS)
            continue;
        if (desc.idVendor != VENDOR_ID)
            continue;
        if (desc.idProduct != PRODUCT_ID_KPOD && desc.idProduct != PRODUCT_ID_ELECRAFT)
            continue;

        // WHY: enumerate-only. Walking config descriptors is cache-only; we
        // intentionally do NOT open the device here. The earlier code opened
        // it every 2 s on the main thread to query firmware/ID, which stressed
        // the USB stack and competed with the active poll session.
        struct libusb_config_descriptor *config = nullptr;
        if (libusb_get_active_config_descriptor(dev, &config) != LIBUSB_SUCCESS)
            continue;
        bool hasVendor = false;
        for (int iface = 0; iface < config->bNumInterfaces && !hasVendor; ++iface) {
            for (int alt = 0; alt < config->interface[iface].num_altsetting; ++alt) {
                if (config->interface[iface].altsetting[alt].bInterfaceClass == VENDOR_INTERFACE_CLASS) {
                    hasVendor = true;
                    break;
                }
            }
        }
        libusb_free_config_descriptor(config);
        if (!hasVendor)
            continue;

        info->detected = true;
        info->vendorId = desc.idVendor;
        info->productId = desc.idProduct;
        info->busNumber = libusb_get_bus_number(dev);
        info->deviceAddress = libusb_get_device_address(dev);
        found = true;
        break;
    }

    libusb_free_device_list(devList, 1);
    return found;
}

bool KpodPlusUsbWorker::queryOpenDeviceInfo(KpodPlusDeviceInfo *info) {
    if (!m_handle)
        return false;

    libusb_device *dev = libusb_get_device(m_handle);
    struct libusb_device_descriptor desc;
    if (libusb_get_device_descriptor(dev, &desc) != LIBUSB_SUCCESS)
        return false;

    unsigned char strBuf[256];
    if (desc.iProduct > 0) {
        int len = libusb_get_string_descriptor_ascii(m_handle, desc.iProduct, strBuf, sizeof(strBuf));
        if (len > 0)
            info->productName = QString::fromLatin1(reinterpret_cast<char *>(strBuf), len);
    }
    if (desc.iManufacturer > 0) {
        int len = libusb_get_string_descriptor_ascii(m_handle, desc.iManufacturer, strBuf, sizeof(strBuf));
        if (len > 0)
            info->manufacturer = QString::fromLatin1(reinterpret_cast<char *>(strBuf), len);
    }

    unsigned char cmdBuf[8];
    unsigned char readBuf[8];
    int transferred = 0;

    // Device ID ('=')
    std::memset(cmdBuf, 0, sizeof(cmdBuf));
    cmdBuf[0] = '=';
    if (libusb_interrupt_transfer(m_handle, 0x01, cmdBuf, sizeof(cmdBuf), &transferred, 100) == LIBUSB_SUCCESS) {
        transferred = 0;
        if (libusb_interrupt_transfer(m_handle, 0x81, readBuf, sizeof(readBuf), &transferred, 100) == LIBUSB_SUCCESS &&
            transferred == 8) {
            QString idStr;
            for (int j = 1; j < 8 && readBuf[j] != 0; ++j)
                idStr += QChar(readBuf[j]);
            info->deviceId = idStr.trimmed();
        }
    }

    // Firmware version ('v') — BCD
    std::memset(cmdBuf, 0, sizeof(cmdBuf));
    cmdBuf[0] = 'v';
    transferred = 0;
    if (libusb_interrupt_transfer(m_handle, 0x01, cmdBuf, sizeof(cmdBuf), &transferred, 100) == LIBUSB_SUCCESS) {
        transferred = 0;
        if (libusb_interrupt_transfer(m_handle, 0x81, readBuf, sizeof(readBuf), &transferred, 100) == LIBUSB_SUCCESS &&
            transferred == 8) {
            const qint16 versionBcd = static_cast<qint16>(readBuf[1] | (readBuf[2] << 8));
            const int major = versionBcd / 100;
            const int minor = versionBcd % 100;
            info->firmwareVersion = QString("%1.%2").arg(major).arg(minor, 2, 10, QChar('0'));
        }
    }

    return true;
}

bool KpodPlusUsbWorker::openHandle() {
    if (m_handle)
        return true;
    if (!m_ctx || !m_info.detected)
        return false;

    libusb_device **devList = nullptr;
    const ssize_t cnt = libusb_get_device_list(m_ctx, &devList);
    if (cnt < 0)
        return false;

    libusb_device *target = nullptr;
    for (ssize_t i = 0; i < cnt; ++i) {
        if (libusb_get_bus_number(devList[i]) == m_info.busNumber &&
            libusb_get_device_address(devList[i]) == m_info.deviceAddress) {
            target = devList[i];
            break;
        }
    }
    if (!target) {
        libusb_free_device_list(devList, 1);
        qCWarning(hwKpodPlus) << "openHandle: device not found at bus" << m_info.busNumber << "addr"
                              << m_info.deviceAddress;
        return false;
    }

    int rc = libusb_open(target, &m_handle);
    libusb_free_device_list(devList, 1);
    if (rc != LIBUSB_SUCCESS) {
        qCWarning(hwKpodPlus) << "libusb_open failed:" << libusb_error_name(rc);
        m_handle = nullptr;
        return false;
    }

#ifdef Q_OS_LINUX
    if (libusb_kernel_driver_active(m_handle, 0) == 1) {
        libusb_detach_kernel_driver(m_handle, 0);
    }
#endif
    rc = libusb_claim_interface(m_handle, 0);
    if (rc != LIBUSB_SUCCESS) {
        qCWarning(hwKpodPlus) << "claim_interface failed:" << libusb_error_name(rc);
        libusb_close(m_handle);
        m_handle = nullptr;
        return false;
    }
    m_interfaceClaimed = true;
    return true;
}

void KpodPlusUsbWorker::releaseHandle() {
    if (m_handle) {
        // Step 1: signal the EP02 reader to drop the handle. The DirectConnection to
        // setDeviceHandle(0) means EP02's atomic handle is cleared synchronously on this
        // thread BEFORE we proceed. EP02's next loop iteration will see null and skip
        // the transfer.
        emit handleClosing();

        // Step 2: wait for any libusb_interrupt_transfer currently in flight on the
        // EP02 thread to complete. The EP02 thread holds m_transferMutex across the
        // libusb call; acquiring it here guarantees no transfer is mid-flight when we
        // proceed to libusb_close. Bounded by the EP02 transfer timeout (100 ms max).
        // Without this, libusb_close could free the handle out from under an in-flight
        // transfer — use-after-free, caught by ASAN during hot-unplug stress.
        if (m_ep02TransferMutex) {
            std::lock_guard<std::mutex> lock(*m_ep02TransferMutex);
            if (m_interfaceClaimed) {
                libusb_release_interface(m_handle, 0);
                m_interfaceClaimed = false;
            }
            libusb_close(m_handle);
            m_handle = nullptr;
        } else {
            // No EP02 worker registered (e.g. tests, or if the façade didn't wire the
            // mutex). Fall back to the unsynchronized close — same behavior as before
            // the sync mutex was added.
            if (m_interfaceClaimed) {
                libusb_release_interface(m_handle, 0);
                m_interfaceClaimed = false;
            }
            libusb_close(m_handle);
            m_handle = nullptr;
        }
    }
}

void KpodPlusUsbWorker::discardBufferedKeying() {
    // Read EP02 dry and throw the result away, BEFORE the handle reaches the EP02 reader.
    //
    // The KPOD+ runs its own keyer. While QK4 has the device closed it keeps reading the paddle,
    // keeps generating elements and keeps them in its own buffer — a USB interrupt-IN endpoint holds
    // data until the host asks for it, and nobody was asking. Reopening then delivered the whole
    // backlog as fast as the host could read it.
    //
    // Reproduced on the bench, 2026-09-18: enable, key, DISABLE, key five characters, wait 47 s,
    // re-enable. Two frames arrived 1 ms apart, the second carrying four dahs plus a letter space
    // and a pause, and the radio transmitted them. The live-keying signature is a 4-byte payload
    // every ~114 ms; a near-full 28-byte frame can only be a drained backlog.
    //
    // WHY discard rather than pace them out: they are elements the operator keyed up to a minute
    // ago, at a moment when QK4 was deliberately not listening to this device. Transmitting them
    // late is wrong at any speed, and silence is the safe failure. Their sidetone already told them
    // what they sent; the radio is the part that must not act on it.
    //
    // Runs on the USB worker thread, before handleOpened publishes the handle, so the EP02 reader
    // cannot be competing for the endpoint. Bounded twice — by a short timeout and by a frame count
    // — so a device that never returns TIMEOUT cannot stall the open.
    if (!m_handle) {
        return;
    }
    constexpr int kDrainTimeoutMs = 20; // long enough for a queued frame, short enough not to stall
    constexpr int kMaxDrainFrames = 64; // 64 x 32 B is far more than the device can hold
    unsigned char scratch[32];
    int frames = 0;
    int bytes = 0;
    for (; frames < kMaxDrainFrames; ++frames) {
        int transferred = 0;
        const int rc =
            libusb_interrupt_transfer(m_handle, 0x82, scratch, sizeof(scratch), &transferred, kDrainTimeoutMs);
        if (rc != 0 || transferred <= 0) {
            break; // TIMEOUT is the expected exit: nothing left to read
        }
        bytes += transferred;
    }
    if (frames > 0) {
        // Warning, not debug. This is the operator's sending being thrown away, and if it happens
        // when they did not expect it that is worth finding in a log without knowing to enable a
        // category first.
        qCWarning(hwKpodPlus) << "KPOD+ discarded" << frames << "buffered keyer frame(s)," << bytes
                              << "bytes, queued by the device while QK4 had it closed. They are not sent.";
    }
}

void KpodPlusUsbWorker::openDevice() {
    if (m_handle) {
        return;
    }
    if (!openHandle()) {
        emit pollError(QStringLiteral("Failed to open KPOD+ device"));
        return;
    }
    resetDecoderState();
    queryOpenDeviceInfo(&m_info);
    emit deviceInfoReady(m_info);
    discardBufferedKeying();
    emit handleOpened(reinterpret_cast<quintptr>(m_handle));
    if (m_pollTimer && !m_pollTimer->isActive())
        m_pollTimer->start();
    emit deviceArrived();
}

void KpodPlusUsbWorker::closeDevice() {
    const bool wasPolling = (m_pollTimer && m_pollTimer->isActive());
    if (m_pollTimer)
        m_pollTimer->stop();
    releaseHandle();
    // Surface a user-visible "device no longer keying" event so the façade
    // can clear m_polling and downstream listeners (KPA1500 mini-panel,
    // KpodPage status row) refresh. Distinct from the hotplug-yank case
    // which still goes via the presence timer.
    if (wasPolling)
        emit deviceRemoved();
}

// =============================================================================
// EP01 polling + helpers
// =============================================================================

bool KpodPlusUsbWorker::sendEp01Out(const unsigned char data[8]) {
    if (!m_handle)
        return false;
    int transferred = 0;
    const int rc = libusb_interrupt_transfer(m_handle, 0x01, const_cast<unsigned char *>(data), 8, &transferred, 50);
    if (rc != LIBUSB_SUCCESS && rc != LIBUSB_ERROR_TIMEOUT) {
        qCWarning(hwKpodPlus) << "EP01 OUT failed:" << libusb_error_name(rc);
        if (rc == LIBUSB_ERROR_NO_DEVICE || rc == LIBUSB_ERROR_IO)
            handleLostDevice(QString::fromLatin1(libusb_error_name(rc)));
        return false;
    }
    return true;
}

bool KpodPlusUsbWorker::readEp01In(unsigned char buffer[8], int timeoutMs) {
    if (!m_handle)
        return false;
    int transferred = 0;
    const int rc = libusb_interrupt_transfer(m_handle, 0x81, buffer, 8, &transferred, timeoutMs);
    if (rc == LIBUSB_SUCCESS && transferred == 8)
        return true;
    if (rc == LIBUSB_ERROR_TIMEOUT)
        return false;
    if (rc == LIBUSB_ERROR_NO_DEVICE || rc == LIBUSB_ERROR_IO) {
        qCWarning(hwKpodPlus) << "EP01 IN failed:" << libusb_error_name(rc);
        handleLostDevice(QString::fromLatin1(libusb_error_name(rc)));
    }
    return false;
}

void KpodPlusUsbWorker::handleLostDevice(QString reason) {
    // Idempotent: handleLostDevice can be invoked from three paths during a
    // single USB transient — sendEp01Out failure, readEp01In failure, and the
    // queued connection from EP02 worker's transferError. Bail if we've
    // already torn down so we don't double-emit signals or re-schedule
    // closeDevice.
    if (!m_devicePresent && !m_handle)
        return;
    qCWarning(hwKpodPlus) << "Device lost:" << reason;
    m_devicePresent = false;
    // Reset cached info so the page header clears immediately. The presence
    // timer re-fills it on its next tick if the device recovers.
    m_info = KpodPlusDeviceInfo{};
    emit deviceInfoReady(m_info);
    QTimer::singleShot(0, this, &KpodPlusUsbWorker::closeDevice);
}

void KpodPlusUsbWorker::onPollTimer() {
    if (!m_handle)
        return;

    unsigned char cmd[8] = {'u', 0, 0, 0, 0, 0, 0, 0};
    if (!sendEp01Out(cmd))
        return;

    unsigned char buffer[8] = {};
    if (!readEp01In(buffer, 5))
        return;

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

void KpodPlusUsbWorker::onPresenceTimer() {
    // Enumerate-only. No libusb_open / no claim_interface — just walks the
    // cached USB topology and matches VID/PID. ~1 ms cost on macOS/Linux.
    KpodPlusDeviceInfo probe;
    const bool now = detectDeviceLocation(&probe);

    if (!m_devicePresent && now) {
        m_devicePresent = true;
        m_info = probe;
        emit deviceInfoReady(m_info);
        openDevice();
    } else if (m_devicePresent && !now) {
        m_devicePresent = false;
        // Reset cached info before closeDevice() so the façade's m_info
        // re-emits with detected=false. Without this, the page header
        // keeps showing "KPOD+ Detected" with stale firmware/ID values
        // even after the user pulls the cable.
        m_info = KpodPlusDeviceInfo{};
        emit deviceInfoReady(m_info);
        closeDevice();
        emit deviceRemoved();
    }
}

// =============================================================================
// Setters — sync EP01 transactions on the worker thread
// =============================================================================

// Member function so it can call handleLostDevice() on hard error. Was static — promoted
// to give it access to the device-lost cascade.
bool KpodPlusUsbWorker::sendConfigCommand(const unsigned char cmd[8]) {
    if (!m_handle)
        return false;

    int transferred = 0;
    int rc = libusb_interrupt_transfer(m_handle, 0x01, const_cast<unsigned char *>(cmd), 8, &transferred, 100);
    if (rc != LIBUSB_SUCCESS) {
        qCWarning(hwKpodPlus) << "config EP01 OUT failed:" << libusb_error_name(rc);
        if (rc == LIBUSB_ERROR_NO_DEVICE || rc == LIBUSB_ERROR_IO)
            handleLostDevice(QString::fromLatin1(libusb_error_name(rc)));
        return false;
    }

    // WHY drain the readback: the device responds to every EP01 OUT with an 8-byte status
    // frame. If we DON'T read it, the byte sits in the host's EP01 IN FIFO and gets returned
    // as the response to the next poll's 'u' command, shifting all subsequent reads by one
    // frame — visible to the user as a stale encoder/button event for one cycle after every
    // config change.
    //
    // Retry on timeout: USB stack contention or a busy device occasionally causes the first
    // read to time out even though the byte is queued. 3 attempts × 100 ms = 300 ms hard
    // cap. On NO_DEVICE / IO, escalate to handleLostDevice — the device went away mid-config.
    unsigned char response[8];
    for (int attempt = 0; attempt < 3; ++attempt) {
        int respTransferred = 0;
        rc = libusb_interrupt_transfer(m_handle, 0x81, response, sizeof(response), &respTransferred, 100);
        if (rc == LIBUSB_SUCCESS)
            return true;
        if (rc == LIBUSB_ERROR_NO_DEVICE || rc == LIBUSB_ERROR_IO) {
            qCWarning(hwKpodPlus) << "config EP01 IN failed (hard):" << libusb_error_name(rc);
            handleLostDevice(QString::fromLatin1(libusb_error_name(rc)));
            return false;
        }
        // Timeout — retry. Anything else (BUSY, INTERRUPTED, …) also retries; the bounded
        // attempt count caps the worst-case time.
    }
    qCWarning(hwKpodPlus) << "config EP01 IN drain failed after 3 attempts; next poll may "
                             "return a stale frame until the FIFO catches up";
    return false;
}

void KpodPlusUsbWorker::setKeyerSpeed(int wpm) {
    unsigned char cmd[8];
    buildKeyerSpeedCmd(wpm, cmd);
    sendConfigCommand(cmd);
}

void KpodPlusUsbWorker::setCwPitch(int freqHz) {
    unsigned char cmd[8];
    buildCwPitchCmd(freqHz, cmd);
    sendConfigCommand(cmd);
}

void KpodPlusUsbWorker::setKeyerParams(int iambicMode, bool paddleReversed) {
    unsigned char cmd[8];
    buildKeyerParamsCmd(iambicMode, paddleReversed, cmd);
    sendConfigCommand(cmd);
}

void KpodPlusUsbWorker::setEncodeMode(int mode) {
    unsigned char cmd[8];
    buildEncodeModeCmd(mode, cmd);
    sendConfigCommand(cmd);
}

void KpodPlusUsbWorker::setStuckTimeout(int seconds) {
    unsigned char cmd[8];
    buildStuckTimeoutCmd(seconds, cmd);
    sendConfigCommand(cmd);
}

// =============================================================================
// EP02 reader worker
// =============================================================================

KpodPlusEp02Worker::KpodPlusEp02Worker(QObject *parent) : QObject(parent) {}

void KpodPlusEp02Worker::setDeviceHandle(quintptr handle) {
    m_handle.store(reinterpret_cast<libusb_device_handle *>(handle), std::memory_order_release);
}

void KpodPlusEp02Worker::requestStop() {
    m_running.store(false, std::memory_order_relaxed);
}

void KpodPlusEp02Worker::run() {
    m_running.store(true, std::memory_order_relaxed);
    unsigned char buffer[32];

    // WHY a longer timeout than the legacy 10 ms: the device's EP02 FIFO is
    // multi-packet deep so we don't lose data during the wait. A short
    // timeout served only to check m_running, which we'd hit at worst once per
    // timeout window — 100 ms is plenty for clean shutdown perception.
    constexpr int kEp02TimeoutMs = 100;

    while (m_running.load(std::memory_order_relaxed)) {
        libusb_device_handle *h = m_handle.load(std::memory_order_acquire);
        if (!h) {
            // No device — sleep briefly and re-check. Using a short blocking
            // wait keeps shutdown latency low without burning CPU.
            QThread::msleep(20);
            continue;
        }

        int transferred = 0;
        int rc;
        {
            // Held during the libusb call so USB worker's releaseHandle() (which acquires
            // the same mutex before libusb_close) waits for any in-flight transfer to
            // finish before the handle is freed. Bounded by the kEp02TimeoutMs timeout.
            std::lock_guard<std::mutex> lock(m_transferMutex);
            // Re-check handle after acquiring the lock; releaseHandle may have just cleared
            // it via setDeviceHandle(0). Avoids an unnecessary syscall with a stale h.
            if (!m_handle.load(std::memory_order_acquire))
                continue;
            rc = libusb_interrupt_transfer(h, 0x82, buffer, sizeof(buffer), &transferred, kEp02TimeoutMs);
        }
        if (rc == LIBUSB_SUCCESS && transferred > 0) {
            QByteArray data(reinterpret_cast<const char *>(buffer), transferred);
            // Diagnostic: timestamp each KZ batch so on-air vs wire-out timing can be compared
            // when investigating "rhythm wrong at the K4" reports. Enable with
            // QT_LOGGING_RULES="hw.kpodplus.debug=true". The device always sends fixed 32-byte
            // transfers zero-padded after the KZ string (docs/KPodKeyerInterface.pdf
            // §"Endpoint 2 IN"), so transferred==32 is expected every read and is NOT a signal
            // of buffer pressure. The real "FIFO backing up" signature would be the trimmed
            // PAYLOAD length approaching 32 bytes; flag at >=24 to catch trouble early.
            const QByteArray trimmed = KpodPlusUsbWorker::trimEp02Buffer(data);
            const char *fillTag = (trimmed.size() >= 24) ? " [PAYLOAD-NEAR-FULL!]" : "";
            qCDebug(hwKpodPlus).noquote() << "KZ@" << QTime::currentTime().toString("HH:mm:ss.zzz") << "[" << trimmed
                                          << "] (payload=" << trimmed.size() << "B)" << fillTag;
            emit keyerDataReceived(data);
        } else if (rc == LIBUSB_ERROR_TIMEOUT) {
            continue;
        } else if (rc == LIBUSB_ERROR_NO_DEVICE || rc == LIBUSB_ERROR_IO) {
            emit transferError(QString::fromLatin1(libusb_error_name(rc)));
            // Clear handle so we don't keep poking a dead device; the façade
            // will set a fresh handle on the next deviceArrived.
            m_handle.store(nullptr, std::memory_order_release);
        } else {
            // Any other return code (OVERFLOW, BUSY, INTERRUPTED, …) was previously
            // silently dropped. Surface them so a high-speed stress test can catch
            // a real USB stack issue if one ever happens. OVERFLOW specifically means
            // the device tried to send more than 32 bytes in a single transfer and
            // the host truncated — i.e. the smoking gun for "we dropped a KZ batch."
            qCWarning(hwKpodPlus) << "KZ EP02 unexpected rc:" << libusb_error_name(rc) << "transferred=" << transferred;
        }
    }
}
