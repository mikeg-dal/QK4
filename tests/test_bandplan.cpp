#include <QTest>
#include "utils/bandplan.h"

using BandPlan::BandMode;

class TestBandPlan : public QObject {
    Q_OBJECT

    // Helper: the mode of the segment containing freqHz (or -1 sentinel via Beacon? use bool).
    static bool segmentAt(int region, qint64 freqHz, BandMode &outMode) {
        const auto segs = BandPlan::segmentsForBand(region, freqHz);
        for (const auto &s : segs) {
            if (freqHz >= s.startHz && freqHz < s.endHz) {
                outMode = s.mode;
                return true;
            }
        }
        return false;
    }

private slots:
    // Region 2 (Americas) — 20m segment boundaries.
    void r2_20m_cw() {
        BandMode m;
        QVERIFY(segmentAt(2, 14025000, m));
        QCOMPARE(m, BandMode::CW);
    }
    void r2_20m_data() {
        BandMode m;
        QVERIFY(segmentAt(2, 14080000, m));
        QCOMPARE(m, BandMode::Data);
    }
    void r2_20m_beacon() {
        BandMode m;
        QVERIFY(segmentAt(2, 14100000, m));
        QCOMPARE(m, BandMode::Beacon);
    }
    void r2_20m_phone() {
        BandMode m;
        QVERIFY(segmentAt(2, 14200000, m));
        QCOMPARE(m, BandMode::Phone);
    }

    // Region 2 — 40m phone starts at 7050 (no separate phone-edge like R1).
    void r2_40m_phone() {
        BandMode m;
        QVERIFY(segmentAt(2, 7150000, m));
        QCOMPARE(m, BandMode::Phone);
    }

    // Region 1 — 80m phone starts at 3600 (differs from R2's 3600 too, but CW span differs).
    void r1_80m_cw() {
        BandMode m;
        QVERIFY(segmentAt(1, 3550000, m));
        QCOMPARE(m, BandMode::CW);
    }
    void r1_80m_phone() {
        BandMode m;
        QVERIFY(segmentAt(1, 3700000, m));
        QCOMPARE(m, BandMode::Phone);
    }

    // Region 3 — 40m is all-modes above 7040.
    void r3_40m_all() {
        BandMode m;
        QVERIFY(segmentAt(3, 7100000, m));
        QCOMPARE(m, BandMode::All);
    }

    // Whole-band lookups return the expected number of segments.
    void r2_20m_segmentCount() { QCOMPARE(BandPlan::segmentsForBand(2, 14100000).size(), 4); }
    void r2_160m_segmentCount() { QCOMPARE(BandPlan::segmentsForBand(2, 1850000).size(), 2); }

    // Out-of-band / out-of-range → empty.
    void outOfBandEmpty() { QVERIFY(BandPlan::segmentsForBand(2, 5000000).isEmpty()); }
    void aboveAllBandsEmpty() { QVERIFY(BandPlan::segmentsForBand(2, 50000000).isEmpty()); }
    void badRegionEmpty() { QVERIFY(BandPlan::segmentsForBand(0, 14100000).isEmpty()); }
    void badRegionHighEmpty() { QVERIFY(BandPlan::segmentsForBand(5, 14100000).isEmpty()); }

    // Region 2 60m is a single ALL block; a freq inside it resolves to ALL.
    void r2_60m_all() {
        BandMode m;
        QVERIFY(segmentAt(2, 5360000, m));
        QCOMPARE(m, BandMode::All);
    }

    // Labels + colors are defined for every bucket.
    void labels() {
        QCOMPARE(BandPlan::modeLabel(BandMode::CW), QString("CW"));
        QCOMPARE(BandPlan::modeLabel(BandMode::Data), QString("Data"));
        QCOMPARE(BandPlan::modeLabel(BandMode::Phone), QString("Phone"));
        QCOMPARE(BandPlan::modeLabel(BandMode::Beacon), QString("Beacon"));
        QCOMPARE(BandPlan::modeLabel(BandMode::All), QString("All"));
    }
    void colorsValid() {
        QVERIFY(BandPlan::modeColor(BandMode::CW).isValid());
        QVERIFY(BandPlan::modeColor(BandMode::Phone).isValid());
        QVERIFY(BandPlan::modeColor(BandMode::All).isValid());
    }

    // Band name lookup.
    void bandName20m() { QCOMPARE(BandPlan::bandName(14074000), QString("20m")); }
    void bandName40m() { QCOMPARE(BandPlan::bandName(7074000), QString("40m")); }
    void bandNameOutOfBand() { QVERIFY(BandPlan::bandName(5000000).isEmpty()); }

