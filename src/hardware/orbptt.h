// SPDX-License-Identifier: GPL-3.0-or-later
//
// When the ORB's PTT/KEY jack may key the transmitter, and when it must let go. Pure logic, so
// every order of switch, mode and setting changes can be tested without a device or a radio.

#ifndef ORBPTT_H
#define ORBPTT_H

#include "models/radiostate.h"

namespace OrbPtt {

struct Inputs {
    bool enabled = false;     // the operator turned the option on
    bool orbSelected = false; // the paddle device type is the ORB
    RadioState::Mode modeA = RadioState::Unknown;
    RadioState::Mode modeB = RadioState::Unknown;
    bool split = false; // transmitting on VFO B
};

inline bool isVoice(RadioState::Mode mode) {
    return mode == RadioState::USB || mode == RadioState::LSB || mode == RadioState::AM || mode == RadioState::FM;
}

// Voice on the VFO that transmits. WHY VFO A must not be in CW either: QK4's keyer follows VFO A,
// so in split with A in CW the same contact would be a straight key and open the microphone too.
inline bool allowed(const Inputs &in) {
    const bool keyerLive = in.modeA == RadioState::CW || in.modeA == RadioState::CW_R;
    return in.enabled && in.orbSelected && !keyerLive && isVoice(in.split ? in.modeB : in.modeA);
}

enum class Action { None, Engage, Release };

// Keys only on a press made while allowed. A switch already held when it becomes allowed (leaving
// CW, turning the option on) does nothing until it is released and pressed again.
class Gate {
public:
    /// The switch closed or opened. Opening it, or losing the device, is jack(false, ...).
    Action jack(bool pressed, bool isAllowed) {
        if (!pressed)
            return release();
        if (!isAllowed || m_holding)
            return Action::None;
        m_holding = true;
        return Action::Engage;
    }

    /// The mode, split or a setting changed.
    Action allowedChanged(bool isAllowed) { return isAllowed ? Action::None : release(); }

private:
    Action release() {
        if (!m_holding)
            return Action::None;
        m_holding = false;
        return Action::Release;
    }

    bool m_holding = false;
};

} // namespace OrbPtt

#endif // ORBPTT_H
