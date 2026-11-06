// SPDX-License-Identifier: GPL-3.0-or-later
//
// When QK4's own keyer may key, and when its local sidetone needs an audio stream. One pure
// decision, so CwController cannot drift between the two and the cases can be swept in a test.

#ifndef CWGATE_H
#define CWGATE_H

namespace CwGate {

struct Inputs {
    bool radioReady = false;     // authenticated and past the RDY dump
    bool inCwMode = false;       // CW or CW-R
    bool kpodPlusOwnsCw = false; // the KPOD+ runs its own keyer and sidetone
    bool halikeyOpen = false;    // a HaliKey port is open: the only source QK4's keyer listens to
};

// The keyer keys nothing unless the radio is ready, in CW, and the KPOD+ does not own CW.
inline bool keyerInhibited(const Inputs &in) {
    return !in.radioReady || !in.inCwMode || in.kpodPlusOwnsCw;
}

// WHY the sidetone stream follows keyability, not the app's lifetime: an open push sink is pumped
// every millisecond and keeps the audio device awake, which cost ~21 % CPU on an idle Linux
// machine with no radio connected (#92). It is opened as soon as keying becomes possible, not at
// the first element, so no element ever waits for a device to open.
inline bool sidetoneStreamWanted(const Inputs &in) {
    return !keyerInhibited(in) && in.halikeyOpen;
}

} // namespace CwGate

#endif // CWGATE_H