    // US (FCC) region — Extra-class boundaries put FT8 in the DATA block, not phone.
    void us_40m_ft8_isData() {
        BandMode m;
        QVERIFY(segmentAt(BandPlan::RegionUS, 7074000, m));
        QCOMPARE(m, BandMode::Data);
    }
    void us_40m_phoneStart7125() {
        BandMode m;
        QVERIFY(segmentAt(BandPlan::RegionUS, 7130000, m));
        QCOMPARE(m, BandMode::Phone);
        QVERIFY(segmentAt(BandPlan::RegionUS, 7120000, m));
        QCOMPARE(m, BandMode::Data); // just below 7125 is still data
    }
    void us_20m_phoneStart14150() {
        BandMode m;
        QVERIFY(segmentAt(BandPlan::RegionUS, 14074000, m));
        QCOMPARE(m, BandMode::Data); // FT8 on 20m
        QVERIFY(segmentAt(BandPlan::RegionUS, 14200000, m));
        QCOMPARE(m, BandMode::Phone);
    }
    void us_160m_conventionSplit() {
        BandMode m;
        QVERIFY(segmentAt(BandPlan::RegionUS, 1805000, m));
        QCOMPARE(m, BandMode::CW);
        QVERIFY(segmentAt(BandPlan::RegionUS, 1900000, m));
        QCOMPARE(m, BandMode::Phone);
    }

    // US markers exist; IARU regions have none.
    void us_40m_hasMarkers() {
        const auto markers = BandPlan::markersForBand(BandPlan::RegionUS, 7100000);
        QVERIFY(markers.size() >= 2);
        bool hasFt8 = false;
        for (const auto &mk : markers)
            if (mk.name == "FT8" && mk.freqHz == 7074000)
                hasFt8 = true;
        QVERIFY(hasFt8);
    }
    void iaru_noMarkers() { QVERIFY(BandPlan::markersForBand(2, 14074000).isEmpty()); }

    // The panadapter caches the plan on planBandIndex. Region 1 40 m ends at 7200 kHz while the K4's
    // 40 m band button runs to 7300 kHz, so 7250 and 7100 kHz share a K4 band but not a plan: a cache
    // keyed on the K4 band kept the strip empty after tuning from one to the other.
    void r1_40m_planKeySeparatesOutsideFromInside() {
        QCOMPARE(BandPlan::planBandIndex(1, 7250000), -1);
        QVERIFY(BandPlan::planBandIndex(1, 7100000) >= 0);
        QVERIFY(BandPlan::segmentsForBand(1, 7250000).isEmpty());
        QVERIFY(!BandPlan::segmentsForBand(1, 7100000).isEmpty());
    }
    void planKeyOutOfRangeRegion() {
        QCOMPARE(BandPlan::planBandIndex(0, 14100000), -1);
        QCOMPARE(BandPlan::planBandIndex(5, 14100000), -1);
    }
    // Sweep: wherever two frequencies share a plan key, everything the overlay shows is the same, so
    // skipping the push on an unchanged key can never leave a stale strip.
    void planKeyDeterminesTheOverlay() {
        for (int region : {1, 2, 3, BandPlan::RegionUS}) {
            int lastKey = -2;
            QVector<BandPlan::BandSegment> lastSegments;
            QVector<BandPlan::BandMarker> lastMarkers;
            QString lastName;
            for (qint64 f = 1700000; f <= 30000000; f += 500) {
                const int key = BandPlan::planBandIndex(region, f);
                const auto segments = BandPlan::segmentsForBand(region, f);
                const auto markers = BandPlan::markersForBand(region, f);
                const QString name = BandPlan::bandName(f);
                QCOMPARE(key < 0, segments.isEmpty());
                if (key == lastKey) {
                    QCOMPARE(segments.size(), lastSegments.size());
                    for (int i = 0; i < segments.size(); ++i) {
                        QCOMPARE(segments[i].startHz, lastSegments[i].startHz);
                        QCOMPARE(segments[i].endHz, lastSegments[i].endHz);
                    }
                    QCOMPARE(markers.size(), lastMarkers.size());
                    // The band name only shows with segments, so it need only agree inside a plan.
                    if (key >= 0)
                        QCOMPARE(name, lastName);
                }
                lastKey = key;
                lastSegments = segments;
                lastMarkers = markers;
                lastName = name;
            }
        }
    }
};

// GUILESS: the test links Qt6::Gui for QColor but needs no GUI app — using
// QCoreApplication avoids requiring a display/platform plugin in headless CI.
QTEST_GUILESS_MAIN(TestBandPlan)
#include "test_bandplan.moc"
