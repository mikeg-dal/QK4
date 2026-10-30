#ifndef MODELS_RADIOSTATE_XVTRBANDSTATE_H
#define MODELS_RADIOSTATE_XVTRBANDSTATE_H

#include <QString>
#include <QVector>

#include <limits>

class RadioState;

// Per-XVTR-band configuration. The K4 supports 12 XVTR (transverter) band
// slots, each independently configured with its RF dial frequency, IF tuned
// on the K4 itself, fine-tune offset, output power, and mode (off/external).
//
// Initial state arrives during RDY as a sequence of XVN<n>; XVM…; XVR…; XVI…;
// XVO…; ME0086.<n>; ME0076…; ME0077…; ... for each of the 12 bands. After
// connect, individual edits (front panel or external CAT) come through as
// XVN<n> followed by whichever fields changed for that band.
//
// Every reported field is unknown until the radio reports it, so its first report always emits.
// mode/rfMhz/ifMhz are never negative on the wire; XVO is signed, so offsetHz has its own sentinel.
struct XvtrBandConfig {
    static constexpr int Unknown = -1;
    static constexpr int OffsetUnknown = (std::numeric_limits<int>::min)();

    int mode = Unknown;           // 0=OFF, 1=External (from XVM<m>)
    int rfMhz = Unknown;          // dial RF frequency MHz (from XVR<5-digit>)
    int ifMhz = Unknown;          // IF on the K4 itself (from XVI<2-digit>)
    int offsetHz = OffsetUnknown; // signed Hz fine-tune (from XVO<signed 5-digit>)
    int powerOutTenthsMw = 10;    // 10 = 1.0 mW; matches D10mW MEDF

    bool configured() const { return mode == 1; }
};

struct XvtrBandState {
    // bands[0] = XVTR Band 1, bands[11] = XVTR Band 12 (fixed size).
    QVector<XvtrBandConfig> bands;
    // Currently-selected band index for per-band ME writes (mirrors ME0086 /
    // XVN). 1-based to match K4 conventions; range 1..12. Unknown until the first XVN, and
    // XVM/XVR/XVI/XVO before it are dropped rather than written to a guessed band.
    int currentSelect = XvtrBandConfig::Unknown;

    XvtrBandState() : bands(12) {}

    void reset() {
        bands = QVector<XvtrBandConfig>(12);
        currentSelect = XvtrBandConfig::Unknown;
    }
};

namespace XvtrBandHandlers {

// XVN<n>; — select XVTR band n (1..12) for subsequent XVM/XVR/XVI/XVO updates.
void handleXVN(XvtrBandState &state, RadioState &owner, const QString &cmd);

// XVM<m>; — Mode (0=OFF, 1=External) for currentSelect band.
void handleXVM(XvtrBandState &state, RadioState &owner, const QString &cmd);

// XVR<nnnnn>; — RF frequency MHz (e.g., XVR00144 = 144 MHz) for currentSelect band.
void handleXVR(XvtrBandState &state, RadioState &owner, const QString &cmd);

// XVI<nn>; — IF frequency MHz (e.g., XVI28 = 28 MHz) for currentSelect band.
void handleXVI(XvtrBandState &state, RadioState &owner, const QString &cmd);

// XVO<±nnnnn>; — signed Hz fine-tune offset for currentSelect band.
void handleXVO(XvtrBandState &state, RadioState &owner, const QString &cmd);

} // namespace XvtrBandHandlers

#endif // MODELS_RADIOSTATE_XVTRBANDSTATE_H
