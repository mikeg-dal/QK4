#ifndef FLEXCONTROLDEVICE_H
#define FLEXCONTROLDEVICE_H

#include <QObject>
#include <QString>

class QThread;
class FlexControlSerialWorker;

struct FlexControlDeviceInfo {
    bool detected = false;
    QString productName;
    QString manufacturer;
    quint16 vendorId = 0;
    quint16 productId = 0;
    QString portName;
    QString firmwareVersion;
};

// Thin Qt façade over the FlexRadio FlexControl serial worker. Modelled on
// KpodDevice: all QSerialPort I/O lives on the worker thread; the façade owns
// the thread, re-emits worker signals, and dispatches setters via
// Qt::QueuedConnection.
//
// The FlexControl is a USB CDC-ACM device (VID 0x2192, PID 0x0010) that appears
// as a virtual serial port. It streams ASCII, ';'-terminated tokens: U/D (plus
// optional multi-tick count) for the tuning knob, and X<n>{S,C,L} for its three
// AUX buttons (short / double-click / long-hold). It has no rocker switch — the
// "which VFO does the knob tune" state is synthesised in HardwareController.
//
// pressType: 0 = short tap, 1 = double click, 2 = long hold.
class FlexControlDevice : public QObject {
    Q_OBJECT

public:
    enum PressType { PressShort = 0, PressDouble = 1, PressLong = 2 };
    Q_ENUM(PressType)

    explicit FlexControlDevice(QObject *parent = nullptr);
    ~FlexControlDevice() override;

    // Cached views — safe to call from the main thread.
    bool isDetected() const;
    FlexControlDeviceInfo deviceInfo() const;
    bool isPolling() const;

    // Forwarded as queued invocations to the worker thread.
    bool startPolling();
    void stopPolling();

signals:
    void deviceConnected();
    void deviceDisconnected();
    void deviceInfoReady();
    void encoderRotated(int ticks);
    void buttonPressed(int buttonNumber, int pressType);
    void pollError(const QString &error);

private:
    FlexControlDeviceInfo m_info;
    bool m_polling = false;

    FlexControlSerialWorker *m_worker = nullptr;
    QThread *m_thread = nullptr;
};

#endif // FLEXCONTROLDEVICE_H
