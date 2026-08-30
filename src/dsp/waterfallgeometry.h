#ifndef DSP_WATERFALLGEOMETRY_H
#define DSP_WATERFALLGEOMETRY_H

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

} // namespace WaterfallGeometry

#endif // DSP_WATERFALLGEOMETRY_H
