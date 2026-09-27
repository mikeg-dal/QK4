#include "rxtxmeterstate.h"

#include "models/radiostate.h"

#include <QHash>
#include <QStringView>
#include <QtGlobal>

void RxTxMeterState::reset() {
    sMeter = 0.0;
    sMeterB = 0.0;
    swrMeter = 1.0;
    alcMeter = 0;
    compressionDb = 0;
    forwardPower = 0.0;
    supplyVoltage = 0.0;
    supplyCurrent = 0.0;
    paDrainCurrent = 0.0;
    calibratedPaEfficiency = 0.0;
    paTemperatureC = -1;
    lpaTemperatureC = -1;
    isTransmitting = false;
    subReceiverEnabled = -1;
    diversityEnabled = -1;
    testMode = false;
    bSetEnabled = false;
    messageBank = -1;
    radioID.clear();
    radioModel.clear();
    optionModules.clear();
    firmwareVersions.clear();
}

namespace RxTxMeterHandlers {

void handleSM(RxTxMeterState &state, RadioState &owner, const QString &cmd) {
    // SMbb — Main RX S-meter. bars 0..18 = S0..S9, >18 encodes dB over S9.
    if (cmd.length() <= 2)
        return;
    bool ok;
    int bars = cmd.mid(2).toInt(&ok);
    if (!ok)
        return;
    if (bars <= 18) {
        state.sMeter = bars / 2.0;
    } else {
        int dbAboveS9 = (bars - 18) * 3;
        state.sMeter = 9.0 + dbAboveS9 / 10.0;
    }
    emit owner.sMeterChanged(state.sMeter);
}

void handleSMSub(RxTxMeterState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 3)
        return;
    bool ok;
    int bars = cmd.mid(3).toInt(&ok);
    if (!ok)
        return;
    if (bars <= 18) {
        state.sMeterB = bars / 2.0;
    } else {
        int dbAboveS9 = (bars - 18) * 3;
        state.sMeterB = 9.0 + dbAboveS9 / 10.0;
    }
    emit owner.sMeterBChanged(state.sMeterB);
}

void handlePO(RxTxMeterState & /*state*/, RadioState & /*owner*/, const QString & /*cmd*/) {
    // PO (raw power meter) — unused; forward power already comes from TM.
}

void handleTM(RxTxMeterState &state, RadioState &owner, const QString &cmd) {
    // TMaaabbbcccddd — ALC, CMP, FWD, SWR (each 3 digits).
    if (cmd.length() < 14)
        return;
    QString data = cmd.mid(2);
    if (data.length() < 12)
        return;

    bool ok1, ok2, ok3, ok4;
    int alc = data.mid(0, 3).toInt(&ok1);
    int cmp = data.mid(3, 3).toInt(&ok2);
    int fwd = data.mid(6, 3).toInt(&ok3);
    int swrRaw = data.mid(9, 3).toInt(&ok4);

    if (!ok1 || !ok2 || !ok3 || !ok4)
        return;

    state.alcMeter = alc;
    state.compressionDb = cmp;
    // FWD is watts in QRO, tenths of a watt in QRP.
    state.forwardPower = owner.isQrpMode() ? fwd / 10.0 : fwd;
    state.swrMeter = swrRaw / 10.0;

    emit owner.txMeterChanged(state.alcMeter, state.compressionDb, state.forwardPower, state.swrMeter);
    emit owner.swrChanged(state.swrMeter);

    // Drive a smooth Id reading at TM frame rate (~10 Hz) by deriving drain current
    // from fwdPower with the calibrated efficiency captured in handleSIRF. SIRF alone
    // arrives at ~0.3 Hz and bounces because the K4 samples PA drain at instantaneous
    // moments that land in CW key-up gaps — using TM frame rate + SIRF calibration
    // gives both smoothness and accuracy. Fallback: a fixed η=0.34 (the historical
    // hardcoded value) until the first SIRF lands, so the meter is never blank in TX.
    if (state.isTransmitting && state.supplyVoltage > 0 && state.forwardPower > 0) {
        constexpr double K4_PA_EFFICIENCY_FALLBACK = 0.34;
        const double eff =
            (state.calibratedPaEfficiency > 0) ? state.calibratedPaEfficiency : K4_PA_EFFICIENCY_FALLBACK;
        const double derivedDrain = state.forwardPower / (state.supplyVoltage * eff);
        if (!qFuzzyCompare(1.0 + derivedDrain, 1.0 + state.paDrainCurrent)) {
            state.paDrainCurrent = derivedDrain;
            emit owner.paDrainCurrentChanged(state.paDrainCurrent);
        }
    }
}

