#ifndef AUDIODEVICESELECTION_H
#define AUDIODEVICESELECTION_H

#include <string>

// Decides which audio device a stream should be on when the device list changes.
//
// Pure logic with no Qt and no audio device, so every combination can be unit-tested without
// plugging anything in — mirroring audio/audiodecimator.h, hardware/usbdevicelifecycle.h and the
// rest.
//
// WHY this exists. The engine followed the OS default when the operator had not pinned a device,
// and did nothing at all when they had. That second branch is wrong, because pinning a device is
// not the same as being ON it:
//
//   1. The operator pins a Bluetooth headset.
//   2. They quit, come back later with the headset in its case, and connect. The pinned device
//      does not exist, so setup falls back to whatever the OS default is - silently.
//   3. They put the headset on. It appears in the device list.
//   4. Nothing happens. The dropdown shows the headset, because the saved id now matches a real
//      device. The audio is still on the fallback.
//
// So the interface says one thing and the audio path does another, which is the same shape as
// INT-005 and just as invisible from the outside. The notification in step 3 is exactly the
// evidence needed to recover, and it was being discarded.
namespace AudioDeviceSelection {

enum class Action {
    Keep,     // stay where we are
    SwitchTo, // rebuild the stream onto Decision::deviceId
};

struct Decision {
    Action action = Action::Keep;
    std::string deviceId;
    // Why, in words, for the log. A device move the operator did not ask for should always be able
    // to explain itself.
    const char *reason = "";
};

// pinnedId        operator's explicit choice; empty means "follow the system default"
// activeId        the device the stream is currently open on; empty if nothing is open
// systemDefaultId the OS default right now; empty if there is none
// pinnedPresent   whether pinnedId currently exists in the device list
// haveStream      whether a stream is built at all
inline Decision onDeviceListChanged(const std::string &pinnedId, const std::string &activeId,
                                    const std::string &systemDefaultId, bool pinnedPresent, bool haveStream) {
    if (!haveStream) {
        // Nothing to move. Whatever opens next resolves the current state fresh, which is already
        // correct - this is not a missed opportunity.
        return {Action::Keep, {}, "no stream built yet; the next open resolves it"};
    }

    if (pinnedId.empty()) {
        if (systemDefaultId.empty())
            return {Action::Keep, {}, "no system default to follow"};
        if (systemDefaultId == activeId)
            return {Action::Keep, {}, "effective default unchanged"};
        return {Action::SwitchTo, systemDefaultId, "following the system default"};
    }

    // A device is pinned. We never follow the OS default in this case - but we do claim the pinned
    // device the moment it becomes available, which is the case this function exists for.
    if (pinnedId == activeId)
        return {Action::Keep, {}, "already on the pinned device"};
    if (!pinnedPresent)
        return {Action::Keep, {}, "pinned device is still absent; staying on the fallback"};
    return {Action::SwitchTo, pinnedId, "pinned device has reappeared"};
}

} // namespace AudioDeviceSelection

#endif // AUDIODEVICESELECTION_H
