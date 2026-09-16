#include "levelsstate.h"

#include "models/radiostate.h"

#include <QtGlobal>

void LevelsState::reset() {
    rfPower = -1.0;
    powerRange = PowerRange::Qro;
    micGain = -1;
    compression = -1;
    rfGain = -999;
    rfGainB = -999;
    squelchLevel = -1;
    squelchLevelB = -1;
}

namespace LevelsHandlers {

void handleMG(LevelsState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 2)
        return;
    bool ok;
    const int gain = cmd.mid(2).toInt(&ok);
    if (ok && gain != state.micGain) {
        state.micGain = gain;
        emit owner.micGainChanged(state.micGain);
    }
}

void handleCP(LevelsState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 2)
        return;
    bool ok;
    const int comp = cmd.mid(2).toInt(&ok);
    if (ok && comp != state.compression) {
        state.compression = comp;
        emit owner.compressionChanged(state.compression);
    }
}

void handlePC(LevelsState &state, RadioState &owner, const QString &cmd) {
    // PCnnnr; where nnn=power value, r=L/H/X.
    // L = QRP (0.1-10W): nnn is watts*10 (e.g., 099 = 9.9W).
    // H = QRO (1-110W): nnn is watts directly.
    // X = XVTR (0.1-10mW): nnn is mW*10 (e.g., 010 = 1.0 mW).
    if (cmd.length() < 6)
        return;
    bool ok;
    const int powerRaw = cmd.mid(2, 3).toInt(&ok);
    if (!ok)
        return;

    const QChar mode = cmd.at(5);
    double value;
    LevelsState::PowerRange range;
    if (mode == 'L') {
        value = powerRaw / 10.0;
        range = LevelsState::PowerRange::Qrp;
    } else if (mode == 'H') {
        value = static_cast<double>(powerRaw);
        range = LevelsState::PowerRange::Qro;
    } else if (mode == 'X') {
        value = powerRaw / 10.0; // mW
        range = LevelsState::PowerRange::Xvtr;
    } else {
        return;
    }

    bool changed = false;
    if (value != state.rfPower) {
        state.rfPower = value;
        changed = true;
    }
    if (range != state.powerRange) {
        state.powerRange = range;
        changed = true;
    }
    if (changed)
        emit owner.rfPowerChanged(state.rfPower, state.powerRange);
}

// WHY: RF gain arrives with a leading dash (e.g., "RG-010"), but is stored as a
// positive magnitude; the minus sign is re-added only on CAT dispatch/display.
void handleRG(LevelsState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 2)
        return;
    bool ok;
    const int rg = qAbs(cmd.mid(2).toInt(&ok));
    if (ok && state.rfGain != rg) {
        state.rfGain = rg;
        emit owner.rfGainChanged(state.rfGain);
    }
}

void handleRGSub(LevelsState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 3)
        return;
    bool ok;
    const int rg = qAbs(cmd.mid(3).toInt(&ok));
    if (ok && state.rfGainB != rg) {
        state.rfGainB = rg;
        emit owner.rfGainBChanged(state.rfGainB);
    }
}

void handleSQ(LevelsState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 2)
        return;
    bool ok;
    const int sq = cmd.mid(2).toInt(&ok);
    if (ok && state.squelchLevel != sq) {
        state.squelchLevel = sq;
        emit owner.squelchChanged(state.squelchLevel);
    }
}

void handleSQSub(LevelsState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 3)
        return;
    bool ok;
    const int sq = cmd.mid(3).toInt(&ok);
    if (ok && state.squelchLevelB != sq) {
        state.squelchLevelB = sq;
        emit owner.squelchBChanged(state.squelchLevelB);
    }
}

void setRfPower(LevelsState &state, RadioState &owner, double watts) {
    // Optimistic UI setter — assumes HF range (auto-pick QRP if ≤10 W). XVTR
    // mW writes don't go through this path; they come from the K4 via handlePC.
    const LevelsState::PowerRange range = (watts <= 10.0) ? LevelsState::PowerRange::Qrp : LevelsState::PowerRange::Qro;
    bool changed = false;
    if (state.rfPower != watts) {
        state.rfPower = watts;
        changed = true;
    }
    if (range != state.powerRange) {
        state.powerRange = range;
        changed = true;
    }
    if (changed)
        emit owner.rfPowerChanged(state.rfPower, state.powerRange);
}

void setMicGain(LevelsState &state, RadioState &owner, int gain) {
    if (state.micGain != gain) {
        state.micGain = gain;
        emit owner.micGainChanged(state.micGain);
    }
}

void setCompression(LevelsState &state, RadioState &owner, int level) {
    if (state.compression != level) {
        state.compression = level;
        emit owner.compressionChanged(state.compression);
    }
}

void setRfGain(LevelsState &state, RadioState &owner, int gain) {
    if (state.rfGain != gain) {
        state.rfGain = gain;
        emit owner.rfGainChanged(state.rfGain);
    }
}

void setRfGainB(LevelsState &state, RadioState &owner, int gain) {
    if (state.rfGainB != gain) {
        state.rfGainB = gain;
        emit owner.rfGainBChanged(state.rfGainB);
    }
}

void setSquelchLevel(LevelsState &state, RadioState &owner, int level) {
    if (state.squelchLevel != level) {
        state.squelchLevel = level;
        emit owner.squelchChanged(state.squelchLevel);
    }
}

void setSquelchLevelB(LevelsState &state, RadioState &owner, int level) {
    if (state.squelchLevelB != level) {
        state.squelchLevelB = level;
        emit owner.squelchBChanged(state.squelchLevelB);
    }
}

} // namespace LevelsHandlers
