#include "xvtrbandstate.h"

#include "models/radiostate.h"

namespace XvtrBandHandlers {

namespace {
// Index validity check — K4 sends bands 1..12; convert to 0..11.
bool isValidBand(int oneBased) {
    return oneBased >= 1 && oneBased <= 12;
}

// Stores a parsed field on the band XVN selected; emits only when it changed.
void setField(XvtrBandState &state, RadioState &owner, int XvtrBandConfig::*field, int value) {
    if (!isValidBand(state.currentSelect))
        return;
    XvtrBandConfig &band = state.bands[state.currentSelect - 1];
    if (band.*field != value) {
        band.*field = value;
        emit owner.xvtrBandsChanged();
    }
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

void handleXVI(XvtrBandState &state, RadioState &owner, const QString &cmd) {
    bool ok = false;
    const int ifMhz = cmd.mid(3).toInt(&ok);
    if (ok && ifMhz >= 0)
        setField(state, owner, &XvtrBandConfig::ifMhz, ifMhz);
}

void handleXVO(XvtrBandState &state, RadioState &owner, const QString &cmd) {
    // Signed: XVO+00000, XVO-01234. The sign is required, so an unsigned value is not a report.
    const QString payload = cmd.mid(3);
    if (payload.isEmpty() || (payload.front() != QLatin1Char('+') && payload.front() != QLatin1Char('-')))
        return;
    bool ok = false;
    const int offset = payload.toInt(&ok);
    if (ok)
        setField(state, owner, &XvtrBandConfig::offsetHz, offset);
}

void handleXVP(XvtrBandState &state, RadioState &owner, const QString &cmd) {
    bool ok = false;
    const int tenthsMw = cmd.mid(3).toInt(&ok);
    if (ok && tenthsMw >= 0)
        setField(state, owner, &XvtrBandConfig::powerOutTenthsMw, tenthsMw);
}

} // namespace XvtrBandHandlers
