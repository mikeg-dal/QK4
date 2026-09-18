#include "halikeyv14worker.h"
#include "halikeyworkerbase.h"
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

#ifdef Q_OS_LINUX
#include <pthread.h>
#include <signal.h>

namespace {
// No-op signal handler. SIGUSR1 is used solely to interrupt a TIOCMIWAIT ioctl
// blocked in the kernel — the handler doesn't need to do anything; the side
// effect of receiving the signal is that the ioctl returns with EINTR.
void halikeyV14SigUsr1Handler(int) {}
} // namespace
#endif

HaliKeyV14Worker::HaliKeyV14Worker(const QString &portName, QObject *parent) : HaliKeyWorkerBase(portName, parent) {}

HaliKeyV14Worker::~HaliKeyV14Worker() {
    closeNativePort();
}

void HaliKeyV14Worker::prepareShutdown() {
#ifdef Q_OS_LINUX
    // Wake the TIOCMIWAIT ioctl with SIGUSR1 — close(fd) alone is unreliable on
    // Linux because the kernel can hold the wait for hundreds of ms. Signal the
    // captured pthread directly so other threads in the process aren't disturbed.
    // The no-op handler returns EINTR from the ioctl; the loop then sees
    // m_running=false and exits within a few ms.
    if (m_linuxThreadHandle != 0) {
        ::pthread_kill(static_cast<pthread_t>(m_linuxThreadHandle), SIGUSR1);
    }
    // Close fd as a belt-and-suspenders backup: if SIGUSR1 was masked or didn't
    // arrive, close(fd) is the documented (if unreliable) wakeup mechanism.
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

#ifdef Q_OS_LINUX
    // Install a no-op SIGUSR1 handler and make sure this thread won't block the
    // signal — so prepareShutdown() can interrupt a TIOCMIWAIT ioctl by sending
    // SIGUSR1 to this exact thread.
    struct sigaction sa = {};
    sa.sa_handler = &halikeyV14SigUsr1Handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    ::sigaction(SIGUSR1, &sa, nullptr);

    sigset_t unblock;
    sigemptyset(&unblock);
    sigaddset(&unblock, SIGUSR1);
    ::pthread_sigmask(SIG_UNBLOCK, &unblock, nullptr);

    m_linuxThreadHandle = static_cast<unsigned long>(::pthread_self());
#endif

    emit portOpened();
    m_running = true;
    monitorLoop();

#ifdef Q_OS_LINUX
    m_linuxThreadHandle = 0;
#endif
}

