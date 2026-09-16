#ifndef NETWORK_CWMACRO_H
#define NETWORK_CWMACRO_H

#include <QMetaType>
#include <QString>
#include <QVector>

// One run of CW text to be keyed at one speed.
//
// Prosigns are left in TCI's |XX| form deliberately. How a prosign is SPELLED is a K4 fact
// (KY maps '(' to KN, '+' to AR and so on), and this file is the TCI side of the wall the design
// draws: the TCI layer never spells a K4 command. CatFrames::cwText does that translation.
struct CwMacroSegment {
    int wpm = 0; // absolute, already resolved from the > and < markers
    QString text;
};
Q_DECLARE_METATYPE(CwMacroSegment)

// The grammar of a TCI CW_MACROS payload (TCI 2.0, "CW macro").
//
// Three things are embedded in what looks like plain text, and ALL THREE MUST BE CONSUMED HERE
// rather than forwarded, because the K4 gives two of the characters a completely different and
// destructive meaning inside a KY command:
//
//   > and <   TCI: raise/lower sending speed by 5 WPM, cumulative.
//             K4 KY: '<' puts the radio into TX TEST MODE until a '>' is received. Forwarding a
//             speed marker would take the transmitter off the air mid-message.
//   |XX|      TCI: run letters together as one prosign.
//             K4 KY: '|' quickly terminates TX in FSK/PSK.
//   ^ ~ *     TCI's escapes for : , and ; - the characters its own framing reserves.
//
// See docs/tci-command-coverage.md section 5.
namespace CwMacro {

// TCI defines the step as 5 WPM per marker.
constexpr int kSpeedStepWpm = 5;

// The K4's documented KS range (K4 manual: "KSnnn, where nnn is the keyer speed, from 8 to 100
// WPM"). Clamped here as well as in CatFrames::keyerSpeed so a segment states the speed that will
// actually be used - a macro of ten '>' markers asks for a speed no radio will key at, and a
// segment claiming 75 WPM while the radio does 50 would be a lie about what happened.
constexpr int kMinWpm = 8;
constexpr int kMaxWpm = 100;

// Splits a CW_MACROS payload into speed-homogeneous segments, resolving > and < against baseWpm.
// Returns empty for text that carries nothing to key.
QVector<CwMacroSegment> parse(const QString &tciText, int baseWpm);

// The escapes alone, without the speed parsing. Exposed for the tests and for any caller that has
// a TCI text field rather than a macro.
QString unescape(const QString &tciText);

} // namespace CwMacro

#endif // NETWORK_CWMACRO_H
