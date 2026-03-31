#include "halikeyv14worker.h"
#include <QDebug>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

HaliKeyV14Worker::HaliKeyV14Worker(const QString &portName, QObject *parent) : HaliKeyWorkerBase(portName, parent) {}

HaliKeyV14Worker::~HaliKeyV14Worker() {
    closeNativePort();
}

void HaliKeyV14Worker::prepareShutdown() {
#ifdef Q_OS_LINUX
    // Close fd to unblock TIOCMIWAIT
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
#endif
}

void HaliKeyV14Worker::start() {
    if (!openNativePort()) {
        return;
    }

    emit portOpened();
    m_running = true;
    monitorLoop();
}

bool HaliKeyV14Worker::openNativePort() {
#ifdef Q_OS_WIN
    QString path = "\\\\.\\" + m_portName;
    m_handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OVERLAPPED, nullptr);
    if (m_handle == INVALID_HANDLE_VALUE) {
        m_handle = nullptr;
        QString error = "Failed to open port " + m_portName;
        qWarning() << "HaliKeyV14Worker:" << error;
        emit errorOccurred(error);
        return false;
    }

    // Configure serial port
    DCB dcb = {};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(m_handle, &dcb)) {
        QString error = "Failed to get port state for " + m_portName;
        qWarning() << "HaliKeyV14Worker:" << error;
        closeNativePort();
        emit errorOccurred(error);
        return false;
    }
    dcb.BaudRate = CBR_9600;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    if (!SetCommState(m_handle, &dcb)) {
        QString error = "Failed to configure port " + m_portName;
        qWarning() << "HaliKeyV14Worker:" << error;
        closeNativePort();
        emit errorOccurred(error);
        return false;
    }

    // Set up event mask for CTS and DSR changes
    if (!SetCommMask(m_handle, EV_CTS | EV_DSR)) {
        QString error = "Failed to set comm mask for " + m_portName;
        qWarning() << "HaliKeyV14Worker:" << error;
        closeNativePort();
        emit errorOccurred(error);
        return false;
    }

    return true;
#else
    QByteArray devPath;
#ifdef Q_OS_MACOS
    devPath = ("/dev/" + m_portName).toUtf8();
    // macOS uses cu. prefix for outgoing connections
    if (!devPath.contains("/dev/cu.") && !devPath.contains("/dev/tty.")) {
        devPath = ("/dev/cu." + m_portName).toUtf8();
    }
#else
    // Linux
    if (m_portName.startsWith("/dev/")) {
        devPath = m_portName.toUtf8();
    } else {
        devPath = ("/dev/" + m_portName).toUtf8();
    }
#endif

    m_fd = ::open(devPath.constData(), O_RDONLY | O_NOCTTY | O_NONBLOCK);
    if (m_fd < 0) {
        QString error = QString("Failed to open port %1: %2").arg(m_portName, QString::fromLocal8Bit(strerror(errno)));
        qWarning() << "HaliKeyV14Worker:" << error;
        emit errorOccurred(error);
        return false;
    }

    // Configure raw serial
    struct termios tio = {};
    if (tcgetattr(m_fd, &tio) < 0) {
        QString error = QString("Failed to get port attributes for %1").arg(m_portName);
        qWarning() << "HaliKeyV14Worker:" << error;
        closeNativePort();
        emit errorOccurred(error);
        return false;
    }

    cfmakeraw(&tio);
    cfsetispeed(&tio, B9600);
    cfsetospeed(&tio, B9600);
    tio.c_cflag |= CLOCAL; // Ignore modem control lines for opening
    tcsetattr(m_fd, TCSANOW, &tio);

    // Enable DTR and RTS so the HaliKey has power to sense paddle contacts
    int bits = TIOCM_DTR | TIOCM_RTS;
    ioctl(m_fd, TIOCMBIS, &bits);

    return true;
#endif
}

void HaliKeyV14Worker::closeNativePort() {
#ifdef Q_OS_WIN
    if (m_handle) {
        CloseHandle(m_handle);
        m_handle = nullptr;
    }
#else
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
#endif
}

bool HaliKeyV14Worker::readPinState(bool &ditState, bool &dahState) {
#ifdef Q_OS_WIN
    DWORD modemStatus = 0;
    if (!GetCommModemStatus(m_handle, &modemStatus)) {
        return false;
    }
    ditState = (modemStatus & MS_CTS_ON) != 0;
    dahState = (modemStatus & MS_DSR_ON) != 0;
    return true;
#else
    int status = 0;
    if (ioctl(m_fd, TIOCMGET, &status) < 0) {
        return false;
    }
    ditState = (status & TIOCM_CTS) != 0;
    dahState = (status & TIOCM_DSR) != 0;
    return true;
#endif
}

