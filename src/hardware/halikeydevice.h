#ifndef HALIKEYDEVICE_H
#define HALIKEYDEVICE_H

#include <QList>
#include <QObject>
#include <QSerialPortInfo>
#include <QString>
#include <QThread>
#include <atomic>

class HaliKeyWorkerBase;

struct HaliKeyPortInfo {
    QString portName;
};

/**
 * @brief Owner of the HaliKey CW paddle device. Creates a worker (V1.4 / MIDI / Linux TIOCMIWAIT
 *        variant — see HaliKeyWorkerBase) on `m_workerThread` and exposes paddle +
 *        line-state signals to the rest of the app. What each line MEANS is the caller's
 *        business — see CwController; this class only debounces and forwards.
 *
 * Debounce responsibility lives in the worker:
 *   - V1.4 serial worker confirms each transition across ≥2 reads before emitting. The read
 *     cadence is platform-specific: blocking TIOCMIWAIT plus one 500 µs confirming re-read on
 *     Linux, a 1 ms high-resolution-timer poll of GetCommModemStatus on Windows, and a 2 kHz
 *     (500 µs) usleep poll on macOS. That count-based filter is the only contact-bounce
 *     defense for the serial path.
 *   - MIDI worker emits firmware-debounced Note On/Off events as-is. There is no
 *     electrical bounce in a MIDI message stream.
 *
 * HalikeyDevice itself only performs same-direction dedupe (`acceptEdge` in the .cpp) as a
 * zero-cost defense-in-depth check — it can never discard a real transition. Earlier
 * revisions ran a 3 ms processing-time gate here, which silently dropped real MIDI releases
 * on Windows when WinMM delivered press+release in a single burst. See
 * docs/halikey-midi-windows-debounce-bug.md for the full diagnosis.
 *
 * Thread note: confirmed-state atomics are written from the worker callback thread.
 * Signals are emitted from the same thread that called onRaw*.
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
    int deviceType() const { return m_deviceType; }

    // Available ports
    static QStringList availablePorts();
    static QList<HaliKeyPortInfo> availablePortsDetailed();
    static QStringList availableMidiDevices();

signals:
    void connected();
    void disconnected();
    void connectionError(const QString &error);

    // Debounced input lines, emitted as one sample whenever any of them changes. See
    // HaliKeyWorkerBase::lineStateChanged for why the three travel together.
    void lineStateChanged(bool dit, bool dah, bool ptt);

private:
    void onRawLines(bool dit, bool dah, bool ptt);

    QThread *m_workerThread = nullptr;
    HaliKeyWorkerBase *m_worker = nullptr;

    QString m_portName;
    bool m_connected = false;
    int m_deviceType = 0; // 0 = V1.4 serial, 1 = MIDI. Set via ctor / setDeviceType.

    // Confirmed paddle state — written from whichever worker thread delivered the raw event
    // (RtMidi callback on the MIDI variant, monitor thread on the V1.4 variant). Same-direction
    // dedupe in acceptEdge() reads/writes these to drop redundant repeats.
    std::atomic<bool> m_confirmedDitState{false};
    std::atomic<bool> m_confirmedDahState{false};
    std::atomic<bool> m_confirmedPttState{false};
};

#endif // HALIKEYDEVICE_H
