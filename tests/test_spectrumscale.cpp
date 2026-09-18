#include <QtTest>

#include "dsp/spectrumscale.h"

// Amplitude-axis arithmetic. The bug that prompted these tests — "S1" printed at two different
// heights while adjusting REF — was pure arithmetic reachable only through the UI, so the logic was
// split out and is pinned here.
class TestSpectrumScale : public QObject {
    Q_OBJECT

private slots:
    void convertsSMeterReadingsToDbm() {
        // RadioState encodes the K4's SM bars as S0..S9 directly, and anything stronger as
        // 9.0 + dBoverS9/10 - so S9+20 arrives as 11.0. Treating that as an S-unit would clamp it
        // to S9 and throw away every strong signal.
        QCOMPARE(SpectrumScale::dbmForSMeterReading(9.0), -73.0f);  // S9
        QCOMPARE(SpectrumScale::dbmForSMeterReading(8.0), -79.0f);  // S8, one 6 dB unit down
        QCOMPARE(SpectrumScale::dbmForSMeterReading(1.0), -121.0f); // S1
        QCOMPARE(SpectrumScale::dbmForSMeterReading(0.0), -127.0f); // S0
        QCOMPARE(SpectrumScale::dbmForSMeterReading(11.0), -53.0f); // S9+20
        QCOMPARE(SpectrumScale::dbmForSMeterReading(13.0), -33.0f); // S9+40
    }

    void sMeterConversionIsMonotonic() {
        // A stronger reading must never report a weaker dBm, including across the S9 boundary
        // where the encoding changes shape.
        float previous = -1000.0f;
        for (int tenths = 0; tenths <= 200; ++tenths) {
            const float dbm = SpectrumScale::dbmForSMeterReading(tenths / 10.0);
            QVERIFY2(dbm >= previous, qPrintable(QString::number(tenths / 10.0)));
            previous = dbm;
        }
    }

    // ---- anchors -------------------------------------------------------------------------

    void sUnitDbmMatchesTheAmateurScale() {
        QCOMPARE(SpectrumScale::sUnitDbm(9), -73.0f);
        QCOMPARE(SpectrumScale::sUnitDbm(8), -79.0f);
        QCOMPARE(SpectrumScale::sUnitDbm(1), -121.0f);
    }

    void sUnitDbmClampsOutOfRangeInput() {
        QCOMPARE(SpectrumScale::sUnitDbm(0), SpectrumScale::sUnitDbm(1));
        QCOMPARE(SpectrumScale::sUnitDbm(99), SpectrumScale::sUnitDbm(9));
    }

    void normalizedPlacesFloorAndCeiling() {
        QCOMPARE(SpectrumScale::normalizedForDb(-120.0f, -120.0f, -60.0f), 0.0f);
        QCOMPARE(SpectrumScale::normalizedForDb(-60.0f, -120.0f, -60.0f), 1.0f);
        QCOMPARE(SpectrumScale::normalizedForDb(-90.0f, -120.0f, -60.0f), 0.5f);
    }

    void normalizedLetsStrongBinsExceedTheTop() {
        // Deliberate: the trace climbs to the edge instead of flat-topping.
        QVERIFY(SpectrumScale::normalizedForDb(-50.0f, -120.0f, -60.0f) > 1.0f);
    }

    void normalizedIsSafeOnADegenerateRange() {
        QCOMPARE(SpectrumScale::normalizedForDb(-90.0f, -60.0f, -60.0f), 0.0f);
    }

    // ---- the reported failure ------------------------------------------------------------

    void reportedCaseHasNoDuplicateSUnit() {
        // REF -138, SCALE 70 produced "S8 S7 S5 S4 S3 S1 S1" — S1 at two different heights.
        const auto labels = SpectrumScale::labelsFor(-138.0f, -68.0f, true, 600, 14);
        QVERIFY(!labels.isEmpty());

        QSet<QString> seen;
        for (const auto &l : labels) {
            QVERIFY2(!seen.contains(l.text), qPrintable("duplicate label: " + l.text));
            seen.insert(l.text);
        }
    }

    void labelsNeverFallOutsideTheWindow() {
        // The old code clamped anything below S1 up to "S1"; below -121 dBm nothing may be emitted.
        const auto labels = SpectrumScale::labelsFor(-138.0f, -68.0f, true, 600, 14);
        for (const auto &l : labels) {
            QVERIFY(l.dbm >= -138.0f);
            QVERIFY(l.dbm <= -68.0f);
        }
    }

