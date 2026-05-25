#include "spectrumdisplaystate.h"

#include "models/radiostate.h"

#include <QtGlobal>

void SpectrumDisplayState::reset() {
    refLevel = -110;
    refLevelB = -110;
    spanHz = 0;
    spanHzB = 0;
    scale = -1;
    miniPanAEnabled = false;
    miniPanBEnabled = false;
    dualPanModeLcd = -1;
    dualPanModeExt = -1;
    displayModeLcd = -1;
    displayModeExt = -1;
    displayFps = 30;
    waterfallColor = -1;
    waterfallHeight = 50;
    waterfallHeightExt = 50;
    averaging = -1;
    peakMode = -1;
    fixedTune = -1;
    fixedTuneMode = -1;
    freeze = -1;
    vfoACursor = -1;
    vfoBCursor = -1;
    autoRefLevel = -1;
    ddcNbMode = -1;
    ddcNbLevel = -1;
}

namespace SpectrumDisplayHandlers {

// Internal int-pair helper — mirrors RadioState::handleIntPair but drives
// subsystem-local fields and owner-qualified signals.
static void applyIntInRange(int &member, int value, int min, int max, RadioState &owner,
                            void (RadioState::*signal)(int)) {
    if (value >= min && value <= max && member != value) {
        member = value;
        (owner.*signal)(member);
    }
}

void handleREF(SpectrumDisplayState &state, RadioState &owner, const QString &cmd) {
    // #REFnnn — ref level, signed int. prefix length 4.
    if (cmd.length() <= 4)
        return;
    bool ok;
    int val = cmd.mid(4).toInt(&ok);
    if (ok)
        applyIntInRange(state.refLevel, val, -200, 50, owner, &RadioState::refLevelChanged);
}

void handleREFSub(SpectrumDisplayState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 5)
        return;
    bool ok;
    int val = cmd.mid(5).toInt(&ok);
    if (ok)
        applyIntInRange(state.refLevelB, val, -200, 50, owner, &RadioState::refLevelBChanged);
}

void handleSPN(SpectrumDisplayState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int val = cmd.mid(4).toInt(&ok);
    if (ok)
        applyIntInRange(state.spanHz, val, 1, 999999, owner, &RadioState::spanChanged);
}

void handleSPNSub(SpectrumDisplayState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 5)
        return;
    bool ok;
    int val = cmd.mid(5).toInt(&ok);
    if (ok)
        applyIntInRange(state.spanHzB, val, 1, 999999, owner, &RadioState::spanBChanged);
}

void handleSCL(SpectrumDisplayState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int scale = cmd.mid(4).toInt(&ok);
    if (ok && scale >= 10 && scale <= 150 && scale != state.scale) {
        state.scale = scale;
        emit owner.scaleChanged(state.scale);
    }
}

// MP / MP$ — single-char bool at fixed position.
void handleMP(SpectrumDisplayState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 4)
        return;
    bool enabled = (cmd.at(3) == '1');
    if (state.miniPanAEnabled != enabled) {
        state.miniPanAEnabled = enabled;
        emit owner.miniPanAEnabledChanged(enabled);
    }
}

void handleMPSub(SpectrumDisplayState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 5)
        return;
    bool enabled = (cmd.at(4) == '1');
    if (state.miniPanBEnabled != enabled) {
        state.miniPanBEnabled = enabled;
        emit owner.miniPanBEnabledChanged(enabled);
    }
}

void handleDPM(SpectrumDisplayState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int mode = cmd.mid(4).toInt(&ok);
    if (ok && mode >= 0 && mode <= 2 && mode != state.dualPanModeLcd) {
        state.dualPanModeLcd = mode;
        emit owner.dualPanModeLcdChanged(state.dualPanModeLcd);
    }
}

void handleHDPM(SpectrumDisplayState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 5)
        return;
    bool ok;
    int mode = cmd.mid(5).toInt(&ok);
    if (ok && mode >= 0 && mode <= 2 && mode != state.dualPanModeExt) {
        state.dualPanModeExt = mode;
        emit owner.dualPanModeExtChanged(state.dualPanModeExt);
    }
}