void HaliKeyV14Worker::monitorLoop() {
    bool lastDitState = false;
    bool lastDahState = false;

    // Raw states and debounce counters
    bool rawDitState = false;
    bool rawDahState = false;
    int ditDebounceCounter = 0;
    int dahDebounceCounter = 0;

    // Read initial state
    readPinState(lastDitState, lastDahState);
    rawDitState = lastDitState;
    rawDahState = lastDahState;
    ditDebounceCounter = DEBOUNCE_COUNT;
    dahDebounceCounter = DEBOUNCE_COUNT;

#ifdef Q_OS_LINUX
    // Linux: use TIOCMIWAIT for kernel-level interrupt-driven monitoring
    while (m_running) {
        // Wait for CTS or DSR change — blocks in kernel until edge detected
        if (ioctl(m_fd, TIOCMIWAIT, TIOCM_CTS | TIOCM_DSR) < 0) {
            if (!m_running)
                break;
            if (errno == EINTR)
                continue;
            QString error = QString("HaliKey monitor error: %1").arg(QString::fromLocal8Bit(strerror(errno)));
            qWarning() << "HaliKeyV14Worker:" << error;
            emit errorOccurred(error);
            return;
        }

        if (!m_running)
            break;

        // Read new state
        bool ditState = false, dahState = false;
        if (!readPinState(ditState, dahState)) {
            if (!m_running)
                break;
            QString error = "Failed to read pin state";
            qWarning() << "HaliKeyV14Worker:" << error;
            emit errorOccurred(error);
            return;
        }

        // Confirm state is stable (matches macOS/Windows debounce)
        bool stable = true;
        for (int i = 1; i < DEBOUNCE_COUNT && m_running; ++i) {
            usleep(500);
            bool d = false, h = false;
            if (!readPinState(d, h)) {
                stable = false;
                break;
            }
            if (d != ditState || h != dahState) {
                stable = false;
                break;
            }
        }
        if (!stable || !m_running)
            continue;

        if (ditState != lastDitState) {
            lastDitState = ditState;
            emit ditStateChanged(ditState);
        }
        if (dahState != lastDahState) {
            lastDahState = dahState;
            emit dahStateChanged(dahState);
        }
    }

#elif defined(Q_OS_WIN)
    // Windows: WaitCommEvent for CTS/DSR changes with poll fallback
    OVERLAPPED ov = {};
    ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) {
        emit errorOccurred("Failed to create event for serial monitoring");
        closeNativePort();
        return;
    }

    while (m_running) {
        DWORD evtMask = 0;
        BOOL result = WaitCommEvent(m_handle, &evtMask, &ov);

        if (!result) {
            if (GetLastError() == ERROR_IO_PENDING) {
                // Wait with timeout so we can check m_running
                DWORD waitResult = WaitForSingleObject(ov.hEvent, 10);
                if (waitResult == WAIT_TIMEOUT) {
                    continue;
                }
                if (waitResult != WAIT_OBJECT_0) {
                    if (!m_running)
                        break;
                    continue;
                }
                DWORD transferred = 0;
                GetOverlappedResult(m_handle, &ov, &transferred, FALSE);
            } else {
                if (!m_running)
                    break;
                // Error — fall back to poll for this iteration
                Sleep(1);
                continue;
            }
        }

        if (!m_running)
            break;

        ResetEvent(ov.hEvent);

        // Read new state
        bool ditState = false, dahState = false;
        if (!readPinState(ditState, dahState)) {
            if (!m_running)
                break;
            emit errorOccurred("Failed to read pin state");
            CloseHandle(ov.hEvent);
            return;
        }

        // Debounce
        if (ditState == rawDitState) {
            if (ditDebounceCounter < DEBOUNCE_COUNT)
                ditDebounceCounter++;
            if (ditDebounceCounter >= DEBOUNCE_COUNT && ditState != lastDitState) {
                lastDitState = ditState;
                emit ditStateChanged(ditState);
            }
        } else {
            rawDitState = ditState;
            ditDebounceCounter = 1;
        }

        if (dahState == rawDahState) {
            if (dahDebounceCounter < DEBOUNCE_COUNT)
                dahDebounceCounter++;
            if (dahDebounceCounter >= DEBOUNCE_COUNT && dahState != lastDahState) {
                lastDahState = dahState;
                emit dahStateChanged(dahState);
            }
        } else {
            rawDahState = dahState;
            dahDebounceCounter = 1;
        }
    }

    CloseHandle(ov.hEvent);

#else
    // macOS (and other POSIX): tight usleep poll loop at 500us (2kHz)
    while (m_running) {
        usleep(500); // 500 microseconds

        bool ditState = false, dahState = false;
        if (!readPinState(ditState, dahState)) {
            if (!m_running)
                break;
            QString error = QString("HaliKey monitor error: %1").arg(QString::fromLocal8Bit(strerror(errno)));
            qWarning() << "HaliKeyV14Worker:" << error;
            emit errorOccurred(error);
            return;
        }

        // Debounce dit
        if (ditState == rawDitState) {
            if (ditDebounceCounter < DEBOUNCE_COUNT)
                ditDebounceCounter++;
            if (ditDebounceCounter >= DEBOUNCE_COUNT && ditState != lastDitState) {
                lastDitState = ditState;
                emit ditStateChanged(ditState);
            }
        } else {
            rawDitState = ditState;
            ditDebounceCounter = 1;
        }

        // Debounce dah
        if (dahState == rawDahState) {
            if (dahDebounceCounter < DEBOUNCE_COUNT)
                dahDebounceCounter++;
            if (dahDebounceCounter >= DEBOUNCE_COUNT && dahState != lastDahState) {
                lastDahState = dahState;
                emit dahStateChanged(dahState);
            }
        } else {
            rawDahState = dahState;
            dahDebounceCounter = 1;
        }
    }
#endif

    closeNativePort();
}
