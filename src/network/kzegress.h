// SPDX-License-Identifier: GPL-3.0-or-later
//
// The path from the CW keying threads to the K4 socket. The keyer thread (KZD/KZU/ML) and the KPOD+
// EP02 reader (the KPOD+'s own KZ strings) post frames here; the I/O thread, which owns the socket,
// drains them in order and writes them.
//
// Posting copies the frame into a preallocated slot of a bounded lock-free queue (Vyukov's bounded
// MPMC design, used here with one consumer) and, when the I/O thread is not already due to drain,
// wakes it through a pipe (Unix) or an event (Windows) watched by a Qt notifier. No lock, no
// allocation, no Qt event and no string is involved on the posting side, which is what a real-time
// thread may do (CONVENTIONS Rule 16). Frames from one thread stay in the order it posted them;
// frames from both threads are written in the order they claimed a slot.

#ifndef KZEGRESS_H
#define KZEGRESS_H

#include "hardware/keyertiming.h"
#include "utils/rtsan.h"

#include <QObject>
#include <QtGlobal>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string_view>

#ifdef Q_OS_WIN
class QWinEventNotifier;
#else
class QSocketNotifier;
#endif

class KzEgress : public QObject, public CwKeying::Link {
    Q_OBJECT

public:
    enum class Source { Kpod, Software };

    // WHY 1024: a frame is never dropped in practice, because a dropped KZU leaves the radio keyed.
    // 1024 frames is more than ten seconds of keying at 50 WPM with the I/O thread not draining at
    // all, by which time the session is lost anyway.
    static constexpr std::size_t kCapacity = 1024;
    // The longest frame: a KPOD+ EP02 transfer is 32 bytes, a keyer frame 8.
    static constexpr std::size_t kMaxFrameBytes = 56;

    explicit KzEgress(QObject *parent = nullptr);
    ~KzEgress() override;

    KzEgress(const KzEgress &) = delete;
    KzEgress &operator=(const KzEgress &) = delete;

    // Any thread. Copies the frame and wakes the owning thread. False, and counted as dropped, when
    // the frame is empty or longer than kMaxFrameBytes or the queue is full.
    bool post(std::string_view frame) noexcept QK4_NONBLOCKING;
    void send(std::string_view command) override { post(command); }

    // The software keyer is a single producer. Its UP keeps the epoch of its preceding DOWN,
    // even when a device switch changes the gate before that UP is posted.
    CwKeying::Link *softwareLink() { return &m_softwareLink; }
    void setSoftwareKeyingAllowed(bool allowed);
    bool softwareKeyingAllowed() const { return (m_softwareState.load(std::memory_order_acquire) & 1U) != 0; }
    quint64 softwareEpoch() const { return m_softwareState.load(std::memory_order_acquire) >> 1; }

    // Owning thread. Calls fn(std::string_view frame, std::int64_t enqueuedNs) for every frame
    // published so far, oldest first. Returns how many it handed over.
    template <typename Fn> int drain(Fn &&fn) {
        return drainTagged(
            [&fn](std::string_view frame, std::int64_t enqueuedNs, Source, quint64, bool) { fn(frame, enqueuedNs); });
    }

    template <typename Fn> int drainTagged(Fn &&fn) {
        int handed = 0;
        while (true) {
            Slot &slot = m_slots[m_dequeuePos & kMask];
            if (slot.sequence.load(std::memory_order_acquire) != m_dequeuePos + 1)
                return handed; // empty, or the next frame is claimed but not yet published
            fn(std::string_view(slot.data, slot.length), slot.enqueuedNs, slot.source, slot.epoch, slot.eligibleAtPost);
            slot.sequence.store(m_dequeuePos + kCapacity, std::memory_order_release);
            ++m_dequeuePos;
            ++handed;
        }
    }

    // Frames refused by post(), since construction. Any thread.
    std::uint64_t droppedCount() const { return m_dropped.load(std::memory_order_relaxed); }

    // The clock enqueuedNs is on: std::chrono::steady_clock, in nanoseconds.
    static std::int64_t nowNs();

signals:
    // On the owning thread, after one or more posts. Connect it to whatever calls drain().
    void framesReady();

private:
    class SoftwareLink final : public CwKeying::Link {
    public:
        explicit SoftwareLink(KzEgress *owner) : m_owner(owner) {}
        void send(std::string_view command) noexcept QK4_NONBLOCKING override;

    private:
        KzEgress *m_owner;
        quint64 m_downEpoch = 0; // only the software keyer thread writes this
    };

    struct Slot {
        std::atomic<std::size_t> sequence{0};
        std::int64_t enqueuedNs = 0;
        std::uint8_t length = 0;
        Source source = Source::Kpod;
        quint64 epoch = 0;
        bool eligibleAtPost = false;
        char data[kMaxFrameBytes] = {};
    };
    static constexpr std::size_t kMask = kCapacity - 1;
    static_assert((kCapacity & kMask) == 0, "capacity must be a power of two");

    void wake() noexcept;
    void onWake();
    bool postTagged(std::string_view frame, Source source, quint64 epoch, bool eligibleAtPost) noexcept QK4_NONBLOCKING;

    std::unique_ptr<Slot[]> m_slots;
    alignas(64) std::atomic<std::size_t> m_enqueuePos{0};
    alignas(64) std::size_t m_dequeuePos = 0; // owning thread only
    std::atomic<bool> m_wakePending{false};
    std::atomic<std::uint64_t> m_dropped{0};
    std::atomic<quint64> m_softwareState{0}; // generation in high bits, allowed in bit 0
    SoftwareLink m_softwareLink{this};

#ifdef Q_OS_WIN
    void *m_event = nullptr; // HANDLE, auto-reset
    QWinEventNotifier *m_notifier = nullptr;
#else
    int m_wakeRead = -1;
    int m_wakeWrite = -1;
    QSocketNotifier *m_notifier = nullptr;
#endif
};

#endif // KZEGRESS_H
