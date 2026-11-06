#ifndef MODELS_RADIOSTATE_MODEFILTERSTATE_H
#define MODELS_RADIOSTATE_MODEFILTERSTATE_H

#include <QChar>
#include <QString>

class RadioState;

// Mode, filter shape, and CW keyer state. Covers MD, MD$, BW, BW$,
// IS, IS$, FP, FP$, CW (pitch), KS (keyer speed), KP (iambic mode
// + paddle orientation + keying weight), and KZL (remote key-down delay). These cluster naturally
// because the CW keyer settings are only meaningful when the mode is
// CW or CW_R, and the filter shape (BW/IS/FP) is per-mode on the K4.
//
// Mode values stored as int to avoid a circular include with
// RadioState::Mode enum. 0=Unknown, 1=LSB, 2=USB, 3=CW, 4=FM, 5=AM,
// 6=DATA, 7=CW_R, 9=DATA_R — matches RadioState::Mode.
struct ModeFilterState {
    int mode = 0; // 0=Unknown
    int modeB = 0;

    int filterBandwidth = -1;
    int filterBandwidthB = -1;
    int filterPosition = -1;
    int filterPositionB = -1;
    int ifShift = -1;
    int ifShiftB = -1;

    int cwPitch = -1;    // Hz
    int keyerSpeed = -1; // WPM

    // KZL: the K4's remote-paddle key-down initial delay, 0-1000 ms (default 80). The radio holds a
    // remote key-down this long before transmitting so later timed key commands arrive in time.
    // It is the operator's setting: QK4 writes it only from the CW Keyer page's Key-Down Delay.
    int keyDownInitialDelayMs = -1;

    // Keyer paddle config (KP command): iambic + paddle + weight.
    QChar iambicMode;        // 'A' or 'B' (null = not yet received)
    QChar paddleOrientation; // 'N'ormal or 'R'everse
    int keyingWeight = -1;   // 090-125 (ratio × 100)

    void reset();
};

namespace ModeFilterHandlers {

void handleMD(ModeFilterState &state, RadioState &owner, const QString &cmd);
void handleMDSub(ModeFilterState &state, RadioState &owner, const QString &cmd);
void handleCW(ModeFilterState &state, RadioState &owner, const QString &cmd);
void handleKS(ModeFilterState &state, RadioState &owner, const QString &cmd);
void handleKP(ModeFilterState &state, RadioState &owner, const QString &cmd);
void handleKZL(ModeFilterState &state, RadioState &owner, const QString &cmd);

// Inline lambda handlers in registerCommandHandlers call these:
void handleBW(ModeFilterState &state, RadioState &owner, const QString &cmd);
void handleBWSub(ModeFilterState &state, RadioState &owner, const QString &cmd);

// Optimistic setters.
void setFilterBandwidth(ModeFilterState &state, RadioState &owner, int bwHz);
void setFilterBandwidthB(ModeFilterState &state, RadioState &owner, int bwHz);
void setIfShift(ModeFilterState &state, RadioState &owner, int shift);
void setIfShiftB(ModeFilterState &state, RadioState &owner, int shift);
void setCwPitch(ModeFilterState &state, RadioState &owner, int pitchHz);
void setKeyerSpeed(ModeFilterState &state, RadioState &owner, int wpm);
void setIambicMode(ModeFilterState &state, RadioState &owner, QChar mode);
void setPaddleOrientation(ModeFilterState &state, RadioState &owner, QChar orientation);
void setKeyingWeight(ModeFilterState &state, RadioState &owner, int weight);

} // namespace ModeFilterHandlers

#endif // MODELS_RADIOSTATE_MODEFILTERSTATE_H
