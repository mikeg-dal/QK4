#include "halikeyv14worker.h"
#include "logging/logcategories.h"
#include "halikeyworkerbase.h"
#include "hardware/keyerruntime.h"
#include "utils/rtscheduling.h"
#include <QDebug>
#include <cstring>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

HaliKeyV14Worker::HaliKeyV14Worker(const QString &portName, int deviceType, QObject *parent)
    : HaliKeyWorkerBase(portName, parent), m_deviceType(deviceType) {}

HaliKeyV14Worker::~HaliKeyV14Worker() {
    closeNativePort();
}

void HaliKeyV14Worker::prepareShutdown() {
    // Nothing to unblock: the monitor loop polls every 1 ms and sees the stop request within a
    // poll. The port is closed by the worker thread itself as the loop exits, never from here.
}

void HaliKeyV14Worker::start() {
    if (stopRequested() || !openNativePort()) {
        return;
    }
    // Promoted here, not when the thread starts: the same thread runs the MIDI worker's presence
    // poll, which needs nothing of the sort.
    RtScheduling::promoteCurrentThread(RtScheduling::Threads::kHaliKey);
    emit portOpened();
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
        qCWarning(lcCwHalikey) << "HaliKeyV14Worker:" << error;
        emit errorOccurred(error);
        return false;
    }

    // Configure serial port
    DCB dcb = {};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(m_handle, &dcb)) {
        QString error = "Failed to get port state for " + m_portName;
        qCWarning(lcCwHalikey) << "HaliKeyV14Worker:" << error;
        closeNativePort();
        emit errorOccurred(error);
        return false;
    }
    dcb.BaudRate = CBR_9600;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    // The HaliKey powers its contacts from DTR and RTS. The ORB does not need them, and its manual
    // says RTS used as power can interfere.
    const bool powerContacts = m_deviceType != HaliKeyType::kOrb;
    dcb.fDtrControl = powerContacts ? DTR_CONTROL_ENABLE : DTR_CONTROL_DISABLE;
    dcb.fRtsControl = powerContacts ? RTS_CONTROL_ENABLE : RTS_CONTROL_DISABLE;
    if (!SetCommState(m_handle, &dcb)) {
        QString error = "Failed to configure port " + m_portName;
        qCWarning(lcCwHalikey) << "HaliKeyV14Worker:" << error;
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
        qCWarning(lcCwHalikey) << "HaliKeyV14Worker:" << error;
        emit errorOccurred(error);
        return false;
    }

    // WHY first thing for the ORB: open() raises DTR and RTS on macOS and Linux, so they must be
    // cleared, not merely left unset, and the sooner the shorter they are up.
    int outputs = TIOCM_DTR | TIOCM_RTS;
    if (m_deviceType == HaliKeyType::kOrb && ioctl(m_fd, TIOCMBIC, &outputs) < 0) {
        QString error =
            QString("Failed to clear DTR/RTS on %1: %2").arg(m_portName, QString::fromLocal8Bit(strerror(errno)));
        qCWarning(lcCwHalikey) << "HaliKeyV14Worker:" << error;
        closeNativePort();
        emit errorOccurred(error);
        return false;
    }

    // Configure raw serial
    struct termios tio = {};
    if (tcgetattr(m_fd, &tio) < 0) {
        QString error = QString("Failed to get port attributes for %1").arg(m_portName);
        qCWarning(lcCwHalikey) << "HaliKeyV14Worker:" << error;
        closeNativePort();
        emit errorOccurred(error);
        return false;
    }

    cfmakeraw(&tio);
    cfsetispeed(&tio, B9600);
    cfsetospeed(&tio, B9600);
    tio.c_cflag |= CLOCAL; // Ignore modem control lines for opening
    if (m_deviceType == HaliKeyType::kOrb)
        tio.c_cflag &= ~CRTSCTS; // hardware flow control would drive RTS
    tcsetattr(m_fd, TCSANOW, &tio);

    // Enable DTR and RTS so the HaliKey has power to sense paddle contacts
    if (m_deviceType != HaliKeyType::kOrb)
        ioctl(m_fd, TIOCMBIS, &outputs);

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

