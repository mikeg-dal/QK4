#ifndef DSP_WATERFALLGEOMETRY_H
#define DSP_WATERFALLGEOMETRY_H

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

// Rows to draw for a waterfall this tall: one row per PHYSICAL pixel of the panel, clamped to what
// is stored less the row reserved for the write head. A result below the height over the ratio means
// the clamp bit and the rows are stretched.
//
// framebufferPixelsPerPanelPixel is PanelScale's real-valued ratio, not a whole number. WHY it need
// not be whole any more: the shader reconstructs rows with a cubic kernel at a continuous phase, so
// a row 1.566 framebuffer pixels tall is drawn as 1.566. Before that, only a whole factor could be
// honoured and macOS's fractional "scaled" modes fell through to one row per FRAMEBUFFER pixel - on
// a 4K panel driven at 3008x1692 that is 0.64 of a panel pixel per row, so the display pipeline
// merged about three rows into every two panel pixels and the detail went with it (Mike's test
// configuration, 2026-11-26).
int visibleRowsFor(float waterfallHeightPx, int storedRows, int minRows, qreal framebufferPixelsPerPanelPixel = 1.0);

// ---- Speed: how a stored row maps onto the screen ---------------------------------------------
//
// SPEED is a QK4 display setting (the K4 has no such command). It never changes how often the radio
// sends spectrum frames (#FPS): it changes how much screen each frame takes.
//
// The operator sees a plain number, 1 (slowest, most history) to 10 (fastest). WHY a bare ordinal
// and not seconds: the seconds a level comes to depend on the window height, the #WFH split and the
// radio's measured frame rate, so a seconds-valued key moved on its own as any of the three drifted,
// and a level sitting near a rounding boundary flickered between two numbers with nothing touched
// (SPEED 1/3 over 693 rows at 14.9 Hz computes 15.50 s exactly). Those are QK4's internals; the
// operator picks a speed and watches the screen. Seconds remain in the qk4.spectrum.diag line, for
// whoever is debugging it.
//
// The number is a fixed index into this table, not a position in whatever happens to fit. A level
// the current window is too short for is SKIPPED when stepping, never renumbered, so level 7 is the
// same speed whatever size the window is.
enum class RowUnit {
    // One row per PHYSICAL pixel of the panel: the sharpest picture and the longest history the
    // display can hold. A 4K panel is meant to get more of both than a 1080p one.
    PanelPixel,
    // A vertical magnification, counted in LOGICAL pixels so the picture travels at the same visible
    // speed on every display. Counting it in framebuffer pixels made the same level scroll at half
    // the speed and hold twice the history on a 2x panel (bench, 2026-11-26).
    LogicalPixel,
};

struct SpeedLevel {
    int framesPerRow = 1; // above 1, that many frames peak-folded into each row
    RowUnit unit = RowUnit::PanelPixel;
    float rowHeight = 1.0f; // in `unit`s
};

// Slowest first. WHY it stops at a 3x fold and not 4x: at a 647-row waterfall and 16 Hz, 3x is
// already two minutes - long enough to watch eight FT8 cycles or a band opening, which is a real
// propagation view. 4x was 2.7 minutes, which is nobody's workflow (Mike, 2026-11-26).
//
// WHY the fast side starts at one logical pixel rather than two: Fill is one PANEL pixel, so on a 2x
// display a two-logical-pixel level is four times Fill, and everything between 41 s and 10 s was
// unreachable - the range an operator actually watches. One logical pixel halves that first jump,
// and on a 1x display it IS Fill, so it withdraws itself there.
inline constexpr std::array<SpeedLevel, 10> SpeedLevels{{
    {3, RowUnit::PanelPixel, 1.0f},   // 1  slowest
    {2, RowUnit::PanelPixel, 1.0f},   // 2
    {1, RowUnit::PanelPixel, 1.0f},   // 3  Fill
    {1, RowUnit::LogicalPixel, 1.0f}, // 4
    {1, RowUnit::LogicalPixel, 1.5f}, // 5
    {1, RowUnit::LogicalPixel, 2.0f}, // 6
    {1, RowUnit::LogicalPixel, 3.0f}, // 7
    {1, RowUnit::LogicalPixel, 4.0f}, // 8
    {1, RowUnit::LogicalPixel, 6.0f}, // 9
    {1, RowUnit::LogicalPixel, 8.0f}, // 10 fastest
}};
inline constexpr int SpeedLevelMin = 1;
inline constexpr int SpeedLevelMax = static_cast<int>(SpeedLevels.size());
inline constexpr int SpeedLevelFill = 3;
inline constexpr int MaxFramesPerRow = 64;

