#include "modefilterstate.h"

#include "models/radiostate.h"
#include "utils/radioutils.h"

#include <QtGlobal>

void ModeFilterState::reset() {
    mode = RadioState::Unknown;
    modeB = RadioState::Unknown;
    filterBandwidth = -1;
    filterBandwidthB = -1;
    filterPosition = -1;
    filterPositionB = -1;
    ifShift = -1;
    ifShiftB = -1;
    cwPitch = -1;
    keyerSpeed = -1;
    keyDownInitialDelayMs = -1;
    iambicMode = QChar();
    paddleOrientation = QChar();
    keyingWeight = -1;
}

namespace ModeFilterHandlers {

void handleMD(ModeFilterState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 2)
        return;
    bool ok;
    const int modeCode = cmd.mid(2).toInt(&ok);
    if (!ok)
        return;
    const int newMode = RadioState::modeFromCode(modeCode);
    if (state.mode != newMode) {
        state.mode = newMode;
        emit owner.modeChanged(static_cast<RadioState::Mode>(newMode));
        // A mode flip can change the active VOX/QSK delay, since those are
        // per-mode-class. Re-emit the delay so listeners refresh.
        const int currentDelay = owner.delayForCurrentMode();
        if (currentDelay >= 0)
            emit owner.qskDelayChanged(currentDelay);
    }
}

void handleMDSub(ModeFilterState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 3)
        return;
    bool ok;
    const int modeCode = cmd.mid(3).toInt(&ok);
    if (!ok)
        return;
    const int newMode = RadioState::modeFromCode(modeCode);
    if (state.modeB != newMode) {
        state.modeB = newMode;
        emit owner.modeBChanged(static_cast<RadioState::Mode>(newMode));
    }
}

void handleCW(ModeFilterState &state, RadioState &owner, const QString &cmd) {
    // CW pitch — but skip "CW-R" mode string (not a pitch value).
    if (cmd.length() < 4 || cmd.startsWith("CW-"))
        return;
    bool ok;
    const int pitchHz = cmd.mid(2).toInt(&ok) * RadioUtils::CW_PITCH_STEP_HZ;
    if (ok && pitchHz >= RadioUtils::CW_PITCH_MIN_HZ && pitchHz <= RadioUtils::CW_PITCH_MAX_HZ) {
        if (pitchHz != state.cwPitch) {
            state.cwPitch = pitchHz;
            emit owner.cwPitchChanged(state.cwPitch);
        }
    }
}

void handleKS(ModeFilterState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 2)
        return;
    bool ok;
    const int wpm = cmd.mid(2).toInt(&ok);
    if (ok && state.keyerSpeed != wpm) {
        state.keyerSpeed = wpm;
        emit owner.keyerSpeedChanged(state.keyerSpeed);
    }
}

void handleKZL(ModeFilterState &state, RadioState &owner, const QString &cmd) {
    // KZLnnnn; nnnn = 0-1000 ms. WHY any width: the RDY dump has been seen sending it unpadded
    // ("KZL60;", tests/golden capture 2026-04-20), so the digits are taken as they come.
    if (cmd.length() <= 3)
        return;
    bool ok;
    const int ms = cmd.mid(3).toInt(&ok);
    if (!ok || ms < 0 || ms > 1000)
        return;
    if (state.keyDownInitialDelayMs != ms) {
        state.keyDownInitialDelayMs = ms;
        emit owner.keyDownInitialDelayChanged(ms);
    }
}

void handleKP(ModeFilterState &state, RadioState &owner, const QString &cmd) {
    // KPionnn; where i=iambic (A/B), o=paddle (N/R), nnn=weight (090-125)
    if (cmd.length() < 7)
        return;
    const QChar iambic = cmd[2];
    const QChar paddle = cmd[3];
    bool ok;
    const int weight = cmd.mid(4, 3).toInt(&ok);
    if (!ok)
        return;
    bool changed = false;
    if (state.iambicMode != iambic) {
        state.iambicMode = iambic;
        changed = true;
    }
    if (state.paddleOrientation != paddle) {
        state.paddleOrientation = paddle;
        changed = true;
    }
    if (state.keyingWeight != weight) {
        state.keyingWeight = weight;
        changed = true;
    }
    if (changed)
        emit owner.keyerPaddleChanged(state.iambicMode, state.paddleOrientation, state.keyingWeight);
}

