#include "qskcontrolstate.h"

#include "models/radiostate.h"

void QskControlState::reset() {
    qskEnabled = false;
    qskDelayCW = -1;
    qskDelayVoice = -1;
    qskDelayData = -1;
}

namespace QskHandlers {

void handleSD(QskControlState &state, RadioState &owner, const QString &cmd) {
    // SDxMzzz where x=QSK flag, M=mode char (C/V/D), zzz=delay in 10ms increments.
    if (cmd.length() < 7)
        return;
    const QChar qskFlag = cmd.at(2);
    const QChar modeChar = cmd.at(3);
    bool ok;
    const int delay = cmd.mid(4, 3).toInt(&ok);
    if (!ok)
        return;

    // QSK enable flag is only meaningful on the CW-mode SD echo.
    if (modeChar == 'C') {
        const bool qskOn = (qskFlag == '1');
        if (qskOn != state.qskEnabled) {
            state.qskEnabled = qskOn;
            emit owner.qskEnabledChanged(state.qskEnabled);
        }
    }

    // WHY: only emit qskDelayChanged when the updated bucket matches the
    // current operating mode, so UI consumers don't refresh for buckets
    // the user can't see.
    int *target = nullptr;
    if (modeChar == 'C')
        target = &state.qskDelayCW;
    else if (modeChar == 'V')
        target = &state.qskDelayVoice;
    else if (modeChar == 'D')
        target = &state.qskDelayData;
    if (!target || *target == delay)
        return;
    *target = delay;
    if (modeChar == QLatin1Char(RadioUtils::modeClassLetter(owner.modeClass())))
        emit owner.qskDelayChanged(delay);
}

} // namespace QskHandlers
