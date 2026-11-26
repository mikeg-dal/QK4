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

int visibleRowsFor(float waterfallHeightPx, int storedRows, int minRows, int devicePxPerRow) {
    if (storedRows <= 0)
        return 0;
    // One row is always reserved for the write head: showing all storedRows would wrap the window
    // onto the whole ring and put the in-flight row back on screen.
    const int maxRows = storedRows - 1;
    const int wanted =
        static_cast<int>(std::lround(waterfallHeightPx / static_cast<float>((std::max)(1, devicePxPerRow))));
    return qBound(qMin(minRows, maxRows), wanted, maxRows);
}

bool isSpeedStep(int step) {
    return std::find(SpeedSteps.begin(), SpeedSteps.end(), step) != SpeedSteps.end();
}

int stepSpeed(int step, int delta) {
    const auto it = std::find(SpeedSteps.begin(), SpeedSteps.end(), step);
    // An unknown value counts as Fill, so anything saved by an older build lands somewhere sane.
    const auto from = it == SpeedSteps.end() ? std::find(SpeedSteps.begin(), SpeedSteps.end(), SpeedFill) : it;
    const int index = static_cast<int>(from - SpeedSteps.begin());
    const int last = static_cast<int>(SpeedSteps.size()) - 1;
    return SpeedSteps[static_cast<std::size_t>(std::clamp(index + delta, 0, last))];
}

float physicalRowHeightFor(int step, qreal dpr, int devicePxPerRow) {
    if (step <= SpeedFill)
        return 1.0f; // Fill and the fold steps: one row per physical pixel of the panel
    const double logicalToFramebuffer = dpr > 0.0 ? static_cast<double>(dpr) : 1.0;
    const double framebufferPerPhysical = (std::max)(1, devicePxPerRow);
    const double physical = static_cast<double>(step) * logicalToFramebuffer / framebufferPerPhysical;
    // A step can only reach 1.0 where the panel's physical pixels are coarser than its logical ones,
    // and at 1.0 it simply IS Fill - the same picture, and the label says the same seconds. That is
    // correct rather than a fault, so clamp and let it be.
    return static_cast<float>((std::max)(1.0, physical));
}

double secondsOnScreen(int pixelRows, int step, double lineRateHz, qreal dpr, int devicePxPerRow) {
    if (pixelRows <= 0 || lineRateHz <= 0.0)
        return 0.0;
    const SpeedPlan plan = speedPlanFor(pixelRows, step, dpr, devicePxPerRow);
    return static_cast<double>(plan.visibleRows) * plan.framesPerRow / lineRateHz;
}

QString speedLabelFor(int step, double seconds) {
    if (seconds <= 0.0) {
        // The rate is unknown for the first second or so of a connection. Name the shape rather
        // than print a number that would be wrong.
        if (step == SpeedFill)
            return QStringLiteral("Fill");
        return step > 0 ? QStringLiteral("1/%1").arg(step) : QStringLiteral("x%1").arg(-step);
    }
    const int whole = (std::max)(1, static_cast<int>(std::lround(seconds)));
    return step == SpeedFill ? QStringLiteral("Fill %1s").arg(whole) : QStringLiteral("%1s").arg(whole);
}

QString speedLabel(int step, int pixelRows, double lineRateHz, qreal dpr, int devicePxPerRow) {
    return speedLabelFor(step, secondsOnScreen(pixelRows, step, lineRateHz, dpr, devicePxPerRow));
}

bool secondsLabelShouldChange(double shownSeconds, double computedSeconds) {
    // Appearing or disappearing is always a change: before a rate is measured the key shows the
    // shape instead of a number, and the switch between the two must not be held back.
    if (computedSeconds <= 0.0 || shownSeconds <= 0.0)
        return !qFuzzyCompare(1.0 + shownSeconds, 1.0 + computedSeconds);
    return std::abs(computedSeconds - shownSeconds) > SecondsLabelHoldFraction * shownSeconds;
}

SpeedPlan speedPlanFor(int pixelRows, int step, qreal dpr, int devicePxPerRow) {
    if (pixelRows <= 0)
        return SpeedPlan{pixelRows, 1, 1.0f};
    if (step <= -2) {
        // Slower than Fill: still one pixel row per stored row, but each row is several frames
        // peak-folded together, so the same height holds more history.
        return SpeedPlan{pixelRows, (std::min)(-step, MaxFramesPerRow), 1.0f};
    }
    if (step <= SpeedFill)
        return SpeedPlan{pixelRows, 1, 1.0f};
    // Faster than Fill: each frame is its own row, drawn `height` physical pixel rows tall. The
    // height is not rounded to a whole number of pixels - the shader's cubic reconstruction takes a
    // continuous phase, so it does not need one.
    // WHY at least 2 rows: one row would be the whole waterfall in a single colour band.
    const float height = physicalRowHeightFor(step, dpr, devicePxPerRow);
    const int rows = (std::max)(2, static_cast<int>(std::lround(static_cast<double>(pixelRows) / height)));
    return SpeedPlan{(std::min)(rows, pixelRows), 1, height};
}

bool speedStepFits(int step, int pixelRows, int minRows, qreal dpr, int devicePxPerRow) {
    if (!isSpeedStep(step))
        return false;
    if (step <= SpeedFill)
        return true; // Fill and the folds keep every row the height can hold
    return speedPlanFor(pixelRows, step, dpr, devicePxPerRow).visibleRows >= minRows;
}

int stepSpeedFitting(int step, int delta, int pixelRows, int minRows, qreal dpr, int devicePxPerRow) {
    if (delta == 0)
        return step;
    const int direction = delta > 0 ? 1 : -1;
    int current = isSpeedStep(step) ? step : SpeedFill;
    for (int remaining = std::abs(delta); remaining > 0; --remaining) {
        int candidate = current;
        for (;;) {
            const int next = stepSpeed(candidate, direction);
            if (next == candidate)
                return current; // ran off the ladder: stay on the last step that fits
            candidate = next;
            if (speedStepFits(candidate, pixelRows, minRows, dpr, devicePxPerRow))
                break;
        }
        current = candidate;
    }
    return current;
}

float scrollPhaseFor(int rowsAdvanced, int storedRows, double elapsedMs, double msPerRow) {
    if (storedRows <= 0 || rowsAdvanced <= 0 || msPerRow <= 0.0)
        return 0.0f;
    const double travelled = std::clamp(elapsedMs / msPerRow, 0.0, static_cast<double>(rowsAdvanced));
    const double remaining = static_cast<double>(rowsAdvanced) - travelled;
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