void handleTX(RxTxMeterState &state, RadioState &owner, const QString & /*cmd*/) {
    if (!state.isTransmitting) {
        state.isTransmitting = true;
        emit owner.transmitStateChanged(true);
    }
}

void handleRX(RxTxMeterState &state, RadioState &owner, const QString & /*cmd*/) {
    if (state.isTransmitting) {
        state.isTransmitting = false;
        emit owner.transmitStateChanged(false);
    }
}

void handleID(RxTxMeterState &state, RadioState & /*owner*/, const QString &cmd) {
    if (cmd.length() > 2)
        state.radioID = cmd.mid(2);
}

void handleOM(RxTxMeterState &state, RadioState & /*owner*/, const QString &cmd) {
    // OM format: 12-char string where each position indicates an option.
    // Positions 3=S (HD/subrx marker), 4=H (HD), 8=4 (K4 model marker).
    if (cmd.length() <= 2)
        return;
    state.optionModules = cmd.mid(2).trimmed();
    if (state.optionModules.length() > 8) {
        const bool hasS = state.optionModules.length() > 3 && state.optionModules[3] == 'S';
        const bool hasH = state.optionModules.length() > 4 && state.optionModules[4] == 'H';
        const bool has4 = state.optionModules.length() > 8 && state.optionModules[8] == '4';

        if (hasH && hasS && has4) {
            state.radioModel = "K4HD";
        } else if (hasS && has4) {
            state.radioModel = "K4D";
        } else if (has4) {
            state.radioModel = "K4";
        }
    }
}

void handleRV(RxTxMeterState &state, RadioState & /*owner*/, const QString &cmd) {
    // RV.COMPONENT-VERSION e.g. "RV.DDC0-00.65 (0:35)"
    if (cmd.length() <= 3)
        return;
    QString versionData = cmd.mid(3);
    int dashIndex = versionData.indexOf('-');
    if (dashIndex > 0) {
        QString component = versionData.left(dashIndex);
        QString version = versionData.mid(dashIndex + 1);
        state.firmwareVersions[component] = version;
    }
}

void handleER(RxTxMeterState & /*state*/, RadioState &owner, const QString &cmd) {
    int colonPos = cmd.indexOf(':');
    if (colonPos > 2) {
        bool ok;
        int errorCode = cmd.mid(2, colonPos - 2).toInt(&ok);
        if (ok)
            emit owner.errorNotificationReceived(errorCode, cmd.mid(colonPos + 1));
    }
}

void handleMN(RxTxMeterState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 3)
        return;
    bool ok;
    int bank = cmd.mid(2).toInt(&ok);
    if (ok && bank >= 1 && bank <= 4 && bank != state.messageBank) {
        state.messageBank = bank;
        emit owner.messageBankChanged(state.messageBank);
    }
}

void handleSIFP(RxTxMeterState &state, RadioState &owner, const QString &cmd) {
    // SIFPVS:xx.x,IS:xx.x ... — supply voltage/current.
    QString data = cmd.mid(4);

    int vsIndex = data.indexOf("VS:");
    if (vsIndex >= 0) {
        int commaIndex = data.indexOf(',', vsIndex);
        QString vsStr =
            (commaIndex > vsIndex) ? data.mid(vsIndex + 3, commaIndex - vsIndex - 3) : data.mid(vsIndex + 3);
        bool ok;
        double voltage = vsStr.toDouble(&ok);
        if (ok && voltage != state.supplyVoltage) {
            state.supplyVoltage = voltage;
            emit owner.supplyVoltageChanged(state.supplyVoltage);
        }
    }

    int isIndex = data.indexOf("IS:");
    if (isIndex >= 0) {
        int commaIndex = data.indexOf(',', isIndex);
        QString isStr =
            (commaIndex > isIndex) ? data.mid(isIndex + 3, commaIndex - isIndex - 3) : data.mid(isIndex + 3);
        bool ok;
        double current = isStr.toDouble(&ok);
        if (ok && current != state.supplyCurrent) {
            state.supplyCurrent = current;
            emit owner.supplyCurrentChanged(state.supplyCurrent);
        }
    }
}

