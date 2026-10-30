#ifndef MODELS_RADIOSTATE_SPECTRUMDISPLAYSTATE_H
#define MODELS_RADIOSTATE_SPECTRUMDISPLAYSTATE_H

#include "reportedflag.h"

#include <QString>

#include <optional>

class RadioState;

// Ranges and defaults for the K4's panadapter amplitude window, shared by the CAT parser and every
// place QK4 adjusts them (wheel, display popup, renderer), so they cannot disagree.
namespace SpectrumDisplayLimits {
// #REF / #REF$ — bottom of the window, dBm.
constexpr int RefLevelMin = -200;
constexpr int RefLevelMax = 50;
// Before the radio has reported a REF (and after reset()): outside the legal range, so the first
// #REF always emits, and anything that steps REF has nothing to step from.
constexpr int RefLevelUnknown = -999;
// The panadapter's window before the radio's first #REF / #SCL. Display only — never sent.
constexpr int RefLevelDefault = -110;
// #SCL — height of the window, dB (global).
constexpr int ScaleMin = 10;
constexpr int ScaleMax = 150;
constexpr int ScaleDefault = 75;
// #WBS / #HWBS — waterfall colour range ("WTR CLRS"), dB. A higher number gives brighter colours
// for the same signal (K4 Operating Manual rev D6, DISPLAY → WTR CLRS).
constexpr int WaterfallColorRangeMin = 5;
constexpr int WaterfallColorRangeMax = 30;
// #FPS — display frame rate. Unknown until the radio reports one; displayFps() reads it as FpsDefault.
constexpr int FpsUnknown = -1;
constexpr int FpsDefault = 30;
} // namespace SpectrumDisplayLimits

// Panadapter / waterfall / display-related state — all of the K4's
// `#`-prefix commands. Covers reference level, span, scale, display
// mode (spectrum / spectrum+waterfall), dual-pan mode, frame rate,
// waterfall color + height, averaging, peak, fixed-tune + mode, freeze,
// VFO cursor, auto ref level, DDC NB mode + level, and mini-pan state.
//
// These all belong to the panadapter display subsystem. They were
// spread across RadioState as ~20 sentinel-init ints.
struct SpectrumDisplayState {
    // #REF / #REF$ — Ref level (dBm). RefLevelUnknown until the radio reports one.
    int refLevel = SpectrumDisplayLimits::RefLevelUnknown;
    int refLevelB = SpectrumDisplayLimits::RefLevelUnknown;

    // #SPN / #SPN$ — panadapter span in Hz. Init 0 to force first emit.
    int spanHz = 0;
    int spanHzB = 0;

    // #SCL — panadapter scale (GLOBAL, 10-150). -1 until first response.
    int scale = -1;

    // #MP / #MP$ — mini-pan enabled.
    ReportedFlag miniPanAEnabled;
    ReportedFlag miniPanBEnabled;

    // Display state (LCD vs EXT variants where applicable)
    int dualPanModeLcd = -1;     // #DPM 0=A, 1=B, 2=Dual
    int dualPanModeExt = -1;     // #HDPM
    int displayModeLcd = -1;     // #DSM 0=spec, 1=spec+waterfall
    int displayModeExt = -1;     // #HDSM
    int displayFps = -1;         // #FPS 12-30; -1 = SpectrumDisplayLimits::FpsUnknown
    int waterfallColor = -1;     // #WFC 0-4
    int waterfallHeight = -1;    // #WFH 0-100%
    int waterfallHeightExt = -1; // #HWFH 0-100%
    int averaging = -1;          // #AVG 1-20
    int peakMode = -1;           // #PKM 0/1 — int to preserve -1 sentinel
    int fixedTune = -1;          // #FXT 0=track, 1=fixed
    int fixedTuneMode = -1;      // #FXA 0-4
    int freeze = -1;             // #FRZ 0/1
    int vfoACursor = -1;         // #VFA 0=OFF, 1=ON, 2=AUTO, 3=HIDE
    int vfoBCursor = -1;         // #VFB
    int autoRefLevel = -1;       // #AR last char: 1=auto, 0=manual
    int ddcNbMode = -1;          // #NB$ 0=off, 1=on, 2=auto
    int ddcNbLevel = -1;         // #NBL$ 0-14

    // #WBS / #HWBS — waterfall colour range, dB (LCD / external monitor).
    int waterfallColorRange = -1;
    int waterfallColorRangeExt = -1;

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
void handleWBS(SpectrumDisplayState &state, RadioState &owner, const QString &cmd);
void handleHWBS(SpectrumDisplayState &state, RadioState &owner, const QString &cmd);
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
// Moves Main's (sub = false) or Sub's REF by `steps` dB within the legal range and applies it
// optimistically. Returns the new level for the caller to send as #REF / #REF$, or nothing when
// there is nothing to send: AutoRef is on (a client #REF freezes the radio's tracking —
// docs/k4-protocol-quirks.md §13), the radio has not reported a REF yet, or the level is at its
// limit.
std::optional<int> stepRefLevel(SpectrumDisplayState &state, RadioState &owner, bool sub, int steps);
void setSpanHzB(SpectrumDisplayState &state, RadioState &owner, int spanHz);
void setMiniPanAEnabled(SpectrumDisplayState &state, RadioState &owner, bool enabled);
void setMiniPanBEnabled(SpectrumDisplayState &state, RadioState &owner, bool enabled);
void setWaterfallHeight(SpectrumDisplayState &state, RadioState &owner, int percent);
void setWaterfallHeightExt(SpectrumDisplayState &state, RadioState &owner, int percent);
void setAveraging(SpectrumDisplayState &state, RadioState &owner, int value);

} // namespace SpectrumDisplayHandlers

#endif // MODELS_RADIOSTATE_SPECTRUMDISPLAYSTATE_H