void handleBW(ModeFilterState &state, RadioState &owner, const QString &cmd) {
    // WHY: K4 reports BW in 10-Hz units (BW0280 = 2800 Hz); we multiply by 10
    // once on ingest so callers work in plain Hz.
    if (cmd.length() <= 2)
        return;
    bool ok;
    const int bw = cmd.mid(2).toInt(&ok);
    if (ok) {
        const int newBw = bw * 10;
        if (state.filterBandwidth != newBw) {
            state.filterBandwidth = newBw;
            emit owner.filterBandwidthChanged(state.filterBandwidth);
        }
    }
}

void handleBWSub(ModeFilterState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 3)
        return;
    bool ok;
    const int bw = cmd.mid(3).toInt(&ok);
    if (ok) {
        const int newBw = bw * 10;
        if (state.filterBandwidthB != newBw) {
            state.filterBandwidthB = newBw;
            emit owner.filterBandwidthBChanged(state.filterBandwidthB);
        }
    }
}

void setFilterBandwidth(ModeFilterState &state, RadioState &owner, int bwHz) {
    if (state.filterBandwidth != bwHz) {
        state.filterBandwidth = bwHz;
        emit owner.filterBandwidthChanged(state.filterBandwidth);
    }
}

void setFilterBandwidthB(ModeFilterState &state, RadioState &owner, int bwHz) {
    if (state.filterBandwidthB != bwHz) {
        state.filterBandwidthB = bwHz;
        emit owner.filterBandwidthBChanged(state.filterBandwidthB);
    }
}

void setIfShift(ModeFilterState &state, RadioState &owner, int shift) {
    if (state.ifShift != shift) {
        state.ifShift = shift;
        emit owner.ifShiftChanged(state.ifShift);
    }
}

void setIfShiftB(ModeFilterState &state, RadioState &owner, int shift) {
    if (state.ifShiftB != shift) {
        state.ifShiftB = shift;
        emit owner.ifShiftBChanged(state.ifShiftB);
    }
}

void setCwPitch(ModeFilterState &state, RadioState &owner, int pitchHz) {
    if (state.cwPitch != pitchHz) {
        state.cwPitch = pitchHz;
        emit owner.cwPitchChanged(state.cwPitch);
    }
}

void setKeyerSpeed(ModeFilterState &state, RadioState &owner, int wpm) {
    if (state.keyerSpeed != wpm) {
        state.keyerSpeed = wpm;
        emit owner.keyerSpeedChanged(state.keyerSpeed);
    }
}

void setIambicMode(ModeFilterState &state, RadioState &owner, QChar mode) {
    if (state.iambicMode != mode) {
        state.iambicMode = mode;
        emit owner.keyerPaddleChanged(state.iambicMode, state.paddleOrientation, state.keyingWeight);
    }
}

void setPaddleOrientation(ModeFilterState &state, RadioState &owner, QChar orientation) {
    if (state.paddleOrientation != orientation) {
        state.paddleOrientation = orientation;
        emit owner.keyerPaddleChanged(state.iambicMode, state.paddleOrientation, state.keyingWeight);
    }
}

void setKeyingWeight(ModeFilterState &state, RadioState &owner, int weight) {
    weight = qBound(90, weight, 125);
    if (state.keyingWeight != weight) {
        state.keyingWeight = weight;
        emit owner.keyerPaddleChanged(state.iambicMode, state.paddleOrientation, state.keyingWeight);
    }
}

} // namespace ModeFilterHandlers
