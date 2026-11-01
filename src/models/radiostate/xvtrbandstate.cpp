#include "xvtrbandstate.h"

#include "models/radiostate.h"

namespace XvtrBandHandlers {

namespace {
// Index validity check — K4 sends bands 1..12; convert to 0..11.
bool isValidBand(int oneBased) {
    return oneBased >= 1 && oneBased <= 12;
}
} // namespace

void handleXVN(XvtrBandState &state, RadioState & /*owner*/, const QString &cmd) {
    bool ok = false;
    const int n = cmd.mid(3).toInt(&ok);
    if (!ok || !isValidBand(n))
        return;
    state.currentSelect = n;
}

void handleXVM(XvtrBandState &state, RadioState &owner, const QString &cmd) {
    bool ok = false;
    const int m = cmd.mid(3).toInt(&ok);
    if (!ok)
        return;
    if (!isValidBand(state.currentSelect))
        return;
    XvtrBandConfig &band = state.bands[state.currentSelect - 1];
    if (band.mode != m) {
        band.mode = m;
        emit owner.xvtrBandsChanged();
    }
}

void handleXVR(XvtrBandState &state, RadioState &owner, const QString &cmd) {
    bool ok = false;
    const int rf = cmd.mid(3).toInt(&ok);
    if (!ok)
        return;
    if (!isValidBand(state.currentSelect))
        return;
    XvtrBandConfig &band = state.bands[state.currentSelect - 1];
    if (band.rfMhz != rf) {
        band.rfMhz = rf;
        emit owner.xvtrBandsChanged();
    }
}

} // namespace XvtrBandHandlers
