#ifndef USBDEVICELIFECYCLE_H
#define USBDEVICELIFECYCLE_H

// Decides what happens to a USB device as it appears, opens, fails, is switched off and goes away.
// Shared by the KPOD (hidapi) and the KPOD+ (libusb), which have the same lifecycle shape and, until
// this existed, two different and disagreeing implementations of it.
//
// Pure logic with no Qt, no hidapi and no libusb, so it can be unit-tested without a device present
// — mirroring hardware/halikey_edge.h, network/connect_failure.h and models/transmitowner.h.
//
// WHY this exists at all. Three defects, all from the same root:
//
//   USB-002 — "Enable K-Pod" did not disable the KPOD+. Every enable check in the app lived in
//   HardwareController, while KpodPlusUsbWorker's presence timer called openDevice() itself. The
//   policy had two owners and only one of them knew about the setting.
//
//   Duplicate removals. One physical unplug produces deviceRemoved TWICE — on the KPOD+ from
//   closeDevice() and again on the next line, on the KPOD from the 20 ms poll failing and then the
//   2 s presence timer noticing seconds later. Code downstream tried to defend with a one-shot
//   "expected stop" flag; the first duplicate consumed it and the second raised a disconnection
//   notification for a cable nobody had pulled.
//
//   A deliberate switch-off was indistinguishable from a yank, because both arrived as the same
//   signal with no cause attached.
//
// The fix for all three is the same: make the cause part of the event, keep the state in one place,
// and derive the effects from the EDGE rather than from the event count. A second Lost for one
// unplug finds `present` already false and returns the zero value — there is nothing left for a
// flag to defend against.
namespace UsbDeviceLifecycle {

enum class Event {
    Detected,     // enumeration found the device on the bus
    Lost,         // enumeration lost it, or a transfer reported NO_DEVICE
    Opened,       // our open succeeded and the device is live
    OpenFailed,   // our open was attempted and failed
    Enabled,      // the operator switched it on
    Disabled,     // the operator switched it off
    ShuttingDown, // the application is quitting
};

struct State {
    bool present = false; // enumerated on the bus
    bool open = false;    // we hold a handle and are polling it
    bool enabled = false; // the operator's setting

    /// True when the device is actually doing its job — which is the only sense in which it can be
    /// said to "own" anything. Detected, enabled and live are three different states, and the
    /// Options page conflating them is what told an operator the KPOD+ keyer was active while it
    /// was switched off.
    bool live() const { return open; }
};

// What the caller must do. Every field defaults to "do nothing", so a decision nobody wrote cannot
// open a device or key a radio — the same property that makes TransmitOwner::Effects safe.
struct Effects {
    bool open = false;  // open the device now
    bool close = false; // close it now

    // Availability changed. Drives the app's own wiring and the Options page. Distinct from the
    // notifications below, because the page must follow a deliberate switch-off and the operator
    // must not be told their cable fell out.
    bool reportArrived = false;
    bool reportRemoved = false;

    // Operator-visible notifications. Never raised for something the operator just did.
    bool notifyConnected = false;
    bool notifyDisconnected = false;
    bool notifyOpenFailed = false; // USB-003: an open failure used to be entirely silent
};

/// Apply `event` to `s`, returning what the caller must do about it.
///
/// The invariant worth stating: `open` is only ever requested when the device is BOTH present and
/// enabled. There is no path through this function that opens a device the operator switched off,
/// which is the whole of USB-002.
inline Effects apply(State &s, Event event) {
    Effects e;

    switch (event) {
    case Event::Detected:
        if (s.present)
            return e; // already known; a repeated enumeration hit is not news
        s.present = true;
        if (s.enabled && !s.open)
            e.open = true;
        return e;

    case Event::Lost:
        if (!s.present && !s.open)
            return e; // THE duplicate killer — see the header note
        s.present = false;
        if (s.open) {
            s.open = false;
            e.close = true;
            e.reportRemoved = true;
            e.notifyDisconnected = true; // unexpected: it was live and now it is gone
        }
        return e;

    case Event::Opened:
        if (s.open)
            return e;
        s.open = true;
        e.reportArrived = true;
        e.notifyConnected = true;
        return e;

    case Event::OpenFailed:
        s.open = false;
        e.notifyOpenFailed = true;
        return e;

    case Event::Enabled:
        if (s.enabled)
            return e;
        s.enabled = true;
        if (s.present && !s.open)
            e.open = true;
        return e;

    case Event::Disabled:
        if (!s.enabled)
            return e;
        s.enabled = false;
        if (s.open) {
            s.open = false;
            e.close = true;
            e.reportRemoved = true;
            // Deliberately NO notifyDisconnected. The operator just switched it off; telling them
            // it disconnected is how the notification added in 5016ea6 reported a cable nobody
            // pulled.
        }
        return e;

    case Event::ShuttingDown:
        if (s.open) {
            s.open = false;
            e.close = true;
            // No reports and no notifications: the window is going away and there is nobody left to
            // tell.
        }
        return e;
    }

    return e;
}

} // namespace UsbDeviceLifecycle

#endif // USBDEVICELIFECYCLE_H
