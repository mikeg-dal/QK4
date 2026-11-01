#ifndef MODELS_RADIOSTATE_TEXTDECODESTATE_H
#define MODELS_RADIOSTATE_TEXTDECODESTATE_H

#include <QString>

class RadioState;

// Text-decode state (TD / TD$) and text-buffer pass-through (TB / TB$).
//
// WHY — Pattern C (plain-struct + façade re-read):
// Each subsystem is a plain struct, not a QObject. RadioState remains the
// only QObject so we preserve single-thread affinity, avoid a moc-files
// explosion, and avoid destructor signal-ordering traps. Handler fragments
// live as free functions in the subsystem's .cpp and take a reference to
// both the owning state struct AND RadioState — the latter lets them emit
// signals through the façade. See PATTERNS.md → Subsystem State.
struct TextDecodeState {
    // WHY: TD<mode><threshold><lines>; carries each field as one digit, so the K4 cannot express
    // 10 lines. Parser, setters and the window all bound by these.
    static constexpr int ModeMin = 0;
    static constexpr int ModeMax = 4;
    static constexpr int ThresholdMin = 0;
    static constexpr int ThresholdMax = 9;
    static constexpr int LinesMin = 1;
    static constexpr int LinesMax = 9;

    // Text Decode (TD) — Main RX
    int textDecodeMode = -1;      // 0=off, 1=DATA/SSB on, 2=8-45WPM, 3=8-60WPM, 4=8-90WPM
    int textDecodeThreshold = -1; // 0=AUTO, 1-9 (CW only)
    int textDecodeLines = -1;     // 1-9 lines

    // Text Decode (TD$) — Sub RX
    int textDecodeModeB = -1;
    int textDecodeThresholdB = -1;
    int textDecodeLinesB = -1;

    // Reset all fields to their sentinel "never received" values so the
    // first post-reconnect echo fires a change signal.
    void reset();
};

namespace TextDecodeHandlers {

// CAT handlers. Each takes the state struct (mutated) and the owning
// RadioState (for emit).
void handleTD(TextDecodeState &state, RadioState &owner, const QString &cmd);
void handleTDSub(TextDecodeState &state, RadioState &owner, const QString &cmd);
void handleTB(RadioState &owner, const QString &cmd);
void handleTBSub(RadioState &owner, const QString &cmd);

// Optimistic setters used by MainWindow when it issues TD SET to the K4
// (the K4 does not echo DT / TD SET commands, so RadioState mutates the
// field directly and emits).
void setMode(TextDecodeState &state, RadioState &owner, int mode);
void setThreshold(TextDecodeState &state, RadioState &owner, int threshold);
void setLines(TextDecodeState &state, RadioState &owner, int lines);
void setModeB(TextDecodeState &state, RadioState &owner, int mode);
void setThresholdB(TextDecodeState &state, RadioState &owner, int threshold);
void setLinesB(TextDecodeState &state, RadioState &owner, int lines);

} // namespace TextDecodeHandlers

#endif // MODELS_RADIOSTATE_TEXTDECODESTATE_H
