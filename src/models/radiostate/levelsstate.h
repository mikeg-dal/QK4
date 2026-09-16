#ifndef MODELS_RADIOSTATE_LEVELSSTATE_H
#define MODELS_RADIOSTATE_LEVELSSTATE_H

#include <QString>

class RadioState;

// Signal-level / gain controls shared across the K4's RX and TX paths.
// TX side: RF power (PC), mic gain (MG), speech compression (CP).
// RX side: RF gain (RG / RG$), squelch (SQ / SQ$).
//
// Plain struct — follows Pattern C (see PATTERNS.md → Subsystem State).
struct LevelsState {
    // PC command range (K4 reference rev D5):
    //   L = QRP (0.1–10.0 W) — rfPower stored as W
    //   H = QRO (1–110 W) — rfPower stored as W
    //   X = XVTR (0.1–10.0 mW) — rfPower stored as mW
    // Used by the side-panel power readout to switch its unit label between
    // "PWR" (W) and "mW", and by CatFrames::rfPowerExtended() to emit PCX
    // with the right suffix.
    enum class PowerRange { Qrp, Qro, Xvtr };

    // TX signal levels.
    double rfPower = -1.0; // sentinel → first PC echo always emits. Units depend on powerRange.
    PowerRange powerRange = PowerRange::Qro;
    int micGain = -1;     // MG: 0-80
    int compression = -1; // CP: 0-30 (SSB only)

    // RX signal levels. RF gain is stored as a positive magnitude 0..60;
    // the minus sign is re-added only when dispatching CAT or formatting for display.
    int rfGain = -999;
    int rfGainB = -999;
    int squelchLevel = -1;
    int squelchLevelB = -1;

    void reset();
};

namespace LevelsHandlers {

// CAT handlers.
void handleMG(LevelsState &state, RadioState &owner, const QString &cmd); // Mic gain
void handleCP(LevelsState &state, RadioState &owner, const QString &cmd); // Compression
void handlePC(LevelsState &state, RadioState &owner, const QString &cmd); // Power control (rfPower + isQrpMode)
void handleRG(LevelsState &state, RadioState &owner, const QString &cmd); // RF gain (Main)
void handleRGSub(LevelsState &state, RadioState &owner, const QString &cmd);
void handleSQ(LevelsState &state, RadioState &owner, const QString &cmd); // Squelch (Main)
void handleSQSub(LevelsState &state, RadioState &owner, const QString &cmd);

// Optimistic setters (radio may not echo these; UI sets them directly).
void setRfPower(LevelsState &state, RadioState &owner, double watts);
void setMicGain(LevelsState &state, RadioState &owner, int gain);
void setCompression(LevelsState &state, RadioState &owner, int level);
void setRfGain(LevelsState &state, RadioState &owner, int gain);
void setRfGainB(LevelsState &state, RadioState &owner, int gain);
void setSquelchLevel(LevelsState &state, RadioState &owner, int level);
void setSquelchLevelB(LevelsState &state, RadioState &owner, int level);

} // namespace LevelsHandlers

#endif // MODELS_RADIOSTATE_LEVELSSTATE_H
