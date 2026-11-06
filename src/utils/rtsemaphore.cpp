// SPDX-License-Identifier: GPL-3.0-or-later

#include "utils/rtsemaphore.h"

#include <algorithm>
#include <cstdint>

#if defined(Q_OS_DARWIN)
#include <dispatch/dispatch.h>
#elif defined(Q_OS_WIN)
#include <climits>
#include <cmath>
#include <windows.h>
#elif defined(Q_OS_LINUX)
#include <cerrno>
#include <time.h>
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 30))
#define QK4_HAVE_SEM_CLOCKWAIT 1
#endif
#endif

#if defined(Q_OS_DARWIN)

RtSemaphore::RtSemaphore() : m_sem(dispatch_semaphore_create(0)) {}

RtSemaphore::~RtSemaphore() {
    dispatch_release(static_cast<dispatch_semaphore_t>(m_sem));
}

void RtSemaphore::post() noexcept {
    dispatch_semaphore_signal(static_cast<dispatch_semaphore_t>(m_sem));
}

void RtSemaphore::wait() {
    dispatch_semaphore_wait(static_cast<dispatch_semaphore_t>(m_sem), DISPATCH_TIME_FOREVER);
}

bool RtSemaphore::waitFor(double seconds) {
    const int64_t ns = seconds > 0 ? static_cast<int64_t>(std::min(seconds, 1e9) * 1e9) : 0;
    return dispatch_semaphore_wait(static_cast<dispatch_semaphore_t>(m_sem), dispatch_time(DISPATCH_TIME_NOW, ns)) == 0;
}

#elif defined(Q_OS_WIN)

RtSemaphore::RtSemaphore() : m_sem(CreateSemaphoreW(nullptr, 0, LONG_MAX, nullptr)) {}

RtSemaphore::~RtSemaphore() {
    if (m_sem)
        CloseHandle(static_cast<HANDLE>(m_sem));
}

void RtSemaphore::post() noexcept {
    ReleaseSemaphore(static_cast<HANDLE>(m_sem), 1, nullptr);
}

void RtSemaphore::wait() {
    WaitForSingleObject(static_cast<HANDLE>(m_sem), INFINITE);
}

// WHY rounded up: a timeout rounded down would return before the time asked for, and a caller
// waiting for a deadline would wait again for the rest.
bool RtSemaphore::waitFor(double seconds) {
    const DWORD ms = seconds > 0 ? static_cast<DWORD>(std::min(std::ceil(seconds * 1000), double(INFINITE - 1))) : 0;
    return WaitForSingleObject(static_cast<HANDLE>(m_sem), ms) == WAIT_OBJECT_0;
}

#elif defined(Q_OS_LINUX)

RtSemaphore::RtSemaphore() {
    sem_init(&m_sem, 0, 0);
}

RtSemaphore::~RtSemaphore() {
    sem_destroy(&m_sem);
}

void RtSemaphore::post() noexcept {
    sem_post(&m_sem);
}

void RtSemaphore::wait() {
    while (sem_wait(&m_sem) != 0 && errno == EINTR) {
    }
}

// WHY CLOCK_MONOTONIC through sem_clockwait (glibc 2.30): sem_timedwait's deadline is wall-clock
// time, which jumps when the clock is set. The deadline is absolute, so a wait interrupted by a
// signal resumes without adding to it.
bool RtSemaphore::waitFor(double seconds) {
    if (!(seconds > 0)) {
        int rc;
        while ((rc = sem_trywait(&m_sem)) != 0 && errno == EINTR) {
        }
        return rc == 0;
    }
#ifdef QK4_HAVE_SEM_CLOCKWAIT
    const clockid_t clockId = CLOCK_MONOTONIC;
#else
    const clockid_t clockId = CLOCK_REALTIME;
#endif
    timespec deadline{};
    clock_gettime(clockId, &deadline);
    const long long ns = static_cast<long long>(std::min(seconds, 1e9) * 1e9) + deadline.tv_nsec;
    deadline.tv_sec += static_cast<time_t>(ns / 1000000000LL);
    deadline.tv_nsec = static_cast<long>(ns % 1000000000LL);
    int rc;
#ifdef QK4_HAVE_SEM_CLOCKWAIT
    while ((rc = sem_clockwait(&m_sem, clockId, &deadline)) != 0 && errno == EINTR) {
    }
#else
    while ((rc = sem_timedwait(&m_sem, &deadline)) != 0 && errno == EINTR) {
    }
#endif
    return rc == 0;
}

#else
#error "RtSemaphore: no implementation for this platform"
#endif
