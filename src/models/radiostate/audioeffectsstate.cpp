#include "audioeffectsstate.h"

#include "models/radiostate.h"

#include <QtGlobal>

void AudioEffectsState::reset() {
    afxMode = 0;

    apfEnabled = false;
    apfBandwidth = 0;
    apfEnabledB = false;
    apfBandwidthB = 0;

    voxCW = false;
    voxVoice = false;
    voxData = false;

    voxGainVoice = -1;
    voxGainData = -1;

    antiVox = -1;

    essbEnabled = false;
    ssbTxBw = -1;

    for (int i = 0; i < 8; ++i) {
        rxEqBands[i] = 0;
        txEqBands[i] = 0;
    }

    lineOutLeft = -1;
    lineOutRight = -1;
    lineOutRightEqualsLeft = false;

    lineInSoundCard = -1;
    lineInJack = -1;
    lineInSource = -1;

    micInput = -1;
    micFrontPreamp = -1;
    micFrontBias = -1;
    micFrontButtons = -1;
    micRearPreamp = -1;
    micRearBias = -1;

    monitorLevelCW = -1;
    monitorLevelData = -1;
    monitorLevelVoice = -1;

    audioMixLeft = -1;
    audioMixRight = -1;

    balanceMode = -1;
    balanceOffset = -99;
}

