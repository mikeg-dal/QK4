#include "datacontrolstate.h"

#include "models/radiostate.h"

#include <QtGlobal>

void DataControlState::reset() {
    dataSubMode = -1;
    dataSubModeB = -1;
    dataRate = -1;
    dataRateB = -1;
    tuningStep = -1;
    tuningStepB = -1;
    streamingLatency = -1;
    dataSubModeEchoHold = QDeadlineTimer();
    dataSubModeBEchoHold = QDeadlineTimer();
    dataRateEchoHold = QDeadlineTimer();
    dataRateBEchoHold = QDeadlineTimer();
}

namespace {
// FSK D and PSK D are direct data, which shares CW's QSK/VOX delay (RadioUtils::ModeClass), so in
// DATA a DT change can switch the delay in effect - as an MD change can (handleMD).
void setMainDataSubMode(DataControlState &state, RadioState &owner, int subMode) {
    const RadioUtils::ModeClass before = owner.modeClass();
    state.dataSubMode = subMode;
    emit owner.dataSubModeChanged(subMode);
    const int delay = owner.delayForCurrentMode();
    if (owner.modeClass() != before && delay >= 0)
        emit owner.qskDelayChanged(delay);
}
} // namespace

namespace DataControlHandlers {

void handleDT(DataControlState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 3)
        return;
    bool ok;
    const int subMode = cmd.mid(2).toInt(&ok);
    if (!ok || subMode < 0 || subMode > 3)
        return;
    // Ignore an echoed DT while a recent optimistic SET is still in flight.
    if (!state.dataSubModeEchoHold.hasExpired())
        return;
    if (subMode != state.dataSubMode)
        setMainDataSubMode(state, owner, subMode);
}

void handleDTSub(DataControlState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 4)
        return;
    bool ok;
    const int subMode = cmd.mid(3).toInt(&ok);
    if (!ok || subMode < 0 || subMode > 3)
        return;
    if (!state.dataSubModeBEchoHold.hasExpired())
        return;
    if (subMode != state.dataSubModeB) {
        state.dataSubModeB = subMode;
        emit owner.dataSubModeBChanged(state.dataSubModeB);
    }
}

void handleDR(DataControlState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 3)
        return;
    bool ok;
    const int rate = cmd.mid(2).toInt(&ok);
    if (!ok || rate < 0 || rate > 1)
        return;
    if (!state.dataRateEchoHold.hasExpired())
        return;
    if (rate != state.dataRate) {
        state.dataRate = rate;
        emit owner.dataRateChanged(state.dataRate);
    }
}

void handleDRSub(DataControlState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 4)
        return;
    bool ok;
    const int rate = cmd.mid(3).toInt(&ok);
    if (!ok || rate < 0 || rate > 1)
        return;
    if (!state.dataRateBEchoHold.hasExpired())
        return;
    if (rate != state.dataRateB) {
        state.dataRateB = rate;
        emit owner.dataRateBChanged(state.dataRateB);
    }
}

void handleVT(DataControlState &state, RadioState &owner, const QString &cmd) {
    // VT - tuning step. Takes the first digit from the payload.
    if (cmd.length() <= 2)
        return;
    bool ok;
    const int step = cmd.mid(2).left(1).toInt(&ok);
    if (!ok)
        return;
    const int ns = qBound(0, step, 5);
    if (ns != state.tuningStep) {
        state.tuningStep = ns;
        emit owner.tuningStepChanged(state.tuningStep);
    }
}

void handleVTSub(DataControlState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 3)
        return;
    bool ok;
    const int step = cmd.mid(3).left(1).toInt(&ok);
    if (!ok)
        return;
    const int ns = qBound(0, step, 5);
    if (ns != state.tuningStepB) {
        state.tuningStepB = ns;
        emit owner.tuningStepBChanged(state.tuningStepB);
    }
}

void handleSL(DataControlState &state, RadioState &owner, const QString &cmd) {
    // SL - streaming latency tier 0-7 (see k4-streaming-latency.md).
    if (cmd.length() <= 2)
        return;
    bool ok;
    const int tier = cmd.mid(2).toInt(&ok);
    if (!ok || tier < 0 || tier > 7)
        return;
    if (tier != state.streamingLatency) {
        state.streamingLatency = tier;
        emit owner.streamingLatencyChanged(state.streamingLatency);
    }
}

void setDataSubMode(DataControlState &state, RadioState &owner, int subMode) {
    subMode = qBound(0, subMode, 3);
    if (state.dataSubMode != subMode) {
        state.dataSubModeEchoHold.setRemainingTime(DataControlState::EchoHoldMs);
        setMainDataSubMode(state, owner, subMode);
    }
}

void setDataSubModeB(DataControlState &state, RadioState &owner, int subMode) {
    subMode = qBound(0, subMode, 3);
    if (state.dataSubModeB != subMode) {
        state.dataSubModeB = subMode;
        state.dataSubModeBEchoHold.setRemainingTime(DataControlState::EchoHoldMs);
        emit owner.dataSubModeBChanged(subMode);
    }
}

void setDataRate(DataControlState &state, RadioState &owner, int rate) {
    rate = qBound(0, rate, 1);
    if (state.dataRate != rate) {
        state.dataRate = rate;
        state.dataRateEchoHold.setRemainingTime(DataControlState::EchoHoldMs);
        emit owner.dataRateChanged(rate);
    }
}

void setDataRateB(DataControlState &state, RadioState &owner, int rate) {
    rate = qBound(0, rate, 1);
    if (state.dataRateB != rate) {
        state.dataRateB = rate;
        state.dataRateBEchoHold.setRemainingTime(DataControlState::EchoHoldMs);
        emit owner.dataRateBChanged(rate);
    }
}

} // namespace DataControlHandlers
