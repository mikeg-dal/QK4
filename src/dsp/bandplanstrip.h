#ifndef DSP_BANDPLANSTRIP_H
#define DSP_BANDPLANSTRIP_H

#include "utils/bandplan.h"

#include <QString>
#include <QVector>
#include <QtGlobal>
#include <functional>

// Layout of the band-plan strip across the top of the panadapter: which pixel range each mode
// segment covers, where the band tag and labels go, and which labels fit. Pure arithmetic with no
// widget or QPainter so it can be tested directly; BandPlanOverlay only paints what this returns.
//
// One row. Screen area outside the band is an OutOfBand block rather than a gap, so the blocks
// always cover the full width — an empty gap at the band edge read as a rendering fault.
namespace BandPlanStrip {

enum class BlockKind { Segment, OutOfBand };

struct Block {
    int x1 = 0;
    int x2 = 0;
    BlockKind kind = BlockKind::OutOfBand;
    BandPlan::BandMode mode = BandPlan::BandMode::All; // meaningful only for Segment
};

// The band name chip ("20m"), pinned to the left edge of the visible in-band area.
struct Tag {
    bool visible = false;
    int x = 0;
    int width = 0;
};

struct Label {
    QString text;
    int x = 0;
    int width = 0;
    int block = 0; // index into Layout::blocks
};

struct Marker {
    QString name;
    int x = 0;
    bool labelVisible = false;
    int labelX = 0;
    int labelWidth = 0;
};

struct Layout {
    QVector<Block> blocks; // contiguous, ordered, covering exactly [0, widthPx]
    Tag tag;
    QVector<Label> labels; // only the labels that fit
    QVector<Marker> markers;
};

using TextWidth = std::function<int(const QString &)>;

constexpr int kLabelPad = 4;
constexpr int kTagPad = 5;
constexpr int kMarkerLabelGap = 4;

QString outOfBandText();

// Screen x for a frequency, clamped to [0, widthPx].
int xForFreq(qint64 freqHz, qint64 viewStartHz, int spanHz, int widthPx);

// viewStartHz is the frequency at the left edge, with any CW dial offset already applied by the
// caller. labelWidth measures segment, out-of-band and tag text; markerWidth measures marker names,
// which use a smaller font.
Layout layout(const QVector<BandPlan::BandSegment> &segments, const QVector<BandPlan::BandMarker> &markers,
              const QString &bandName, qint64 viewStartHz, int spanHz, int widthPx, const TextWidth &labelWidth,
              const TextWidth &markerWidth);

} // namespace BandPlanStrip

#endif // DSP_BANDPLANSTRIP_H
