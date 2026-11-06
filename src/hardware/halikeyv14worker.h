#ifndef HALIKEYV14WORKER_H
#define HALIKEYV14WORKER_H

#include "halikeyworkerbase.h"

/**
 * @brief HaliKey worker for the V1.4 native-hardware variant. Opens the serial port with
 *        platform-specific native APIs (HANDLE on Windows, fd on POSIX), powers the paddle contacts
 *        with DTR and RTS, and polls the modem-control lines every 1 ms on every platform. Each
 *        poll goes through SerialLinePoller, the port of K4-Companion's serial path: rest state at
 *        open is not an edge, a straight key is debounced for 20 ms, and each edge carries its
 *        line's delta. Emits paddleChanged on the worker thread.
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
    bool readPinState(bool &ditState, bool &dahState);

    // Native port handle
#ifdef Q_OS_WIN
    void *m_handle = nullptr; // HANDLE
#else
    int m_fd = -1;
#endif
};

#endif // HALIKEYV14WORKER_H
