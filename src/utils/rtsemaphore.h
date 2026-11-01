// SPDX-License-Identifier: GPL-3.0-or-later
//
// Counting semaphore for waking a real-time thread. post() never takes a lock the waiter holds and
// never allocates, so any thread, including a normal-priority one, may wake a real-time thread
// without the priority inversion a shared mutex and condition variable would allow. Built on the
// OS semaphore: dispatch_semaphore on macOS, sem_t on Linux, a Win32 semaphore on Windows.
// (std::counting_semaphore is C++20; the project is C++17.)

#ifndef RTSEMAPHORE_H
#define RTSEMAPHORE_H

#include "utils/rtsan.h"

#include <QtGlobal>

#if defined(Q_OS_LINUX)
#include <semaphore.h>
#endif

class RtSemaphore {
public:
    RtSemaphore();
    ~RtSemaphore();
    RtSemaphore(const RtSemaphore &) = delete;
    RtSemaphore &operator=(const RtSemaphore &) = delete;

    void post() noexcept QK4_NONBLOCKING; // any thread
    void wait();                          // blocks until a post is available, and takes it
    // As wait(), but gives up after `seconds` (at once if not positive). True if a post was taken.
    bool waitFor(double seconds);

private:
#if defined(Q_OS_LINUX)
    sem_t m_sem;
#else
    void *m_sem = nullptr; // dispatch_semaphore_t or HANDLE
#endif
};

#endif // RTSEMAPHORE_H
