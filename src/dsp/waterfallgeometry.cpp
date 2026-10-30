#include "waterfallgeometry.h"

#include "dsp/spectrumscale.h"
#include "models/radiostate/spectrumdisplaystate.h"

#include <QtGlobal>
#include <algorithm>
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

ColorWindow colorWindowFor(float refDbm, int colorRangeDb) {
    const bool known = colorRangeDb >= SpectrumDisplayLimits::WaterfallColorRangeMin &&
                       colorRangeDb <= SpectrumDisplayLimits::WaterfallColorRangeMax;
    const float range = static_cast<float>(known ? colorRangeDb : DefaultColorRangeDb);
    return ColorWindow{refDbm - ColorRangeBaseFactor * range, PaletteSpanDb};
}

float paletteFractionFor(float dbm, const ColorWindow &window) {
    if (window.spanDb <= 0.0f)
        return 0.0f;
    return qBound(0.0f, (dbm - window.lowDbm) / window.spanDb, 1.0f);
}

quint8 storedByteFor(float dbm) {
    const long byte = std::lround(dbm + SpectrumScale::K4_DBM_OFFSET);
    return static_cast<quint8>(qBound(static_cast<long>(NoDataByte) + 1, byte, 255L));
}

quint8 capturedByteFor(float dbm, const ColorWindow &window) {
    const long byte = std::lround(paletteFractionFor(dbm, window) * 255.0f);
    return static_cast<quint8>(std::clamp(byte, static_cast<long>(NoDataByte) + 1, 255L));
}

ColorUniforms colorUniformsFor(const ColorWindow &window) {
    if (window.spanDb <= 0.0f)
        return ColorUniforms{};
    // In texel units: the byte for lowDbm, normalised as the GPU normalises an R8 texel (/255).
    return ColorUniforms{(window.lowDbm + SpectrumScale::K4_DBM_OFFSET) / 255.0f, 255.0f / window.spanDb};
}

float shaderFractionFor(quint8 storedByte, const ColorUniforms &uniforms) {
    if (storedByte == NoDataByte)
        return 0.0f;
    const float v = static_cast<float>(storedByte) / 255.0f;
    return qBound(0.0f, (v - uniforms.colorLow) * uniforms.colorGain, 1.0f);
}

} // namespace WaterfallGeometry
