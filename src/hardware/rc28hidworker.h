#ifndef RC28HIDWORKER_H
#define RC28HIDWORKER_H

#include "rc28device.h" // Rc28DeviceInfo
#include <QElapsedTimer>
#include <QObject>
#include <QString>

typedef struct hid_device_ hid_device;

#ifdef Q_OS_LINUX
class KpodUdevWorker;
class QThread;
#endif

class QTimer;

// Worker that owns the hidapi handle for an Icom RC-28 and runs all hid_* I/O on
// its own QThread. Modelled on KpodHidWorker. The RC-28 differs from the K-POD
// in two ways that matter here:
//
//   * It is an interrupt-IN device — it *pushes* 64-byte reports rather than
//     answering a polled request. We simply hid_read() them; no 'u' request
//     byte is written each cycle.
//   * Its buttons have no hardware tap/hold distinction (the report just says
//     "button X is currently down"). Tap vs hold is synthesised here with a
//     hold-duration timer, mirroring the K-POD's tap/hold semantics.
//
// Report layout (reverse-engineered — see gi1mic/rc28_emulator):
//   byte0  report type (0x01 encoder/button, 0x02 firmware version)
//   byte1  encoder acceleration / speed (1..4), valid when byte5 == 0x07
//   byte3  encoder direction: 0x01 = CCW/down, 0x02 = CW/up, 0x00 = stopped
//   byte5  0x07 = encoder frame (no button); else a button code:
//          0x7D = F1, 0x03 = F2, 0x06 = TX
//
// The pure decoder (decodeReport) is exposed for unit tests.
class Rc28HidWorker : public QObject {
    Q_OBJECT

public:
    static const quint16 VENDOR_ID = 0x0C26;
    static const quint16 PRODUCT_ID = 0x001E;

    // Raw byte5 button codes.
    static const unsigned char BTN_NONE = 0x07; // encoder frame / all released
    static const unsigned char BTN_F1 = 0x7D;
    static const unsigned char BTN_F2 = 0x03;
    static const unsigned char BTN_TX = 0x06;

    explicit Rc28HidWorker(QObject *parent = nullptr);
    ~Rc28HidWorker() override;

    // Pure decoder for a single 64-byte report. Returns the raw (stateless)
    // interpretation; tap/hold synthesis happens in onPollTimer using timers.
    struct ReportEvents {
        bool emitEncoder = false;
        int encoderTicks = 0;
        int buttonDown = 0; // 0 = none, else 1=F1 2=F2 3=TX
        bool isVersion = false;
        QString firmwareVersion;
    };
    static ReportEvents decodeReport(const unsigned char *buffer, int len);

public slots:
    void start();    // wired to QThread::started — hid_init + timers + initial detection
    void shutdown(); // BlockingQueued from façade dtor — stops timers, closes handle, hid_exit

    void openDevice();
    void closeDevice();
    void setLeds(bool f1, bool f2, bool tx);

signals:
    void deviceInfoReady(Rc28DeviceInfo info);
    void deviceArrived();
    void deviceRemoved();
    void encoderRotated(int ticks);
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
    void releaseHandle();
    Rc28DeviceInfo detectDeviceInfo();
    void updateButtonState(int buttonDown);

    hid_device *m_hidDevice = nullptr;
    Rc28DeviceInfo m_info;
    bool m_devicePresent = false;

    QTimer *m_pollTimer = nullptr;
    static const int POLL_INTERVAL_MS = 10;

#ifndef Q_OS_LINUX
    QTimer *m_presenceTimer = nullptr;
    static const int PRESENCE_CHECK_INTERVAL_MS = 2000;
#else
    KpodUdevWorker *m_udevWorker = nullptr;
    QThread *m_udevThread = nullptr;
#endif

    // Tap/hold synthesis state.
    int m_pressedButton = 0;
    bool m_holdEmitted = false;
    QElapsedTimer m_pressTimer;
    static const int HOLD_THRESHOLD_MS = 500;

    bool m_hidInitialized = false;
};

#endif // RC28HIDWORKER_H