void handleSIRF(RxTxMeterState &state, RadioState &owner, const QString &cmd) {
    // SIRF V8:..,V5:..,LT:..,LM:..,PA:..,PM:..,PT:.. — RF deck status. We surface:
    //   - PM (peak drain) ÷ 768 → Id (matches K4 front-panel "Id" within ~1 A)
    //   - PT → PA temperature in °C
    //   - LT → Lower-PA temperature in °C
    //
    // PM-to-Id reference:
    //   50 W TX  PM ~12298 / 768 = 16.0 A (panel: 16 A)
    //   90 W TX  PM ~15214 / 768 = 19.8 A (panel: 19–20 A)
    //  100 W TX  PM ~15976 / 768 = 20.8 A (panel: 20–21 A)
    // (LM is one stage's drain, ~5–7 A low vs the panel — don't use it.)
    //
    // WHY tokenize instead of indexOf("KEY:"): naive substring matching is fragile —
    // an added or relocated SIRF field like `XPT:` or `RPT:` could mis-route the PT
    // lookup to the wrong value. Tokenizing once into a key→value map keys every
    // lookup on exact field names, so unrelated field additions can never collide.

    QStringView payload(cmd);
    payload = payload.mid(4); // strip "SIRF"

    QHash<QStringView, QStringView> fields;
    for (auto token : payload.split(u',', Qt::SkipEmptyParts)) {
        const qsizetype colon = token.indexOf(u':');
        if (colon <= 0)
            continue;
        fields.insert(token.left(colon), token.mid(colon + 1));
    }

    bool ok = false;
    const auto pmIt = fields.constFind(u"PM");
    if (pmIt == fields.constEnd())
        return; // PM is the original required field — preserve that contract.
    const double pmRaw = pmIt.value().toDouble(&ok);
    if (!ok)
        return;
    constexpr double K4_PM_TO_AMPS = 768.0;
    const double current = pmRaw / K4_PM_TO_AMPS;

    // Capture the K4's actual efficiency at the current power/band so handleTM can
    // derive a smooth Id at frame rate. Only update calibration when we have a real
    // TX-time SIRF (current > 0.1 A — avoids divide-by-near-zero and TX-edge stale-zero
    // frames) AND a sensible forward power reading. Calibration is sticky: it stays
    // valid across band changes until the next valid SIRF resets it.
    if (state.isTransmitting && current > 0.1 && state.forwardPower > 0 && state.supplyVoltage > 0) {
        const double eff = state.forwardPower / (current * state.supplyVoltage);
        // Sanity-clamp to a plausible PA-efficiency range (15–60 %). Outside this is
        // almost certainly a stale value or PA-load mismatch; keep the previous good
        // calibration rather than overwriting with noise.
        if (eff > 0.15 && eff < 0.6)
            state.calibratedPaEfficiency = eff;
    }

    if (!qFuzzyCompare(1.0 + current, 1.0 + state.paDrainCurrent)) {
        state.paDrainCurrent = current;
        emit owner.paDrainCurrentChanged(state.paDrainCurrent);
    }

    // PA temperature (PT, °C).
    if (const auto it = fields.constFind(u"PT"); it != fields.constEnd()) {
        const int t = it.value().toInt(&ok);
        if (ok && t != state.paTemperatureC) {
            state.paTemperatureC = t;
            emit owner.paTemperatureChanged(t);
        }
    }

    // Lower-PA temperature (LT, °C).
    if (const auto it = fields.constFind(u"LT"); it != fields.constEnd()) {
        const int t = it.value().toInt(&ok);
        if (ok && t != state.lpaTemperatureC) {
            state.lpaTemperatureC = t;
            emit owner.lpaTemperatureChanged(t);
        }
    }
}

void handleSB(RxTxMeterState &state, RadioState &owner, const QString &cmd) {
    // SB0=off, SB1=on, SB3=on with the Sub mini-pan running (see catframes.h)
    if (cmd.length() <= 2)
        return;
    int newState = (cmd.mid(2) != "0") ? 1 : 0;
    if (newState != state.subReceiverEnabled) {
        state.subReceiverEnabled = newState;
        emit owner.subRxEnabledChanged(newState == 1);
    }
}

void handleDV(RxTxMeterState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() <= 2)
        return;
    int newState = (cmd.mid(2) == "1") ? 1 : 0;
    if (newState != state.diversityEnabled) {
        state.diversityEnabled = newState;
        emit owner.diversityChanged(newState == 1);
    }
}

void handleTS(RxTxMeterState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 3)
        return;
    bool enabled = (cmd.mid(2, 1) == "1");
    if (enabled != state.testMode) {
        state.testMode = enabled;
        emit owner.testModeChanged(state.testMode);
    }
}

void handleBS(RxTxMeterState &state, RadioState &owner, const QString &cmd) {
    if (cmd.length() < 3)
        return;
    bool enabled = (cmd.mid(2, 1) == "1");
    if (enabled != state.bSetEnabled) {
        state.bSetEnabled = enabled;
        emit owner.bSetChanged(state.bSetEnabled);
    }
}

void setBSetEnabled(RxTxMeterState &state, RadioState &owner, bool enabled) {
    if (enabled != state.bSetEnabled) {
        state.bSetEnabled = enabled;
        emit owner.bSetChanged(state.bSetEnabled);
    }
}

} // namespace RxTxMeterHandlers
