// SPDX-License-Identifier: GPL-3.0-or-later

#include "utils/rtscheduling.h"

#include <QThread>
#include <QtGlobal>
#include <algorithm>
#include <array>
#include <atomic>
#include <utility>

#if defined(Q_OS_MACOS)
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#include <pthread/qos.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#include <avrt.h>
#elif defined(Q_OS_LINUX)
#include <cerrno>
#include <sched.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#ifndef SCHED_RESET_ON_FORK
#define SCHED_RESET_ON_FORK 0x40000000
#endif
#endif

namespace RtScheduling {

const char *className(Class c) {
    switch (c) {
    case Class::RealTime:
        return "real-time";
    case Class::High:
        return "high";
    case Class::Normal:
        break;
    }
    return "normal";
}

const char *outcomeName(Outcome o) {
    switch (o) {
    case Outcome::RealTime:
        return "real-time";
    case Outcome::Elevated:
        return "raised priority, not real-time";
    case Outcome::Unchanged:
        break;
    }
    return "unchanged";
}

// ---- decisions ----

Result applyMac(Class requested, const MacOps &ops) {
    Result r;
    r.requested = requested;
    if (requested == Class::Normal)
        return r;
    // QoS first in both classes: it steers the thread onto performance cores, and it is what is
    // left if the time-constraint policy is refused.
    const int qos = ops.setQosUserInteractive(ops.context);
    if (requested == Class::RealTime) {
        const int tc = ops.setTimeConstraint(ops.context);
        if (tc == 0) {
            r.outcome = Outcome::RealTime;
            r.mechanism = "Mach time-constraint policy (1 ms period, 0.2 ms computation)";
            return r;
        }
        r.error = tc;
    }
    if (qos == 0) {
        r.outcome = Outcome::Elevated;
        r.mechanism = requested == Class::RealTime ? "QoS user-interactive (time-constraint policy refused)"
                                                   : "QoS user-interactive";
    } else {
        r.mechanism = "default (QoS refused)";
        if (r.error == 0)
            r.error = qos;
    }
    return r;
}

Result applyWindows(Class requested, const WindowsOps &ops) {
    Result r;
    r.requested = requested;
    if (requested == Class::Normal)
        return r;
    if (requested == Class::RealTime) {
        const int mmcss = ops.joinMmcssProAudio(ops.context);
        if (mmcss == 0) {
            r.outcome = Outcome::RealTime;
            r.mechanism = "MMCSS Pro Audio, high";
            return r;
        }
        r.error = mmcss;
        if (ops.setThreadPriority(ops.context, WindowsPriority::TimeCritical) == 0) {
            r.outcome = Outcome::Elevated;
            r.mechanism = "THREAD_PRIORITY_TIME_CRITICAL (MMCSS refused)";
            return r;
        }
    }
    const int highest = ops.setThreadPriority(ops.context, WindowsPriority::Highest);
    if (highest == 0) {
        r.outcome = Outcome::Elevated;
        r.mechanism = requested == Class::RealTime ? "THREAD_PRIORITY_HIGHEST (MMCSS and TIME_CRITICAL refused)"
                                                   : "THREAD_PRIORITY_HIGHEST";
    } else {
        r.mechanism = "default (every priority refused)";
        if (r.error == 0)
            r.error = highest;
    }
    return r;
}

Result applyLinux(Class requested, const LinuxOps &ops) {
    Result r;
    r.requested = requested;
    if (requested == Class::Normal)
        return r;
    // Ignored once SCHED_FIFO is granted (real-time threads have no slack); what matters for the
    // fallback, where every sleep would otherwise be stretched by up to 50 µs.
    ops.setTimerSlackNs(ops.context, 1);
    if (requested == Class::RealTime) {
        const int fifo = ops.setFifo(ops.context, kLinuxFifoPriority);
        if (fifo == 0) {
            r.outcome = Outcome::RealTime;
            r.mechanism = "SCHED_FIFO 15, reset on fork";
            return r;
        }
        r.error = fifo;
    }
    const bool rt = requested == Class::RealTime;
    const int nice = ops.setNice(ops.context, kLinuxHighNice);
    if (nice == 0) {
        r.outcome = Outcome::Elevated;
        r.nice = kLinuxHighNice;
        r.mechanism = rt ? "nice -10, timer slack 1 ns (SCHED_FIFO refused)" : "nice -10, timer slack 1 ns";
        return r;
    }
    if (r.error == 0)
        r.error = nice;
    // WHY a second attempt: an unprivileged thread may lower its nice value as far as RLIMIT_NICE
    // allows ("The actual ceiling for the nice value is calculated as 20 - rlim_cur", getrlimit(2)),
    // and a limit that allows -5 must not end as no promotion at all because -10 was asked first.
    // CAP_SYS_NICE ignores the limit, which is why -10 is still tried before it is read.
    const int floor = ops.niceFloor(ops.context);
    if (floor > kLinuxHighNice && floor < 0 && ops.setNice(ops.context, floor) == 0) {
        r.outcome = Outcome::Elevated;
        r.nice = floor;
        r.mechanism = rt ? "nice at the RLIMIT_NICE limit, timer slack 1 ns (SCHED_FIFO and nice -10 refused)"
                         : "nice at the RLIMIT_NICE limit, timer slack 1 ns (nice -10 refused)";
        return r;
    }
    r.mechanism = rt ? "SCHED_OTHER, timer slack 1 ns (SCHED_FIFO and nice refused)"
                     : "SCHED_OTHER, timer slack 1 ns (nice refused)";
    return r;
}

// ---- the real calls ----

namespace {

#if defined(Q_OS_MACOS)
int macQos(void *) {
    return pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
}

// WHY the time-constraint policy and not QoS alone: measured on macOS 2026-10-29, a 0.7 ms
// mach_wait_until woke 188 µs late at the median with default scheduling and the same with QoS
// user-interactive (macOS adds timer leeway to every non-real-time wait), against 10 µs median and
// 52 µs at the 99th percentile under this policy. The values describe a thread that wakes about
// once a millisecond and does little each time; these threads all block between wake-ups, so the
// kernel's fail-safe for real-time threads that overrun never has reason to demote them.
int macTimeConstraint(void *) {
    mach_timebase_info_data_t tb{};
    if (mach_timebase_info(&tb) != KERN_SUCCESS || tb.numer == 0)
        return KERN_FAILURE;
    const double ticksPerMs = 1e6 * tb.denom / tb.numer;
    thread_time_constraint_policy_data_t policy{};
    policy.period = static_cast<uint32_t>(1.0 * ticksPerMs);
    policy.computation = static_cast<uint32_t>(0.2 * ticksPerMs);
    policy.constraint = static_cast<uint32_t>(1.0 * ticksPerMs);
    policy.preemptible = TRUE;
    const thread_port_t self = pthread_mach_thread_np(pthread_self());
    return thread_policy_set(self, THREAD_TIME_CONSTRAINT_POLICY, reinterpret_cast<thread_policy_t>(&policy),
                             THREAD_TIME_CONSTRAINT_POLICY_COUNT);
}
#elif defined(Q_OS_WIN)
// Leaves MMCSS when the thread exits, as AvRevertMmThreadCharacteristics asks.
struct MmcssRegistration {
    HANDLE handle = nullptr;
    ~MmcssRegistration() {
        if (handle)
            AvRevertMmThreadCharacteristics(handle);
    }
};
thread_local MmcssRegistration t_mmcss;

int winMmcss(void *) {
    if (t_mmcss.handle)
        return 0;
    DWORD taskIndex = 0;
    HANDLE h = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    if (!h)
        return static_cast<int>(GetLastError());
    t_mmcss.handle = h;
    AvSetMmThreadPriority(h, AVRT_PRIORITY_HIGH);
    return 0;
}

int winPriority(void *, WindowsPriority p) {
    const int level = p == WindowsPriority::TimeCritical ? THREAD_PRIORITY_TIME_CRITICAL : THREAD_PRIORITY_HIGHEST;
    return SetThreadPriority(GetCurrentThread(), level) ? 0 : static_cast<int>(GetLastError());
}
#elif defined(Q_OS_LINUX)
int linuxFifo(void *, int priority) {
    sched_param param{};
    param.sched_priority = priority;
    // WHY sched_setscheduler with pid 0: on Linux it applies to the calling thread, and unlike
    // pthread_setschedparam it takes SCHED_RESET_ON_FORK, so a child process never inherits it.
    return sched_setscheduler(0, SCHED_FIFO | SCHED_RESET_ON_FORK, &param) == 0 ? 0 : errno;
}

int linuxNice(void *, int nice) {
    const auto tid = static_cast<id_t>(syscall(SYS_gettid));
    return setpriority(PRIO_PROCESS, tid, nice) == 0 ? 0 : errno;
}

int linuxNiceFloor(void *) {
    rlimit limit{};
    if (getrlimit(RLIMIT_NICE, &limit) != 0)
        return 0;
    if (limit.rlim_cur == RLIM_INFINITY || limit.rlim_cur >= 40)
        return -20;
    return 20 - static_cast<int>(limit.rlim_cur);
}

int linuxSlack(void *, unsigned long ns) {
    return prctl(PR_SET_TIMERSLACK, ns, 0UL, 0UL, 0UL) == 0 ? 0 : errno;
}
#endif

// ---- the report table ----

constexpr int kSlots = 32;
enum SlotState : int { Empty, Writing, Ready, Reading };

struct Slot {
    std::atomic<int> state{Empty};
    std::atomic<unsigned long long> order{0};
    const char *thread = nullptr;
    Result result;
};

std::array<Slot, kSlots> g_slots;
std::atomic<unsigned long long> g_order{0};

void record(const char *thread, const Result &result) {
    for (Slot &slot : g_slots) {
        int expected = Empty;
        if (!slot.state.compare_exchange_strong(expected, Writing, std::memory_order_acquire))
            continue;
        slot.thread = thread;
        slot.result = result;
        slot.order.store(g_order.fetch_add(1, std::memory_order_relaxed), std::memory_order_relaxed);
        slot.state.store(Ready, std::memory_order_release);
        return;
    }
    // Every slot holds an unreported result: this one is not reported. Nothing else is affected.
}

} // namespace

Result promoteCurrentThread(Class requested, const char *threadName) {
#if defined(Q_OS_MACOS)
    const Result r = applyMac(requested, MacOps{nullptr, macQos, macTimeConstraint});
#elif defined(Q_OS_WIN)
    const Result r = applyWindows(requested, WindowsOps{nullptr, winMmcss, winPriority});
#elif defined(Q_OS_LINUX)
    const Result r = applyLinux(requested, LinuxOps{nullptr, linuxFifo, linuxNice, linuxSlack, linuxNiceFloor});
#else
    Result r;
    r.requested = requested;
    r.mechanism = "default (no scheduling support on this OS)";
#endif
    record(threadName, r);
    return r;
}

void promoteOnStart(QThread *thread, const ThreadRole &role) {
    // WHY started with a direct connection: QThread emits started "from the associated thread when
    // it starts executing" (QThread::started), so a direct slot runs on the new thread, which is the
    // only thread a macOS QoS class or a Linux per-thread nice can be applied to from here.
    const ThreadRole copy = role;
    QObject::connect(thread, &QThread::started, thread, [copy] { promoteCurrentThread(copy); }, Qt::DirectConnection);
}

#if defined(Q_OS_WIN)
const char *optOutOfPowerThrottling() {
#ifndef PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
#define PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION 0x4
#endif
    PROCESS_POWER_THROTTLING_STATE state{};
    state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    // ControlMask: the policies this process decides for itself; StateMask 0: each one off.
    state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    state.StateMask = 0;
    if (SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof state))
        return "power throttling and timer-resolution throttling off";
    // Older Windows 10 knows only EXECUTION_SPEED; ask for that alone.
    state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    if (SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof state))
        return "power throttling off (this Windows cannot exempt timer resolution)";
    return "power throttling left to Windows (SetProcessInformation refused)";
}
#else
const char *optOutOfPowerThrottling() {
    return nullptr;
}
#endif

#ifndef Q_OS_MACOS
void LatencyCriticalActivity::begin() {}
void LatencyCriticalActivity::end() {}
#endif

std::vector<Report> takeUnreported() {
    std::vector<std::pair<unsigned long long, Report>> found;
    for (Slot &slot : g_slots) {
        int expected = Ready;
        if (!slot.state.compare_exchange_strong(expected, Reading, std::memory_order_acquire))
            continue;
        found.push_back({slot.order.load(std::memory_order_relaxed), Report{slot.thread, slot.result}});
        slot.state.store(Empty, std::memory_order_release);
    }
    std::sort(found.begin(), found.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    std::vector<Report> out;
    out.reserve(found.size());
    for (const auto &f : found)
        out.push_back(f.second);
    return out;
}

} // namespace RtScheduling
