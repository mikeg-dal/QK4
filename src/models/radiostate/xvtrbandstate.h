#ifndef MODELS_RADIOSTATE_XVTRBANDSTATE_H
#define MODELS_RADIOSTATE_XVTRBANDSTATE_H

#include <QString>
#include <QVector>

#include <limits>

class RadioState;

// Per-XVTR-band configuration. The K4 supports 12 XVTR (transverter) band
// slots, each independently configured with its RF dial frequency, IF tuned
// on the K4 itself, fine-tune offset, output power, and mode (off/external).
// All of each band's setup is kept (Programmer's Reference D12, XVN/XVM/XVR/XVI/XVO/XVP): the band
// popup's XVTR cells read mode and RF; IF, offset and power out are there for transverter work.
//
// Initial state arrives during RDY as a sequence of XVN<n>; XVM…; XVR…; XVI…;
// XVO…; ME0086.<n>; ME0076…; ME0077…; ... for each of the 12 bands. After
// connect, individual edits (front panel or external CAT) come through as
// XVN<n> followed by whichever fields changed for that band.
//
// Every reported field is unknown until the radio reports it, so its first report always emits.
// mode, rfMhz, ifMhz and power are never negative on the wire; XVO is signed, so offsetHz has its
// own sentinel.
struct XvtrBandConfig {
    static constexpr int Unknown = -1;
    static constexpr int OffsetUnknown = (std::numeric_limits<int>::min)();

    int mode = Unknown;             // 0=OFF, 1=External (from XVM<m>)
    int rfMhz = Unknown;            // dial RF frequency MHz (from XVR<5-digit>)
    int ifMhz = Unknown;            // IF band on the K4 in MHz, 0-53 (from XVI<nn>)
    int offsetHz = OffsetUnknown;   // signed fine-tune offset in Hz (from XVO<+/-nnnnn>)
    int powerOutTenthsMw = Unknown; // power out in 0.1 mW units, XVP010 = 1.0 mW (from XVP<nnn>)

    bool configured() const { return mode == 1; }
};

struct XvtrBandState {
    // bands[0] = XVTR Band 1, bands[11] = XVTR Band 12 (fixed size).
    QVector<XvtrBandConfig> bands;
    // Currently-selected band index for per-band ME writes (mirrors ME0086 /
    // XVN). 1-based to match K4 conventions; range 1..12. Unknown until the first XVN, and
    // XVM/XVR/XVI/XVO/XVP before it are dropped rather than written to a guessed band.
    int currentSelect = XvtrBandConfig::Unknown;

    XvtrBandState() : bands(12) {}

    void reset() {
        bands = QVector<XvtrBandConfig>(12);
        currentSelect = XvtrBandConfig::Unknown;
    }
};

namespace XvtrBandHandlers {

// XVN<n>; — select XVTR band n (1..12) for subsequent XVM/XVR/XVI/XVO/XVP updates.
void handleXVN(XvtrBandState &state, RadioState &owner, const QString &cmd);

// XVM<m>; — Mode (0=OFF, 1=External) for currentSelect band.
void handleXVM(XvtrBandState &state, RadioState &owner, const QString &cmd);

// XVR<nnnnn>; — RF frequency MHz (e.g., XVR00144 = 144 MHz) for currentSelect band.
void handleXVR(XvtrBandState &state, RadioState &owner, const QString &cmd);

// XVI<nn>; — IF band in MHz (0-53, e.g. XVI28 = 28 MHz) for currentSelect band.
void handleXVI(XvtrBandState &state, RadioState &owner, const QString &cmd);

// XVO<+/-nnnnn>; — signed fine-tune offset in Hz (e.g. XVO-01234) for currentSelect band.
void handleXVO(XvtrBandState &state, RadioState &owner, const QString &cmd);

// XVP<nnn>; — power out in 0.1 mW units (XVP010 = 1.0 mW) for currentSelect band.
void handleXVP(XvtrBandState &state, RadioState &owner, const QString &cmd);

} // namespace XvtrBandHandlers

#endif // MODELS_RADIOSTATE_XVTRBANDSTATE_H
