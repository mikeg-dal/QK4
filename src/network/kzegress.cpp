// SPDX-License-Identifier: GPL-3.0-or-later

#include "network/kzegress.h"

#include <chrono>
#include <cstring>

#ifdef Q_OS_WIN
#include <QWinEventNotifier>
#include <windows.h>
#else
#include <QSocketNotifier>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

KzEgress::KzEgress(QObject *parent) : QObject(parent), m_slots(std::make_unique<Slot[]>(kCapacity)) {
    for (std::size_t i = 0; i < kCapacity; ++i)
        m_slots[i].sequence.store(i, std::memory_order_relaxed);

#ifdef Q_OS_WIN
    m_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (m_event) {
        m_notifier = new QWinEventNotifier(static_cast<HANDLE>(m_event), this);
        connect(m_notifier, &QWinEventNotifier::activated, this, &KzEgress::onWake);
    }
#else
    int fds[2];
    if (::pipe(fds) == 0) {
        m_wakeRead = fds[0];
        m_wakeWrite = fds[1];
        for (int fd : fds) {
            ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
            ::fcntl(fd, F_SETFD, FD_CLOEXEC);
        }
        // WHY a child created here, before the owner moves to its thread: QSocketNotifier follows a
        // moveToThread() of its parent and re-arms itself on the new thread's event loop.
        m_notifier = new QSocketNotifier(m_wakeRead, QSocketNotifier::Read, this);
        connect(m_notifier, &QSocketNotifier::activated, this, &KzEgress::onWake);
    }
#endif
}

KzEgress::~KzEgress() {
#ifdef Q_OS_WIN
    delete m_notifier;
    if (m_event)
        CloseHandle(static_cast<HANDLE>(m_event));
#else
    delete m_notifier;
    if (m_wakeRead >= 0)
        ::close(m_wakeRead);
    if (m_wakeWrite >= 0)
        ::close(m_wakeWrite);
#endif
}

std::int64_t KzEgress::nowNs() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

bool KzEgress::post(std::string_view frame) noexcept {
    return postTagged(frame, Source::Kpod, 0, true);
}

void KzEgress::setSoftwareKeyingAllowed(bool allowed) {
    quint64 state = m_softwareState.load(std::memory_order_acquire);
    while (static_cast<bool>(state & 1U) != allowed) {
        const quint64 next = ((state >> 1) + 1) * 2 + static_cast<quint64>(allowed);
        if (m_softwareState.compare_exchange_weak(state, next, std::memory_order_acq_rel))
            return;
    }
}

void KzEgress::SoftwareLink::send(std::string_view command) noexcept {
    const quint64 state = m_owner->m_softwareState.load(std::memory_order_acquire);
    quint64 epoch = state >> 1;
    if (command.substr(0, 3) == "KZD")
        m_downEpoch = epoch;
    else if (command.substr(0, 3) == "KZU")
        epoch = m_downEpoch;
    m_owner->postTagged(command, Source::Software, epoch, (state & 1U) != 0);
}

bool KzEgress::postTagged(std::string_view frame, Source source, quint64 epoch, bool eligibleAtPost) noexcept {
    if (frame.empty() || frame.size() > kMaxFrameBytes) {
        m_dropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    std::size_t pos = m_enqueuePos.load(std::memory_order_relaxed);
    Slot *slot;
    while (true) {
        slot = &m_slots[pos & kMask];
        const std::size_t sequence = slot->sequence.load(std::memory_order_acquire);
        const auto diff = static_cast<std::intptr_t>(sequence) - static_cast<std::intptr_t>(pos);
        if (diff == 0) {
            if (m_enqueuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                break;
        } else if (diff < 0) {
            m_dropped.fetch_add(1, std::memory_order_relaxed);
            return false; // full
        } else {
            pos = m_enqueuePos.load(std::memory_order_relaxed);
        }
    }
    slot->enqueuedNs = nowNs();
    slot->length = static_cast<std::uint8_t>(frame.size());
    slot->source = source;
    slot->epoch = epoch;
    slot->eligibleAtPost = eligibleAtPost;
    std::memcpy(slot->data, frame.data(), frame.size());
    slot->sequence.store(pos + 1, std::memory_order_release);

    // WHY the fence and the exchange: the drain clears m_wakePending before it reads the queue (see
    // onWake). Either this post sees the flag clear and wakes the I/O thread, or the drain that is
    // about to run is ordered after the publish above and picks the frame up.
    std::atomic_thread_fence(std::memory_order_seq_cst);
    if (!m_wakePending.exchange(true, std::memory_order_acq_rel))
        wake();
    return true;
}

void KzEgress::wake() noexcept {
    if (!m_notifier) {
        // Only if the OS refused a pipe or event at construction: a queued call still gets the
        // frame out, at the cost of an allocation on the posting thread.
        // WHY allowed: a degraded fallback that a working OS never reaches; losing the frame instead
        // could leave the radio keyed.
        RtSan::Allow allow;
        QMetaObject::invokeMethod(this, &KzEgress::onWake, Qt::QueuedConnection);
        return;
    }
    // WHY allowed: the wake-up is one system call that cannot wait. The pipe is non-blocking (a full
    // one returns at once) and the event is auto-reset. RTSan reports any write() as I/O.
    RtSan::Allow allow;
#ifdef Q_OS_WIN
    SetEvent(static_cast<HANDLE>(m_event));
#else
    const char byte = 1;
    // A full pipe already holds a wake-up; nothing else can go wrong that a retry would fix.
    [[maybe_unused]] const auto written = ::write(m_wakeWrite, &byte, 1);
#endif
}

void KzEgress::onWake() {
#ifndef Q_OS_WIN
    char sink[64];
    while (m_wakeRead >= 0 && ::read(m_wakeRead, sink, sizeof sink) > 0) {
    }
#endif
    m_wakePending.store(false, std::memory_order_seq_cst);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    emit framesReady();
}
