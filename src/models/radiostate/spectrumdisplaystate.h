#ifndef MODELS_RADIOSTATE_SPECTRUMDISPLAYSTATE_H
#define MODELS_RADIOSTATE_SPECTRUMDISPLAYSTATE_H

#include <QString>

class RadioState;

// Panadapter / waterfall / display-related state — all of the K4's
// `#`-prefix commands. Covers reference level, span, scale, display
// mode (spectrum / spectrum+waterfall), dual-pan mode, frame rate,
// waterfall color + height, averaging, peak, fixed-tune + mode, freeze,
// VFO cursor, auto ref level, DDC NB mode + level, and mini-pan state.
//
// These all belong to the panadapter display subsystem. They were
// spread across RadioState as ~20 sentinel-init ints.
struct SpectrumDisplayState {
    // #REF / #REF$ — Ref level (dBm), default -110
    int refLevel = -110;
    int refLevelB = -110;

    // #SPN / #SPN$ — panadapter span in Hz. Init 0 to force first emit.
    int spanHz = 0;
    int spanHzB = 0;

    // #SCL — panadapter scale (GLOBAL, 10-150). -1 until first response.
    int scale = -1;

    // #MP / #MP$ — mini-pan enabled.
    bool miniPanAEnabled = false;
    bool miniPanBEnabled = false;

    // Display state (LCD vs EXT variants where applicable)
    int dualPanModeLcd = -1;     // #DPM 0=A, 1=B, 2=Dual
    int dualPanModeExt = -1;     // #HDPM
    int displayModeLcd = -1;     // #DSM 0=spec, 1=spec+waterfall
    int displayModeExt = -1;     // #HDSM
    int displayFps = 30;         // #FPS 12-30
    int waterfallColor = -1;     // #WFC 0-4
    int waterfallHeight = 50;    // #WFH 0-100%
    int waterfallHeightExt = 50; // #HWFH 0-100%
    int averaging = -1;          // #AVG 1-20
    int peakMode = -1;           // #PKM 0/1 — int to preserve -1 sentinel
    int fixedTune = -1;          // #FXT 0=track, 1=fixed
    int fixedTuneMode = -1;      // #FXA 0-4
    int freeze = -1;             // #FRZ 0/1
    int vfoACursor = -1;         // #VFA 0=OFF, 1=ON, 2=AUTO, 3=HIDE
    int vfoBCursor = -1;         // #VFB
    int autoRefLevel = -1;       // #AR A=auto, M=manual
    int ddcNbMode = -1;          // #NB$ 0=off, 1=on, 2=auto
    int ddcNbLevel = -1;         // #NBL$ 0-14

    void reset();
};

namespace SpectrumDisplayHandlers {

// CAT inbound handlers.
void handleREF(SpectrumDisplayState &state, RadioState &owner, const QString &cmd);
void handleREFSub(SpectrumDisplayState &state, RadioState &owner, const QString &cmd);
void handleSPN(SpectrumDisplayState &state, RadioState &owner, const QString &cmd);
void handleSPNSub(SpectrumDisplayState &state, RadioState &owner, const QString &cmd);
void handleSCL(SpectrumDisplayState &state, RadioState &owner, const QString &cmd);
void handleMP(SpectrumDisplayState &state, RadioState &owner, const QString &cmd);
void handleMPSub(SpectrumDisplayState &state, RadioState &owner, const QString &cmd);
void handleDPM(SpectrumDisplayState &state, RadioState &owner, const QString &cmd);
void handleHDPM(SpectrumDisplayState &state, RadioState &owner, const QString &cmd);
void handleDSM(SpectrumDisplayState &state, RadioState &owner, const QString &cmd);
void handleHDSM(SpectrumDisplayState &state, RadioState &owner, const QString &cmd);
void handleFPS(SpectrumDisplayState &state, RadioState &owner, const QString &cmd);
void handleWFC(SpectrumDisplayState &state, RadioState &owner, const QString &cmd);
void handleWFH(SpectrumDisplayState &state, RadioState &owner, const QString &cmd);
void handleHWFH(SpectrumDisplayState &state, RadioState &owner, const QString &cmd);
void handleAVG(SpectrumDisplayState &state, RadioState &owner, const QString &cmd);
void handlePKM(SpectrumDisplayState &state, RadioState &owner, const QString &cmd);
void handleFXT(SpectrumDisplayState &state, RadioState &owner, const QString &cmd);
void handleFXA(SpectrumDisplayState &state, RadioState &owner, const QString &cmd);
void handleFRZ(SpectrumDisplayState &state, RadioState &owner, const QString &cmd);
void handleVFA(SpectrumDisplayState &state, RadioState &owner, const QString &cmd);
void handleVFB(SpectrumDisplayState &state, RadioState &owner, const QString &cmd);
void handleAR(SpectrumDisplayState &state, RadioState &owner, const QString &cmd);
void handleNB(SpectrumDisplayState &state, RadioState &owner, const QString &cmd);
void handleNBL(SpectrumDisplayState &state, RadioState &owner, const QString &cmd);

// Optimistic setters.
void setRefLevel(SpectrumDisplayState &state, RadioState &owner, int level);
void setScale(SpectrumDisplayState &state, RadioState &owner, int scale);
void setSpanHz(SpectrumDisplayState &state, RadioState &owner, int spanHz);
void setRefLevelB(SpectrumDisplayState &state, RadioState &owner, int level);
void setSpanHzB(SpectrumDisplayState &state, RadioState &owner, int spanHz);
void setMiniPanAEnabled(SpectrumDisplayState &state, RadioState &owner, bool enabled);
void setMiniPanBEnabled(SpectrumDisplayState &state, RadioState &owner, bool enabled);
void setWaterfallHeight(SpectrumDisplayState &state, RadioState &owner, int percent);
void setWaterfallHeightExt(SpectrumDisplayState &state, RadioState &owner, int percent);
void setAveraging(SpectrumDisplayState &state, RadioState &owner, int value);
void setVfoACursor(SpectrumDisplayState &state, RadioState &owner, int mode);
void setVfoBCursor(SpectrumDisplayState &state, RadioState &owner, int mode);

} // namespace SpectrumDisplayHandlers

#endif // MODELS_RADIOSTATE_SPECTRUMDISPLAYSTATE_H
