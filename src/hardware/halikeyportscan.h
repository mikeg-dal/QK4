#ifndef HALIKEYPORTSCAN_H
#define HALIKEYPORTSCAN_H

#include <QObject>
#include <QStringList>
#include <functional>

class QThread;

/**
 * @brief Lists the ports a HaliKey can be on without blocking the GUI thread.
 *
 * Enumeration runs on a short-lived worker thread and the result comes back as a queued
 * finished() on the thread this object lives on. The enumerator only returns names; nothing in it
 * may touch a widget.
 *
 * One scan at a time. A request for the device type already being scanned is absorbed by the scan
 * in flight; a request for a different type replaces it, and the outdated result is dropped rather
 * than delivered.
 *
 * The scan thread belongs to this object: it is reaped when it finishes and joined, with a
 * deadline, when this object is destroyed, so quitting mid-scan never leaves it running unowned.
 */
class HaliKeyPortScan : public QObject {
    Q_OBJECT

public:
    using Enumerator = std::function<QStringList()>;

    explicit HaliKeyPortScan(QObject *parent = nullptr);
    ~HaliKeyPortScan() override;

    void request(int deviceType, Enumerator enumerate);
    bool isScanning() const { return m_scanning; }

    /// The row to select: the saved port if listed, else for MIDI the first port named HaliKey,
    /// else -1 (leave the list's own default).
    static int chooseIndex(const QStringList &ports, const QString &savedPort, int deviceType);

    static constexpr int kSerialType = 0;
    static constexpr int kMidiType = 1;

signals:
    void finished(int deviceType, const QStringList &ports);

private:
    void start(int deviceType, Enumerator enumerate);
    void onWorkerDone(int deviceType, const QStringList &ports);

    bool m_scanning = false;
    int m_runningType = -1;
    bool m_hasPending = false;
    int m_pendingType = -1;
    Enumerator m_pending;
    QThread *m_thread = nullptr; // the scan in flight; nullptr between scans
};

#endif // HALIKEYPORTSCAN_H