namespace AudioEffectsHandlers {

void handleFX(AudioEffectsState &state, RadioState &owner, const QString &cmd) {
    // FXn where n=0(off)/1(delay)/2(pitch-map)
    if (cmd.length() < 3)
        return;
    bool ok;
    int fx = cmd.mid(2).toInt(&ok);
    if (ok && fx >= 0 && fx <= 2 && fx != state.afxMode) {
        state.afxMode = fx;
        emit owner.afxModeChanged(state.afxMode);
    }
}

void handleAP(AudioEffectsState &state, RadioState &owner, const QString &cmd) {
    // APmb where m=enabled, b=bandwidth
    if (cmd.length() < 4)
        return;
    bool ok;
    int m = cmd.mid(2, 1).toInt(&ok);
    if (!ok)
        return;
    int b = cmd.mid(3, 1).toInt(&ok);
    if (!ok)
        return;

    bool enabled = (m == 1);
    int bandwidth = qBound(0, b, 2);
    if (enabled != state.apfEnabled || bandwidth != state.apfBandwidth) {
        state.apfEnabled = enabled;
        state.apfBandwidth = bandwidth;
        emit owner.apfChanged(state.apfEnabled, state.apfBandwidth);
    }
}

void handleAPSub(AudioEffectsState &state, RadioState &owner, const QString &cmd) {
    // AP$mb — Sub RX
    if (cmd.length() < 5)
        return;
    bool ok;
    int m = cmd.mid(3, 1).toInt(&ok);
    if (!ok)
        return;
    int b = cmd.mid(4, 1).toInt(&ok);
    if (!ok)
        return;

    bool enabled = (m == 1);
    int bandwidth = qBound(0, b, 2);
    if (enabled != state.apfEnabledB || bandwidth != state.apfBandwidthB) {
        state.apfEnabledB = enabled;
        state.apfBandwidthB = bandwidth;
        emit owner.apfBChanged(state.apfEnabledB, state.apfBandwidthB);
    }
}

void handleVX(AudioEffectsState &state, RadioState &owner, const QString &cmd) {
    // VXmn where m=mode (C/V/D), n=0/1
    if (cmd.length() < 4)
        return;
    QChar modeChar = cmd.at(2);
    bool enabled = (cmd.at(3) == '1');
    bool changed = false;
    if (modeChar == 'C' && state.voxCW != enabled) {
        state.voxCW = enabled;
        changed = true;
    } else if (modeChar == 'V' && state.voxVoice != enabled) {
        state.voxVoice = enabled;
        changed = true;
    } else if (modeChar == 'D' && state.voxData != enabled) {
        state.voxData = enabled;
        changed = true;
    }
    if (changed) {
        emit owner.voxChanged(owner.voxEnabled());
    }
}

void handleVG(AudioEffectsState &state, RadioState &owner, const QString &cmd) {
    // VGmnnn where m=V(voice)/D(data), nnn=000-060
    if (cmd.length() < 5)
        return;
    QChar modeChar = cmd.at(2);
    bool ok;
    int gain = cmd.mid(3, 3).toInt(&ok);
    if (ok && gain >= 0 && gain <= 60) {
        if (modeChar == 'V' && gain != state.voxGainVoice) {
            state.voxGainVoice = gain;
            emit owner.voxGainChanged(0, gain);
        } else if (modeChar == 'D' && gain != state.voxGainData) {
            state.voxGainData = gain;
            emit owner.voxGainChanged(1, gain);
        }
    }
}

void handleVI(AudioEffectsState &state, RadioState &owner, const QString &cmd) {
    // VInnn where nnn=000-060
    if (cmd.length() < 5)
        return;
    bool ok;
    int level = cmd.mid(2, 3).toInt(&ok);
    if (ok && level >= 0 && level <= 60 && level != state.antiVox) {
        state.antiVox = level;
        emit owner.antiVoxChanged(level);
    }
}

void handleLO(AudioEffectsState &state, RadioState &owner, const QString &cmd) {
    // LOlllrrrm where lll=left, rrr=right, m=mode
    if (cmd.length() < 9)
        return;
    bool okL, okR;
    int left = cmd.mid(2, 3).toInt(&okL);
    int right = cmd.mid(5, 3).toInt(&okR);
    int modeVal = cmd.mid(8, 1).toInt();

    if (okL && okR && left >= 0 && left <= 40 && right >= 0 && right <= 40) {
        bool changed = false;
        if (left != state.lineOutLeft) {
            state.lineOutLeft = left;
            changed = true;
        }
        if (right != state.lineOutRight) {
            state.lineOutRight = right;
            changed = true;
        }
        if ((modeVal == 1) != state.lineOutRightEqualsLeft) {
            state.lineOutRightEqualsLeft = (modeVal == 1);
            changed = true;
        }
        if (changed)
            emit owner.lineOutChanged();
    }
}

void handleLI(AudioEffectsState &state, RadioState &owner, const QString &cmd) {
    // LIuuullls where uuu=soundcard, lll=linein, s=source
    if (cmd.length() < 9)
        return;
    bool okS, okL;
    int soundCard = cmd.mid(2, 3).toInt(&okS);
    int lineIn = cmd.mid(5, 3).toInt(&okL);
    int source = cmd.mid(8, 1).toInt();

    if (okS && okL && soundCard >= 0 && soundCard <= 250 && lineIn >= 0 && lineIn <= 250 &&
        (source == 0 || source == 1)) {
        bool changed = false;
        if (soundCard != state.lineInSoundCard) {
            state.lineInSoundCard = soundCard;
            changed = true;
        }
        if (lineIn != state.lineInJack) {
            state.lineInJack = lineIn;
            changed = true;
        }
        if (source != state.lineInSource) {
            state.lineInSource = source;
            changed = true;
        }
        if (changed)
            emit owner.lineInChanged();
    }
}

void handleMI(AudioEffectsState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 3)
        return;
    int input = cmd.mid(2, 1).toInt();
    if (input >= 0 && input <= 4 && input != state.micInput) {
        state.micInput = input;
        emit owner.micInputChanged(state.micInput);
    }
}

void handleMS(AudioEffectsState &state, RadioState &owner, const QString &cmd) {
    // MSabcde — front preamp/bias/buttons, rear preamp/bias
    if (cmd.length() < 7)
        return;
    int frontPreamp = cmd.mid(2, 1).toInt();
    int frontBias = cmd.mid(3, 1).toInt();
    int frontButtons = cmd.mid(4, 1).toInt();
    int rearPreamp = cmd.mid(5, 1).toInt();
    int rearBias = cmd.mid(6, 1).toInt();

    bool changed = false;
    if (frontPreamp >= 0 && frontPreamp <= 2 && frontPreamp != state.micFrontPreamp) {
        state.micFrontPreamp = frontPreamp;
        changed = true;
    }
    if ((frontBias == 0 || frontBias == 1) && frontBias != state.micFrontBias) {
        state.micFrontBias = frontBias;
        changed = true;
    }
    if ((frontButtons == 0 || frontButtons == 1) && frontButtons != state.micFrontButtons) {
        state.micFrontButtons = frontButtons;
        changed = true;
    }
    if ((rearPreamp == 0 || rearPreamp == 1) && rearPreamp != state.micRearPreamp) {
        state.micRearPreamp = rearPreamp;
        changed = true;
    }
    if ((rearBias == 0 || rearBias == 1) && rearBias != state.micRearBias) {
        state.micRearBias = rearBias;
        changed = true;
    }
    if (changed)
        emit owner.micSetupChanged();
}