bool HaliKeyV14Worker::openNativePort() {
#ifdef Q_OS_WIN
    QString path = "\\\\.\\" + m_portName;
    m_handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OVERLAPPED, nullptr);
    if (m_handle == INVALID_HANDLE_VALUE) {
        m_handle = nullptr;
        QString error = "Failed to open port " + m_portName;
        qCWarning(hwHalikey) << "HaliKeyV14Worker:" << error;
        emit errorOccurred(error);
        return false;
    }

    // Configure serial port
    DCB dcb = {};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(m_handle, &dcb)) {
        QString error = "Failed to get port state for " + m_portName;
        qCWarning(hwHalikey) << "HaliKeyV14Worker:" << error;
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
        qCWarning(hwHalikey) << "HaliKeyV14Worker:" << error;
        closeNativePort();
        emit errorOccurred(error);
        return false;
    }

    // WHY no SetCommMask: monitorLoop() polls GetCommModemStatus on a 1 ms timer rather than
    // blocking in WaitCommEvent, so no event mask is needed. See the WHY NOT WaitCommEvent
    // block in monitorLoop() for why the event-driven approach was abandoned.

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
        qCWarning(hwHalikey) << "HaliKeyV14Worker:" << error;
        emit errorOccurred(error);
        return false;
    }

    // Configure raw serial
    struct termios tio = {};
    if (tcgetattr(m_fd, &tio) < 0) {
        QString error = QString("Failed to get port attributes for %1").arg(m_portName);
        qCWarning(hwHalikey) << "HaliKeyV14Worker:" << error;
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

bool HaliKeyV14Worker::readPinState(bool &ditState, bool &dahState, bool &pttState) {
    // WHY: HaliKey V1.4 firmware exposes raw modem-status pin states. Empirically (verified via
    // qk4.hardware.debug logs in this user's setup, NORMAL paddle wiring):
    //
    //   physical dah lever  → DCD AND DSR transition together (always paired)
    //   physical dit lever  → CTS only
    //   foot pedal          → CTS only (indistinguishable from dit lever on the wire)
    //
    // We map the three logical signals as follows:
    //
    //   ditState = false                (V1.4 cannot uniquely identify the dit lever — both
    //                                    pedal and dit lever drive CTS. dit-vs-pedal demux
    //                                    happens in HardwareController based on operating
    //                                    mode: in CW we treat CTS as dit, in voice as PTT.)
    //   dahState = DCD || DSR           (paddle dah lever — both pins fire together; OR collapses
    //                                    them into one stable edge.)
    //   pttState = CTS                  (mode-routed downstream: voice → PTT, CW → setDitPaddle.)
    //
    // The MIDI worker is unaffected — it has true distinct sources for dit/dah/PTT (notes
    // 20/21/31), so HardwareController's mode-routing only kicks in for the V1.4 variant.
#ifdef Q_OS_WIN
    DWORD modemStatus = 0;
    if (!GetCommModemStatus(m_handle, &modemStatus)) {
        return false;
    }
    bool cts = (modemStatus & MS_CTS_ON) != 0;
    bool dsr = (modemStatus & MS_DSR_ON) != 0;
    bool dcd = (modemStatus & MS_RLSD_ON) != 0;
    // Diagnostic: log each raw modem-line transition so we can see whether DCD and DSR
    // (which together form the dah lever via `dcd || dsr`) actually rise/fall as a pair.
    // A lingering DSR after DCD drops would latch dahState true → stuck dah. Chatty by
    // design; gated behind hw.halikey.debug. Compares against last-logged raw bits.
    if (cts != m_lastRawCts || dsr != m_lastRawDsr || dcd != m_lastRawDcd) {
        qCDebug(hwHalikey) << "HaliKeyV14Worker: raw pins  CTS:" << cts << " DCD:" << dcd << " DSR:" << dsr
                           << " => dah(dcd||dsr):" << (dcd || dsr);
        m_lastRawCts = cts;
        m_lastRawDsr = dsr;
        m_lastRawDcd = dcd;
    }
    ditState = false;
    dahState = dcd || dsr;
    pttState = cts;
    return true;
#else
    int status = 0;
    if (ioctl(m_fd, TIOCMGET, &status) < 0) {
        return false;
    }
    bool cts = (status & TIOCM_CTS) != 0;
    bool dsr = (status & TIOCM_DSR) != 0;
    bool dcd = (status & TIOCM_CD) != 0;
    ditState = false;
    dahState = dcd || dsr;
    pttState = cts;
    return true;
#endif
}

void HaliKeyV14Worker::monitorLoop() {
    // WHY: This count-based confirmation (DEBOUNCE_COUNT=2 across ≥500 µs read intervals) is
    // the ONLY contact-bounce defense in the serial path. HalikeyDevice no longer runs a
    // time-window debounce — see docs/halikey-midi-windows-debounce-bug.md for why that was
    // removed. Do not relax this filter without first restoring an alternative bounce gate.
    bool lastDitState = false;
    bool lastDahState = false;
    bool lastPttState = false;

    // Raw states and debounce counters
    bool rawDitState = false;
    bool rawDahState = false;
    bool rawPttState = false;
    int ditDebounceCounter = 0;
    int dahDebounceCounter = 0;
    int pttDebounceCounter = 0;

    // Read initial state
    readPinState(lastDitState, lastDahState, lastPttState);
    rawDitState = lastDitState;
    rawDahState = lastDahState;
    rawPttState = lastPttState;
    ditDebounceCounter = DEBOUNCE_COUNT;
    dahDebounceCounter = DEBOUNCE_COUNT;
    pttDebounceCounter = DEBOUNCE_COUNT;

#ifdef Q_OS_LINUX
    // Linux: use TIOCMIWAIT for kernel-level interrupt-driven monitoring
    while (m_running) {
        // Wait for CTS, DSR, or DCD change — blocks in kernel until edge detected.
        // DCD added so foot-pedal/PTT presses wake the loop the same way paddles do.
        if (ioctl(m_fd, TIOCMIWAIT, TIOCM_CTS | TIOCM_DSR | TIOCM_CD) < 0) {
            if (!m_running)
                break;
            if (errno == EINTR)
                continue;
            QString error = QString("HaliKey monitor error: %1").arg(QString::fromLocal8Bit(strerror(errno)));
            qCWarning(hwHalikey) << "HaliKeyV14Worker:" << error;
            emit errorOccurred(error);
            return;
        }

        if (!m_running)
            break;

        // Read until the lines settle, rather than going back to TIOCMIWAIT when they disagree.
        //
        // WHY: TIOCMIWAIT snapshots the kernel's interrupt counters when it is ENTERED, so any
        // change arriving before re-entry is never reported. Giving up on an unstable pair and
        // waiting again therefore threw away the settled state: a squeeze release whose second
        // lever drops a millisecond later (which is what the recommended 1 ms FTDI latency timer
        // produces) left both levers reading down with no further edge to correct them, and the
        // keyer sent dit-dah forever. The macOS and Windows branches cannot hit this because they
        // poll — every tick re-reads. This loop now re-reads too.
        bool ditState = false, dahState = false, pttState = false;
        bool settled = false;
        for (int attempt = 0; attempt < kSettleAttempts && m_running; ++attempt) {
            if (!readPinState(ditState, dahState, pttState)) {
                if (!m_running)
                    break;
                QString error = "Failed to read pin state";
                qCWarning(hwHalikey) << "HaliKeyV14Worker:" << error;
                emit errorOccurred(error);
                return;
            }

            // Confirm across DEBOUNCE_COUNT reads ≥500 µs apart — the only contact-bounce defense
            // on this path, unchanged in substance from the original count-based filter.
            bool stable = true;
            for (int i = 1; i < DEBOUNCE_COUNT && m_running; ++i) {
                usleep(500);
                bool d = false, h = false, p = false;
                if (!readPinState(d, h, p)) {
                    stable = false;
                    break;
                }
                if (d != ditState || h != dahState || p != pttState) {
                    stable = false;
                    break;
                }
            }
            if (stable) {
                settled = true;
                break;
            }
        }
        if (!m_running)
            break;
        if (!settled) {
            // Still bouncing after kSettleAttempts. Fall back to the last read rather than
            // dropping it: an unsettled line is still closer to the truth than a stale one, and
            // the next edge will correct it.
            qCDebug(hwHalikey) << "HaliKeyV14Worker: lines did not settle, using last read";
        }

        // One emit for the whole sample. These lines were read together and must stay together:
        // see the lineStateChanged comment in halikeyworkerbase.h.
        if (ditState != lastDitState || dahState != lastDahState || pttState != lastPttState) {
            qCDebug(hwHalikey) << "HaliKeyV14Worker: lines dit:" << ditState << " dah:" << dahState
                               << " ptt:" << pttState;
            lastDitState = ditState;
            lastDahState = dahState;
            lastPttState = pttState;
            emit lineStateChanged(ditState, dahState, pttState);
        }

        // Re-read once more before blocking again, and emit if anything moved while we were
        // confirming. Without this the change that lands between the confirm read and re-entry
        // into TIOCMIWAIT is lost for good — the same hole as above, at the other end of the loop.
        bool reDit = false, reDah = false, rePtt = false;
        if (m_running && readPinState(reDit, reDah, rePtt)) {
            if (reDit != lastDitState || reDah != lastDahState || rePtt != lastPttState) {
                qCDebug(hwHalikey) << "HaliKeyV14Worker: lines moved while confirming, dit:" << reDit
                                   << " dah:" << reDah << " ptt:" << rePtt;
                lastDitState = reDit;
                lastDahState = reDah;
                lastPttState = rePtt;
                emit lineStateChanged(reDit, reDah, rePtt);
            }
        }
    }

#elif defined(Q_OS_WIN)
    // Windows: POLL the modem-status lines on a fixed ~1 ms cadence — same model as the
    // macOS branch below. WHY NOT WaitCommEvent (the previous approach): USB-serial (FTDI)
    // VCP drivers do NOT reliably deliver EV_CTS / EV_DSR / EV_RLSD modem-status events.
    // A paddle line transition with no other line activity frequently fails to wake
    // WaitCommEvent at all, so a release was missed and the key stuck "on" until some
    // unrelated line changed and belatedly woke the wait. This was confirmed in hw.halikey
    // traces: a dah whose release was never signalled showed a bogus hold of ~2.5 s and only
    // "released" when the next CTS press woke the loop. macOS (pure poll) and Linux
    // (TIOCMIWAIT, kernel-level) were unaffected because neither depends on WaitCommEvent.
    // GetCommModemStatus is a cheap local call, so polling catches every transition within
    // one poll period. Debounce is the macOS count-based filter (DEBOUNCE_COUNT reads).
    //
    // Cadence: a high-resolution waitable timer (Win10 1803+) gives a true ~1 ms tick without
    // raising the process-global timer resolution via timeBeginPeriod. Falls back to Sleep(1)
    // if the high-res timer can't be created.
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
    HANDLE pollTimer =
        CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (pollTimer) {
        LARGE_INTEGER due;
        due.QuadPart = -10000LL; // first fire in 1 ms (100 ns units, negative = relative)
        SetWaitableTimer(pollTimer, &due, 1 /* ms period */, nullptr, nullptr, FALSE);
    }

    while (m_running) {
        if (pollTimer)
            WaitForSingleObject(pollTimer, 5); // ~1 ms tick; 5 ms cap bounds m_running latency
        else
            Sleep(1);

        bool ditState = false, dahState = false, pttState = false;
        if (!readPinState(ditState, dahState, pttState)) {
            if (!m_running)
                break;
            emit errorOccurred("Failed to read pin state");
            if (pollTimer)
                CloseHandle(pollTimer);
            return;
        }

        // One emit per loop iteration covering every line that settled on this pass — the lines
        // were sampled together by readPinState() and must reach the keyer together.
        bool linesChanged = false;

        // Debounce dit (count-based; identical to the macOS branch)
        if (ditState == rawDitState) {
            if (ditDebounceCounter < DEBOUNCE_COUNT)
                ditDebounceCounter++;
            if (ditDebounceCounter >= DEBOUNCE_COUNT && ditState != lastDitState) {
                lastDitState = ditState;
                linesChanged = true;
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
                linesChanged = true;
            }
        } else {
            rawDahState = dahState;
            dahDebounceCounter = 1;
        }

        // Debounce ptt
        if (pttState == rawPttState) {
            if (pttDebounceCounter < DEBOUNCE_COUNT)
                pttDebounceCounter++;
            if (pttDebounceCounter >= DEBOUNCE_COUNT && pttState != lastPttState) {
                lastPttState = pttState;
                linesChanged = true;
            }
        } else {
            rawPttState = pttState;
            pttDebounceCounter = 1;
        }

        if (linesChanged) {
            qCDebug(hwHalikey) << "HaliKeyV14Worker: lines dit:" << lastDitState << " dah:" << lastDahState
                               << " ptt:" << lastPttState;
            emit lineStateChanged(lastDitState, lastDahState, lastPttState);
        }
    }

    if (pollTimer)
        CloseHandle(pollTimer);

#else
    // macOS (and other POSIX): tight usleep poll loop at 500us (2kHz)
    while (m_running) {
        usleep(500); // 500 microseconds

        bool ditState = false, dahState = false, pttState = false;
        if (!readPinState(ditState, dahState, pttState)) {
            if (!m_running)
                break;
            QString error = QString("HaliKey monitor error: %1").arg(QString::fromLocal8Bit(strerror(errno)));
            qCWarning(hwHalikey) << "HaliKeyV14Worker:" << error;
            emit errorOccurred(error);
            return;
        }

        // One emit per loop iteration covering every line that settled on this pass — the lines
        // were sampled together by readPinState() and must reach the keyer together.
        bool linesChanged = false;

        // Debounce dit
        if (ditState == rawDitState) {
            if (ditDebounceCounter < DEBOUNCE_COUNT)
                ditDebounceCounter++;
            if (ditDebounceCounter >= DEBOUNCE_COUNT && ditState != lastDitState) {
                lastDitState = ditState;
                linesChanged = true;
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
                linesChanged = true;
            }
        } else {
            rawDahState = dahState;
            dahDebounceCounter = 1;
        }

        // Debounce ptt
        if (pttState == rawPttState) {
            if (pttDebounceCounter < DEBOUNCE_COUNT)
                pttDebounceCounter++;
            if (pttDebounceCounter >= DEBOUNCE_COUNT && pttState != lastPttState) {
                lastPttState = pttState;
                linesChanged = true;
            }
        } else {
            rawPttState = pttState;
            pttDebounceCounter = 1;
        }

        if (linesChanged) {
            qCDebug(hwHalikey) << "HaliKeyV14Worker: lines dit:" << lastDitState << " dah:" << lastDahState
                               << " ptt:" << lastPttState;
            emit lineStateChanged(lastDitState, lastDahState, lastPttState);
        }
    }
#endif

    closeNativePort();
}
