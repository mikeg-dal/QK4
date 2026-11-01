#include "bandplan.h"
#include "ui/styling/k4constants.h"

namespace BandPlan {

namespace {

struct Band {
    qint64 lo;
    qint64 hi;
    QVector<BandSegment> segments;
    QVector<BandMarker> markers;
};

inline qint64 kHz(double v) {
    return static_cast<qint64>(v * 1000.0 + 0.5);
}

// Shorthand: build a segment from kHz endpoints.
BandSegment seg(double startKHz, double endKHz, BandMode mode) {
    return {kHz(startKHz), kHz(endKHz), mode};
}

BandMarker mk(const char *name, double freqKHz) {
    return {kHz(freqKHz), QString::fromLatin1(name)};
}

Band band(double loKHz, double hiKHz, QVector<BandSegment> segments, QVector<BandMarker> markers = {}) {
    return {kHz(loKHz), kHz(hiKHz), std::move(segments), std::move(markers)};
}

using M = BandMode;

// IARU Region 1 — Europe, Africa, Middle East, Northern Asia.
const QVector<Band> &region1() {
    static const QVector<Band> bands = {
        band(1810, 2000, {seg(1810, 1838, M::CW), seg(1838, 1840, M::Data), seg(1840, 2000, M::All)}),
        band(3500, 3800, {seg(3500, 3570, M::CW), seg(3570, 3600, M::Data), seg(3600, 3800, M::Phone)}),
        band(5351.5, 5366.5, {seg(5351.5, 5354, M::Data), seg(5354, 5366.5, M::All)}),
        band(7000, 7200,
             {seg(7000, 7040, M::CW), seg(7040, 7050, M::Data), seg(7050, 7060, M::All), seg(7060, 7200, M::Phone)}),
        band(10100, 10150, {seg(10100, 10130, M::CW), seg(10130, 10150, M::Data)}),
        band(14000, 14350,
             {seg(14000, 14070, M::CW), seg(14070, 14099, M::Data), seg(14099, 14101, M::Beacon),
              seg(14101, 14350, M::Phone)}),
        band(18068, 18168,
             {seg(18068, 18095, M::CW), seg(18095, 18109, M::Data), seg(18109, 18111, M::Beacon),
              seg(18111, 18168, M::Phone)}),
        band(21000, 21450,
             {seg(21000, 21070, M::CW), seg(21070, 21149, M::Data), seg(21149, 21151, M::Beacon),
              seg(21151, 21450, M::Phone)}),
        band(24890, 24990,
             {seg(24890, 24915, M::CW), seg(24915, 24929, M::Data), seg(24929, 24931, M::Beacon),
              seg(24931, 24990, M::Phone)}),
        band(28000, 29700,
             {seg(28000, 28070, M::CW), seg(28070, 28190, M::Data), seg(28190, 28225, M::Beacon),
              seg(28225, 29700, M::Phone)}),
    };
    return bands;
}

// IARU Region 2 — The Americas.
const QVector<Band> &region2() {
    static const QVector<Band> bands = {
        band(1800, 2000, {seg(1800, 1840, M::CW), seg(1840, 2000, M::All)}),
        band(3500, 4000, {seg(3500, 3580, M::CW), seg(3580, 3600, M::Data), seg(3600, 4000, M::Phone)}),
        band(5330.5, 5406.4, {seg(5330.5, 5406.4, M::All)}),
        band(7000, 7300, {seg(7000, 7040, M::CW), seg(7040, 7050, M::Data), seg(7050, 7300, M::Phone)}),
        band(10100, 10150, {seg(10100, 10130, M::CW), seg(10130, 10150, M::Data)}),
        band(14000, 14350,
             {seg(14000, 14070, M::CW), seg(14070, 14099, M::Data), seg(14099, 14101, M::Beacon),
              seg(14101, 14350, M::Phone)}),
        band(18068, 18168,
             {seg(18068, 18095, M::CW), seg(18095, 18109, M::Data), seg(18109, 18111, M::Beacon),
              seg(18111, 18168, M::Phone)}),
        band(21000, 21450,
             {seg(21000, 21070, M::CW), seg(21070, 21149, M::Data), seg(21149, 21151, M::Beacon),
              seg(21151, 21450, M::Phone)}),
        band(24890, 24990,
             {seg(24890, 24915, M::CW), seg(24915, 24929, M::Data), seg(24929, 24931, M::Beacon),
              seg(24931, 24990, M::Phone)}),
        band(28000, 29700,
             {seg(28000, 28070, M::CW), seg(28070, 28190, M::Data), seg(28190, 28225, M::Beacon),
              seg(28225, 29700, M::Phone)}),
    };
    return bands;
}

// IARU Region 3 — Asia-Pacific.
const QVector<Band> &region3() {
    static const QVector<Band> bands = {
        band(1800, 2000, {seg(1800, 1810, M::CW), seg(1810, 2000, M::All)}),
        band(3500, 3900, {seg(3500, 3535, M::CW), seg(3535, 3900, M::All)}),
        band(5351.5, 5366.5, {seg(5351.5, 5354, M::Data), seg(5354, 5366.5, M::All)}),
        band(7000, 7300, {seg(7000, 7040, M::CW), seg(7040, 7300, M::All)}),
        band(10100, 10150, {seg(10100, 10130, M::CW), seg(10130, 10150, M::Data)}),
        band(14000, 14350, {seg(14000, 14070, M::CW), seg(14070, 14112, M::Data), seg(14112, 14350, M::Phone)}),
        band(18068, 18168, {seg(18068, 18095, M::CW), seg(18095, 18111, M::Data), seg(18111, 18168, M::Phone)}),
        band(21000, 21450, {seg(21000, 21070, M::CW), seg(21070, 21149, M::Data), seg(21149, 21450, M::Phone)}),
        band(24890, 24990, {seg(24890, 24915, M::CW), seg(24915, 24931, M::Data), seg(24931, 24990, M::Phone)}),
        band(28000, 29700,
             {seg(28000, 28070, M::CW), seg(28070, 28190, M::Data), seg(28190, 28225, M::Beacon),
              seg(28225, 29700, M::Phone)}),
    };
    return bands;
}

// United States (FCC) — practical/legal mode boundaries for an Extra-class operator,
// plus FT8/FT4/WSPR digital calling-frequency markers. 160m keeps the conventional
// CW/DATA/PHONE split (FCC imposes no legal mode sub-bands there).
const QVector<Band> &regionUS() {
    static const QVector<Band> bands = {
        band(1800, 2000, {seg(1800, 1810, M::CW), seg(1810, 1843, M::Data), seg(1843, 2000, M::Phone)},
             {mk("WSPR", 1836.6), mk("FT8", 1840)}),
        band(3500, 4000, {seg(3500, 3570, M::CW), seg(3570, 3600, M::Data), seg(3600, 4000, M::Phone)},
             {mk("WSPR", 3568.6), mk("FT8", 3573), mk("FT4", 3575)}),
        band(5330.5, 5406.4, {seg(5330.5, 5406.4, M::All)},
             {mk("Ch1", 5332), mk("Ch2", 5348), mk("Ch3", 5358.5), mk("Ch4", 5373), mk("Ch5", 5405)}),
        band(7000, 7300, {seg(7000, 7040, M::CW), seg(7040, 7125, M::Data), seg(7125, 7300, M::Phone)},
             {mk("WSPR", 7038.6), mk("FT4", 7047.5), mk("FT8", 7074)}),
        band(10100, 10150, {seg(10100, 10130, M::CW), seg(10130, 10150, M::Data)},
             {mk("FT8", 10136), mk("WSPR", 10138.7), mk("FT4", 10140)}),
        band(14000, 14350, {seg(14000, 14070, M::CW), seg(14070, 14150, M::Data), seg(14150, 14350, M::Phone)},
             {mk("FT8", 14074), mk("FT4", 14080), mk("WSPR", 14095.6)}),
        band(18068, 18168, {seg(18068, 18100, M::CW), seg(18100, 18110, M::Data), seg(18110, 18168, M::Phone)},
             {mk("FT8", 18100), mk("FT4", 18104), mk("WSPR", 18104.6)}),
        band(21000, 21450, {seg(21000, 21070, M::CW), seg(21070, 21200, M::Data), seg(21200, 21450, M::Phone)},
             {mk("FT8", 21074), mk("WSPR", 21094.6), mk("FT4", 21140)}),
        band(24890, 24990, {seg(24890, 24915, M::CW), seg(24915, 24930, M::Data), seg(24930, 24990, M::Phone)},
             {mk("FT8", 24915), mk("FT4", 24919), mk("WSPR", 24924.6)}),
        band(28000, 29700,
             {seg(28000, 28070, M::CW), seg(28070, 28190, M::Data), seg(28190, 28300, M::Beacon),
              seg(28300, 29700, M::Phone)},
             {mk("FT8", 28074), mk("WSPR", 28124.6), mk("FT4", 28180)}),
    };
    return bands;
}

const QVector<Band> &regionBands(int region) {
    switch (region) {
    case 1:
        return region1();
    case 3:
        return region3();
    case RegionUS:
        return regionUS();
    case 2:
    default:
        return region2();
    }
}

const Band *bandAt(int region, qint64 freqHz) {
    const int index = planBandIndex(region, freqHz);
    return index < 0 ? nullptr : &regionBands(region)[index];
}

} // namespace

// WHY a key of its own rather than the K4's band: the K4 band table, these
// per-region plan tables and bandName() deliberately disagree at the edges. The K4 table follows
// the radio's band buttons (widest allocation, e.g. 40 m to 7300 kHz everywhere), the plan tables
// follow each region's actual allocation (Region 1 40 m ends at 7200 kHz) and bandName() is
// region-independent. Caching the plan on the K4 band kept a Region 1 overlay empty after tuning
// from 7250 kHz (no plan) to 7100 kHz (same K4 band, but inside the plan).
int planBandIndex(int region, qint64 freqHz) {
    if (region < 1 || (region > 3 && region != RegionUS))
        return -1;
    const QVector<Band> &bands = regionBands(region);
    for (int i = 0; i < bands.size(); ++i) {
        if (freqHz >= bands[i].lo && freqHz <= bands[i].hi)
            return i;
    }
    return -1;
}

QVector<BandSegment> segmentsForBand(int region, qint64 freqHz) {
    const Band *b = bandAt(region, freqHz);
    return b ? b->segments : QVector<BandSegment>{};
}

QVector<BandMarker> markersForBand(int region, qint64 freqHz) {
    const Band *b = bandAt(region, freqHz);
    return b ? b->markers : QVector<BandMarker>{};
}

QString bandName(qint64 freqHz) {
    struct NamedBand {
        qint64 lo;
        qint64 hi;
        const char *name;
    };
    static const NamedBand kBands[] = {
        {1800000, 2000000, "160m"},  {3500000, 4000000, "80m"},   {5250000, 5450000, "60m"},
        {7000000, 7300000, "40m"},   {10100000, 10150000, "30m"}, {14000000, 14350000, "20m"},
        {18068000, 18168000, "17m"}, {21000000, 21450000, "15m"}, {24890000, 24990000, "12m"},
        {28000000, 29700000, "10m"},
    };
    for (const NamedBand &b : kBands) {
        if (freqHz >= b.lo && freqHz <= b.hi)
            return QString::fromLatin1(b.name);
    }
    return QString();
}

QColor modeColor(BandMode mode) {
    switch (mode) {
    case BandMode::CW:
        return QColor(K4Styles::Colors::BandPlanCw);
    case BandMode::Data:
        return QColor(K4Styles::Colors::BandPlanData);
    case BandMode::Phone:
        return QColor(K4Styles::Colors::BandPlanPhone);
    case BandMode::Beacon:
        return QColor(K4Styles::Colors::BandPlanBeacon);
    case BandMode::All:
        return QColor(K4Styles::Colors::BandPlanAll);
    }
    return QColor(K4Styles::Colors::BandPlanAll);
}

QString modeLabel(BandMode mode) {
    switch (mode) {
    case BandMode::CW:
        return QStringLiteral("CW");
    case BandMode::Data:
        return QStringLiteral("Data");
    case BandMode::Phone:
        return QStringLiteral("Phone");
    case BandMode::Beacon:
        return QStringLiteral("Beacon");
    case BandMode::All:
        return QStringLiteral("All");
    }
    return QString();
}

} // namespace BandPlan
