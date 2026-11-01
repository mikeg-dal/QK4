#ifndef KPODPLUSDEVICE_H
#define KPODPLUSDEVICE_H

#include <QObject>
#include <QString>

class QThread;
namespace CwKeying {
class Link;
}
class KpodPlusUsbWorker;
class KpodPlusEp02Worker;

struct KpodPlusDeviceInfo {
    bool detected = false;
    QString productName;
    QString manufacturer;
    quint16 vendorId = 0;
    quint16 productId = 0;
    QString firmwareVersion;
    QString deviceId;
    quint8 busNumber = 0;
    quint8 deviceAddress = 0;
};

// Thin Qt façade over the libusb worker. All libusb work happens on
// KpodPlusUsbWorker (its own thread) and EP02 reads happen on
// KpodPlusEp02Worker (real-time thread). The façade lives on the main
// thread; every setter is dispatched via QueuedConnection so the main thread
// never touches libusb.
class KpodPlusDevice : public QObject {
    Q_OBJECT

public:
    enum RockerPosition { RockerCenter = 0, RockerRight = 1, RockerLeft = 2 };
    Q_ENUM(RockerPosition)

    explicit KpodPlusDevice(QObject *parent = nullptr);
    ~KpodPlusDevice() override;

    // Cached views of worker state — safe to call from the main thread.
    bool isDetected() const;
    KpodPlusDeviceInfo deviceInfo() const;
    bool isPolling() const;
    RockerPosition rockerPosition() const;

    // Forwarded as queued invocations to the worker thread.
    bool startPolling();
    void stopPolling();
    void setKeyerSpeed(int wpm);
    void setCwPitch(int freqHz);
    void setKeyerParams(int iambicMode, bool paddleReversed);
    void setEncodeMode(int mode);
    void setStuckTimeout(int seconds);

    // Where the KPOD+'s own KZ strings go, straight from the EP02 reader thread; nullptr drops
    // them. Any thread. The link must outlive the EP02 reader's use of it.
    void setKeyerLink(CwKeying::Link *link);

signals:
    void deviceConnected();
    void deviceDisconnected();
    void deviceInfoReady();
    void encoderRotated(int ticks);
    void rockerPositionChanged(RockerPosition position);
    void buttonTapped(int buttonNumber);
    void buttonHeld(int buttonNumber);
    void pollError(const QString &error);

private:
    KpodPlusDeviceInfo m_info;
    RockerPosition m_lastRocker = RockerCenter;
    bool m_polling = false;

    KpodPlusUsbWorker *m_usbWorker = nullptr;
    QThread *m_usbThread = nullptr;

    KpodPlusEp02Worker *m_ep02Worker = nullptr;
    QThread *m_ep02Thread = nullptr;
    // EP02 worker's finished -> deleteLater, cut if the USB thread is disowned (see the destructor).
    QMetaObject::Connection m_ep02DeleteConnection;
};

#endif // KPODPLUSDEVICE_H
