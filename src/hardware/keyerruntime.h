// SPDX-License-Identifier: GPL-3.0-or-later
//
// Production implementations of the seams in keyertiming.h: a steady clock with precise sleeps and
// a link onto the K4 connection's KZ egress. The tests use virtual ones instead
// (tests/paddlekeyersim.h).

#ifndef KEYERRUNTIME_H
#define KEYERRUNTIME_H

#include "hardware/keyertiming.h"

#include <atomic>

// std::chrono::steady_clock in seconds. Sleeps are as precise as the platform allows:
//   - macOS: mach_wait_until on an absolute mach_absolute_time deadline;
//   - Linux: clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME), with the calling thread's timer slack
//     cut from the default 50 µs to 1 ns on its first sleep;
//   - Windows: a high-resolution waitable timer (the default timer tick there is 15.6 ms).
// The keyer learns and subtracts whatever overrun is left (wake_overrun).
class SteadyKeyerClock : public CwKeying::Clock {
public:
    double now() override;
    void sleep(double seconds) override;
    void sleepUntil(double when) override;
};

// Forwards each command to a target link set at runtime (the K4 connection's KZ egress), or drops
// it while there is none. The target is an atomic pointer, so the keyer thread sends without a lock
// while the main thread wires it. The target must outlive every send: it is cleared only after
// the keyer thread is joined.
class ForwardingLink : public CwKeying::Link {
public:
    void setTarget(CwKeying::Link *target);
    void send(std::string_view command) override;

private:
    std::atomic<CwKeying::Link *> m_target{nullptr};
};

#endif // KEYERRUNTIME_H
