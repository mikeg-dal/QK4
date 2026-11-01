#ifndef HALIKEYDEVICE_H
#define HALIKEYDEVICE_H

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>
#include <atomic>
#include <functional>
#include <memory>

class HaliKeyWorkerBase;
struct HaliKeyMidiSink;

struct HaliKeyPortInfo {
    QString portName;
};

/**
 * @brief Owner of the HaliKey CW paddle device. Creates a worker (V1.4 serial or MIDI — see
 *        HaliKeyWorkerBase) on `m_workerThread` and passes its paddle input straight through.
 *        What a paddle MEANS is the caller's business — see CwController.
 *
 * Neither path is debounced or queued here, because the keyer timestamps each edge on arrival:
 *   - V1.4 serial: the worker polls the lines every 1 ms through SerialLinePoller (rest state at
 *     open, 20 ms debounce for a straight key only) and emits paddleChanged on its own thread.
 *   - MIDI: raw messages reach the sink set by setMidiSink() on RtMidi's thread. There is no
 *     electrical bounce in a MIDI message stream.
 */
class HalikeyDevice : public QObject {
    Q_OBJECT

public:
    // deviceType: 0 = V1.4 serial, 1 = MIDI. HardwareController passes
    // the current RadioSettings value at construction and re-sets it
    // via setDeviceType() when settings change — this device class
    // never reads RadioSettings itself, keeping the hardware layer
    // decoupled from the settings singleton.
    explicit HalikeyDevice(int deviceType, QObject *parent = nullptr);
    ~HalikeyDevice();

    // Port management
    bool openPort(const QString &portName);
    void closePort();
    bool isConnected() const;
    QString portName() const;

    // Updates the worker variant used on the next openPort(). Does not
    // re-open an in-flight connection — callers that want a live switch
    // should closePort() + openPort() themselves.
    void setDeviceType(int deviceType) { m_deviceType = deviceType; }

    // Available ports
    static QStringList availablePorts();
    static QList<HaliKeyPortInfo> availablePortsDetailed();
    static QStringList availableMidiDevices();

    // Raw MIDI messages, delivered on RtMidi's thread as they arrive (MIDI device type only).
    // CwController points this at the keyer. Thread-safe; may be set before or after openPort().
    void setMidiSink(std::function<void(int status, int note, int velocity, double deltaTime)> sink);

    // Straight key / external keyer on the dit line (serial: 20 ms debounce, dah line ignored).
    void setStraightKey(bool straight);

signals:
    void connected();
    void disconnected();
    void connectionError(const QString &error);

    // V1.4 serial: one paddle line changed. See HaliKeyWorkerBase::paddleChanged. Emitted on the
    // worker thread.
    void paddleChanged(bool dah, bool asserted, double deltaTime);

private:
    std::shared_ptr<HaliKeyMidiSink> m_midiSink;
    QThread *m_workerThread = nullptr;
    HaliKeyWorkerBase *m_worker = nullptr;

    QString m_portName;
    bool m_connected = false;
    int m_deviceType = 0; // 0 = V1.4 serial, 1 = MIDI. Set via ctor / setDeviceType.
    std::atomic<bool> m_straightKey{false};
};

#endif // HALIKEYDEVICE_H
