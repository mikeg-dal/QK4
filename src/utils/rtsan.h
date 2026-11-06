// SPDX-License-Identifier: GPL-3.0-or-later
//
// RealtimeSanitizer markers (CONVENTIONS Rule 16). Clang's RTSan (-fsanitize=realtime) treats a
// function marked QK4_NONBLOCKING, and everything it calls, as a real-time context: a call there to
// malloc/free, a mutex, a sleep, a file or socket call, or anything marked [[clang::blocking]] is
// reported, and the test fails. The test build that uses it is QK4_TESTS_RTSAN
// (tests/CMakeLists.txt), run by the RealtimeSanitizer job in ci-checks.yml.
//
// What to mark: the work a real-time thread does between two wake-ups, per element, edge, packet or
// block. Not the loop itself: a real-time thread must block between wake-ups (Rule 16), and the
// wait would be reported. Not configuration, start-up or shutdown either.
//
// Where it goes: after the parameter list, any cv/ref qualifier and noexcept, on the declaration
// (the definition inherits it). The function is noexcept too: Apple Clang warns otherwise
// (-Wperf-constraint-implies-noexcept), and real-time code does not throw.
//     void pump() noexcept QK4_NONBLOCKING;
//     bool push(const T &item) noexcept QK4_NONBLOCKING;
//
// It expands to nothing on MSVC, GCC and Clang without the attribute. Where Clang has it, it
// changes nothing but an RTSan build and the opt-in -Wfunction-effects (not in -Wall or -Wextra).
//
// RtSan::Allow is the exception, for a call that RTSan cannot tell from a blocking one but that is
// bounded by construction, such as a one-byte write to a non-blocking wake-up pipe, or the wait a
// real-time thread blocks in between wake-ups. Its scope is the statement it covers, and every use
// carries a WHY saying why that call is allowed. It compiles to nothing outside an RTSan build.
//
// RtSan::BoundedMutex is a std::mutex that RTSan does not report. Rule 16 allows a real-time thread
// a lock that no thread holds for long; RTSan reports every lock, so the locks the rule allows are
// marked by this type. Its contract, which review checks and RTSan cannot: no real-time thread ever
// waits long for it. Every holder that a real-time thread can wait on holds it for a short
// computation only: never across a sleep, a wait, a send or an allocation, and calling out only into
// code that keeps the same contract (another BoundedMutex section, a semaphore post). Anything a
// real-time thread does while holding it is still checked.

#ifndef RTSAN_H
#define RTSAN_H

#if defined(__has_cpp_attribute)
#if __has_cpp_attribute(clang::nonblocking)
#define QK4_NONBLOCKING [[clang::nonblocking]]
#endif
#endif
#ifndef QK4_NONBLOCKING
#define QK4_NONBLOCKING
#endif

#if defined(__has_feature)
#if __has_feature(realtime_sanitizer)
#define QK4_HAS_RTSAN 1
#endif
#endif

#ifdef QK4_HAS_RTSAN
#include <sanitizer/rtsan_interface.h>
#endif

#include <mutex>

namespace RtSan {

#ifdef QK4_HAS_RTSAN
using Allow = __rtsan::ScopedDisabler;
#else
struct Allow {
    // WHY user-provided: a trivial guard would be reported as an unused variable.
    Allow() {}
    ~Allow() {}
    Allow(const Allow &) = delete;
    Allow &operator=(const Allow &) = delete;
};
#endif

class BoundedMutex {
public:
    BoundedMutex() = default;
    BoundedMutex(const BoundedMutex &) = delete;
    BoundedMutex &operator=(const BoundedMutex &) = delete;

    void lock() {
        Allow allow;
        m_mutex.lock();
    }
    bool try_lock() {
        Allow allow;
        return m_mutex.try_lock();
    }
    void unlock() {
        Allow allow;
        m_mutex.unlock();
    }

private:
    std::mutex m_mutex;
};

} // namespace RtSan

#endif // RTSAN_H
