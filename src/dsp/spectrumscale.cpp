#include "dsp/spectrumscale.h"

#include <QtGlobal>
#include <cmath>

namespace SpectrumScale {

namespace {

// Above S9 the amateur convention is round decades — S9+10, S9+20 — not the exact dB over S9.
constexpr int kDbPerPlusStep = 10;

bool inWindow(float dbm, float minDb, float maxDb) {
    return dbm >= minDb && dbm <= maxDb;
}

// Smallest stride, in dB, that keeps neighbouring labels apart. Computed rather than picked from a
// fixed list: the window can be 150 dB across a chart only tens of pixels tall, and any list short
// enough to read is eventually too short to separate them.
int strideAtLeast(float neededDb, int unitDb) {
    if (neededDb <= 0.0f)
        return unitDb;
    const int units = static_cast<int>(std::ceil(neededDb / static_cast<float>(unitDb)));
    return qMax(1, units) * unitDb;
}

// dBm mode prefers values an operator reads easily — 10, 20, 50 — over the raw minimum.
int roundedDbStride(float neededDb) {
    static constexpr int kNice[] = {1, 2, 5, 10, 20, 25, 50, 100};
    for (int n : kNice) {
        if (static_cast<float>(n) >= neededDb)
            return n;
    }
    // Past the table, step in whole hundreds.
    return static_cast<int>(std::ceil(neededDb / 100.0f)) * 100;
}

// Minimum dB between labels for them not to overlap, given how many pixels the window occupies.
float minDbSeparation(float minDb, float maxDb, int availablePx, int labelHeightPx) {
    const float span = maxDb - minDb;
    if (availablePx <= 0 || span <= 0.0f)
        return 0.0f;
    return span * (static_cast<float>(labelHeightPx) / static_cast<float>(availablePx));
}

} // namespace

float sUnitDbm(int sUnit) {
    const int s = qBound(1, sUnit, 9);
    return S9_DBM - static_cast<float>(9 - s) * DB_PER_S_UNIT;
}

float normalizedForDb(float dbm, float minDb, float maxDb) {
    const float range = maxDb - minDb;
    if (range <= 0.0f)
        return 0.0f;
    return qMax(0.0f, (dbm - minDb) / range);
}

QVector<Label> labelsFor(float minDb, float maxDb, bool useSUnits, int availablePx, int labelHeightPx) {
    QVector<Label> out;
    if (maxDb <= minDb)
        return out;

    const float needed = minDbSeparation(minDb, maxDb, availablePx, labelHeightPx);

    if (!useSUnits) {
        const int step = roundedDbStride(needed);
        // Walk round multiples of the step so labels land on -120, not -129.75.
        const int first = static_cast<int>(std::floor(maxDb / step)) * step;
        for (int v = first; v >= static_cast<int>(std::ceil(minDb)); v -= step) {
            if (inWindow(static_cast<float>(v), minDb, maxDb))
                out.append({static_cast<float>(v), QStringLiteral("%1 dBm").arg(v)});
        }
        return out;
    }

    // S-units step in whole units; above S9 in whole decades. Both derived from the same required
    // separation, so a label above S9 cannot collide with S9 itself either.
    const int sStride = strideAtLeast(needed, static_cast<int>(DB_PER_S_UNIT)) / static_cast<int>(DB_PER_S_UNIT);
    const int plusStride = strideAtLeast(needed, kDbPerPlusStep);

    // Above S9 first, so the result reads top-down.
    if (maxDb > S9_DBM) {
        const int highest = static_cast<int>(std::floor((maxDb - S9_DBM) / plusStride)) * plusStride;
        for (int over = highest; over >= plusStride; over -= plusStride) {
            const float dbm = S9_DBM + static_cast<float>(over);
            if (inWindow(dbm, minDb, maxDb))
                out.append({dbm, QStringLiteral("S9+%1").arg(over)});
        }
    }

    // S9 down to S1, on the stride. Anchored at S9 so that unit always appears when visible.
    for (int s = 9; s >= 1; s -= sStride) {
        const float dbm = sUnitDbm(s);
        if (inWindow(dbm, minDb, maxDb))
            out.append({dbm, QStringLiteral("S%1").arg(s)});
    }

    return out;
}

} // namespace SpectrumScale
