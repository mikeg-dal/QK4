#include <QtTest>

#include "dsp/bandplanstrip.h"
#include "utils/bandplan.h"

using BandPlan::BandMode;
using BandPlanStrip::BlockKind;

// Band-plan strip layout. The strip used to leave an empty gap past the band edge, which read as a
// rendering fault, and dropped a segment's label once the segment started off-screen. Both were
// placement arithmetic inside paintEvent; it now lives in BandPlanStrip and is pinned here.
class TestBandPlanStrip : public QObject {
    Q_OBJECT

    static constexpr int kCharPx = 7;
    static constexpr int kMarkerCharPx = 5;

    static int labelWidth(const QString &text) { return text.size() * kCharPx; }
    static int markerWidth(const QString &text) { return text.size() * kMarkerCharPx; }

    static BandPlanStrip::Layout layoutFor(int region, qint64 bandFreqHz, qint64 viewStartHz, int spanHz, int widthPx) {
        return BandPlanStrip::layout(BandPlan::segmentsForBand(region, bandFreqHz),
                                     BandPlan::markersForBand(region, bandFreqHz), BandPlan::bandName(bandFreqHz),
                                     viewStartHz, spanHz, widthPx, labelWidth, markerWidth);
    }

    static const BandPlanStrip::Label *labelFor(const BandPlanStrip::Layout &layout, const QString &text) {
        for (const auto &label : layout.labels) {
            if (label.text == text)
                return &label;
        }
        return nullptr;
    }

    // The invariants every layout must hold, whatever the band, span or position.
    static bool isConsistent(const BandPlanStrip::Layout &layout, int widthPx, QString *why) {
        if (layout.blocks.isEmpty()) {
            *why = QStringLiteral("no blocks");
            return false;
        }
        if (layout.blocks.first().x1 != 0 || layout.blocks.last().x2 != widthPx) {
            *why = QStringLiteral("blocks do not cover [0, %1]").arg(widthPx);
            return false;
        }
        for (int i = 0; i < layout.blocks.size(); ++i) {
            const auto &b = layout.blocks[i];
            if (b.x2 <= b.x1) {
                *why = QStringLiteral("empty block %1").arg(i);
                return false;
            }
            if (i > 0 && layout.blocks[i - 1].x2 != b.x1) {
                *why = QStringLiteral("gap or overlap before block %1").arg(i);
                return false;
            }
        }

        QVector<QPair<int, int>> placed;
        if (layout.tag.visible)
            placed.append({layout.tag.x, layout.tag.x + layout.tag.width});
        for (const auto &label : layout.labels) {
            const auto &b = layout.blocks[label.block];
            if (label.x < b.x1 || label.x + label.width > b.x2) {
                *why = QStringLiteral("label %1 outside its block").arg(label.text);
                return false;
            }
            placed.append({label.x, label.x + label.width});
        }
        for (const auto &m : layout.markers) {
            if (m.x <= 0 || m.x >= widthPx) {
                *why = QStringLiteral("marker %1 off screen").arg(m.name);
                return false;
            }
            if (m.labelVisible)
                placed.append({m.labelX, m.labelX + m.labelWidth});
        }
        for (int i = 0; i < placed.size(); ++i) {
            for (int j = i + 1; j < placed.size(); ++j) {
                if (placed[i].first < placed[j].second && placed[i].second > placed[j].first) {
                    *why = QStringLiteral("text overlaps at %1..%2").arg(placed[i].first).arg(placed[i].second);
                    return false;
                }
            }
        }
        return true;
    }

private slots:
    void aViewInsideTheBandHasNoOutOfBandBlock() {
        // 20 m US, 14.020-14.060 MHz: all CW.
        const auto layout = layoutFor(BandPlan::RegionUS, 14040000, 14020000, 40000, 1000);
        QCOMPARE(layout.blocks.size(), 1);
        QCOMPARE(layout.blocks[0].kind, BlockKind::Segment);
        QCOMPARE(layout.blocks[0].mode, BandMode::CW);
    }

    void theScreenBelowTheBandEdgeIsOutOfBandNotAGap() {
        // The reported case: 97 kHz span with the left edge at 13.99729 MHz, below 14.000.
        const int width = 1970;
        const qint64 viewStart = 13997290;
        const auto layout = layoutFor(BandPlan::RegionUS, 14059950, viewStart, 97000, width);

        QVERIFY(layout.blocks.size() >= 3);
        QCOMPARE(layout.blocks[0].kind, BlockKind::OutOfBand);
        QCOMPARE(layout.blocks[0].x1, 0);
        QCOMPARE(layout.blocks[0].x2, BandPlanStrip::xForFreq(14000000, viewStart, 97000, width));
        QCOMPARE(layout.blocks[1].kind, BlockKind::Segment);
        QCOMPARE(layout.blocks[1].mode, BandMode::CW);
        QCOMPARE(layout.blocks[2].mode, BandMode::Data);
        QCOMPARE(layout.blocks.last().x2, width);
    }