// `level` held to the table, with anything unrecognised landing on Fill.
int clampSpeedLevel(int level);

// Physical pixel rows one drawn row covers at `level` on a display with this device pixel ratio and
// framebuffer-to-panel ratio.
//
// The answer need not be a whole number (dpr 1.5 at three logical pixels is 4.5). That is safe
// because the shader reconstructs rows with a cubic kernel at a continuous phase instead of snapping
// each row to a texel centre. The snap is what turned a fractional height into rows of 12 and 13
// pixels in a pattern that slid through the picture as it scrolled.
float physicalRowHeightFor(int level, qreal dpr, qreal framebufferPixelsPerPanelPixel);

// Seconds of history `level` puts on screen over `pixelRows` at `lineRateHz`. 0 when the rate is not
// known yet, which is the first second or so of a connection. DIAGNOSTIC ONLY - this number is not
// shown to the operator; see the note at the top of this section.
double secondsOnScreen(int pixelRows, int level, double lineRateHz, qreal dpr, qreal framebufferPerPanel);

struct SpeedPlan {
    int visibleRows = 0;          // rows drawn over the waterfall's height
    int framesPerRow = 1;         // spectrum frames folded into each stored row
    float pixelRowsPerRow = 1.0f; // physical pixel rows each drawn row covers; fractional is fine
};

// pixelRows is visibleRowsFor's answer for this height (one row per panel pixel). Exact in the sense
// that matters: nothing here reads a measured frame rate, so the plan cannot hop between two answers
// as the rate drifts.
SpeedPlan speedPlanFor(int pixelRows, int level, qreal dpr, qreal framebufferPerPanel);

// Whether `level` is worth offering for a waterfall this tall on this display. A level is withdrawn
// when it would leave fewer than `minRows` rows - a handful of thick colour bands is not a waterfall
// - or when its rows would come out no taller than Fill's, where it would draw Fill's picture under
// another number. Fill and the folds always fit: they keep every row the height can hold.
bool speedLevelFits(int level, int pixelRows, int minRows, qreal dpr, qreal framebufferPerPanel);

// The level `delta` places from `level`, walking past the ones this window is too short for and
// stopping where it runs out rather than landing on one that does not fit.
int stepSpeedLevel(int level, int delta, int pixelRows, int minRows, qreal dpr, qreal framebufferPerPanel);

// The sub-row scroll residual, in texture-V units, to ADD to V (so <= 0, like rowShiftFor).
//
// WHY: a row arrives every 1/lineRate seconds - about 66 ms at 15 Hz - and the ring's write row
// jumps a whole row the instant it does. At Fill that jump is one physical pixel and nobody can see
// it; at SPEED 1/12 it is twelve, so the picture stands still for four display refreshes and then
// lurches twelve pixels, fifteen times a second. That is the stutter.
//
// So the ring advances immediately and the shader is handed a residual that starts at the whole
// distance just jumped and closes to zero over the measured row interval. The travel is spread
// across every refresh in between, and the picture moves instead of lurching. Returns 0 before a
// rate has been measured and during a pause, where animating against a guess would be worse than
// not animating at all.
//
// Capped at MaxScrollLagRows because that is all the shader reserves below the oldest visible row.
// A larger residual drags V past what the tap clamp allows, which pins every tap to one row and
// draws the whole waterfall as vertical streaks of a single spectrum - seen on the bench on
// 2026-11-26 when the caller accumulated a row per frame instead of replacing it.
inline constexpr int MaxScrollLagRows = 2;
float scrollPhaseFor(int rowsAdvanced, int storedRows, double elapsedMs, double msPerRow);

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