void handleDSM(SpectrumDisplayState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int mode = cmd.mid(4).toInt(&ok);
    if (ok && (mode == 0 || mode == 1) && mode != state.displayModeLcd) {
        state.displayModeLcd = mode;
        emit owner.displayModeLcdChanged(state.displayModeLcd);
    }
}

void handleHDSM(SpectrumDisplayState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 5)
        return;
    bool ok;
    int mode = cmd.mid(5).toInt(&ok);
    if (ok && (mode == 0 || mode == 1) && mode != state.displayModeExt) {
        state.displayModeExt = mode;
        emit owner.displayModeExtChanged(state.displayModeExt);
    }
}

void handleFPS(SpectrumDisplayState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int fps = cmd.mid(4).toInt(&ok);
    if (ok && fps >= 12 && fps <= 30 && fps != state.displayFps) {
        state.displayFps = fps;
        emit owner.displayFpsChanged(state.displayFps);
    }
}

void handleWFC(SpectrumDisplayState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int color = cmd.mid(4).toInt(&ok);
    if (ok && color >= 0 && color <= 4 && color != state.waterfallColor) {
        state.waterfallColor = color;
        emit owner.waterfallColorChanged(state.waterfallColor);
    }
}

void handleWFH(SpectrumDisplayState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int height = cmd.mid(4).toInt(&ok);
    if (ok && height >= 0 && height <= 100 && height != state.waterfallHeight) {
        state.waterfallHeight = height;
        emit owner.waterfallHeightChanged(state.waterfallHeight);
    }
}

void handleHWFH(SpectrumDisplayState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 5)
        return;
    bool ok;
    int height = cmd.mid(5).toInt(&ok);
    if (ok && height >= 0 && height <= 100 && height != state.waterfallHeightExt) {
        state.waterfallHeightExt = height;
        emit owner.waterfallHeightExtChanged(state.waterfallHeightExt);
    }
}

void handleAVG(SpectrumDisplayState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int avg = cmd.mid(4).toInt(&ok);
    if (ok && avg >= 1 && avg <= 20 && avg != state.averaging) {
        state.averaging = avg;
        emit owner.averagingChanged(state.averaging);
    }
}

void handlePKM(SpectrumDisplayState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int pkm = cmd.mid(4).toInt(&ok);
    if (ok && (pkm == 0 || pkm == 1) && pkm != state.peakMode) {
        state.peakMode = pkm;
        emit owner.peakModeChanged(state.peakMode > 0);
    }
}

void handleFXT(SpectrumDisplayState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int fxt = cmd.mid(4).toInt(&ok);
    if (ok && (fxt == 0 || fxt == 1) && fxt != state.fixedTune) {
        state.fixedTune = fxt;
        emit owner.fixedTuneChanged(state.fixedTune, state.fixedTuneMode);
    }
}

void handleFXA(SpectrumDisplayState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int fxa = cmd.mid(4).toInt(&ok);
    if (ok && fxa >= 0 && fxa <= 4 && fxa != state.fixedTuneMode) {
        state.fixedTuneMode = fxa;
        emit owner.fixedTuneChanged(state.fixedTune, state.fixedTuneMode);
    }
}

void handleFRZ(SpectrumDisplayState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int frz = cmd.mid(4).toInt(&ok);
    if (ok && (frz == 0 || frz == 1) && frz != state.freeze) {
        state.freeze = frz;
        emit owner.freezeChanged(state.freeze > 0);
    }
}

void handleVFA(SpectrumDisplayState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int vfa = cmd.mid(4).toInt(&ok);
    if (ok && vfa >= 0 && vfa <= 3 && vfa != state.vfoACursor) {
        state.vfoACursor = vfa;
        emit owner.vfoACursorChanged(state.vfoACursor);
    }
}

void handleVFB(SpectrumDisplayState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int vfb = cmd.mid(4).toInt(&ok);
    if (ok && vfb >= 0 && vfb <= 3 && vfb != state.vfoBCursor) {
        state.vfoBCursor = vfb;
        emit owner.vfoBCursorChanged(state.vfoBCursor);
    }
}