    void theTagSitsAtTheBandEdgeWhenTheEdgeIsOnScreen() {
        const qint64 viewStart = 13990000;
        const auto layout = layoutFor(BandPlan::RegionUS, 14059950, viewStart, 97000, 1970);
        QVERIFY(layout.tag.visible);
        QCOMPARE(layout.tag.x, BandPlanStrip::xForFreq(14000000, viewStart, 97000, 1970));
    }

    void theTagPinsToTheLeftEdgeWhenTheBandStartsOffScreen() {
        const auto layout = layoutFor(BandPlan::RegionUS, 14100000, 14050000, 97000, 1970);
        QVERIFY(layout.tag.visible);
        QCOMPARE(layout.tag.x, 0);
    }

    void aNarrowBandShowsOutOfBandOnBothSides() {
        // 30 m is 50 kHz wide; a 200 kHz view centred on it runs past both edges.
        const auto layout = layoutFor(BandPlan::RegionUS, 10125000, 10025000, 200000, 1000);
        QCOMPARE(layout.blocks.first().kind, BlockKind::OutOfBand);
        QCOMPARE(layout.blocks.last().kind, BlockKind::OutOfBand);
        QVERIFY(layoutFor(BandPlan::RegionUS, 10125000, 10025000, 200000, 1000).blocks.size() >= 4);
    }

    void aSegmentStartingOffScreenKeepsItsLabelRightOfTheTag() {
        // CW started well to the left; its label stays on screen, after the "20m" tag.
        const auto layout = layoutFor(BandPlan::RegionUS, 14040000, 14030000, 20000, 1000);
        const auto *cw = labelFor(layout, QStringLiteral("CW"));
        QVERIFY(cw);
        QVERIFY(layout.tag.visible);
        QCOMPARE(cw->x, layout.tag.x + layout.tag.width + BandPlanStrip::kLabelPad);
    }

    void aSegmentTooNarrowForItsLabelShowsNoLabel() {
        // US 17 m data segment is 10 kHz; at a 368 kHz span on 300 px it is a sliver.
        const auto layout = layoutFor(BandPlan::RegionUS, 18105000, 17930000, 368000, 300);
        for (const auto &label : layout.labels)
            QVERIFY(label.text != QStringLiteral("Data"));
    }

    void theTagHidesWhenTheVisibleBandIsNarrowerThanIt() {
        // Only 1 kHz of 20 m on screen at a 97 kHz span: a few pixels, far less than the tag.
        const auto layout = layoutFor(BandPlan::RegionUS, 14200000, 14349000, 97000, 1000);
        QVERIFY(!layout.tag.visible);
    }

    void markersOffScreenAreDropped() {
        // FT8 is 14.074; a view of 14.000-14.040 must not carry it.
        const auto layout = layoutFor(BandPlan::RegionUS, 14020000, 14000000, 40000, 1000);
        for (const auto &m : layout.markers)
            QVERIFY(m.name != QStringLiteral("FT8"));
    }

    void aZeroWidthOrSpanProducesNothing() {
        QVERIFY(layoutFor(BandPlan::RegionUS, 14040000, 14000000, 40000, 0).blocks.isEmpty());
        QVERIFY(layoutFor(BandPlan::RegionUS, 14040000, 14000000, 0, 1000).blocks.isEmpty());
    }

    // The gate: every region, every band, a range of spans, widths and positions from wholly below
    // the band to wholly above it.
    void everyLayoutIsContiguousAndNothingOverlaps() {
        const QVector<qint64> bandProbesHz = {1850000,  3700000,  5360000,  7100000,  10120000,
                                              14100000, 18100000, 21200000, 24950000, 28500000};
        const QVector<int> regions = {1, 2, 3, BandPlan::RegionUS};
        const QVector<int> spans = {5000, 10000, 24000, 50000, 97000, 200000, 368000};
        const QVector<int> widths = {120, 300, 1095, 2190};

        int checked = 0;
        for (int region : regions) {
            for (qint64 probe : bandProbesHz) {
                const auto segments = BandPlan::segmentsForBand(region, probe);
                if (segments.isEmpty())
                    continue;
                const qint64 lo = segments.first().startHz;
                const qint64 hi = segments.last().endHz;
                for (int span : spans) {
                    for (int width : widths) {
                        const qint64 step = qMax<qint64>(1000, (hi - lo + 2 * span) / 40);
                        for (qint64 start = lo - span; start <= hi + span; start += step) {
                            const auto layout = layoutFor(region, probe, start, span, width);
                            QString why;
                            if (!isConsistent(layout, width, &why)) {
                                QFAIL(qPrintable(QStringLiteral("region %1 band %2 span %3 width %4 start %5: %6")
                                                     .arg(region)
                                                     .arg(probe)
                                                     .arg(span)
                                                     .arg(width)
                                                     .arg(start)
                                                     .arg(why)));
                            }
                            ++checked;
                        }
                    }
                }
            }
        }
        QVERIFY(checked > 1000);
    }
};

QTEST_GUILESS_MAIN(TestBandPlanStrip)
#include "test_bandplanstrip.moc"
