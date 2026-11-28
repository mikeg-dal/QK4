#ifndef HALIKEYDEVICETYPE_H
#define HALIKEYDEVICETYPE_H

// The paddle device types QK4 supports. The values are stored in settings (halikey/deviceType), so
// they never change meaning.
namespace HaliKeyType {

inline constexpr int kSerialV14 = 0;
inline constexpr int kMidi = 1;
inline constexpr int kOrb = 2; // ORB Control Device: contacts on a USB serial port's modem lines

inline constexpr int kFirst = kSerialV14;
inline constexpr int kLast = kOrb;

constexpr bool isSerial(int type) {
    return type != kMidi;
}

/// The name shown to the operator, in the Options dropdown, notifications and logs.
constexpr const char *name(int type) {
    switch (type) {
    case kMidi:
        return "HaliKey MIDI";
    case kOrb:
        return "ORB Control Device";
    default:
        return "HaliKey V1.4";
    }
}

} // namespace HaliKeyType

#endif // HALIKEYDEVICETYPE_H
