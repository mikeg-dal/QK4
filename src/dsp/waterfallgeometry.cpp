#include "waterfallgeometry.h"

#include "models/radiostate/spectrumdisplaystate.h"

#include <QtGlobal>
#include <algorithm>
#include <cstddef>
#include <cmath>

namespace WaterfallGeometry {

int devicePxPerRowFor(qreal framebufferPixelsPerPanelPixel) {
    const long whole = std::lround(framebufferPixelsPerPanelPixel);
    if (whole < 2 || std::abs(framebufferPixelsPerPanelPixel - static_cast<qreal>(whole)) > 0.01 * whole)
        return 1;
    return static_cast<int>(whole);
}

float rowShiftFor(qint64 viewportBottomFramebufferRow, int devicePxPerRow) {
    if (devicePxPerRow <= 1)
        return 0.0f;
    qint64 phase = viewportBottomFramebufferRow % devicePxPerRow;
    if (phase < 0)
        phase += devicePxPerRow;
    return -static_cast<float>(phase) / static_cast<float>(devicePxPerRow);
}

int visibleRowsFor(float waterfallHeightPx, int storedRows, int minRows, qreal framebufferPixelsPerPanelPixel) {
    if (storedRows <= 0)
        return 0;
    // One row is always reserved for the write head: showing all storedRows would wrap the window
    // onto the whole ring and put the in-flight row back on screen.
    const int maxRows = storedRows - 1;
    const double perPanel =
        framebufferPixelsPerPanelPixel > 0.0 ? static_cast<double>(framebufferPixelsPerPanelPixel) : 1.0;
    const int wanted = static_cast<int>(std::lround(static_cast<double>(waterfallHeightPx) / perPanel));
    return qBound(qMin(minRows, maxRows), wanted, maxRows);
}

int clampSpeedLevel(int level) {
    if (level < SpeedLevelMin || level > SpeedLevelMax)
        return SpeedLevelFill; // anything saved by an older build lands somewhere sane
    return level;
}

namespace {
const SpeedLevel &levelAt(int level) {
    return SpeedLevels[static_cast<std::size_t>(clampSpeedLevel(level) - 1)];
}
// Below this a level draws Fill's picture under another number, so it is not offered.
constexpr float kAboveFill = 1.001f;
} // namespace

float physicalRowHeightFor(int level, qreal dpr, qreal framebufferPixelsPerPanelPixel) {
    const SpeedLevel &entry = levelAt(level);
    if (entry.unit == RowUnit::PanelPixel)
        return entry.rowHeight; // Fill and the folds: one row per physical pixel of the panel
    const double logicalToFramebuffer = dpr > 0.0 ? static_cast<double>(dpr) : 1.0;
    const double framebufferPerPhysical =
        framebufferPixelsPerPanelPixel > 0.0 ? static_cast<double>(framebufferPixelsPerPanelPixel) : 1.0;
    const double physical = static_cast<double>(entry.rowHeight) * logicalToFramebuffer / framebufferPerPhysical;
    // A logical pixel can be smaller than a panel pixel, and a row never is: at that point the level
    // simply IS Fill, which is correct rather than a fault. speedLevelFits withdraws it.
    return static_cast<float>((std::max)(1.0, physical));
}

double secondsOnScreen(int pixelRows, int level, double lineRateHz, qreal dpr, qreal framebufferPerPanel) {
    if (pixelRows <= 0 || lineRateHz <= 0.0)
        return 0.0;
    const SpeedPlan plan = speedPlanFor(pixelRows, level, dpr, framebufferPerPanel);
    return static_cast<double>(plan.visibleRows) * plan.framesPerRow / lineRateHz;
}

SpeedPlan speedPlanFor(int pixelRows, int level, qreal dpr, qreal framebufferPerPanel) {
    if (pixelRows <= 0)
        return SpeedPlan{pixelRows, 1, 1.0f};
    const SpeedLevel &entry = levelAt(level);
    if (entry.framesPerRow > 1) {
        // Slower than Fill: still one row per panel pixel, but each row is several frames peak
        // folded together, so the same height holds more history.
        return SpeedPlan{pixelRows, (std::min)(entry.framesPerRow, MaxFramesPerRow), 1.0f};
    }
    const float height = physicalRowHeightFor(level, dpr, framebufferPerPanel);
    if (height <= kAboveFill)
        return SpeedPlan{pixelRows, 1, 1.0f};
    // Faster than Fill: each frame is its own row, drawn `height` physical pixel rows tall. The
    // height is not rounded to a whole number - the shader's cubic reconstruction takes a continuous
    // phase, so it does not need one.
    // WHY at least 2 rows: one row would be the whole waterfall in a single colour band.
    const int rows = (std::max)(2, static_cast<int>(std::lround(static_cast<double>(pixelRows) / height)));
    return SpeedPlan{(std::min)(rows, pixelRows), 1, height};
}

bool speedLevelFits(int level, int pixelRows, int minRows, qreal dpr, qreal framebufferPerPanel) {
    if (level < SpeedLevelMin || level > SpeedLevelMax)
        return false;
    const SpeedLevel &entry = SpeedLevels[static_cast<std::size_t>(level - 1)];
    if (entry.unit == RowUnit::PanelPixel)
        return true; // Fill and the folds keep every row the height can hold
    if (physicalRowHeightFor(level, dpr, framebufferPerPanel) <= kAboveFill)
        return false; // it would draw Fill's picture under another number
    return speedPlanFor(pixelRows, level, dpr, framebufferPerPanel).visibleRows >= minRows;
}

int stepSpeedLevel(int level, int delta, int pixelRows, int minRows, qreal dpr, qreal framebufferPerPanel) {
    int current = clampSpeedLevel(level);
    if (delta == 0)
        return current;
    const int direction = delta > 0 ? 1 : -1;
    for (int remaining = std::abs(delta); remaining > 0; --remaining) {
        int candidate = current;
        for (;;) {
            const int next = candidate + direction;
            if (next < SpeedLevelMin || next > SpeedLevelMax)
                return current; // ran off the ladder: stay on the last level that fits
            candidate = next;
            if (speedLevelFits(candidate, pixelRows, minRows, dpr, framebufferPerPanel))
                break;
        }
        current = candidate;
    }
    return current;
}

float scrollPhaseFor(int rowsAdvanced, int storedRows, double elapsedMs, double msPerRow) {
    if (storedRows <= 0 || rowsAdvanced <= 0 || msPerRow <= 0.0)
        return 0.0f;
    const double rows = (std::min)(rowsAdvanced, MaxScrollLagRows);
    const double travelled = std::clamp(elapsedMs / msPerRow, 0.0, rows);
    const double remaining = rows - travelled;
    // Negative, like rowShiftFor: the ring has already advanced, so V has to be pulled back toward
    // the older rows and released to zero as the interval runs out.
    return static_cast<float>(-remaining / static_cast<double>(storedRows));
}

void foldPeak(quint8 *row, const quint8 *frame, int count) {
    for (int i = 0; i < count; ++i)
        row[i] = (std::max)(row[i], frame[i]);
}

int oldestVisibleRow(int writeRow, int visibleRows, int storedRows) {
    if (storedRows <= 0)
        return 0;
    int oldest = (writeRow - visibleRows) % storedRows;
    if (oldest < 0)
        oldest += storedRows;
    return oldest;
}

float scrollOffsetFor(int oldestVisibleRow, int storedRows) {
    if (storedRows <= 0)
        return 0.0f;
    return static_cast<float>(oldestVisibleRow) / static_cast<float>(storedRows);
}

float visibleFractionFor(int visibleRows, int storedRows) {
    if (storedRows <= 0)
        return 0.0f;
    return static_cast<float>(visibleRows) / static_cast<float>(storedRows);
}

int rowForPixel(int pixel, float viewportHeightPx, int writeRow, int visibleRows, int storedRows, float rowShift) {
    if (storedRows <= 0 || viewportHeightPx <= 0.0f)
        return 0;

    // waterfall.vert: V = t * visibleFraction + scrollOffset + rowShift / storedRows, t at the
    // pixel's centre.
    const float t = (static_cast<float>(pixel) + 0.5f) / viewportHeightPx;
    const float v = t * visibleFractionFor(visibleRows, storedRows) +
                    scrollOffsetFor(oldestVisibleRow(writeRow, visibleRows, storedRows), storedRows) +
                    rowShift / static_cast<float>(storedRows);

    // waterfall.frag: snap to a texel centre, then Repeat addressing wraps the ring.
    int row = static_cast<int>(std::floor(v * static_cast<float>(storedRows))) % storedRows;
    if (row < 0)
        row += storedRows;
    return row;
}

ColorWindow colorWindowFor(float refDbm, int scaleDb, int colorRangeDb) {
    const bool known = colorRangeDb >= SpectrumDisplayLimits::WaterfallColorRangeMin &&
                       colorRangeDb <= SpectrumDisplayLimits::WaterfallColorRangeMax;
    const float range = static_cast<float>(known ? colorRangeDb : DefaultColorRangeDb);
    const bool scaleKnown = scaleDb >= SpectrumDisplayLimits::ScaleMin && scaleDb <= SpectrumDisplayLimits::ScaleMax;
    const float scale = static_cast<float>(scaleKnown ? scaleDb : SpectrumDisplayLimits::ScaleDefault);
    return ColorWindow{refDbm - scale * range / ColorRangeDivisor, scale};
}

float paletteFractionFor(float dbm, const ColorWindow &window) {
    if (window.spanDb <= 0.0f)
        return 0.0f;
    return qBound(0.0f, (dbm - window.lowDbm) / window.spanDb, 1.0f);
}

quint8 capturedByteFor(float dbm, const ColorWindow &window) {
    const long byte = std::lround(paletteFractionFor(dbm, window) * 255.0f);
    return static_cast<quint8>(std::clamp(byte, static_cast<long>(NoDataByte) + 1, 255L));
}

float shaderFractionFor(quint8 storedByte, const ColorUniforms &uniforms) {
    if (storedByte == NoDataByte)
        return 0.0f;
    const float v = static_cast<float>(storedByte) / 255.0f;
    return qBound(0.0f, (v - uniforms.colorLow) * uniforms.colorGain, 1.0f);
}

} // namespace WaterfallGeometry
