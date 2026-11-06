#include "kpodplusdevice.h"
#include "logging/logcategories.h"
#include "logging/logsink.h"
#include "kpodplususbworker.h"
#include "utils/threadshutdown.h"
#include <QLoggingCategory>
#include <QThread>
#include <chrono>

namespace {
// WHY 1 s for USB: shutdown() is queued behind at most one EP01 poll (up to 55 ms) or one config
// command (up to 400 ms), then releases the handle.
constexpr std::chrono::milliseconds kUsbStopTimeout{1000};
// WHY 500 ms for EP02: requestStop() wakes an idle reader at once, and a reader inside a transfer
// sees it within one EP02 timeout (100 ms).
constexpr std::chrono::milliseconds kEp02StopTimeout{500};
} // namespace

KpodPlusDevice::KpodPlusDevice(QObject *parent) : QObject(parent) {
    // --- EP02 reader thread (promotes itself to real-time in run()) -----------
    m_ep02Thread = new QThread(this);
    m_ep02Thread->setObjectName("KpodPlusEP02");
    Logging::markRealtimeOnStart(m_ep02Thread);
    m_ep02Worker = new KpodPlusEp02Worker();
    m_ep02Worker->moveToThread(m_ep02Thread);
    connect(m_ep02Thread, &QThread::started, m_ep02Worker, &KpodPlusEp02Worker::run);
    m_ep02DeleteConnection = connect(m_ep02Thread, &QThread::finished, m_ep02Worker, &QObject::deleteLater);
    m_ep02Thread->start();

    // --- USB worker thread (default priority) --------------------------------
    m_usbThread = new QThread(this);
    m_usbThread->setObjectName("KpodPlusUSB");
    m_usbWorker = new KpodPlusUsbWorker();
    m_usbWorker->moveToThread(m_usbThread);
    // Cross-link: the USB worker's releaseHandle() acquires the EP02 worker's transfer
    // mutex before libusb_close so the close cannot race with an in-flight EP02 transfer.
    // Non-owning pointer, so the EP02 worker MUST outlive the USB worker's last use of it —
    // which is why the destructor shuts the USB worker down FIRST and only then stops the EP02
    // thread. Reversing that frees the mutex and then locks it.
    m_usbWorker->setEp02TransferMutex(m_ep02Worker->transferMutex());
    connect(m_usbThread, &QThread::started, m_usbWorker, &KpodPlusUsbWorker::start);
    connect(m_usbThread, &QThread::finished, m_usbWorker, &QObject::deleteLater);

    // Cache + re-emit worker signals.
    connect(m_usbWorker, &KpodPlusUsbWorker::deviceInfoReady, this, [this](KpodPlusDeviceInfo info) {
        m_info = info;
        qCInfo(lcKpodPlus) << "KPOD+ detected:" << m_info.detected << "- this is the point the CW gate keys off";
        emit deviceInfoReady();
    });
    connect(m_usbWorker, &KpodPlusUsbWorker::deviceArrived, this, [this]() {
        m_polling = true;
        qCInfo(lcKpodPlus) << "KPOD+ arrived - polling, and it now owns CW keying";
        emit deviceConnected();
    });
    connect(m_usbWorker, &KpodPlusUsbWorker::deviceRemoved, this, [this]() {
        m_polling = false;
        qCInfo(lcKpodPlus) << "KPOD+ removed - CW keying returns to QK4's own keyer";
        emit deviceDisconnected();
    });
    connect(m_usbWorker, &KpodPlusUsbWorker::encoderRotated, this, &KpodPlusDevice::encoderRotated);
    connect(m_usbWorker, &KpodPlusUsbWorker::rockerPositionChanged, this, [this](int position) {
        m_lastRocker = static_cast<RockerPosition>(position);
        emit rockerPositionChanged(m_lastRocker);
    });
    connect(m_usbWorker, &KpodPlusUsbWorker::buttonTapped, this, &KpodPlusDevice::buttonTapped);
    connect(m_usbWorker, &KpodPlusUsbWorker::buttonHeld, this, &KpodPlusDevice::buttonHeld);
    connect(m_usbWorker, &KpodPlusUsbWorker::pollError, this, &KpodPlusDevice::pollError);

    // Route the live libusb_device_handle from the USB worker into the EP02
    // reader. setDeviceHandle stores m_ep02Worker.m_handle and wakes the reader if
    // it is idle; safe to invoke from any thread, so DirectConnection is intentional.
    // The handlers capture the EP02 worker, not this: a USB thread disowned at shutdown can still
    // emit after this object is gone.
    //
    // WHY not QueuedConnection: m_ep02Worker::run() is a blocking sync-read
    // loop that never returns to its thread's event loop, so queued events to
    // m_ep02Worker would never be dispatched — the handle would stay null and
    // no keyer data would ever flow.
    connect(m_usbWorker, &KpodPlusUsbWorker::handleOpened, m_ep02Worker, &KpodPlusEp02Worker::setDeviceHandle,
            Qt::DirectConnection);
    connect(
        m_usbWorker, &KpodPlusUsbWorker::handleClosing, m_ep02Worker,
        [ep02 = m_ep02Worker]() { ep02->setDeviceHandle(0); }, Qt::DirectConnection);

    // EP02 read errors (NO_DEVICE / IO) on the EP02 thread propagate to the
    // USB worker so the device is closed and re-detected. Without this, an
    // EP02-only transient (e.g. one-endpoint glitch) would silently kill KZ
    // data forever while EP01 polling kept ticking — the user-visible
    // "paddles stopped registering after a while" symptom.
    connect(m_ep02Worker, &KpodPlusEp02Worker::transferError, m_usbWorker, &KpodPlusUsbWorker::handleLostDevice,
            Qt::QueuedConnection);

    m_usbThread->start();
}

