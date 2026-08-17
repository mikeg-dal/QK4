#ifndef DSP_SPECTRUMSCALE_H
#define DSP_SPECTRUMSCALE_H

#include <QString>
#include <QVector>

// Amplitude axis for the panadapter: where a given dBm sits on screen, and which values earn a
// label. Pure arithmetic with no Qt widget dependency so it can be tested directly — the bug that
// prompted it (S1 printed at two different heights) was arithmetic, and only reachable through the
// UI before this was split out.
//
// The K4 reports REF in dBm and SCALE in dB, so the visible window is [REF, REF + SCALE] and every
// value here is absolute dBm. S-units follow the amateur convention: S9 = -73 dBm, 6 dB per unit
// below it, 10 dB steps above.
namespace SpectrumScale {

// S9 and the width of one S-unit, in dBm/dB.
constexpr float S9_DBM = -73.0f;
constexpr float DB_PER_S_UNIT = 6.0f;

// dBm of S1..S9. Values outside 1..9 are clamped to that range; callers wanting "is this on the
// scale at all" should test the dBm against the display window instead.
float sUnitDbm(int sUnit);

// Fraction of the chart height for a dBm value: 0 at minDb (bottom), 1 at maxDb (top).
//
// Deliberately unclamped at the top so a bin stronger than maxDb returns > 1 and the trace climbs
// to the edge instead of flat-topping. The renderer and the scale labels must both go through this
// function — the axis was mislabelled precisely because two places computed height independently.
float normalizedForDb(float dbm, float minDb, float maxDb);

struct Label {
    float dbm;    // true position on the axis
    QString text; // "S9", "S9+20", "-110 dBm"
};

// Labels for the visible window, in descending dBm (top of the chart first).
//
// A value is emitted only when its true dBm falls inside [minDb, maxDb] — never clamped into
// range, which is what printed S1 twice. Spacing is decimated so that neighbouring labels stay at
// least labelHeightPx apart: availablePx is the chart height the labels are drawn across, and
// labelHeightPx the height of one line of text.
QVector<Label> labelsFor(float minDb, float maxDb, bool useSUnits, int availablePx, int labelHeightPx);

} // namespace SpectrumScale

#endif // DSP_SPECTRUMSCALE_H
