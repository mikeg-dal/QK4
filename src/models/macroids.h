#ifndef MACROIDS_H
#define MACROIDS_H

#include <QRegularExpression>
#include <QStringList>

// Function ID constants for macro system
namespace MacroIds {
// Startup macro (executed on radio connect)
const QString Startup = "Startup";

// Programmable Function keys (K4 front panel)
const QString PF1 = "PF1";
const QString PF2 = "PF2";
const QString PF3 = "PF3";
const QString PF4 = "PF4";

// Fn popup functions
const QString FnF1 = "Fn.F1";
const QString FnF2 = "Fn.F2";
const QString FnF3 = "Fn.F3";
const QString FnF4 = "Fn.F4";
const QString FnF5 = "Fn.F5";
const QString FnF6 = "Fn.F6";
const QString FnF7 = "Fn.F7";
const QString FnF8 = "Fn.F8";

// Special buttons
const QString RemAnt = "REM_ANT";

// KPOD buttons (T=Tap, H=Hold)
const QString Kpod1T = "K-pod.1T";
const QString Kpod1H = "K-pod.1H";
const QString Kpod2T = "K-pod.2T";
const QString Kpod2H = "K-pod.2H";
const QString Kpod3T = "K-pod.3T";
const QString Kpod3H = "K-pod.3H";
const QString Kpod4T = "K-pod.4T";
const QString Kpod4H = "K-pod.4H";
const QString Kpod5T = "K-pod.5T";
const QString Kpod5H = "K-pod.5H";
const QString Kpod6T = "K-pod.6T";
const QString Kpod6H = "K-pod.6H";
const QString Kpod7T = "K-pod.7T";
const QString Kpod7H = "K-pod.7H";
const QString Kpod8T = "K-pod.8T";
const QString Kpod8H = "K-pod.8H";

// Keyboard Function Keys (F1-F12)
const QString KbdF1 = "Keyboard-F1";
const QString KbdF2 = "Keyboard-F2";
const QString KbdF3 = "Keyboard-F3";
const QString KbdF4 = "Keyboard-F4";
const QString KbdF5 = "Keyboard-F5";
const QString KbdF6 = "Keyboard-F6";
const QString KbdF7 = "Keyboard-F7";
const QString KbdF8 = "Keyboard-F8";
const QString KbdF9 = "Keyboard-F9";
const QString KbdF10 = "Keyboard-F10";
const QString KbdF11 = "Keyboard-F11";
const QString KbdF12 = "Keyboard-F12";

// CAT command patterns forbidden in Startup macro.
// Each entry is a regex pattern matched against uppercase command text.
// Patterns are anchored to command boundaries (start of string or after ";")
// to avoid false positives (e.g. "#FPS12;" should not match the "PS" pattern).
// Only truly destructive or init-conflicting commands are blocked;
// the user is trusted for everything else.
inline const QStringList ForbiddenStartupPatterns = {
    "(?:^|;)PS\\d*;", // PS0; PS1; etc — power state commands
    "(?:^|;)EE\\d*;", // EE; EE0; — EEPROM write/reset
    "(?:^|;)RDY;",    // Would cause a double state dump
    "(?:^|;)EM\\d;",  // Audio encode mode — conflicts with init sequence
    "(?:^|;)SL\\d;",  // Streaming latency — conflicts with init sequence
    "(?:^|;)K4[01];", // Protocol mode — conflicts with init sequence
    "(?:^|;)AI\\d*;", // Auto-info mode — conflicts with server-managed state
};

// Returns the first forbidden pattern found in command, or empty string if clean
inline QString checkForbiddenStartupCommand(const QString &command) {
    QString upper = command.toUpper();
    for (const QString &pattern : ForbiddenStartupPatterns) {
        QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch match = re.match(upper);
        if (match.hasMatch()) {
            return match.captured(0);
        }
    }
    return {};
}

// Built-in functions (not user-configurable)
const QString ScrnCap = "SCRN_CAP";
const QString Macros = "MACROS";
const QString SwList = "SW_LIST";
const QString Update = "UPDATE";
const QString DxList = "DXLIST";
} // namespace MacroIds

#endif // MACROIDS_H
