#include "spectrumgeometry.h"

#include <cmath>

namespace SpectrumGeometry {

Mode modeFromString(const QString &mode) {
    if (mode == QLatin1String("LSB"))
        return Mode::Lsb;
    if (mode == QLatin1String("USB"))
        return Mode::Usb;
    if (mode == QLatin1String("CW"))
        return Mode::Cw;
    if (mode == QLatin1String("CW-R"))
        return Mode::CwR;
    if (mode == QLatin1String("DATA"))
        return Mode::Data;
    if (mode == QLatin1String("DATA-R"))
        return Mode::DataR;
    if (mode == QLatin1String("AM"))
        return Mode::Am;
    if (mode == QLatin1String("FM"))
        return Mode::Fm;
    return Mode::Unknown;
}

DataSubMode dataSubModeFromInt(int subMode) {
    switch (subMode) {
    case 1:
        return DataSubMode::AfskA;
    case 2:
        return DataSubMode::FskD;
    case 3:
        return DataSubMode::PskD;
    default:
        return DataSubMode::DataA;
    }
}

bool isCw(Mode mode) {
    return mode == Mode::Cw || mode == Mode::CwR;
}

bool isData(Mode mode) {
    return mode == Mode::Data || mode == Mode::DataR;
}

bool isRtty(Mode mode, DataSubMode subMode) {
    return isData(mode) && (subMode == DataSubMode::AfskA || subMode == DataSubMode::FskD);
}

qint64 displayCenterHz(qint64 centerFreqHz, int ifShiftDecaHz, Mode mode) {
    const qint64 shiftHz = static_cast<qint64>(ifShiftDecaHz) * 10;
    if (mode == Mode::Cw)
        return centerFreqHz + shiftHz;
    if (mode == Mode::CwR)
        return centerFreqHz - shiftHz;
    return centerFreqHz;
}

qint64 markerFreqHz(qint64 tunedFreqHz, int ifShiftDecaHz, Mode mode) {
    return displayCenterHz(tunedFreqHz, ifShiftDecaHz, mode);
}

Passband passbandFor(Mode mode, DataSubMode subMode, qint64 tunedFreqHz, int bandwidthHz, int ifShiftDecaHz,
                     int rttyShiftHz) {
    const qint64 shiftHz = static_cast<qint64>(ifShiftDecaHz) * 10;
    qint64 center = tunedFreqHz;
    if (isData(mode) && subMode == DataSubMode::PskD) {
        center = tunedFreqHz;
    } else if (isRtty(mode, subMode)) {
        // The dial is the mark tone and space sits one shift below it (both sub-modes are LSB), so
        // the passband straddles the pair, half a shift below the dial.
        center = tunedFreqHz - rttyShiftHz / 2;
    } else if (mode == Mode::Lsb || mode == Mode::CwR) {
        center = tunedFreqHz - shiftHz;
    } else if (mode == Mode::Usb || mode == Mode::Cw || isData(mode)) {
        center = tunedFreqHz + shiftHz;
    }
    // AM, FM and Unknown stay centred on the carrier: both sidebands, no IF shift.
    return {center - bandwidthHz / 2, center + bandwidthHz / 2};
}

int usableBinCount(qsizetype payloadSize, int binsOffset, int binCount, int maxBins) {
    if (binsOffset < 0 || binCount <= 0 || binCount > maxBins)
        return 0;
    if (static_cast<qsizetype>(binsOffset) + binCount > payloadSize)
        return 0;
    return binCount;
}

int frequencyLabelStepHz(int gridIntervalHz, int spanHz, double widthPx, double widestLabelPx, double gapPx) {
    if (gridIntervalHz <= 0 || spanHz <= 0 || widthPx <= 0.0)
        return gridIntervalHz;
    const double pxPerHz = widthPx / static_cast<double>(spanHz);
    const double neededPx = widestLabelPx + gapPx;
    static constexpr int kMultipliers[] = {1, 2, 5};
    for (qint64 decade = 1;; decade *= 10) {
        for (int m : kMultipliers) {
            const qint64 step = static_cast<qint64>(gridIntervalHz) * m * decade;
            if (static_cast<double>(step) * pxPerHz >= neededPx || step >= spanHz)
                return static_cast<int>(step);
        }
    }
}

float fillAlphaFor(float y, float peakY, float glowWidth, float glowIntensity, float glowAlpha) {
    const float distFromPeak = y - peakY;
    if (distFromPeak < -glowWidth * 2.0f)
        return 0.0f; // discarded above the trace and its glow
    // smoothstep(-0.002, 0.002, distFromPeak)
    const float t = qBound(0.0f, (distFromPeak + 0.002f) / 0.004f, 1.0f);
    const float fillMask = t * t * (3.0f - 2.0f * t);
    const float glowDist = std::abs(distFromPeak);
    float glow = std::exp(-glowDist / glowWidth) * glowIntensity;
    glow = qMax(glow, std::exp(-glowDist / (glowWidth * 0.3f)) * glowIntensity * 0.5f);
    return qMax(0.9f * fillMask, glow * glowAlpha);
}

} // namespace SpectrumGeometry
