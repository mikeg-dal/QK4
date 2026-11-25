#include "antennastate.h"

#include "models/radiostate.h"

#include <algorithm>

void AntennaState::reset() {
    selectedAntenna = -1;
    receiveAntenna = -1;
    receiveAntennaSub = -1;
    atuMode = -1;
    antennaNames.clear();
    mainRxDisplayAll.reset();
    std::fill(std::begin(mainRxAntMask), std::end(mainRxAntMask), false);
    subRxDisplayAll.reset();
    std::fill(std::begin(subRxAntMask), std::end(subRxAntMask), false);
    txDisplayAll.reset();
    std::fill(std::begin(txAntMask), std::end(txAntMask), false);
}

namespace AntennaHandlers {

void handleAN(AntennaState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 2)
        return;
    bool ok;
    const int an = cmd.mid(2).toInt(&ok);
    if (ok && an >= 1 && an <= 6 && an != state.selectedAntenna) {
        state.selectedAntenna = an;
        emit owner.antennaChanged(state.selectedAntenna, state.receiveAntenna, state.receiveAntennaSub);
    }
}

void handleAR(AntennaState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 2)
        return;
    bool ok;
    const int ar = cmd.mid(2).toInt(&ok);
    if (ok && ar >= 0 && ar <= 7 && ar != state.receiveAntenna) {
        state.receiveAntenna = ar;
        emit owner.antennaChanged(state.selectedAntenna, state.receiveAntenna, state.receiveAntennaSub);
    }
}

void handleARSub(AntennaState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 3)
        return;
    bool ok;
    const int ar = cmd.mid(3).toInt(&ok);
    if (ok && ar >= 0 && ar <= 7 && ar != state.receiveAntennaSub) {
        state.receiveAntennaSub = ar;
        emit owner.antennaChanged(state.selectedAntenna, state.receiveAntenna, state.receiveAntennaSub);
    }
}

void handleAT(AntennaState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 3)
        return;
    bool ok;
    const int at = cmd.mid(2).toInt(&ok);
    if (ok && at >= 0 && at <= 2 && at != state.atuMode) {
        state.atuMode = at;
        emit owner.atuModeChanged(state.atuMode);
    }
}

QString defaultAntennaName(int index) {
    if (index == 4 || index == 5)
        return QStringLiteral("RX%1").arg(index - 3);
    return QStringLiteral("ANT%1").arg(index);
}

void handleACN(AntennaState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 4)
        return;
    bool ok;
    const int index = cmd.mid(3, 1).toInt(&ok);
    if (!ok || index < 1 || index > 7)
        return;

    // WHY an empty name is a clear, not a no-op: `ACNn~;` clears a custom name, and the radio then
    // reports the antenna with no name at all. Ignoring that left the cleared custom name on screen
    // for the rest of the session. R40 made the radio report the restored default instead, so this
    // path only matters on older firmware — but both forms must land on the same displayed name.
    const QString name = cmd.mid(4).trimmed();
    const QString effective = name.isEmpty() ? defaultAntennaName(index) : name;
    if (effective == state.antennaNames.value(index))
        return;
    state.antennaNames[index] = effective;
    emit owner.antennaNameChanged(index, effective);
}

void handleACM(AntennaState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 11)
        return;
    const bool displayAll = (cmd.at(3) == '1');
    bool changed = state.mainRxDisplayAll.set(displayAll);
    for (int i = 0; i < 7; i++) {
        const bool enabled = (cmd.at(4 + i) == '1');
        if (enabled != state.mainRxAntMask[i]) {
            state.mainRxAntMask[i] = enabled;
            changed = true;
        }
    }
    if (changed)
        emit owner.mainRxAntCfgChanged();
}

void handleACS(AntennaState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 11)
        return;
    const bool displayAll = (cmd.at(3) == '1');
    bool changed = state.subRxDisplayAll.set(displayAll);
    for (int i = 0; i < 7; i++) {
        const bool enabled = (cmd.at(4 + i) == '1');
        if (enabled != state.subRxAntMask[i]) {
            state.subRxAntMask[i] = enabled;
            changed = true;
        }
    }
    if (changed)
        emit owner.subRxAntCfgChanged();
}

void handleACT(AntennaState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 7)
        return;
    const bool displayAll = (cmd.at(3) == '1');
    bool changed = state.txDisplayAll.set(displayAll);
    for (int i = 0; i < 3; i++) {
        const bool enabled = (cmd.at(4 + i) == '1');
        if (enabled != state.txAntMask[i]) {
            state.txAntMask[i] = enabled;
            changed = true;
        }
    }
    if (changed)
        emit owner.txAntCfgChanged();
}

namespace {
// True if anything changed. `count` is the number of antennas the command carries.
bool applyAntConfig(ReportedFlag &displayAllFlag, bool *antMask, int count, bool displayAll,
                    const QVector<bool> &mask) {
    bool changed = displayAllFlag.set(displayAll);
    for (int i = 0; i < qMin(static_cast<int>(mask.size()), count); i++) {
        if (mask[i] != antMask[i]) {
            antMask[i] = mask[i];
            changed = true;
        }
    }
    return changed;
}
} // namespace

void setMainRxAntConfig(AntennaState &state, RadioState &owner, bool displayAll, const QVector<bool> &mask) {
    if (applyAntConfig(state.mainRxDisplayAll, state.mainRxAntMask, 7, displayAll, mask))
        emit owner.mainRxAntCfgChanged();
}

void setSubRxAntConfig(AntennaState &state, RadioState &owner, bool displayAll, const QVector<bool> &mask) {
    if (applyAntConfig(state.subRxDisplayAll, state.subRxAntMask, 7, displayAll, mask))
        emit owner.subRxAntCfgChanged();
}

void setTxAntConfig(AntennaState &state, RadioState &owner, bool displayAll, const QVector<bool> &mask) {
    if (applyAntConfig(state.txDisplayAll, state.txAntMask, 3, displayAll, mask))
        emit owner.txAntCfgChanged();
}

} // namespace AntennaHandlers
