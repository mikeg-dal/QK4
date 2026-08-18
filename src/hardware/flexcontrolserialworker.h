#ifndef FLEXCONTROLSERIALWORKER_H
#define FLEXCONTROLSERIALWORKER_H

#include "flexcontroldevice.h" // FlexControlDeviceInfo
#include <QByteArray>
#include <QObject>
#include <QString>

class QSerialPort;
class QTimer;

// Worker that owns the QSerialPort for a FlexRadio FlexControl and runs all
// serial I/O on its own QThread. Modelled on KpodHidWorker's façade/worker
// split.
//
// The FlexControl is a CDC-ACM virtual serial port. It pushes ASCII tokens,
// each terminated by ';':
//   U            one detent clockwise
//   D            one detent counter-clockwise
//   U02 .. U06   multiple detents clockwise (accumulated between USB polls)
//   D02 .. D06   multiple detents counter-clockwise
//   X1S/X1C/X1L  AUX button 1: short tap / double click / long hold
//   X2*, X3*     AUX buttons 2 and 3
//   S/C/L         center knob: short tap / double click / long hold
//   F0304        emitted on device reset (ignored)
//
// Host-to-device LED commands are Ixyz;, where x/y/z select the three AUX
// LEDs. For example, I100; lights only AUX1.
//
// The token decoder is pure and static for unit testing.
class FlexControlSerialWorker : public QObject {
    Q_OBJECT

public:
    static const quint16 VENDOR_ID = 0x2192;
    static const quint16 PRODUCT_ID = 0x0010;

    explicit FlexControlSerialWorker(QObject *parent = nullptr);
    ~FlexControlSerialWorker() override;

    struct Event {
        enum Type { None, Encoder, Button } type = None;
        int ticks = 0;       // Encoder: signed detents
        int buttonNumber = 0; // Button: 0=center knob, 1..3=AUX buttons
        int pressType = 0;    // Button: 0=short 1=double 2=long
    };

    // Decode a single ';'-stripped token. Returns true and fills `out` if the
    // token maps to an event; false for empty/unknown/reset tokens.
    static bool decodeToken(const QByteArray &token, Event &out);
    static QByteArray encodeLedCommand(bool aux1, bool aux2, bool aux3);

public slots:
    void start();
    void shutdown();

    void openDevice();
    void closeDevice();
    void setLeds(bool aux1, bool aux2, bool aux3);

signals:
    void deviceInfoReady(FlexControlDeviceInfo info);
    void deviceArrived();
    void deviceRemoved();
    void encoderRotated(int ticks);
    void buttonPressed(int buttonNumber, int pressType);
    void pollError(QString message);

private slots:
    void onReadyRead();
    void onPresenceTimer();

private:
    FlexControlDeviceInfo detectDeviceInfo();
    bool openHandle();
    void releaseHandle();
    void processBuffer();

    QSerialPort *m_port = nullptr;
    FlexControlDeviceInfo m_info;
    bool m_devicePresent = false;
    QByteArray m_rxBuffer;

    QTimer *m_presenceTimer = nullptr;
    static const int PRESENCE_CHECK_INTERVAL_MS = 2000;
};

#endif // FLEXCONTROLSERIALWORKER_H
