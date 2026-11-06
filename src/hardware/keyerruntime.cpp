// SPDX-License-Identifier: GPL-3.0-or-later

#include "hardware/keyerruntime.h"

#include <QtGlobal>
#include <chrono>
#include <cmath>
#include <thread>

#if defined(Q_OS_WIN)
#include <windows.h>
#elif defined(Q_OS_MACOS)
#include <mach/mach_time.h>
#elif defined(Q_OS_LINUX)
#include <cerrno>
#include <sys/prctl.h>
#include <time.h>
#endif

double SteadyKeyerClock::now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void SteadyKeyerClock::sleep(double seconds) {
    if (!(seconds > 0))
        return;
    sleepUntil(now() + seconds);
}

// WHY the deadline is re-expressed on the platform's own clock just before waiting, rather than
// assumed to be steady_clock's epoch: which clock steady_clock reads is the standard library's
// choice. The two reads are adjacent, and after them the wait is absolute.
//
// WHY Windows stays relative: an absolute waitable-timer due time is wall-clock time, which jumps
// with clock adjustments. The relative due time is computed from the absolute deadline at the
// last moment instead. A per-thread high-resolution waitable timer, because Sleep() rounds up to
// the 15.6 ms system tick, longer than a dit at 80 WPM; Sleep() remains the fallback where the
// high-resolution flag is unsupported (pre-1803 Windows 10).
void SteadyKeyerClock::sleepUntil(double when) {
    const double seconds = when - now();
    if (!(seconds > 0))
        return;
#if defined(Q_OS_MACOS)
    static const mach_timebase_info_data_t timebase = [] {
        mach_timebase_info_data_t tb{};
        mach_timebase_info(&tb);
        return tb;
    }();
    const double ticks = seconds * 1e9 * timebase.denom / timebase.numer;
    const uint64_t deadline = mach_absolute_time() + static_cast<uint64_t>(ticks);
    while (mach_wait_until(deadline) == KERN_ABORTED) {
    }
#elif defined(Q_OS_LINUX)
    // WHY: the default 50 µs slack lets the kernel defer a wake-up to batch it with others. Timer
    // slack is per thread, so it is set by the thread that sleeps, once.
    thread_local const bool slackCut = prctl(PR_SET_TIMERSLACK, 1UL, 0UL, 0UL, 0UL) == 0;
    (void)slackCut;
    timespec deadline{};
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    const long long ns = static_cast<long long>(seconds * 1e9) + deadline.tv_nsec;
    deadline.tv_sec += static_cast<time_t>(ns / 1000000000LL);
    deadline.tv_nsec = static_cast<long>(ns % 1000000000LL);
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, nullptr) == EINTR) {
    }
#elif defined(Q_OS_WIN)
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
    thread_local HANDLE timer =
        CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (timer) {
        LARGE_INTEGER due;
        due.QuadPart = -static_cast<LONGLONG>(std::llround(seconds * 1e7)); // 100 ns units, relative
        if (due.QuadPart == 0)
            due.QuadPart = -1;
        if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
            WaitForSingleObject(timer, INFINITE);
            return;
        }
    }
    Sleep(static_cast<DWORD>(std::ceil(seconds * 1000)));
#else
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
#endif
}

void ForwardingLink::setTarget(CwKeying::Link *target) {
    m_target.store(target, std::memory_order_release);
}

void ForwardingLink::send(std::string_view command) {
    if (CwKeying::Link *target = m_target.load(std::memory_order_acquire))
        target->send(command);
}