void handleES(AudioEffectsState &state, RadioState &owner, const QString &cmd) {
    // ESnbb where n=0/1, bb=bandwidth
    if (cmd.length() < 4)
        return;
    bool ok;
    int modeVal = cmd.mid(2, 1).toInt(&ok);
    if (!ok || (modeVal != 0 && modeVal != 1))
        return;

    bool newEssb = (modeVal == 1);
    int newBw = state.ssbTxBw;

    if (cmd.length() >= 5) {
        newBw = cmd.mid(3, 2).toInt(&ok);
        if (!ok || newBw < 24 || newBw > 45) {
            newBw = state.ssbTxBw;
        }
    }

    bool changed = false;
    if (newEssb != state.essbEnabled) {
        state.essbEnabled = newEssb;
        changed = true;
    }
    if (newBw >= 24 && newBw <= 45 && newBw != state.ssbTxBw) {
        state.ssbTxBw = newBw;
        changed = true;
    }
    if (changed) {
        emit owner.essbChanged(state.essbEnabled, state.ssbTxBw);
    }
}

void handleRE(AudioEffectsState &state, RadioState &owner, const QString &cmd) {
    // RE + 8×3-char signed dB values (-16..+16)
    if (cmd.length() < 26)
        return;
    bool changed = false;
    for (int i = 0; i < 8; i++) {
        bool ok;
        int val = cmd.mid(2 + i * 3, 3).toInt(&ok);
        if (ok && val >= -16 && val <= 16 && val != state.rxEqBands[i]) {
            state.rxEqBands[i] = val;
            changed = true;
        }
    }
    if (changed)
        emit owner.rxEqChanged();
}

void handleTE(AudioEffectsState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 26)
        return;
    bool changed = false;
    for (int i = 0; i < 8; i++) {
        bool ok;
        int val = cmd.mid(2 + i * 3, 3).toInt(&ok);
        if (ok && val >= -16 && val <= 16 && val != state.txEqBands[i]) {
            state.txEqBands[i] = val;
            changed = true;
        }
    }
    if (changed)
        emit owner.txEqChanged();
}

void handleML(AudioEffectsState &state, RadioState &owner, const QString &cmd) {
    // MLmnnn where m=mode (0=CW, 1=Data, 2=Voice), nnn=000-100
    if (cmd.length() < 5)
        return;
    bool ok;
    int modeVal = cmd.mid(2, 1).toInt(&ok);
    if (!ok || modeVal < 0 || modeVal > 2)
        return;

    int level = cmd.mid(3).toInt(&ok);
    if (!ok || level < 0 || level > 100)
        return;

    int *target = nullptr;
    switch (modeVal) {
    case 0:
        target = &state.monitorLevelCW;
        break;
    case 1:
        target = &state.monitorLevelData;
        break;
    case 2:
        target = &state.monitorLevelVoice;
        break;
    }
    if (target && *target != level) {
        *target = level;
        emit owner.monitorLevelChanged(modeVal, level);
    }
}

void handleBL(AudioEffectsState &state, RadioState &owner, const QString &cmd) {
    // BLm±nn where m=mode (0=NOR, 1=BAL), ±nn=offset (-50..+50)
    if (cmd.length() < 5)
        return;
    int modeVal = cmd[2].digitValue();
    if (modeVal < 0 || modeVal > 1)
        return;
    QChar sign = cmd[3];
    bool ok;
    int nn = cmd.mid(4).toInt(&ok);
    if (!ok)
        return;
    int offset = (sign == '-') ? -nn : nn;
    offset = qBound(-50, offset, 50);
    if (state.balanceMode != modeVal || state.balanceOffset != offset) {
        state.balanceMode = modeVal;
        state.balanceOffset = offset;
        emit owner.balanceChanged(modeVal, offset);
    }
}

