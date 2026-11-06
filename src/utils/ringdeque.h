// SPDX-License-Identifier: GPL-3.0-or-later
//
// A double-ended queue on one preallocated ring, for one thread at a time (callers that share it
// lock around it). The subset of std::deque the keyer and sidetone use: push at the back, pop at
// the front, index from the front, insert anywhere.
//
// WHY not std::deque: a deque allocates and frees fixed-size blocks as items pass through it,
// whatever its size, so a real-time thread that pushes and pops one allocates every few dozen items
// (libstdc++: every 512 bytes), which CONVENTIONS Rule 16 forbids and RealtimeSanitizer reports.
// This one allocates once, at construction, and again only if it outgrows that: it doubles rather
// than refuse, so it never drops an item a deque would have kept. Size the capacity so that it
// does not happen; RTSan reports it if it does on a real-time thread.

#ifndef RINGDEQUE_H
#define RINGDEQUE_H

#include <cstddef>
#include <utility>
#include <vector>

template <typename T> class RingDeque {
public:
    // Room for at least `capacity` items before the first growth.
    explicit RingDeque(std::size_t capacity) : m_items(roundUpToPowerOfTwo(capacity)) {}

    bool empty() const noexcept { return m_size == 0; }
    std::size_t size() const noexcept { return m_size; }
    std::size_t capacity() const noexcept { return m_items.size(); }

    // i-th item from the front. i < size().
    T &operator[](std::size_t i) noexcept { return m_items[(m_head + i) & mask()]; }
    const T &operator[](std::size_t i) const noexcept { return m_items[(m_head + i) & mask()]; }
    T &front() noexcept { return (*this)[0]; }
    const T &front() const noexcept { return (*this)[0]; }

    void push_back(T item) {
        if (m_size == m_items.size())
            grow();
        (*this)[m_size] = std::move(item);
        ++m_size;
    }

    // Before the item now at `index` (index == size() appends).
    void insert(std::size_t index, T item) {
        if (m_size == m_items.size())
            grow();
        for (std::size_t i = m_size; i > index; --i)
            (*this)[i] = std::move((*this)[i - 1]);
        (*this)[index] = std::move(item);
        ++m_size;
    }

    // Not on an empty queue.
    void pop_front() noexcept {
        m_head = (m_head + 1) & mask();
        --m_size;
    }

    void clear() noexcept {
        m_head = 0;
        m_size = 0;
    }

private:
    static std::size_t roundUpToPowerOfTwo(std::size_t n) {
        std::size_t p = 1;
        while (p < n)
            p <<= 1;
        return p;
    }
    std::size_t mask() const noexcept { return m_items.size() - 1; }

    void grow() {
        std::vector<T> bigger(m_items.size() * 2);
        for (std::size_t i = 0; i < m_size; ++i)
            bigger[i] = std::move((*this)[i]);
        m_items.swap(bigger);
        m_head = 0;
    }

    std::vector<T> m_items; // size is a power of two
    std::size_t m_head = 0;
    std::size_t m_size = 0;
};

#endif // RINGDEQUE_H
