#ifndef PANADAPTER_CONSTANTS_H
#define PANADAPTER_CONSTANTS_H

// Shared rendering constants for PanadapterRhiWidget and MiniPanRhiWidget.
// Colors and theme values live in K4Styles; these are GPU rendering parameters.

namespace PanadapterConstants {

// RTTY shift (Hz) — fixed at 170 Hz for standard amateur RTTY.
// FSK Mark-Tone is user-configurable from K4 front panel (default 915 Hz);
// the shift between Mark and Space is always 170 Hz.
constexpr float RttyShiftHz = 170.0f;
constexpr float RttyHalfShiftHz = RttyShiftHz / 2.0f;

// Fixed spectrum grid cell size in LOGICAL px (scaled by devicePixelRatio at draw time so cells
// stay a constant VISIBLE size — unlike the marker widths below, which are physical px). The grid
// holds this cell size and adds/removes cells as the window or spectrum/waterfall ratio changes,
// so cells never stretch. Values are the measured default first-run cell size (logical panadapter
// width 1095/16 and spectrum height 177/8 at the 1340×840 default window) so the default view is
// unchanged (16×8 cells); larger windows / spectrum just get more cells of the same size.
constexpr float GridCellWidthPx = 68.44f;
constexpr float GridCellHeightPx = 22.14f;

// Line widths (pixels)
constexpr float MarkerLineWidth = 2.0f;
constexpr float RttyDashLineWidth = 1.5f;
constexpr float PassbandEdgeWidth = 2.0f;

// Dash pattern (pixels)
constexpr float DashLengthPx = 6.0f;
constexpr float DashGapPx = 4.0f;
constexpr float DashStridePx = DashLengthPx + DashGapPx;

// Floats a dashed vertical line needs for a span this tall, in DEVICE pixels: six vertices (two
// triangles) x two floats per dash, one dash per stride. The +1 covers the partial dash the loop
// always starts with and absorbs float accumulation drift in `y += stride`.
constexpr int FloatsPerDash = 12;
constexpr int dashFloatsFor(float spanPx) {
    return spanPx <= 0.0f ? 0 : (static_cast<int>(spanPx / DashStridePx) + 1) * FloatsPerDash;
}

// Tallest render surface the dash buffers are sized for, in DEVICE pixels: an 8K panel at DPR 1,
// or 4K at DPR 2. The tallest shipping panels are the 5K iMac (2880) and the Pro Display XDR
// (3384).
//
// WHY a ceiling rather than the live render-target height: both RHI widgets create their resources
// once and never on resize, and m_spectrumRatio moves without a resize at all. The dash builder
// clamps to MaxDashFloats, so exceeding this ceiling truncates the line and warns — it cannot
// overrun the buffer. That clamp is what makes a fixed ceiling safe; do not remove one without the
// other. Each buffer this sizes is ~21 KB, against a 4096x2048 waterfall texture.
constexpr float MaxDisplayHeightPx = 4320.0f;
constexpr int MaxDashFloats = dashFloatsFor(MaxDisplayHeightPx);

// Passband alpha (0–255)
constexpr int PassbandFillAlpha = 64;
constexpr int PassbandEdgeAlpha = 180;

// MiniPan passband alpha — higher than main pan for visibility at small scale
constexpr int MiniPanFillAlpha = 100;

// MiniPan span Hz — Narrow used for CW/FSK/AFSK, Wide for all others
constexpr int SpanNarrowHz = 2000;
constexpr int SpanWideHz = 10000;

} // namespace PanadapterConstants

#endif // PANADAPTER_CONSTANTS_H
