// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef HALIKEYSAFETY_H
#define HALIKEYSAFETY_H

#include <atomic>
#include <cstdint>

// One atomic snapshot prevents an input callback from arming a connection that was reset
// while it was checking the release. The upper bits distinguish connection/approval epochs.
class HaliKeySafety {
public:
    void disarm() { restart(0); }
    void enable(bool straight) { restart(kEnabled | (straight ? kStraight : 0)); }
    std::uint64_t epoch() const { return m_state.load(std::memory_order_acquire) & ~(kEpoch - 1); }
    bool armed() const { return m_state.load(std::memory_order_acquire) & kArmed; }

    bool serialSample(bool ditDown, bool dahDown) noexcept {
        auto state = m_state.load(std::memory_order_acquire);
        if (!(state & kEnabled))
            return false;
        if (state & kArmed)
            return true;
        if (!ditDown && ((state & kStraight) || !dahDown))
            m_state.compare_exchange_strong(state, state | kArmed, std::memory_order_acq_rel);
        return false; // The sample that arms never forwards an edge.
    }

    bool midiMessage(int status, int note) noexcept {
        auto state = m_state.load(std::memory_order_acquire);
        if (!(state & kEnabled))
            return false;
        if (state & kArmed)
            return true;
        if ((status != 0x80 && status != 0x90) || (note != 20 && note != 21))
            return false;
        const auto known = note == 20 ? kDitKnown : kDahKnown;
        const auto down = note == 20 ? kDitDown : kDahDown;
        auto next = state | known;
        if (status == 0x90)
            next |= down;
        else
            next &= ~down;
        const auto required = (state & kStraight) ? kDitKnown : kDitKnown | kDahKnown;
        const auto held = (state & kStraight) ? kDitDown : kDitDown | kDahDown;
        if ((next & required) == required && !(next & held))
            next |= kArmed;
        m_state.compare_exchange_strong(state, next, std::memory_order_acq_rel);
        return false;
    }

private:
    void restart(std::uint64_t flags) {
        auto state = m_state.load(std::memory_order_acquire);
        while (!m_state.compare_exchange_weak(state, ((state + kEpoch) & ~(kEpoch - 1)) | flags,
                                              std::memory_order_acq_rel)) {
        }
    }
    static constexpr std::uint64_t kEnabled = 1, kArmed = 2, kStraight = 4;
    static constexpr std::uint64_t kDitKnown = 8, kDahKnown = 16, kDitDown = 32, kDahDown = 64;
    static constexpr std::uint64_t kEpoch = 256;
    std::atomic<std::uint64_t> m_state{0};
};

#endif // HALIKEYSAFETY_H