    // ---- exhaustive over the radio's legal settings ---------------------------------------

    void everyRefAndScaleYieldsUniqueOrderedLabels() {
        // REF -140..10 dBm, SCALE 10..150 dB — the K4's whole space. Proves "S1 in two places"
        // cannot recur at any setting, rather than just at the one Mike happened to find.
        for (int ref = -140; ref <= 10; ++ref) {
            for (int scale = 10; scale <= 150; ++scale) {
                const float lo = static_cast<float>(ref);
                const float hi = static_cast<float>(ref + scale);
                for (bool sUnits : {true, false}) {
                    const auto labels = SpectrumScale::labelsFor(lo, hi, sUnits, 600, 14);
                    QSet<QString> seen;
                    float previous = std::numeric_limits<float>::max();
                    for (const auto &l : labels) {
                        QVERIFY2(l.dbm >= lo && l.dbm <= hi,
                                 qPrintable(QString("label %1 outside [%2,%3]").arg(l.text).arg(lo).arg(hi)));
                        QVERIFY2(
                            !seen.contains(l.text),
                            qPrintable(QString("duplicate %1 at ref %2 scale %3").arg(l.text).arg(ref).arg(scale)));
                        QVERIFY2(
                            l.dbm < previous,
                            qPrintable(QString("out of order %1 at ref %2 scale %3").arg(l.text).arg(ref).arg(scale)));
                        seen.insert(l.text);
                        previous = l.dbm;
                    }
                }
            }
        }
    }

    // ---- decimation ----------------------------------------------------------------------

    void labelsNeverOverlapHoweverShortTheChart() {
        const float lo = -140.0f, hi = 10.0f; // the widest window the radio allows
        for (int px : {800, 600, 400, 200, 120, 60}) {
            const int labelPx = 14;
            const auto labels = SpectrumScale::labelsFor(lo, hi, true, px, labelPx);
            for (int i = 1; i < labels.size(); ++i) {
                const float dbGap = labels[i - 1].dbm - labels[i].dbm;
                const float pxGap = dbGap / (hi - lo) * px;
                QVERIFY2(pxGap >= labelPx - 1, qPrintable(QString("labels %1/%2 only %3 px apart at %4 px tall")
                                                              .arg(labels[i - 1].text)
                                                              .arg(labels[i].text)
                                                              .arg(pxGap)
                                                              .arg(px)));
            }
        }
    }

    void aTallerChartShowsAtLeastAsManyLabels() {
        const auto few = SpectrumScale::labelsFor(-140.0f, 10.0f, true, 120, 14);
        const auto many = SpectrumScale::labelsFor(-140.0f, 10.0f, true, 800, 14);
        QVERIFY(many.size() >= few.size());
    }

    // ---- conventions ---------------------------------------------------------------------

    void aboveS9UsesRoundDecades() {
        const auto labels = SpectrumScale::labelsFor(-100.0f, -30.0f, true, 600, 14);
        bool sawPlus = false;
        for (const auto &l : labels) {
            if (!l.text.startsWith("S9+"))
                continue;
            sawPlus = true;
            const int over = l.text.mid(3).toInt();
            QVERIFY2(over % 10 == 0, qPrintable("not a round decade: " + l.text));
            QCOMPARE(l.dbm, SpectrumScale::S9_DBM + static_cast<float>(over));
        }
        QVERIFY(sawPlus);
    }

    void dbmModeUsesRoundValues() {
        // The old code labelled eighths of the window, producing values like -129.75.
        const auto labels = SpectrumScale::labelsFor(-138.0f, -68.0f, false, 600, 14);
        QVERIFY(!labels.isEmpty());
        for (const auto &l : labels)
            QCOMPARE(l.dbm, std::round(l.dbm));
    }

    void anEmptyOrInvertedWindowYieldsNothing() {
        QVERIFY(SpectrumScale::labelsFor(-60.0f, -120.0f, true, 600, 14).isEmpty());
        QVERIFY(SpectrumScale::labelsFor(-60.0f, -60.0f, true, 600, 14).isEmpty());
    }
};

QTEST_MAIN(TestSpectrumScale)
#include "test_spectrumscale.moc"