void handleMX(AudioEffectsState &state, RadioState &owner, const QString &cmd) {
    // MXL.R — left.right routing. Components: A=main(0), B=sub(1),
    // AB=main+sub(2), -A=neg main(3).
    if (cmd.length() < 5)
        return;

    QString body = cmd.mid(2);
    int dotPos = body.indexOf('.');
    if (dotPos < 1 || dotPos >= body.length() - 1)
        return;

    QString leftStr = body.left(dotPos);
    QString rightStr = body.mid(dotPos + 1);

    auto mapComponent = [](const QString &s) -> int {
        if (s == "A")
            return 0;
        if (s == "B")
            return 1;
        if (s == "AB")
            return 2;
        if (s == "-A")
            return 3;
        return -1;
    };

    int left = mapComponent(leftStr);
    int right = mapComponent(rightStr);
    if (left < 0 || right < 0)
        return;

    if (state.audioMixLeft != left || state.audioMixRight != right) {
        state.audioMixLeft = left;
        state.audioMixRight = right;
        emit owner.audioMixChanged(left, right);
    }
}

// ---------------------------------------------------------------------------
// Optimistic setters
// ---------------------------------------------------------------------------

void setAudioMix(AudioEffectsState &state, RadioState &owner, int left, int right) {
    left = qBound(0, left, 3);
    right = qBound(0, right, 3);
    if (state.audioMixLeft != left || state.audioMixRight != right) {
        state.audioMixLeft = left;
        state.audioMixRight = right;
        emit owner.audioMixChanged(left, right);
    }
}

void setBalance(AudioEffectsState &state, RadioState &owner, int mode, int offset) {
    mode = qBound(0, mode, 1);
    offset = qBound(-50, offset, 50);
    if (state.balanceMode != mode || state.balanceOffset != offset) {
        state.balanceMode = mode;
        state.balanceOffset = offset;
        emit owner.balanceChanged(mode, offset);
    }
}

void setMonitorLevel(AudioEffectsState &state, RadioState &owner, int mode, int level) {
    level = qBound(0, level, 100);
    int *target = nullptr;
    switch (mode) {
    case 0:
        target = &state.monitorLevelCW;
        break;
    case 1:
        target = &state.monitorLevelData;
        break;
    case 2:
        target = &state.monitorLevelVoice;
        break;
    default:
        return;
    }
    if (*target != level) {
        *target = level;
        emit owner.monitorLevelChanged(mode, level);
    }
}

void setRxEqBand(AudioEffectsState &state, RadioState &owner, int index, int dB) {
    if (index < 0 || index >= 8)
        return;
    dB = qBound(-16, dB, 16);
    if (state.rxEqBands[index] != dB) {
        state.rxEqBands[index] = dB;
        emit owner.rxEqChanged();
    }
}

void setRxEqBands(AudioEffectsState &state, RadioState &owner, const QVector<int> &bands) {
    bool changed = false;
    for (int i = 0; i < qMin(bands.size(), 8); i++) {
        int dB = qBound(-16, bands[i], 16);
        if (state.rxEqBands[i] != dB) {
            state.rxEqBands[i] = dB;
            changed = true;
        }
    }
    if (changed) {
        emit owner.rxEqChanged();
    }
}

void setTxEqBand(AudioEffectsState &state, RadioState &owner, int index, int dB) {
    if (index < 0 || index >= 8)
        return;
    dB = qBound(-16, dB, 16);
    if (state.txEqBands[index] != dB) {
        state.txEqBands[index] = dB;
        emit owner.txEqChanged();
    }
}

void setTxEqBands(AudioEffectsState &state, RadioState &owner, const QVector<int> &bands) {
    bool changed = false;
    for (int i = 0; i < qMin(bands.size(), 8); i++) {
        int dB = qBound(-16, bands[i], 16);
        if (state.txEqBands[i] != dB) {
            state.txEqBands[i] = dB;
            changed = true;
        }
    }
    if (changed) {
        emit owner.txEqChanged();
    }
}

void setLineOutLeft(AudioEffectsState &state, RadioState &owner, int level) {
    level = qBound(0, level, 40);
    if (level != state.lineOutLeft) {
        state.lineOutLeft = level;
        emit owner.lineOutChanged();
    }
}

