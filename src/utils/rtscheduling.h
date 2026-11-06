// SPDX-License-Identifier: GPL-3.0-or-later
//
// Scheduling class for QK4's timing-critical threads. A thread calls promoteCurrentThread() on
// itself, first thing, and gets the best the OS will grant, falling back quietly when it refuses:
//
//   class     | macOS                               | Windows                         | Linux / Pi
//   ----------|-------------------------------------|---------------------------------|---------------------------
//   RealTime  | Mach time-constraint policy, else   | MMCSS "Pro Audio" (high), else  | SCHED_FIFO 15 with
//             | QoS user-interactive                | TIME_CRITICAL, else HIGHEST     | SCHED_RESET_ON_FORK, else
//             |                                     |                                 | the High chain
//   High      | QoS user-interactive                | THREAD_PRIORITY_HIGHEST         | nice -10, else the lowest
//             |                                     |                                 | nice RLIMIT_NICE allows,
//             |                                     |                                 | else unchanged
//   Normal    | unchanged                           | unchanged                       | unchanged
//
// Linux also cuts the thread's timer slack to 1 ns for RealTime and High, whatever else it gets.
//
// The decision for each OS is a pure function over that OS's calls (applyMac, applyWindows,
// applyLinux), so every platform's fallback chain is unit-tested on every platform. A real-time
// thread may not log (CONVENTIONS Rule 16), so promoteCurrentThread() records its result in a
// fixed lock-free table and an ordinary thread reports it later with takeUnreported().

#ifndef RTSCHEDULING_H
#define RTSCHEDULING_H

#include <vector>

class QThread;

namespace RtScheduling {

enum class Class { RealTime, High, Normal };
enum class Outcome { RealTime, Elevated, Unchanged };

struct Result {
    Class requested = Class::Normal;
    Outcome outcome = Outcome::Unchanged;
    const char *mechanism = "default"; // a static string: what the thread runs under now
    int error = 0;                     // the OS error of the first refused step, 0 if none
    int nice = 0;                      // Linux: the nice value the thread now has, 0 if unchanged
};

// Promotes the calling thread and records the result under `threadName` (a string literal).
// Never allocates, locks or logs, so it is safe on the thread it promotes.
Result promoteCurrentThread(Class requested, const char *threadName);

// Every promoted thread, with the class it gets. One table so the policy is reviewed, and tested,
// in one place: the CW threads are real-time; the I/O and audio threads are High, never real-time
// (they do far more work per wake-up than a time-constraint or SCHED_FIFO budget allows, and a
// real-time thread that overruns starves the cores the CW threads need).
struct ThreadRole {
    const char *name; // also the name in the scheduling report
    Class schedulingClass;
};
namespace Threads {
inline constexpr ThreadRole kKeyer{"Keyer", Class::RealTime};
inline constexpr ThreadRole kSidetone{"Sidetone", Class::RealTime};
inline constexpr ThreadRole kHaliKey{"HaliKey", Class::RealTime};
inline constexpr ThreadRole kKpodPlusEp02{"KpodPlusEP02", Class::RealTime};
inline constexpr ThreadRole kIo{"I/O", Class::High};
inline constexpr ThreadRole kAudioRx{"AudioEngine", Class::High};
inline constexpr ThreadRole kAudioTx{"AudioTX", Class::High};
} // namespace Threads

inline Result promoteCurrentThread(const ThreadRole &role) {
    return promoteCurrentThread(role.schedulingClass, role.name);
}

// For a QThread running an event loop: promotes it on itself as it starts, before its event loop
// runs anything, and again each time it is restarted. Connect before start().
void promoteOnStart(QThread *thread, const ThreadRole &role);

struct Report {
    const char *thread;
    Result result;
};

// Every result recorded since the last call, oldest first. For an ordinary thread to log.
std::vector<Report> takeUnreported();

// ---- process-wide ----

// Windows: opts the whole process out of power throttling (EcoQoS) and of the timer-resolution
// throttling Windows 11 applies to a minimised or occluded window, so the CW threads keep their
// timing when QK4 is not in front. A no-op elsewhere. Returns a static description for the log.
const char *optOutOfPowerThrottling();

// macOS: while held, an NSProcessInfo activity (latency-critical, user-initiated) keeps App Nap
// and timer coalescing away from the process. QK4 holds one while connected to the radio. A no-op
// elsewhere. Main thread.
class LatencyCriticalActivity {
public:
    LatencyCriticalActivity() = default;
    ~LatencyCriticalActivity() { end(); }
    LatencyCriticalActivity(const LatencyCriticalActivity &) = delete;
    LatencyCriticalActivity &operator=(const LatencyCriticalActivity &) = delete;

    void begin();
    void end();

private:
    void *m_token = nullptr; // the activity object, retained (macOS only)
};

const char *className(Class c);
const char *outcomeName(Outcome o);

// ---- the per-OS decisions, behind injectable calls ----

// The Linux SCHED_FIFO priority: well below PipeWire's and JACK's data threads (rt.prio 88 by
// default), so QK4's threads never delay the audio server they feed.
constexpr int kLinuxFifoPriority = 15;
constexpr int kLinuxHighNice = -10;

struct MacOps {
    void *context = nullptr;
    int (*setQosUserInteractive)(void *context) = nullptr; // 0 or an errno
    int (*setTimeConstraint)(void *context) = nullptr;     // 0 or a kern_return_t
};
Result applyMac(Class requested, const MacOps &ops);

enum class WindowsPriority { Highest, TimeCritical };
struct WindowsOps {
    void *context = nullptr;
    int (*joinMmcssProAudio)(void *context) = nullptr;                    // 0 or GetLastError()
    int (*setThreadPriority)(void *context, WindowsPriority p) = nullptr; // 0 or GetLastError()
};
Result applyWindows(Class requested, const WindowsOps &ops);

struct LinuxOps {
    void *context = nullptr;
    int (*setFifo)(void *context, int priority) = nullptr; // 0 or an errno
    int (*setNice)(void *context, int nice) = nullptr;     // 0 or an errno
    int (*setTimerSlackNs)(void *context, unsigned long ns) = nullptr;
    int (*niceFloor)(void *context) = nullptr; // lowest nice RLIMIT_NICE allows: 20 - rlim_cur, -20..19
};
Result applyLinux(Class requested, const LinuxOps &ops);

} // namespace RtScheduling

#endif // RTSCHEDULING_H
