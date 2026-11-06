// SPDX-License-Identifier: GPL-3.0-or-later
//
// Port of K4-Companion's SerialInput.signal_loop (Dale Farnsworth, GPL-3.0; k4companion:18890).

#include "hardware/seriallinepoller.h"

SerialLinePoller::SerialLinePoller(PaddleKeyer::Signal dit, PaddleKeyer::Signal dah, bool straightKeying)
    : m_dit(dit), m_dah(dah), m_straight(straightKeying), m_debounce(straightKeying ? kStraightKeyDebounceSeconds : 0) {
}

bool SerialLinePoller::start(const ReadLine &read) {
    if (!read)
        return false;
    m_ditState = read(m_dit);
    m_dahState = read(m_dah) && !m_straight;
    m_ditTime.reset();
    m_dahTime.reset();
    return true;
}

SerialLinePoller::Changes SerialLinePoller::poll(const ReadLine &read, double now) noexcept {
    Changes out;
    const bool ditState = read(m_dit);
    const bool dahState = read(m_dah) && !m_straight;
    if (ditState != m_ditState) {
        m_ditTime = now;
        m_ditState = ditState;
    }
    if (dahState != m_dahState) {
        m_dahTime = now;
        m_dahState = dahState;
    }
    if (m_ditTime && now - *m_ditTime >= m_debounce) {
        const double delta = m_previousDitTime ? *m_ditTime - *m_previousDitTime : 0;
        out.items[out.count++] = {m_dit, ditState, delta};
        m_previousDitTime = m_ditTime;
        m_ditTime.reset();
    }
    if (m_dahTime && now - *m_dahTime >= m_debounce) {
        const double delta = m_previousDahTime ? *m_dahTime - *m_previousDahTime : 0;
        out.items[out.count++] = {m_dah, dahState, delta};
        m_previousDahTime = m_dahTime;
        m_dahTime.reset();
    }
    return out;
}