bool HaliKeyV14Worker::readModemLines(ModemLines &lines) noexcept {
#ifdef Q_OS_WIN
    DWORD modemStatus = 0;
    if (!GetCommModemStatus(m_handle, &modemStatus)) {
        return false;
    }
    lines.cts = (modemStatus & MS_CTS_ON) != 0;
    lines.dsr = (modemStatus & MS_DSR_ON) != 0;
    lines.dcd = (modemStatus & MS_RLSD_ON) != 0;
    return true;
#else
    int status = 0;
    if (ioctl(m_fd, TIOCMGET, &status) < 0) {
        return false;
    }
    lines.cts = (status & TIOCM_CTS) != 0;
    lines.dsr = (status & TIOCM_DSR) != 0;
    lines.dcd = (status & TIOCM_CD) != 0;
    return true;
#endif
}

void HaliKeyV14Worker::monitorLoop() {
    // WHY one 1 ms poll on every platform, through SerialLinePoller: this is K4-Companion's serial
    // path (SerialInput.signal_loop), which its keyer was bench-tuned against. Three things it does
    // that the per-platform loops here did not:
    //   - the lines' state at open is rest, never an edge, so an adapter idling with a line asserted
    //     cannot look like a paddle held down;
    //   - a straight key waits for 20 ms without a change, so contact bounce never keys a short
    //     element;
    //   - each edge carries the time since that line's previous change, which is what a straight
    //     key's KZD/KZU are built from.
    // Paddles are reported on the first poll that sees them. The keyer reads its paddles from
    // time-stamped history, so a bounce inside an element changes nothing on the air. A blocking
    // edge wait would not do: it cannot time the 20 ms quiet a straight key needs.
    //
    // Which line is which contact depends on the device: SerialPaddleInput::map. One read per poll,
    // so the lines are always a joint sample.
    ModemLines lines;
    SerialPaddleInput input(m_deviceType);

    auto fail = [this]() {
        if (stopRequested())
            return;
#ifdef Q_OS_WIN
        const QString error = QStringLiteral("Failed to read pin state");
#else
        const QString error = QString("HaliKey monitor error: %1").arg(QString::fromLocal8Bit(strerror(errno)));
#endif
        qCWarning(lcCwHalikey) << "HaliKeyV14Worker:" << error;
        emit errorOccurred(error);
    };

    SteadyKeyerClock clock;
    if (!readModemLines(lines)) {
        fail();
        closeNativePort();
        return;
    }
    input.start(lines, m_straightKey.load(std::memory_order_acquire));

#ifdef Q_OS_WIN
    // WHY a high-resolution waitable timer: GetCommModemStatus polled with Sleep(1) runs at the
    // 15.6 ms system tick, and raising it with timeBeginPeriod is process-global. Falls back to
    // Sleep(1) if the high-resolution timer can't be created.
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
#else
    // WHY absolute deadlines on a 1 ms grid: usleep(1000) sleeps at least 1 ms after whatever the
    // poll itself took, so every period ran long and the error accumulated. After a stall of more
    // than a few periods the grid restarts from now rather than polling back-to-back to catch up.
    constexpr double kPollSeconds = 0.001;
    constexpr double kMaxBehindSeconds = 0.005;
    double nextPoll = clock.now();
#endif

    while (!stopRequested()) {
#ifdef Q_OS_WIN
        // The periodic timer keeps its own 1 ms grid, so it does not drift either.
        if (pollTimer)
            WaitForSingleObject(pollTimer, 5); // ~1 ms tick; 5 ms cap bounds stop latency
        else
            Sleep(1);
#else
        nextPoll += kPollSeconds;
        if (clock.now() - nextPoll > kMaxBehindSeconds)
            nextPoll = clock.now() + kPollSeconds;
        clock.sleepUntil(nextPoll);
#endif
        if (!readModemLines(lines)) {
            fail();
            break;
        }
        // No log line per edge: this is a real-time thread (CONVENTIONS Rule 16). What the keyer
        // made of the edges is in the CAT traffic bench item.
        const bool straight = m_straightKey.load(std::memory_order_acquire);
        for (const auto &change : input.poll(lines, straight, clock.now()))
            emit paddleChanged(change.signal == SerialPaddleInput::kDah, change.asserted, change.deltaTime);
        // Polled every time, so a switch held while unwanted is never reported as a new press.
        // WHY sent only when wanted: this signal is queued to the main thread, which allocates, on a
        // real-time thread. Unwanted (option off, CW), it must cost nothing - a straight key's every
        // element moves this line.
        const auto jack = input.pollJack(lines, clock.now());
        if (jack && m_pttJackWanted.load(std::memory_order_acquire))
            emit pttJackChanged(*jack);
    }

#ifdef Q_OS_WIN
    if (pollTimer)
        CloseHandle(pollTimer);
#endif
    closeNativePort();
}