KpodPlusDevice::~KpodPlusDevice() {
    // ORDER IS LOAD-BEARING: the USB worker goes first, while the EP02 worker still exists.
    //
    // shutdown() reaches releaseHandle(), which locks the EP02 worker's transfer mutex before
    // libusb_close so the close cannot race an in-flight transfer. That mutex belongs to the EP02
    // worker, and the worker is deleted by the deleteLater wired to its thread's finished signal.
    // Stopping EP02 first therefore destroyed the mutex and then locked it — a use-after-free on
    // every quit where the handle was still open, which is any quit whose queued closeDevice was
    // still sitting behind a poll (up to 55 ms) or a config command (up to 400 ms).
    //
    // Draining libusb first is also correct on its own terms: it clears EP02's handle
    // synchronously via handleClosing, so the reader sees null and skips its next transfer.
    const QDeadlineTimer usbDeadline(kUsbStopTimeout);
    if (KpodPlusUsbWorker *usb = m_usbWorker) {
        ThreadShutdown::callBlocking(usb, [usb]() { usb->shutdown(); }, usbDeadline);
    }
    const bool usbStopped =
        ThreadShutdown::retire(m_usbThread, usbDeadline, lcKpodPlus()) == ThreadShutdown::Outcome::Joined;
    m_usbWorker = nullptr;

    // WHY the EP02 worker is kept alive when the USB thread was not stopped: that thread still holds
    // a pointer to the EP02 worker's transfer mutex and may yet lock it in releaseHandle(). Freeing
    // the worker would be the same use-after-free the order above exists to prevent, so it is
    // leaked with the disowned thread instead of being deleted when its own thread finishes.
    if (!usbStopped) {
        disconnect(m_ep02DeleteConnection);
    }

    // Then stop the EP02 reader. requestStop sets the flag the read loop checks and wakes it if it
    // is idle, so this returns at once without a device and within one EP02 timeout (100 ms) with.
    if (m_ep02Worker) {
        m_ep02Worker->requestStop();
    }
    ThreadShutdown::retire(m_ep02Thread, QDeadlineTimer(kEp02StopTimeout), lcKpodPlus());
    m_ep02Worker = nullptr;
}

bool KpodPlusDevice::isDetected() const {
    return m_info.detected;
}

KpodPlusDeviceInfo KpodPlusDevice::deviceInfo() const {
    return m_info;
}

bool KpodPlusDevice::isPolling() const {
    return m_polling;
}

KpodPlusDevice::RockerPosition KpodPlusDevice::rockerPosition() const {
    return m_lastRocker;
}

bool KpodPlusDevice::startPolling() {
    if (m_polling)
        return true;
    qCDebug(lcKpodPlus) << "KPOD+ startPolling requested (detected=" << m_info.detected << ")";
    QMetaObject::invokeMethod(m_usbWorker, "openDevice", Qt::QueuedConnection);
    // Result is asynchronous; isPolling() becomes true on deviceArrived.
    return m_info.detected;
}

void KpodPlusDevice::stopPolling() {
    qCDebug(lcKpodPlus) << "KPOD+ stopPolling requested";
    QMetaObject::invokeMethod(m_usbWorker, "closeDevice", Qt::QueuedConnection);
}

void KpodPlusDevice::setKeyerSpeed(int wpm) {
    QMetaObject::invokeMethod(m_usbWorker, "setKeyerSpeed", Qt::QueuedConnection, Q_ARG(int, wpm));
}

void KpodPlusDevice::setCwPitch(int freqHz) {
    QMetaObject::invokeMethod(m_usbWorker, "setCwPitch", Qt::QueuedConnection, Q_ARG(int, freqHz));
}

void KpodPlusDevice::setKeyerParams(int iambicMode, bool paddleReversed) {
    QMetaObject::invokeMethod(m_usbWorker, "setKeyerParams", Qt::QueuedConnection, Q_ARG(int, iambicMode),
                              Q_ARG(bool, paddleReversed));
}

void KpodPlusDevice::setEncodeMode(int mode) {
    QMetaObject::invokeMethod(m_usbWorker, "setEncodeMode", Qt::QueuedConnection, Q_ARG(int, mode));
}

void KpodPlusDevice::setStuckTimeout(int seconds) {
    QMetaObject::invokeMethod(m_usbWorker, "setStuckTimeout", Qt::QueuedConnection, Q_ARG(int, seconds));
}

void KpodPlusDevice::setKeyerLink(CwKeying::Link *link) {
    m_ep02Worker->setKeyerLink(link);
}
