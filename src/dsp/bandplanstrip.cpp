#include "bandplanstrip.h"

#include <QPair>
#include <cmath>

namespace BandPlanStrip {

namespace {

using Span = QPair<int, int>;

bool overlapsAny(int left, int right, const QVector<Span> &occupied) {
    for (const Span &r : occupied) {
        if (left < r.second && right > r.first)
            return true;
    }
    return false;
}

void appendBlock(QVector<Block> &blocks, int x1, int x2, BlockKind kind, BandPlan::BandMode mode) {
    if (x2 <= x1)
        return;
    blocks.append({x1, x2, kind, mode});
}

} // namespace

QString outOfBandText() {
    return QStringLiteral("OUT OF BAND");
}

int xForFreq(qint64 freqHz, qint64 viewStartHz, int spanHz, int widthPx) {
    if (spanHz <= 0 || widthPx <= 0)
        return 0;
    const double n = static_cast<double>(freqHz - viewStartHz) / static_cast<double>(spanHz);
    return static_cast<int>(std::lround(qBound(0.0, n, 1.0) * widthPx));
}

Layout layout(const QVector<BandPlan::BandSegment> &segments, const QVector<BandPlan::BandMarker> &markers,
              const QString &bandName, qint64 viewStartHz, int spanHz, int widthPx, const TextWidth &labelWidth,
              const TextWidth &markerWidth) {
    Layout out;
    if (widthPx <= 0 || spanHz <= 0)
        return out;

    // Blocks. The cursor walks left to right; anything it skips over is out of band, which also
    // covers a gap between two segments should a band plan ever have one.
    int cursor = 0;
    for (const BandPlan::BandSegment &s : segments) {
        const int x1 = xForFreq(s.startHz, viewStartHz, spanHz, widthPx);
        const int x2 = xForFreq(s.endHz, viewStartHz, spanHz, widthPx);
        appendBlock(out.blocks, cursor, x1, BlockKind::OutOfBand, BandPlan::BandMode::All);
        const int start = qMax(cursor, x1);
        appendBlock(out.blocks, start, x2, BlockKind::Segment, s.mode);
        cursor = qMax(cursor, x2);
    }
    appendBlock(out.blocks, cursor, widthPx, BlockKind::OutOfBand, BandPlan::BandMode::All);

    QVector<Span> occupied;

    // Band tag, at the left edge of whatever part of the band is on screen.
    if (!segments.isEmpty() && !bandName.isEmpty()) {
        const int bandX1 = xForFreq(segments.first().startHz, viewStartHz, spanHz, widthPx);
        const int bandX2 = xForFreq(segments.last().endHz, viewStartHz, spanHz, widthPx);
        const int tagWidth = labelWidth(bandName) + 2 * kTagPad;
        if (bandX2 - bandX1 >= tagWidth) {
            out.tag = {true, bandX1, tagWidth};
            occupied.append({bandX1, bandX1 + tagWidth});
        }
    }

    // Labels sit at the left of their block, pushed right of the tag when they share its space.
    // Because blocks are clipped to the screen, a segment that starts off-screen keeps its label at
    // the left edge until the next segment's start leaves it no room.
    for (int i = 0; i < out.blocks.size(); ++i) {
        const Block &b = out.blocks[i];
        const QString text = b.kind == BlockKind::Segment ? BandPlan::modeLabel(b.mode) : outOfBandText();
        const int width = labelWidth(text);
        int x = b.x1 + kLabelPad;
        if (out.tag.visible && b.x1 < out.tag.x + out.tag.width && b.x2 > out.tag.x)
            x = qMax(x, out.tag.x + out.tag.width + kLabelPad);
        if (x + width + kLabelPad > b.x2 || overlapsAny(x, x + width, occupied))
            continue;
        out.labels.append({text, x, width, i});
        occupied.append({x, x + width});
    }

    // Markers: a tick wherever the frequency is on screen and clear of the tag, a label only where
    // it fits without colliding with anything placed above.
    for (const BandPlan::BandMarker &m : markers) {
        if (m.freqHz <= viewStartHz || m.freqHz >= viewStartHz + spanHz)
            continue;
        const int x = xForFreq(m.freqHz, viewStartHz, spanHz, widthPx);
        if (x <= 0 || x >= widthPx)
            continue;
        if (out.tag.visible && x >= out.tag.x && x <= out.tag.x + out.tag.width)
            continue;
        Marker marker{m.name, x, false, 0, 0};
        const int width = markerWidth(m.name);
        const int lx = x + kMarkerLabelGap;
        if (lx + width <= widthPx && !overlapsAny(lx, lx + width, occupied)) {
            marker.labelVisible = true;
            marker.labelX = lx;
            marker.labelWidth = width;
            occupied.append({lx, lx + width});
        }
        out.markers.append(marker);
    }

    return out;
}

} // namespace BandPlanStrip
