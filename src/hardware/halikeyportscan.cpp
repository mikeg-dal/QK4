#include "hardware/halikeyportscan.h"

#include "logging/logcategories.h"
#include "utils/threadshutdown.h"

#include <QDeadlineTimer>
#include <QThread>

#include <chrono>
#include <memory>

namespace {
// An enumeration cannot be interrupted, so this is how long quitting waits for one: the first
// CoreMIDI enumeration in a process has taken up to 1.9 s.
constexpr std::chrono::milliseconds kStopTimeout{2500};
// finished() is emitted just before the thread exits, so reaping it then waits only for that.
constexpr std::chrono::milliseconds kReapTimeout{500};
} // namespace

HaliKeyPortScan::HaliKeyPortScan(QObject *parent) : QObject(parent) {}

HaliKeyPortScan::~HaliKeyPortScan() {
    // Disowned rather than destroyed if it misses the deadline (ThreadShutdown). The enumerator
    // captures nothing but values and the result is shared, so a disowned scan touches nothing of
    // this object's.
    ThreadShutdown::join(m_thread, QDeadlineTimer(kStopTimeout), lcHardware());
}

void HaliKeyPortScan::request(int deviceType, Enumerator enumerate) {
    if (!m_scanning) {
        start(deviceType, std::move(enumerate));
        return;
    }
    if (deviceType == m_runningType && !m_hasPending)
        return;
    m_hasPending = true;
    m_pendingType = deviceType;
    m_pending = std::move(enumerate);
}

void HaliKeyPortScan::start(int deviceType, Enumerator enumerate) {
    m_scanning = true;
    m_runningType = deviceType;

    // WHY a worker thread: the first CoreMIDI enumeration in a process took 0.8-1.9 s on macOS, and
    // Windows enumerates COM ports through SetupAPI, which is slow with Bluetooth serial ports
    // installed. On the GUI thread either froze the Options dialog the first time the HaliKey page
    // opened. RtMidi's CoreMIDI client is created without a notification callback, so building it on
    // a thread with no run loop loses nothing (the MIDI worker already does the same).
    auto result = std::make_shared<QStringList>();
    m_thread = QThread::create([enumerate = std::move(enumerate), result]() { *result = enumerate(); });
    m_thread->setObjectName(QStringLiteral("HaliKeyPortScan"));
    // Queued back to this object's thread. If this object is gone by then, the destructor has
    // already joined or disowned the thread, and Qt drops the call.
    connect(m_thread, &QThread::finished, this, [this, deviceType, result]() {
        ThreadShutdown::join(m_thread, QDeadlineTimer(kReapTimeout), lcHardware());
        onWorkerDone(deviceType, *result);
    });
    m_thread->start();
}

void HaliKeyPortScan::onWorkerDone(int deviceType, const QStringList &ports) {
    m_scanning = false;
    m_runningType = -1;
    if (m_hasPending) {
        m_hasPending = false;
        Enumerator next = std::move(m_pending);
        m_pending = nullptr;
        start(m_pendingType, std::move(next));
        return;
    }
    emit finished(deviceType, ports);
}

int HaliKeyPortScan::chooseIndex(const QStringList &ports, const QString &savedPort, int deviceType) {
    const int saved = savedPort.isEmpty() ? -1 : static_cast<int>(ports.indexOf(savedPort));
    if (saved >= 0 || deviceType != kMidiType)
        return saved;
    for (int i = 0; i < ports.size(); ++i) {
        if (ports[i].contains(QStringLiteral("HaliKey"), Qt::CaseInsensitive))
            return i;
    }
    return -1;
}
