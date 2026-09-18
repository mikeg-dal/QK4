#ifndef KPODHIDWORKER_H
#define KPODHIDWORKER_H

#include "kpoddevice.h" // KpodDeviceInfo
#include <QObject>
#include <QString>

typedef struct hid_device_ hid_device;

#ifdef Q_OS_LINUX
class KpodUdevWorker;
class QThread;
#endif

class QTimer;

// Worker that owns the hidapi handle and runs all hid_* I/O on its own QThread.
// Mirrors the KpodPlusUsbWorker pattern: detection, polling, hotplug monitoring all on
// the worker thread; the main thread never touches hidapi. Public slots dispatched from
// the façade via Qt::QueuedConnection.
//
// Pure helpers (decodeResponse + the rocker/button state machine) are exposed so the
// unit tests can exercise the protocol parsing without a real device.
class KpodHidWorker : public QObject {
    Q_OBJECT

public:
    static const quint16 VENDOR_ID = 0x04D8;
    static const quint16 PRODUCT_ID = 0xF12D;

    explicit KpodHidWorker(QObject *parent = nullptr);
    ~KpodHidWorker() override;

    // Pure protocol decoder. Returns the set of events the response would trigger.
    // Exposed for unit tests; production code calls it from onPollTimer.
    struct ResponseEvents {
        bool emitEncoder = false;
        int encoderTicks = 0;
        bool emitButtonTap = false;
        bool emitButtonHold = false;
        int buttonNum = 0;
        bool emitRocker = false;
        int rockerPosition = -1;
    };
    ResponseEvents decodeResponse(const unsigned char buffer[8]);
    void resetDecoderState(); // test seam

public slots:
    void start();    // wired to QThread::started — hid_init + timers + initial detection
    void shutdown(); // BlockingQueued from façade dtor — stops timers, closes handle, hid_exit

    void openDevice();
    void closeDevice();

signals:
    void deviceInfoReady(KpodDeviceInfo info);
    void deviceArrived();
    void deviceRemoved();
    void encoderRotated(int ticks);
    void rockerPositionChanged(int position);
    void buttonTapped(int buttonNumber);
    void buttonHeld(int buttonNumber);
    void pollError(QString message);

private slots:
    void onPollTimer();
    void onPresenceTimer();
    void onDeviceArrivedFromHotplug();
    void onDeviceRemovedFromHotplug();

private:
    bool openHandle();
    /// Report the device lost, once, and leave it recoverable.
    ///
    /// Idempotent: one unplug reaches here twice (the 20 ms poll fails, then the 2 s presence timer
    /// notices), and the second must be silent. Clears m_devicePresent so the presence timer can
    /// re-detect a device that is still enumerated after a transient — without that, a sleep/wake
    /// left the knob dead until it was physically unplugged (USB-005).
    void handleLostDevice(const char *reason);

    void releaseHandle();
    KpodDeviceInfo detectDeviceInfo();

    hid_device *m_hidDevice = nullptr;
    KpodDeviceInfo m_info;
    bool m_devicePresent = false;

    QTimer *m_pollTimer = nullptr;
    static const int POLL_INTERVAL_MS = 20;

#ifndef Q_OS_LINUX
    QTimer *m_presenceTimer = nullptr;
    static const int PRESENCE_CHECK_INTERVAL_MS = 2000;
#else
    KpodUdevWorker *m_udevWorker = nullptr;
    QThread *m_udevThread = nullptr;
#endif

    int m_lastRockerPosition = 0;
    quint8 m_lastButtonState = 0;
    bool m_holdEmitted = false;

    bool m_hidInitialized = false;
};

#endif // KPODHIDWORKER_H