void setLineOutRight(AudioEffectsState &state, RadioState &owner, int level) {
    level = qBound(0, level, 40);
    if (level != state.lineOutRight) {
        state.lineOutRight = level;
        emit owner.lineOutChanged();
    }
}

void setLineOutRightEqualsLeft(AudioEffectsState &state, RadioState &owner, bool enabled) {
    if (enabled != state.lineOutRightEqualsLeft) {
        state.lineOutRightEqualsLeft = enabled;
        emit owner.lineOutChanged();
    }
}

void setLineInSoundCard(AudioEffectsState &state, RadioState &owner, int level) {
    level = qBound(0, level, 250);
    if (level != state.lineInSoundCard) {
        state.lineInSoundCard = level;
        emit owner.lineInChanged();
    }
}

void setLineInJack(AudioEffectsState &state, RadioState &owner, int level) {
    level = qBound(0, level, 250);
    if (level != state.lineInJack) {
        state.lineInJack = level;
        emit owner.lineInChanged();
    }
}

void setLineInSource(AudioEffectsState &state, RadioState &owner, int source) {
    if ((source == 0 || source == 1) && source != state.lineInSource) {
        state.lineInSource = source;
        emit owner.lineInChanged();
    }
}

void setMicInput(AudioEffectsState &state, RadioState &owner, int input) {
    if (input >= 0 && input <= 4 && input != state.micInput) {
        state.micInput = input;
        emit owner.micInputChanged(state.micInput);
    }
}

void setMicFrontPreamp(AudioEffectsState &state, RadioState &owner, int preamp) {
    if (preamp >= 0 && preamp <= 2 && preamp != state.micFrontPreamp) {
        state.micFrontPreamp = preamp;
        emit owner.micSetupChanged();
    }
}

void setMicFrontBias(AudioEffectsState &state, RadioState &owner, int bias) {
    if ((bias == 0 || bias == 1) && bias != state.micFrontBias) {
        state.micFrontBias = bias;
        emit owner.micSetupChanged();
    }
}

void setMicFrontButtons(AudioEffectsState &state, RadioState &owner, int buttons) {
    if ((buttons == 0 || buttons == 1) && buttons != state.micFrontButtons) {
        state.micFrontButtons = buttons;
        emit owner.micSetupChanged();
    }
}

void setMicRearPreamp(AudioEffectsState &state, RadioState &owner, int preamp) {
    if ((preamp == 0 || preamp == 1) && preamp != state.micRearPreamp) {
        state.micRearPreamp = preamp;
        emit owner.micSetupChanged();
    }
}

void setMicRearBias(AudioEffectsState &state, RadioState &owner, int bias) {
    if ((bias == 0 || bias == 1) && bias != state.micRearBias) {
        state.micRearBias = bias;
        emit owner.micSetupChanged();
    }
}

void setVoxGainVoice(AudioEffectsState &state, RadioState &owner, int gain) {
    gain = qBound(0, gain, 60);
    if (gain != state.voxGainVoice) {
        state.voxGainVoice = gain;
        emit owner.voxGainChanged(0, gain);
    }
}

void setVoxGainData(AudioEffectsState &state, RadioState &owner, int gain) {
    gain = qBound(0, gain, 60);
    if (gain != state.voxGainData) {
        state.voxGainData = gain;
        emit owner.voxGainChanged(1, gain);
    }
}

void setAntiVox(AudioEffectsState &state, RadioState &owner, int level) {
    level = qBound(0, level, 60);
    if (level != state.antiVox) {
        state.antiVox = level;
        emit owner.antiVoxChanged(level);
    }
}

void setEssbEnabled(AudioEffectsState &state, RadioState &owner, bool enabled) {
    if (enabled != state.essbEnabled) {
        state.essbEnabled = enabled;
        emit owner.essbChanged(state.essbEnabled, state.ssbTxBw);
    }
}

void setSsbTxBw(AudioEffectsState &state, RadioState &owner, int bw) {
    bw = qBound(30, bw, 45);
    if (bw != state.ssbTxBw) {
        state.ssbTxBw = bw;
        emit owner.essbChanged(state.essbEnabled, state.ssbTxBw);
    }
}

} // namespace AudioEffectsHandlers
