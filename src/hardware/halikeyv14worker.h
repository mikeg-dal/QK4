#ifndef HALIKEYV14WORKER_H
#define HALIKEYV14WORKER_H

#include "halikeyworkerbase.h"

/**
 * @brief HaliKey worker for the V1.4 native-hardware variant. Opens the serial port with
 *        platform-specific native APIs (HANDLE on Windows, fd on POSIX) and watches the
 *        modem-control pins with a 2-sample debounce. Cadence differs per platform: 500 µs
 *        usleep poll on macOS (~1 ms to confirm), 1 ms high-resolution-timer poll on Windows
 *        (~2 ms to confirm), and a blocking TIOCMIWAIT edge plus one 500 µs confirming re-read
 *        on Linux. `prepareShutdown` toggles the port state to unblock any pending read so the
 *        monitor loop can exit.
 */
class HaliKeyV14Worker : public HaliKeyWorkerBase {
    Q_OBJECT

public:
    explicit HaliKeyV14Worker(const QString &portName, QObject *parent = nullptr);
    ~HaliKeyV14Worker() override;

    void prepareShutdown() override;

public slots:
    void start() override; // Opens port, enters monitor loop

private:
    void monitorLoop(); // Platform-specific main loop
    bool openNativePort();
    void closeNativePort();
    bool readPinState(bool &ditState, bool &dahState, bool &pttState);

    // Debounce: 2 consecutive reads at ~500us = ~1ms
    static constexpr int DEBOUNCE_COUNT = 2;

    // Linux TIOCMIWAIT branch only: how many times to re-read a bouncing line before emitting the
    // last read anyway. Bounded so a chattering contact cannot spin the loop; at DEBOUNCE_COUNT=2
    // that is at most ~4 ms of confirming before the sample goes out regardless.
    static constexpr int kSettleAttempts = 8;

#ifdef Q_OS_WIN
    // Diagnostic: last-logged raw modem-line states, so readPinState() only logs on a
    // transition rather than every ~1 ms poll. Windows-only (the raw-pin trace lives in
    // the Q_OS_WIN branch of readPinState).
    bool m_lastRawCts = false;
    bool m_lastRawDsr = false;
    bool m_lastRawDcd = false;
#endif

    // Native port handle
#ifdef Q_OS_WIN
    void *m_handle = nullptr; // HANDLE
#else
    int m_fd = -1;
#endif

#ifdef Q_OS_LINUX
    // POSIX thread handle for the monitor loop. Captured in start() so that
    // prepareShutdown() can pthread_kill the worker with SIGUSR1 to interrupt
    // an in-flight TIOCMIWAIT ioctl. close(fd) alone is unreliable for waking
    // TIOCMIWAIT on Linux — the kernel can hold the wait for hundreds of ms.
    // SIGUSR1 returns the ioctl with EINTR; the loop then checks m_running
    // and exits cleanly within a few ms.
    unsigned long m_linuxThreadHandle = 0; // pthread_t opaqued to avoid pulling pthread.h here
#endif
};

#endif // HALIKEYV14WORKER_H
