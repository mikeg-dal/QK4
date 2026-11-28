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
 * @brief Owner of the CW paddle device (HaliKey or ORB). Creates a worker (serial or MIDI — see
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
    // deviceType: a HaliKeyType value. HardwareController passes
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

    // ORB: whether pttJackChanged is wanted now. Off until asked for.
    void setPttJackWanted(bool wanted);

signals:
    void connected();
    void disconnected();
    void connectionError(const QString &error);

    // V1.4 serial: one paddle line changed. See HaliKeyWorkerBase::paddleChanged. Emitted on the
    // worker thread.
    void paddleChanged(bool dah, bool asserted, double deltaTime);

    // ORB only: the PTT/KEY jack closed or opened. Emitted on this object's thread, and only for
    // the port that is open now. No release follows a closePort(): watch disconnected() too.
    void pttJackChanged(bool pressed);

private:
    std::shared_ptr<HaliKeyMidiSink> m_midiSink;
    QThread *m_workerThread = nullptr;
    HaliKeyWorkerBase *m_worker = nullptr;

    QString m_portName;
    bool m_connected = false;
    quint64 m_session = 0; // counts openPort/closePort, to drop queued events from a closed port
    int m_deviceType = 0;  // HaliKeyType. Set via ctor / setDeviceType.
    std::atomic<bool> m_straightKey{false};
    bool m_pttJackWanted = false; // main thread; handed to each new worker
};

#endif // HALIKEYDEVICE_H
