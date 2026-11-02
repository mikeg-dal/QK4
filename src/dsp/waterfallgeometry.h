#ifndef DSP_WATERFALLGEOMETRY_H
#define DSP_WATERFALLGEOMETRY_H

#include <QString>
#include <QtGlobal>

#include <array>

// Which stored waterfall row each screen pixel shows. Pure arithmetic with no Qt widget or RHI
// dependency so it can be tested directly — the shaders cannot be unit tested at all, and the bug
// that prompted this was arithmetic: the visible-row fraction carried a -1 that, once V snapping
// landed, would have dropped the newest complete row off the top of the display.
//
// The waterfall texture is a ring of `storedRows` rows. `writeRow` is the row about to be written,
// so it holds stale data and must never be sampled; the newest complete row is `writeRow - 1`. The
// display shows the newest `visibleRows` of them, oldest at the bottom.
namespace WaterfallGeometry {

// Device pixels each row is drawn with: one row per PHYSICAL pixel of the panel.
//
// Normally a device pixel is a physical one and this is 1. When the OS draws a framebuffer larger
// than the panel and scales it down (macOS "HiDPI" on a 4K panel: 7680x4320 halved to 3840x2160,
// still devicePixelRatio 2), a 1-device-pixel row is half a physical pixel, and every one-row
// scroll step moves the image by half a physical pixel. The display's downscale then pairs the rows
// differently on alternate steps: a one-row feature is crisp in one physical pixel, then smeared at
// half strength across two, then crisp again — the whole noise field twinkles at half the frame
// rate while it scrolls. With rows as tall as the downscale factor, each step is exactly one
// physical pixel and the scroll is rigid again.
//
// Only a whole factor can be honoured (2.0 within 1%); a fractional macOS "scaled" mode has no row
// height that lands on the panel's grid, so it keeps 1. Below 1.5 is 1.
int devicePxPerRowFor(qreal framebufferPixelsPerPanelPixel);

// Rows added to V (waterfall.vert's rowShift uniform) so that each devicePxPerRow-tall row covers
// exactly the device pixels of one physical pixel, not half of one and half of the next.
//
// Rows are counted up from the waterfall's bottom edge, but the panel's pixels are counted down
// from the top of the framebuffer: physical pixel n is framebuffer rows n*k .. n*k+k-1. Whether the
// two grids agree depends on where the widget sits. At DPR 3 (macOS 2x times a 1.5 interface
// scale) a widget whose bottom edge is at logical y 377 ends at framebuffer row 1131 — odd — so
// unshifted 2-pixel rows straddle physical pixels and the twinkle is back.
//
// viewportBottomFramebufferRow is the framebuffer row just below the waterfall (its bottom edge,
// counted from the top of the window). Returns -(that row mod k) / k: 0 when the grids agree, else a
// shift that starts the bottom row early, leaving a partial row of the next-older history under it.
// That extra row means one more stored row is on screen; visibleRowsFor's clamp does not know, so
// the caller must keep visibleRows <= storedRows - 2 whenever this is non-zero.
float rowShiftFor(qint64 viewportBottomFramebufferRow, int devicePxPerRow);

// Rows to draw for a waterfall this tall, one row per devicePxPerRow device pixels, clamped to what
// is stored less the row reserved for the write head. pxPerRow above devicePxPerRow means a clamp
// bit and the blur is back.
int visibleRowsFor(float waterfallHeightPx, int storedRows, int minRows, int devicePxPerRow = 1);

// ---- Speed: how many seconds of history the waterfall shows -----------------------------------
//
// SPEED is a QK4 display setting (the K4 has no such command; Flex calls it the waterfall "rate").
// It never changes how often the radio sends spectrum frames (#FPS): it changes how much screen each
// frame takes. 0 is Fill, today's behaviour: one stored row per pixel row, so the history on screen
// follows the window's height. A number is that many seconds on screen at any window size:
//   - slower than Fill: several frames fold into one row (each bin's peak, so a short burst stays
//     visible), and the window keeps one row per pixel row;
//   - faster than Fill: each frame is its own row, fewer rows are shown, and each is drawn a whole
//     number of pixel rows tall. WHY whole: 450 rows over 607 px drew rows of 1 and 2 px in a pattern
//     fixed on screen, so a trace changed thickness and the scroll step changed size as it moved down -
//     the uneven step one row per pixel row exists to avoid. Where the closest whole number is 1, the
//     plan is Fill: a few seconds more history beats uneven rows.
inline constexpr std::array<int, 6> SpeedStepsSeconds{0, 15, 30, 60, 120, 240};
inline constexpr int SpeedFill = 0;
inline constexpr int MaxFramesPerRow = 64;

// The step `delta` places from `seconds` (clamped to the ends); an unknown value counts as Fill.
int stepSpeed(int seconds, int delta);

// True for a value SpeedStepsSeconds holds.
bool isSpeedStep(int seconds);

// "Fill", "15s" ...
QString speedLabel(int seconds);

struct SpeedPlan {
    int visibleRows = 0;     // rows drawn over the waterfall's height
    int framesPerRow = 1;    // spectrum frames folded into each stored row
    int pixelRowsPerRow = 1; // pixel rows each drawn row covers (faster than Fill: a whole number > 1)
};

// pixelRows is visibleRowsFor's answer for this height (one row per pixel row); lineRateHz the
// measured rate frames become rows (0 before it is known, which plans Fill).
SpeedPlan speedPlanFor(int pixelRows, double lineRateHz, int seconds);

// Folds one frame's row into the row being built: each byte the larger of the two, so the brightest
// moment of every bin over the frames survives (bytes are palette positions, rising with dBm).
void foldPeak(quint8 *row, const quint8 *frame, int count);

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
// pixel 0 is the bottom of the waterfall (oldest). viewportHeightPx is the waterfall viewport's
// height in device pixels, which is fractional in general (924 px x 0.6 = 554.4); the pixels drawn
// are those whose centre lies inside it.
int rowForPixel(int pixel, float viewportHeightPx, int writeRow, int visibleRows, int storedRows,
                float rowShift = 0.0f);

// ---- Colour: which dBm lands where in the waterfall palette ----------------------------------
//
// Each waterfall row stores its bins' palette positions under the colour window in effect when the
// row was captured (capturedByteFor), and waterfall.frag passes them through. So a change of REF or
// of the radio's colour range recolours only new rows, as on the K4.
//
// THE COLOUR WINDOW, fitted to Mike's K4D (photos 2026-10-30, WWV, AutoRef, #WBS 14, SCALE 25/50/80;
// correct it here, nowhere else):
//   span   = SCALE                         (the palette covers the spectrum's dB range)
//   bottom = REF - SCALE x #WBS / ColorRangeDivisor
// The K4's waterfall is brighter at a smaller SCALE (25: blue with green speckle; 50: royal blue;
// 80: blue, WWV sidebands green, carrier red), so its window scales with SCALE, and a higher #WBS
// lowers the bottom, brightening everything (manual rev D6, WTR CLRS: "a higher number will use
// brighter colors"). Noise a few dB above REF lands at about 0.47 / 0.41 / 0.39 of QK4's palette at
// SCALE 25 / 50 / 80, matching the photos. Unverified: the #WBS term (all photos were at 14) and
// DefaultColorRangeDb (Mike's radio's value; the factory default is unknown).
constexpr float ColorRangeDivisor = 40.0f;
constexpr int DefaultColorRangeDb = 14;

struct ColorWindow {
    float lowDbm = 0.0f; // dBm at palette position 0
    float spanDb = 1.0f; // dB from position 0 to position 1
};

// The window for a panadapter at refDbm with the radio's colour range (#WBS). A colour range outside
// 5..30 (the -1 sentinel before the radio reports one) uses DefaultColorRangeDb.
ColorWindow colorWindowFor(float refDbm, int scaleDb, int colorRangeDb);

// Palette position 0..1 for a level — what waterfall.frag computes. Mirror only; must be kept in sync
// with the shader by hand, like rowForPixel.
float paletteFractionFor(float dbm, const ColorWindow &window);

// The byte reserved for "no data" (rows not yet written, a cleared history), which the shader draws
// at palette position 0.
constexpr quint8 NoDataByte = 0;

// waterfall.frag's colorLow / colorGain. The shader reads the byte as a normalised 0..1 texel value
// v and computes clamp((v - colorLow) * colorGain, 0, 1). Both widgets send the defaults, which
// pass v through unchanged: rows already hold palette positions.
struct ColorUniforms {
    float colorLow = 0.0f;
    float colorGain = 1.0f;
};

// The byte a waterfall row stores for a level: its palette position under the window in effect when
// the row is captured, scaled to 1..255 (0 stays "no data"). The panadapter draws these with
// pass-through uniforms, so a row keeps the colour it was drawn with: an AutoRef REF step or a #WBS
// change recolours only new rows, as on the K4. Recolouring the whole history live made every
// ~1 dB AutoRef step flash the entire waterfall (bench 2026-10-30).
quint8 capturedByteFor(float dbm, const ColorWindow &window);

// What the shader returns for a stored byte under the given uniforms — the GPU path mirrored end to
// end, so a test can check that capturedByteFor + the shader agree with paletteFractionFor.
float shaderFractionFor(quint8 storedByte, const ColorUniforms &uniforms);

} // namespace WaterfallGeometry

#endif // DSP_WATERFALLGEOMETRY_H
