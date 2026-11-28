#ifndef HALIKEYV14WORKER_H
#define HALIKEYV14WORKER_H

#include "halikeyworkerbase.h"
#include "serialpaddleinput.h"

/**
 * @brief Worker for the serial paddle devices: HaliKey V1.4 and the ORB Control Device. Opens the
 *        serial port with platform-specific native APIs (HANDLE on Windows, fd on POSIX) and polls
 *        the modem-control lines every 1 ms on every platform. DTR and RTS are on for the HaliKey,
 *        which powers its contacts with them, and off for the ORB. Each poll goes through
 *        SerialPaddleInput (the device's line map, then the port of K4-Companion's serial path):
 *        rest state at open is not an edge, a straight key is debounced for 20 ms, and each edge
 *        carries its line's delta. Emits paddleChanged on the worker thread.
 */
class HaliKeyV14Worker : public HaliKeyWorkerBase {
    Q_OBJECT

public:
    HaliKeyV14Worker(const QString &portName, int deviceType, QObject *parent = nullptr);
    ~HaliKeyV14Worker() override;

    void prepareShutdown() override;

public slots:
    void start() override; // Opens port, enters monitor loop

private:
    void monitorLoop(); // Platform-specific main loop
    bool openNativePort();
    void closeNativePort();
    bool readModemLines(ModemLines &lines) noexcept;

    const int m_deviceType; // HaliKeyType

    // Native port handle
#ifdef Q_OS_WIN
    void *m_handle = nullptr; // HANDLE
#else
    int m_fd = -1;
#endif
};

#endif // HALIKEYV14WORKER_H
