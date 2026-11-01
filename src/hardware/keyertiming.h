// SPDX-License-Identifier: GPL-3.0-or-later
//
// Seams the paddle keyer is written against: a clock and the link to the K4. Production wires them
// to steady_clock and TcpClient's KZ egress; the tests wire them to a virtual clock so a whole
// keying run is deterministic.
//
// The keyer and its sidetone scheduler are a port of the keyer in Dale Farnsworth's K4-Companion
// (GPL-3.0, https://github.com/DaleFarnsworth/K4-Companion). See docs/cw-keyer-port-plan.md.

#ifndef KEYERTIMING_H
#define KEYERTIMING_H

#include <cmath>
#include <string_view>

namespace CwKeying {

// WHY seconds as double: the port has to reproduce K4-Companion's arithmetic bit for bit, so the
// golden runs in tests/data/paddlekeyer/ replay exactly. Python's float is an IEEE double and every
// expression is kept in its original order.
class Clock {
public:
    virtual ~Clock() = default;
    virtual double now() = 0;
    virtual void sleep(double seconds) = 0;
    // Sleep until now() reaches `when`. A real clock waits for the absolute deadline, so time lost
    // between deciding to sleep and sleeping (a preemption, a signal) is not added to the wait.
    virtual void sleepUntil(double when) { sleep(when - now()); }
};

// WHY a view: the keyer formats each frame into a stack buffer and the egress copies it into a
// preallocated slot, so sending a KZ never allocates on the keyer or EP02 thread.
class Link {
public:
    virtual ~Link() = default;
    virtual void send(std::string_view command) = 0;
};

// WHY: Python's round() rounds half to even. std::nearbyint does the same under the default
// FE_TONEAREST mode; std::lround would round half away from zero and move boundary runs.
inline long long pyRound(double x) {
    return static_cast<long long>(std::nearbyint(x));
}

} // namespace CwKeying

#endif // KEYERTIMING_H
