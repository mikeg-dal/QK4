#ifndef HALIKEYWORKERBASE_H
#define HALIKEYWORKERBASE_H

#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <atomic>

// Shared logging category for HaliKey device + workers. Definition in halikeydevice.cpp.
Q_DECLARE_LOGGING_CATEGORY(hwHalikey)

/**
 * @brief Base class for HaliKey paddle-input workers. Concrete workers (one per platform) live on
 *        `HalikeyDevice::m_workerThread` and translate hardware events into
 *        one `lineStateChanged(dit, dah, ptt)` signal per sample.
 *
 * WHY three concrete subclasses instead of one:
 *   - `HaliKeyV14Worker`  — native V1.4 hardware protocol (direct serial frames).
 *   - `HaliKeyMidiWorker` — MIDI interface variant (used when the V1.4 firmware exposes MIDI).
 *   - (Linux-only)        — `TIOCMIWAIT` blocking ioctl on the serial FD.
 * Each has a different event model (line-oriented reads, MIDI messages, blocking ioctl) so sharing
 * a single loop is impossible.
 *
 * WHY `prepareShutdown()` exists: the Linux `TIOCMIWAIT`-based worker blocks inside a kernel
 * ioctl and cannot observe `m_running = false` until an edge arrives. `prepareShutdown()` is the
 * hook that variant uses (typically toggling a modem-control line on the same FD) to force the
 * ioctl to return so the thread can exit cleanly. The base no-op is correct for variants whose
 * event loops poll `m_running` naturally.
 */
class HaliKeyWorkerBase : public QObject {
    Q_OBJECT

public:
    explicit HaliKeyWorkerBase(const QString &portName, QObject *parent = nullptr);
    ~HaliKeyWorkerBase() override = default;

    virtual void prepareShutdown() {} // Override for platform-specific unblocking (e.g. Linux TIOCMIWAIT)

public slots:
    virtual void start() = 0; // Called when thread starts
    void stop();              // Sets atomic flag to exit loop

signals:
    // All three input lines as ONE sample, emitted whenever any of them changes.
    //
    // WHY a single signal rather than one per line: iambic decisions are a function of the paddle
    // PAIR, and the V1.4 worker already reads every line in one ioctl/API call. Splitting that
    // sample into separate signals threw the "read together" fact away, and a squeeze release then
    // reached the keyer as two events — if the element timer fired between them one lever still
    // read down, and an element nobody keyed was appended. Emitting the sample intact keeps the
    // pair consistent all the way to the keyer.
    //
    // The MIDI variant cannot produce a true joint sample (each lever is its own note message), so
    // it carries its last-known values for the lines that did not change. That still gives the
    // keyer a coherent pair and one uniform interface for both transports.
    void lineStateChanged(bool dit, bool dah, bool ptt);
    void errorOccurred(const QString &error);
    void portOpened();

protected:
    QString m_portName;
    std::atomic<bool> m_running{false};
};

#endif // HALIKEYWORKERBASE_H
