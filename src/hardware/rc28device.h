#ifndef RC28DEVICE_H
#define RC28DEVICE_H

#include <QObject>
#include <QString>

class QThread;
class Rc28HidWorker;

struct Rc28DeviceInfo {
    bool detected = false;
    QString productName;
    QString manufacturer;
    quint16 vendorId = 0;
    quint16 productId = 0;
    QString devicePath;
    QString firmwareVersion;
};

// Thin Qt façade over the Icom RC-28 hidapi worker. Modelled directly on
// KpodDevice: all hid_* I/O lives on the worker thread, the façade owns the
// worker thread, re-emits worker signals, and dispatches setters via
// Qt::QueuedConnection. The main thread never touches hidapi.
//
// The RC-28 is a Raw-HID device (VID 0x0C26, PID 0x001E) with a tuning
// encoder and three buttons (F1, F2, TX) each backed by an LED. Unlike the
// K-POD it has no rocker switch — the "which VFO does the knob tune" state is
// synthesised in HardwareController and reflected back onto the three LEDs via
// setLeds(). Button numbers: 1 = F1, 2 = F2, 3 = TX.
class Rc28Device : public QObject {
    Q_OBJECT

public:
    enum Button { ButtonF1 = 1, ButtonF2 = 2, ButtonTx = 3 };
    Q_ENUM(Button)

    explicit Rc28Device(QObject *parent = nullptr);
    ~Rc28Device() override;

    // Cached views — safe to call from the main thread.
    bool isDetected() const;
    Rc28DeviceInfo deviceInfo() const;
    bool isPolling() const;

    // Forwarded as queued invocations to the worker thread.
    bool startPolling();
    void stopPolling();

    // Best-effort LED control (F1 / F2 / TX indicator LEDs). The RC-28 LED
    // report layout is reverse-engineered and treated as non-essential; a
    // wrong guess is harmless (tuning still works).
    void setLeds(bool f1, bool f2, bool tx);

signals:
    void deviceConnected();
    void deviceDisconnected();
    void deviceInfoReady();
    void encoderRotated(int ticks);
    void buttonTapped(int buttonNumber);
    void buttonHeld(int buttonNumber);
    void pollError(const QString &error);

private:
    Rc28DeviceInfo m_info;
    bool m_polling = false;

    Rc28HidWorker *m_hidWorker = nullptr;
    QThread *m_hidThread = nullptr;
};

#endif // RC28DEVICE_H
