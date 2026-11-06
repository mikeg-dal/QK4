// SPDX-License-Identifier: GPL-3.0-or-later
//
// A fixed-capacity, lock-free, wait-free ring for exactly ONE producer and ONE consumer at a time.
// Header-only and Qt-free. Preallocated: nothing in it locks, allocates or waits.
//
// Two ways to use it. In place, so neither side copies an element it does not need to: the
// producer fills the slot beginWrite() hands it and publishes it with commitWrite(); the consumer
// reads (and may modify) front() and releases it with pop(). Or by value, for trivially copyable
// items: push(item) and pop(out). A full ring refuses the write; what to do then is the
// producer's decision.
//
// "One producer" means one write at a time: several threads may produce if something else (a
// mutex they all hold) serialises them, which also orders their writes. Likewise the consumer.
//
// Memory order: the producer's release store of m_tail publishes the slot contents to the
// consumer's acquire load; the consumer's release store of m_head hands the slot back to the
// producer's acquire load. Each index is written by one side only.
//
// Two producers, or two consumers, writing at once corrupt it silently; ThreadSanitizer is the
// check (SpscRingTests runs under it).

#ifndef SPSCRING_H
#define SPSCRING_H

#include "utils/rtsan.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

template <typename T, std::size_t Capacity> class SpscRing {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    static constexpr std::size_t capacity() { return Capacity; }

    // Producer. The slot to fill, or nullptr when the ring is full. Calling it twice without a
    // commitWrite() returns the same slot.
    T *beginWrite() noexcept QK4_NONBLOCKING {
        const std::size_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail - m_head.load(std::memory_order_acquire) >= Capacity)
            return nullptr;
        return &m_slots[tail & (Capacity - 1)];
    }

    // Producer. Publish the slot beginWrite() returned.
    void commitWrite() noexcept QK4_NONBLOCKING {
        m_tail.store(m_tail.load(std::memory_order_relaxed) + 1, std::memory_order_release);
    }

    // Producer. Copy item into the ring; false when full, and the item is not queued.
    bool push(const T &item) noexcept QK4_NONBLOCKING {
        static_assert(std::is_trivially_copyable_v<T>, "push() copies by value; use beginWrite()");
        T *slot = beginWrite();
        if (!slot)
            return false;
        *slot = item;
        commitWrite();
        return true;
    }

    // Consumer. The oldest published slot, or nullptr when empty.
    T *front() noexcept QK4_NONBLOCKING {
        const std::size_t head = m_head.load(std::memory_order_relaxed);
        if (head == m_tail.load(std::memory_order_acquire))
            return nullptr;
        return &m_slots[head & (Capacity - 1)];
    }

    // Consumer. The i-th published slot from the front (0 = front()), or nullptr past the end.
    // Lets the consumer look at what is queued - for the jitter policy - without popping it.
    T *peek(std::size_t i) noexcept QK4_NONBLOCKING {
        const std::size_t head = m_head.load(std::memory_order_relaxed);
        if (i >= m_tail.load(std::memory_order_acquire) - head)
            return nullptr;
        return &m_slots[(head + i) & (Capacity - 1)];
    }

    // Consumer. Release front() back to the producer. Must not be called on an empty ring.
    void pop() noexcept QK4_NONBLOCKING {
        m_head.store(m_head.load(std::memory_order_relaxed) + 1, std::memory_order_release);
    }

    // Consumer. Copy the oldest item out and release it; false when empty.
    bool pop(T &out) noexcept QK4_NONBLOCKING {
        static_assert(std::is_trivially_copyable_v<T>, "pop(out) copies by value; use front()");
        T *slot = front();
        if (!slot)
            return false;
        out = *slot;
        pop();
        return true;
    }

    // Published slots. Exact on the consumer thread (only the producer can make it grow
    // meanwhile); a snapshot on any other.
    std::size_t size() const noexcept QK4_NONBLOCKING {
        return m_tail.load(std::memory_order_acquire) - m_head.load(std::memory_order_acquire);
    }

    bool empty() const noexcept QK4_NONBLOCKING { return size() == 0; }

private:
    // Separate cache lines so the two sides do not false-share their indices.
    alignas(64) std::atomic<std::size_t> m_head{0}; // written by the consumer only
    alignas(64) std::atomic<std::size_t> m_tail{0}; // written by the producer only
    alignas(64) std::array<T, Capacity> m_slots{};
};

#endif // SPSCRING_H
