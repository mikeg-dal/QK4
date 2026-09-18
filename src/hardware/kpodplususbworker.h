#ifndef KPODPLUSUSBWORKER_H
#define KPODPLUSUSBWORKER_H

#include "kpodplusdevice.h" // KpodPlusDeviceInfo struct
#include <QByteArray>
#include <QObject>
#include <QString>
#include <atomic>
#include <mutex>

struct libusb_context;
struct libusb_device_handle;
class QTimer;

// Worker that owns the libusb context and handle and runs all libusb I/O on
// its own QThread. Public open/close and setter slots are dispatched from the
// façade via Qt::QueuedConnection so the main thread never touches libusb.
//
// Pure helpers (buildXxxCmd, trimEp02Buffer, decodeResponse) are exposed so
// the unit tests can exercise the parsing + state machine without a real USB
// device.
class KpodPlusUsbWorker : public QObject {
    Q_OBJECT

public:
    static const quint16 VENDOR_ID = 0x04D8;
    static const quint16 PRODUCT_ID_KPOD = 0xF12D;
    static const quint16 PRODUCT_ID_ELECRAFT = 0xEFA5;
    static const quint8 VENDOR_INTERFACE_CLASS = 255;

    explicit KpodPlusUsbWorker(QObject *parent = nullptr);
    ~KpodPlusUsbWorker() override;

    // --- Pure helpers (no libusb / no QObject state) -------------------------
    static void buildKeyerSpeedCmd(int wpm, unsigned char out[8]);
    static void buildCwPitchCmd(int freqHz, unsigned char out[8]);
    static void buildKeyerParamsCmd(int iambicMode, bool reversed, unsigned char out[8]);
    static void buildEncodeModeCmd(int mode, unsigned char out[8]);
    static void buildStuckTimeoutCmd(int seconds, unsigned char out[8]);
    static QByteArray trimEp02Buffer(const QByteArray &raw);

    // EP01 response decoder result. The worker turns these flags into Qt
    // signals; tests inspect them directly.
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

    // Device-handle accessor — consumed by KpodPlusEp02Worker once open
    // succeeds (handle is set via a queued slot, never read concurrently).
    libusb_device_handle *deviceHandle() const { return m_handle; }

public slots:
    void start();    // wired to QThread::started; libusb_init + timer setup
    void shutdown(); // BlockingQueuedConnection from façade dtor

    void openDevice();
    void closeDevice();

    void setKeyerSpeed(int wpm);
    void setCwPitch(int freqHz);
    void setKeyerParams(int iambicMode, bool paddleReversed);
    void setEncodeMode(int mode);
    void setStuckTimeout(int seconds);

    // Called when any libusb path detects the device handle has gone bad
    // (LIBUSB_ERROR_NO_DEVICE / LIBUSB_ERROR_IO) — from sendEp01Out,
    // readEp01In, or EP02 worker's transferError signal (queued).
    // Schedules closeDevice and resets m_devicePresent so the presence
    // timer re-opens the device on its next tick. Without this, a brief
    // USB transient closes the handle but leaves m_devicePresent stuck
    // true, so re-detection is never triggered and the keyer stays dead.
    void handleLostDevice(QString reason);

signals:
    void deviceInfoReady(KpodPlusDeviceInfo info);
    void deviceArrived();
    void deviceRemoved();
    void encoderRotated(int ticks);
    void rockerPositionChanged(int position);
    void buttonTapped(int n);
    void buttonHeld(int n);
    void pollError(QString message);

    // Emitted right after openHandle() succeeds. The façade re-emits to the
    // EP02 worker (queued) so the reader can pick up the new handle.
    void handleOpened(quintptr handle);
    void handleClosing();

private slots:
    void onPollTimer();
    void onPresenceTimer();

private:
    bool sendEp01Out(const unsigned char data[8]);
    bool readEp01In(unsigned char buffer[8], int timeoutMs);
    bool sendConfigCommand(const unsigned char cmd[8]);
    bool detectDeviceLocation(KpodPlusDeviceInfo *info);
    bool queryOpenDeviceInfo(KpodPlusDeviceInfo *info);
    bool openHandle();
    void releaseHandle();

    /// Read EP02 dry and throw the result away, before the handle reaches the EP02 reader.
    ///
    /// The device keeps running its keyer while QK4 has it closed, and a USB interrupt-IN endpoint
    /// holds what it produced until the host asks. Without this, reopening delivered a whole
    /// backlog at once and the radio transmitted it — reproduced on the bench 2026-09-18. Elements
    /// keyed while QK4 was deliberately not listening are stale; silence is the safe failure.
    void discardBufferedKeying();

public:
    // The façade wires this to the EP02 worker's transfer mutex so that
    // releaseHandle() can wait for any in-flight EP02 read to finish before
    // libusb_close frees the handle out from under it. Non-owning pointer;
    // lifetime is owned by the EP02 worker.
    void setEp02TransferMutex(std::mutex *m) { m_ep02TransferMutex = m; }

private:
    std::mutex *m_ep02TransferMutex = nullptr;

    libusb_context *m_ctx = nullptr;
    libusb_device_handle *m_handle = nullptr;
    bool m_interfaceClaimed = false;
    bool m_devicePresent = false;
    KpodPlusDeviceInfo m_info;

    QTimer *m_pollTimer = nullptr;
    QTimer *m_presenceTimer = nullptr;
    static const int POLL_INTERVAL_MS = 20;
    static const int PRESENCE_CHECK_INTERVAL_MS = 2000;

    int m_lastRockerPosition = 0;
    quint8 m_lastButtonState = 0;
    bool m_holdEmitted = false;
};

// Reads EP02 IN with a blocking sync interrupt transfer. Lives on its own
// HighPriority thread so KZ stream timing is not affected by the GUI or the
// EP01 worker. Emits keyerDataReceived(QByteArray) — wired by the façade
// directly to TcpClient on the I/O thread (queued), so the main thread is
// never on the hot path.
//
// THREADING CONSTRAINT: run() is a blocking synchronous loop that NEVER
// returns to the Qt event loop. As a result, every connect() targeting this
// QObject MUST use Qt::DirectConnection — queued events posted to it will
// sit unhandled forever. setDeviceHandle is annotated as a slot for symmetry
// but is invoked via direct method call from the USB worker, not via a
// queued signal. Adding a new signal or slot to this class without using
// DirectConnection is silently broken (Qt will not warn).
class KpodPlusEp02Worker : public QObject {
    Q_OBJECT

public:
    explicit KpodPlusEp02Worker(QObject *parent = nullptr);

    void requestStop();

    // Held by run() across each libusb_interrupt_transfer call. KpodPlusUsbWorker
    // acquires this in releaseHandle() before libusb_close, so the close cannot
    // race with an in-flight EP02 transfer holding a stale handle pointer. The
    // mutex bounds the wait by the EP02 transfer timeout (100 ms max).
    std::mutex *transferMutex() { return &m_transferMutex; }

public slots:
    void run();
    void setDeviceHandle(quintptr handle); // 0 = clear

signals:
    void keyerDataReceived(QByteArray data);
    void transferError(QString message);

private:
    std::atomic<libusb_device_handle *> m_handle{nullptr};
    std::atomic<bool> m_running{false};
    std::mutex m_transferMutex;
};

#endif // KPODPLUSUSBWORKER_H
