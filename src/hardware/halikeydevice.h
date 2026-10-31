#ifndef HALIKEYDEVICE_H
#define HALIKEYDEVICE_H

#include <QList>
#include <QObject>
#include <QSerialPortInfo>
#include <QString>
#include <QThread>
#include <atomic>
#include <memory>
#include <functional>
#include "vail_cat_routing.h"

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
    enum DeviceType { Serial = 0, Midi = 1, Vail = 2 };

    // deviceType: 0 = V1.4 serial, 1 = HaliKey MIDI, 2 = Vail. HardwareController passes
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

    void setDeviceType(int deviceType);
    int deviceType() const { return m_deviceType; }

    void configureVailKeyer(int wpm, int pitchHz, int mode);
    void setVailKeyingEnabled(bool enabled);
    std::shared_ptr<VailCatRouting::Control> vailRoutingControl() const { return m_vailRouting; }

    void setMidiSink(std::function<void(int, int, int, double)> sink);
    void setStraightKey(bool straight);

    // Available ports
    static QStringList availablePorts();
    static QList<HaliKeyPortInfo> availablePortsDetailed();
    static QStringList availableMidiDevices();

signals:
    void connected();
    void disconnected();
    void connectionError(const QString &error);
    void vailCleanupFailed();

    // V1.4 serial: one paddle line changed. See HaliKeyWorkerBase::paddleChanged. Emitted on the
    // worker thread.
    void paddleChanged(bool dah, bool asserted, double deltaTime);
    void timedKeyingCommand(const QString &command);
    void routedVailKeyingCommand(const QString &command, quint64 epoch);
    void routedVailKeyingStateChanged(bool down, quint64 epoch, qint64 timestampNs);

private:
    void closeWorker(bool cleanupVail);
    void updateVailRouting();
    std::shared_ptr<HaliKeyMidiSink> m_midiSink;
    std::atomic<bool> m_straightKey{false};
    QThread *m_workerThread = nullptr;
    HaliKeyWorkerBase *m_worker = nullptr;

    QString m_portName;
    bool m_connected = false;
    int m_deviceType = Serial;
    int m_vailWpm = 20;
    int m_vailPitchHz = 600;
    int m_vailMode = 0;
    bool m_vailKeyingEnabled = false;
    std::shared_ptr<VailCatRouting::Control> m_vailRouting = std::make_shared<VailCatRouting::Control>();
    // Each worker gets a separate token; only its event loop reads/writes it.
    std::shared_ptr<quint64> m_vailWorkerEpoch;
    bool m_vailWorkerEnabled = false;
    quint64 m_workerGeneration = 0;
};

#endif // HALIKEYDEVICE_H
