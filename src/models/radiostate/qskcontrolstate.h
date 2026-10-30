#ifndef MODELS_RADIOSTATE_QSKCONTROLSTATE_H
#define MODELS_RADIOSTATE_QSKCONTROLSTATE_H

#include "reportedflag.h"

#include <QString>

class RadioState;

// Full-break-in / QSK control. Tracks whether QSK is enabled and the TX-to-RX
// turnaround delay (in 10ms increments) per operating mode — CW, Voice, and
// Data all have independent delay values on the K4. Driven by the SD command.
//
// Plain struct — follows Pattern C (see PATTERNS.md → Subsystem State).
struct QskControlState {
    ReportedFlag qskEnabled; // CW-mode only; extracted from SD command x flag
    int qskDelayCW = -1;     // 0..255 (×10ms)
    int qskDelayVoice = -1;  // LSB / USB / AM / FM
    int qskDelayData = -1;

    void reset();
};

namespace QskHandlers {

// Parses SDxMzzz — updates qskEnabled (when mode char = 'C') and the
// per-mode qskDelay{CW,Voice,Data}. Emits qskEnabledChanged and
// qskDelayChanged via the façade.
void handleSD(QskControlState &state, RadioState &owner, const QString &cmd);

} // namespace QskHandlers

#endif // MODELS_RADIOSTATE_QSKCONTROLSTATE_H
