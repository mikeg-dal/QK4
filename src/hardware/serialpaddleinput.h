// SPDX-License-Identifier: GPL-3.0-or-later
//
// Raw modem lines to paddle changes for a serial paddle device: which line is which contact on
// each device, fed through SerialLinePoller. No port I/O, so the whole path is testable.

#ifndef SERIALPADDLEINPUT_H
#define SERIALPADDLEINPUT_H

#include "hardware/halikeydevicetype.h"
#include "hardware/seriallinepoller.h"
#include "utils/rtsan.h"

/// One joint reading of the port's modem-status inputs. true = asserted.
struct ModemLines {
    bool cts = false;
    bool dsr = false;
    bool dcd = false;
};

class SerialPaddleInput {
public:
    // SerialLinePoller names its two inputs by modem line. Here they are only labels for the
    // logical dit and dah; which physical line feeds each is decided by map().
    static constexpr PaddleKeyer::Signal kDit = PaddleKeyer::Signal::CTS;
    static constexpr PaddleKeyer::Signal kDah = PaddleKeyer::Signal::DCD;

    struct Contacts {
        bool dit = false;
        bool dah = false;
    };

    // WHY these maps:
    //   HaliKey V1.4 (bench-verified, normal paddle wiring): the dah lever drives DCD and DSR
    //   together, so DCD || DSR is one stable edge. The dit lever drives CTS, and so does the foot
    //   pedal, which is why footswitch PTT is withdrawn (see cwcontroller.h).
    //   ORB Control Device (verified on Windows and macOS): paddle jack tip = DSR, ring = DCD.
    //   The separate PTT/KEY jack drives CTS, and is where a straight key plugs in.
    static constexpr Contacts map(int deviceType, ModemLines lines, bool straightKey) noexcept {
        if (deviceType == HaliKeyType::kOrb) {
            if (straightKey)
                return {lines.cts, false};
            return {lines.dsr, lines.dcd};
        }
        return {lines.cts, lines.dcd || lines.dsr};
    }

    explicit SerialPaddleInput(int deviceType) : m_deviceType(deviceType) {}

    // m_read captures this object.
    SerialPaddleInput(const SerialPaddleInput &) = delete;
    SerialPaddleInput &operator=(const SerialPaddleInput &) = delete;

    /// Takes the contacts as they stand as rest: nothing is reported for them.
    void start(ModemLines lines, bool straightKey) noexcept QK4_NONBLOCKING {
        m_straight = straightKey;
        m_contacts = map(m_deviceType, lines, straightKey);
        m_poller = SerialLinePoller(kDit, kDah, straightKey);
        m_poller.start(m_read);
    }

    /// One poll at time `now`.
    // WHY the mode comes in with the reading: on the ORB it chooses the lines. A reading mapped for
    // the old mode must never become the new mode's rest state, or the next poll reports an edge
    // nothing made.
    SerialLinePoller::Changes poll(ModemLines lines, bool straightKey, double now) noexcept QK4_NONBLOCKING {
        if (straightKey != m_straight) {
            start(lines, straightKey);
            return {};
        }
        m_contacts = map(m_deviceType, lines, straightKey);
        return m_poller.poll(m_read, now);
    }

private:
    const int m_deviceType;
    bool m_straight = false;
    Contacts m_contacts;
    // Built once: constructing a std::function is not allowed on the polling path.
    const SerialLinePoller::ReadLine m_read = [this](PaddleKeyer::Signal s) {
        return s == kDit ? m_contacts.dit : m_contacts.dah;
    };
    SerialLinePoller m_poller{kDit, kDah, false};
};

#endif // SERIALPADDLEINPUT_H
