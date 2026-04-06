#ifndef HALIKEYDEVICE_H
#define HALIKEYDEVICE_H

#include <QList>
#include <QObject>
#include <QSerialPortInfo>
#include <QString>
#include <QThread>
#include <QTimer>
#include <atomic>

class HaliKeyWorkerBase;

struct HaliKeyPortInfo {
    QString portName;
};

class HalikeyDevice : public QObject {
    Q_OBJECT

public:
    explicit HalikeyDevice(QObject *parent = nullptr);
    ~HalikeyDevice();

    // Port management
    bool openPort(const QString &portName);
    void closePort();
    bool isConnected() const;
    QString portName() const;

    // Available ports
    static QStringList availablePorts();
    static QList<HaliKeyPortInfo> availablePortsDetailed();
    static QStringList availableMidiDevices();

signals:
    void connected();
    void disconnected();
    void connectionError(const QString &error);

    // Paddle state changes (debounced)
    void ditStateChanged(bool pressed);
    void dahStateChanged(bool pressed);
    void pttStateChanged(bool pressed);

private:
    void onRawDit(bool pressed);
    void onRawDah(bool pressed);
    void onRawPtt(bool pressed);

    QThread *m_workerThread = nullptr;
    HaliKeyWorkerBase *m_worker = nullptr;

    QString m_portName;
    bool m_connected = false;

    // Raw state from worker (updated on RtMidi OS callback thread, read from main thread)
    std::atomic<bool> m_rawDitState{false};
    std::atomic<bool> m_rawDahState{false};
    std::atomic<bool> m_rawPttState{false};

    // Confirmed state (after debounce, what we've emitted)
    std::atomic<bool> m_confirmedDitState{false};
    std::atomic<bool> m_confirmedDahState{false};
    std::atomic<bool> m_confirmedPttState{false};

    // Debounce timers — emit ON immediately, delay OFF by 3ms to absorb bounce
    QTimer *m_ditDebounceTimer = nullptr;
    QTimer *m_dahDebounceTimer = nullptr;
    QTimer *m_pttDebounceTimer = nullptr;
    static constexpr int DEBOUNCE_MS = 3;
};

#endif // HALIKEYDEVICE_H
