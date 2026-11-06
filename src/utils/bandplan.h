#ifndef BANDPLAN_H
#define BANDPLAN_H

#include <QColor>
#include <QString>
#include <QVector>
#include <QtGlobal>

/**
 * @brief Simplified HF band-plan data for the panadapter band-plan overlay.
 *
 * Five display buckets (CW / DATA / PHONE / BEACON / ALL) per the user-supplied
 * mode-color map. Region codes: 1/2/3 = IARU Regions (voluntary recommendations,
 * national rules override); 4 = United States (FCC) with practical Extra-class mode
 * boundaries plus FT8/FT4/WSPR calling-frequency markers.
 */
namespace BandPlan {

constexpr int RegionUS = 4; // FCC mode boundaries (Extra class) + digital markers

enum class BandMode { CW, Data, Phone, Beacon, All };

struct BandSegment {
    qint64 startHz;
    qint64 endHz;
    BandMode mode;
};

struct BandMarker {
    qint64 freqHz;
    QString name; // e.g. "FT8", "FT4", "WSPR"
};

/// Segments of the band containing freqHz for the given region (1/2/3 IARU, 4 US).
/// Returns an empty vector when freqHz is outside every modeled amateur band or
/// region is out of range.
QVector<BandSegment> segmentsForBand(int region, qint64 freqHz);

/// Identifies the plan band containing freqHz for the region: its index in that region's table, or
/// -1 when freqHz is outside every modeled band (or region is out of range). segmentsForBand and
/// markersForBand depend on (region, freqHz) only through this value, so it is the key to cache
/// their results on.
int planBandIndex(int region, qint64 freqHz);

/// Digital calling-frequency markers for the band containing freqHz. Only the US
/// region (4) defines markers; IARU regions return empty.
QVector<BandMarker> markersForBand(int region, qint64 freqHz);

/// Short ham band name for a frequency ("160m" .. "10m"), or empty if not in an HF
/// amateur band. Region-independent (band names are universal).
QString bandName(qint64 freqHz);

/// Display color for a mode bucket (from K4Styles::Colors::BandPlan*).
QColor modeColor(BandMode mode);

/// Short uppercase label for a mode bucket ("CW", "DATA", "PHONE", "BEACON", "ALL").
QString modeLabel(BandMode mode);

} // namespace BandPlan

#endif // BANDPLAN_H