void handleAR(SpectrumDisplayState &state, RadioState &owner, const QString &cmd) {
    // #AR*********A (or M) — last char is the mode.
    if (cmd.length() < 12)
        return;
    QChar mode = cmd.at(cmd.length() - 1);
    int newValue = (mode == 'A') ? 1 : 0;
    if (newValue != state.autoRefLevel) {
        state.autoRefLevel = newValue;
        emit owner.autoRefLevelChanged(state.autoRefLevel > 0);
    }
}

void handleNB(SpectrumDisplayState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int mode = cmd.mid(4).toInt(&ok);
    if (ok && mode >= 0 && mode <= 2 && mode != state.ddcNbMode) {
        state.ddcNbMode = mode;
        emit owner.ddcNbModeChanged(state.ddcNbMode);
    }
}

void handleNBL(SpectrumDisplayState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 5)
        return;
    bool ok;
    int level = cmd.mid(5).toInt(&ok);
    if (ok && level >= 0 && level <= 14 && level != state.ddcNbLevel) {
        state.ddcNbLevel = level;
        emit owner.ddcNbLevelChanged(state.ddcNbLevel);
    }
}

// ---------------------------------------------------------------------------
// Optimistic setters
// ---------------------------------------------------------------------------

void setRefLevel(SpectrumDisplayState &state, RadioState &owner, int level) {
    if (level != state.refLevel) {
        state.refLevel = level;
        emit owner.refLevelChanged(state.refLevel);
    }
}

void setScale(SpectrumDisplayState &state, RadioState &owner, int scale) {
    if (scale >= 10 && scale <= 150 && scale != state.scale) {
        state.scale = scale;
        emit owner.scaleChanged(state.scale);
    }
}

void setSpanHz(SpectrumDisplayState &state, RadioState &owner, int spanHz) {
    if (spanHz > 0 && spanHz != state.spanHz) {
        state.spanHz = spanHz;
        emit owner.spanChanged(state.spanHz);
    }
}

void setRefLevelB(SpectrumDisplayState &state, RadioState &owner, int level) {
    if (level != state.refLevelB) {
        state.refLevelB = level;
        emit owner.refLevelBChanged(state.refLevelB);
    }
}

void setSpanHzB(SpectrumDisplayState &state, RadioState &owner, int spanHz) {
    if (spanHz > 0 && spanHz != state.spanHzB) {
        state.spanHzB = spanHz;
        emit owner.spanBChanged(state.spanHzB);
    }
}

void setMiniPanAEnabled(SpectrumDisplayState &state, RadioState &owner, bool enabled) {
    if (state.miniPanAEnabled != enabled) {
        state.miniPanAEnabled = enabled;
        emit owner.miniPanAEnabledChanged(enabled);
    }
}

void setMiniPanBEnabled(SpectrumDisplayState &state, RadioState &owner, bool enabled) {
    if (state.miniPanBEnabled != enabled) {
        state.miniPanBEnabled = enabled;
        emit owner.miniPanBEnabledChanged(enabled);
    }
}

void setWaterfallHeight(SpectrumDisplayState &state, RadioState &owner, int percent) {
    percent = qBound(10, percent, 90);
    if (state.waterfallHeight != percent) {
        state.waterfallHeight = percent;
        emit owner.waterfallHeightChanged(percent);
    }
}

void setWaterfallHeightExt(SpectrumDisplayState &state, RadioState &owner, int percent) {
    percent = qBound(10, percent, 90);
    if (state.waterfallHeightExt != percent) {
        state.waterfallHeightExt = percent;
        emit owner.waterfallHeightExtChanged(percent);
    }
}

void setAveraging(SpectrumDisplayState &state, RadioState &owner, int value) {
    value = qBound(1, value, 20);
    if (state.averaging != value) {
        state.averaging = value;
        emit owner.averagingChanged(value);
    }
}

void setVfoACursor(SpectrumDisplayState &state, RadioState &owner, int mode) {
    mode = qBound(0, mode, 3);
    if (state.vfoACursor != mode) {
        state.vfoACursor = mode;
        emit owner.vfoACursorChanged(mode);
    }
}

void setVfoBCursor(SpectrumDisplayState &state, RadioState &owner, int mode) {
    mode = qBound(0, mode, 3);
    if (state.vfoBCursor != mode) {
        state.vfoBCursor = mode;
        emit owner.vfoBCursorChanged(mode);
    }
}

} // namespace SpectrumDisplayHandlers
