#include "frequencyvfostate.h"

#include "models/radiostate.h"

void FrequencyVfoState::reset() {
    frequency = 0;
    vfoA = 0;
    vfoB = 0;
    splitEnabled.reset();
    ritEnabled.reset();
    xitEnabled.reset();
    ritXitOffset = RitXitOffsetUnknown;
    ritEnabledB.reset();
    ritXitOffsetB = RitXitOffsetUnknown;
    vfoLink.reset();
    lockA.reset();
    lockB.reset();
}

namespace FrequencyVfoHandlers {

void handleFA(FrequencyVfoState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 2)
        return;
    bool ok;
    const quint64 freq = cmd.mid(2).toULongLong(&ok);
    if (ok && state.vfoA != freq) {
        state.vfoA = freq;
        state.frequency = freq;
        emit owner.frequencyChanged(freq);
    }
}

void handleFB(FrequencyVfoState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 2)
        return;
    bool ok;
    const quint64 freq = cmd.mid(2).toULongLong(&ok);
    if (ok && state.vfoB != freq) {
        state.vfoB = freq;
        emit owner.frequencyBChanged(freq);
    }
}

void handleFT(FrequencyVfoState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 2)
        return;
    const bool newSplit = (cmd.mid(2) == "1");
    if (state.splitEnabled.set(newSplit))
        emit owner.splitChanged(newSplit);
}

void handleRT(FrequencyVfoState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 3 || (cmd.at(2) != '0' && cmd.at(2) != '1'))
        return;
    if (state.ritEnabled.set(cmd.at(2) == '1'))
        emit owner.ritXitChanged(owner.ritEnabled(), owner.xitEnabled(), owner.ritXitOffset());
}

void handleRTSub(FrequencyVfoState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 4 || (cmd.at(3) != '0' && cmd.at(3) != '1'))
        return;
    if (state.ritEnabledB.set(cmd.at(3) == '1'))
        emit owner.ritXitBChanged(owner.ritEnabledB(), owner.ritXitOffsetB());
}

void handleXT(FrequencyVfoState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 3 || (cmd.at(2) != '0' && cmd.at(2) != '1'))
        return;
    if (state.xitEnabled.set(cmd.at(2) == '1'))
        emit owner.ritXitChanged(owner.ritEnabled(), owner.xitEnabled(), owner.ritXitOffset());
}

void handleRO(FrequencyVfoState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 3)
        return;
    bool ok;
    const int offset = cmd.mid(2).toInt(&ok);
    if (ok && offset != state.ritXitOffset) {
        state.ritXitOffset = offset;
        emit owner.ritXitChanged(owner.ritEnabled(), owner.xitEnabled(), offset);
    }
}

void handleROSub(FrequencyVfoState &state, RadioState &owner, const QString &cmd) {
    // WHY RO$ is per-VFO even though RT/XT are split across RO and RO$:
    //   - No split, RIT or XIT → offset in RO (VFO A)
    //   - Split + XIT          → offset in RO$ (VFO B = TX VFO)
    //   - BSET + RIT           → offset in RO$ (VFO B)
    // We always mutate ritXitOffsetB here and let the UI reassemble the
    // right display based on split/XIT/BSET state.
    if (cmd.length() < 4)
        return;
    bool ok;
    const int offset = cmd.mid(3).toInt(&ok);
    if (ok && offset != state.ritXitOffsetB) {
        state.ritXitOffsetB = offset;
        emit owner.ritXitBChanged(owner.ritEnabledB(), offset);
    }
}

void handleLN(FrequencyVfoState &state, RadioState &owner, const QString &cmd) {
    // LNx where x = 1 (linked) / 0 (unlinked).
    if (cmd.length() < 3)
        return;
    bool ok;
    const int ln = cmd.mid(2).toInt(&ok);
    if (!ok)
        return;
    const bool linked = (ln == 1);
    if (state.vfoLink.set(linked))
        emit owner.vfoLinkChanged(linked);
}

void handleLK(FrequencyVfoState &state, RadioState &owner, const QString &cmd) {
    // LKx where x = 0/1.
    if (cmd.length() < 3)
        return;
    const bool locked = (cmd.at(2) == '1');
    if (state.lockA.set(locked))
        emit owner.lockAChanged(locked);
}

void handleLKSub(FrequencyVfoState &state, RadioState &owner, const QString &cmd) {
    // LK$x where x = 0/1.
    if (cmd.length() < 4)
        return;
    const bool locked = (cmd.at(3) == '1');
    if (state.lockB.set(locked))
        emit owner.lockBChanged(locked);
}

} // namespace FrequencyVfoHandlers
