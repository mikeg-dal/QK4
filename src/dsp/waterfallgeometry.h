#ifndef DSP_WATERFALLGEOMETRY_H
#define DSP_WATERFALLGEOMETRY_H

#include <QtGlobal>

// Which stored waterfall row each screen pixel shows. Pure arithmetic with no Qt widget or RHI
// dependency so it can be tested directly — the shaders cannot be unit tested at all, and the bug
// that prompted this was arithmetic: the visible-row fraction carried a -1 that, once V snapping
// landed, would have dropped the newest complete row off the top of the display.
//
// The waterfall texture is a ring of `storedRows` rows. `writeRow` is the row about to be written,
// so it holds stale data and must never be sampled; the newest complete row is `writeRow - 1`. The
// display shows the newest `visibleRows` of them, oldest at the bottom.
namespace WaterfallGeometry {

// Rows to draw for a waterfall this tall, one row per device pixel, clamped to what is stored less
// the row reserved for the write head. pxPerRow above 1 means a clamp bit and the blur is back.
int visibleRowsFor(float waterfallHeightPx, int storedRows, int minRows);

// Oldest row on display, wrapped into [0, storedRows). Excluding the in-flight row is this
// function's job alone: taking writeRow - visibleRows puts the newest drawn row at writeRow - 1.
int oldestVisibleRow(int writeRow, int visibleRows, int storedRows);

// scrollOffset and visibleFraction uniforms for waterfall.vert, which computes
// V = t * visibleFraction + scrollOffset with t spanning 0..1 across the quad.
//
// visibleFraction is deliberately visibleRows/storedRows and NOT (visibleRows - 1)/storedRows.
// The -1 form maps the top pixel to writeRow - 2 once V is snapped, hiding the newest complete row
// and skipping one row entirely. The in-flight guard belongs in oldestVisibleRow, not here.
float scrollOffsetFor(int oldestVisibleRow, int storedRows);
float visibleFractionFor(int visibleRows, int storedRows);

// The row a given pixel ends up sampling — a mirror of waterfall.vert's V computation followed by
// waterfall.frag's snap, which is what makes the mapping testable without a GPU.
//
// MUST be kept in sync with src/dsp/shaders/waterfall.{vert,frag} by hand. Same coupling as
// RhiUtils::WaterfallUniforms, and it exists for the same reason: the alternative is no coverage.
// pixel 0 is the bottom of the waterfall (oldest), pixelCount - 1 the top (newest).
int rowForPixel(int pixel, int pixelCount, int writeRow, int visibleRows, int storedRows);

// ---- Colour: which dBm lands where in the waterfall palette ----------------------------------
//
// The waterfall texture holds each bin's calibrated level, not a position in the palette: one byte
// per bin, byte = dBm + SpectrumScale::K4_DBM_OFFSET, the same encoding the K4 sends. waterfall.frag
// places that level in the palette with two uniforms. So the colour window is live: a change of REF
// or of the radio's colour range recolours the whole history at once, and no row is quantised
// against whatever REF happened to be current when it was captured.
//
// THE HYPOTHESIS (not measured — correct it here, nowhere else, once K4 screenshots exist):
//   bottom of the palette = REF - colourRange   (colourRange = #WBS, 5..30 dB)
//   top of the palette    = bottom + PaletteSpanDb
// The K4 manual (rev D6, DISPLAY → WTR CLRS) says only that a higher number "will use brighter
// colors for a given amount of waterfall signal intensity". Lowering the bottom edge by #WBS does
// exactly that, and it ties the palette to REF, which AutoRef parks on the average noise. With the
// K4's 14 dB and a 40 dB span, noise a few dB above REF lands at about 0.4 of QK4's palette — mid
// blue, with its downward excursions dark and its peaks light blue — which is what the K4 photos
// of WWV (2026-10-30) show; a carrier 20 dB up reaches orange. The previous mapping (palette =
// REF..REF+SCALE, 80 dB) put the same noise at about 0.05, which the palette draws black.
//
// GUESSES: PaletteSpanDb (fitted by eye to two phone photos) and DefaultColorRangeDb (the value on
// Mike's K4D; the K4's factory default is unknown). That #WBS moves the bottom edge rather than
// scaling the span is itself the hypothesis.
constexpr float PaletteSpanDb = 40.0f;
constexpr int DefaultColorRangeDb = 14;

struct ColorWindow {
    float lowDbm = 0.0f; // dBm at palette position 0
    float spanDb = 1.0f; // dB from position 0 to position 1
};

// The window for a panadapter at refDbm with the radio's colour range (#WBS). A colour range outside
// 5..30 (the -1 sentinel before the radio reports one) uses DefaultColorRangeDb.
ColorWindow colorWindowFor(float refDbm, int colorRangeDb);

// Palette position 0..1 for a level — what waterfall.frag computes. Mirror only; must be kept in sync
// with the shader by hand, like rowForPixel.
float paletteFractionFor(float dbm, const ColorWindow &window);

// The stored byte for a level: dBm + K4_DBM_OFFSET, rounded and clamped to 1..255. 0 is reserved
// for "no data" (rows not yet written, a cleared history), which the shader draws at palette
// position 0 whatever the window.
constexpr quint8 NoDataByte = 0;
quint8 storedByteFor(float dbm);

// waterfall.frag's colorLow / colorGain for a window. The shader reads the byte as a normalised
// 0..1 texel value v and computes clamp((v - colorLow) * colorGain, 0, 1). colorLow 0 and gain 1
// pass v through unchanged, which is what the mini-pan sends: it still stores palette positions.
struct ColorUniforms {
    float colorLow = 0.0f;
    float colorGain = 1.0f;
};
ColorUniforms colorUniformsFor(const ColorWindow &window);

// What the shader returns for a stored byte under the given uniforms — the GPU path mirrored end to
// end, so a test can check that storedByteFor + colorUniformsFor agree with paletteFractionFor.
float shaderFractionFor(quint8 storedByte, const ColorUniforms &uniforms);

} // namespace WaterfallGeometry

#endif // DSP_WATERFALLGEOMETRY_H
