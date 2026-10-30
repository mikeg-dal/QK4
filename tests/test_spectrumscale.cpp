#include <QtTest>

#include <algorithm>

#include "dsp/spectrumscale.h"

// Amplitude-axis arithmetic. The bug that prompted these tests — "S1" printed at two different
// heights while adjusting REF — was pure arithmetic reachable only through the UI, so the logic was
// split out and is pinned here.
class TestSpectrumScale : public QObject {
    Q_OBJECT

private slots:
    // K4_DBM_OFFSET pinned to the bench pairs it was measured from (2026-10-30, WWV 10 MHz carrier,
    // peak PAN byte vs the radio's own SMH dBm at the same moment, 48 and 96 kHz tiers). Every pair
    // must come out within 1.5 dB of the radio's reading; 146 missed them all by about 16 dB.
    void panByteOffsetMatchesTheRadiosDbmMeter() {
        struct Pair {
            int peakByte;
            int smhDbm;
        };
        const Pair pairs[] = {{95, -66},  {107, -54}, {95, -67},  {106, -55}, {106, -56},  // 48 kHz tier
                              {113, -50}, {111, -52}, {113, -50}, {111, -52}, {103, -59}}; // 96 kHz tier
        for (const Pair &p : pairs) {
            const float dbm = static_cast<float>(p.peakByte) - SpectrumScale::K4_DBM_OFFSET;
            QVERIFY2(std::abs(dbm - static_cast<float>(p.smhDbm)) <= 1.5f,
                     qPrintable(QStringLiteral("byte %1 -> %2 dBm, radio %3").arg(p.peakByte).arg(dbm).arg(p.smhDbm)));
        }
    }

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

    // ---- readable spacing ----------------------------------------------------------------

    void aShortPaneLabelsEveryTenDbNotEveryFive() {
        // The reported case: a ~300 px spectrum area at SCALE 75 printed -55, -60 … -130 dBm,
        // sixteen labels touching each other. Line spacing 11 px is the 9 px Inter label font.
        const auto labels = SpectrumScale::labelsFor(-130.0f, -55.0f, false, 300, 11.0f);
        QVERIFY(labels.size() >= 2);
        QVERIFY(labels.size() <= 9);
        QCOMPARE(labels[0].dbm - labels[1].dbm, 10.0f);

        // Giving most of the pane to the waterfall leaves ~180 px: the step widens again.
        const auto shorter = SpectrumScale::labelsFor(-130.0f, -55.0f, false, 180, 11.0f);
        QVERIFY(shorter.size() >= 2);
        QCOMPARE(shorter[0].dbm - shorter[1].dbm, 20.0f);
    }

    void everySettingKeepsLabelsAReadableDistanceApart() {
        // REF and SCALE over the radio's whole range, every chart height from a near-hidden
        // spectrum to a tall one, and label fonts from the smallest to a large interface scale.
        static constexpr int kNiceSteps[] = {1, 2, 5, 10, 20, 25, 50, 100};
        for (int ref = -200; ref <= 50; ref += 5) {
            for (int scale = 10; scale <= 150; scale += 5) {
                const float lo = static_cast<float>(ref);
                const float hi = static_cast<float>(ref + scale);
                for (int px : {60, 120, 180, 240, 300, 400, 600, 900, 1400}) {
                    for (float line : {9.0f, 11.0f, 14.0f, 18.0f}) {
                        const float minPitch = SpectrumScale::kLabelPitchLines * line - 0.5f;
                        for (bool sUnits : {true, false}) {
                            const auto labels = SpectrumScale::labelsFor(lo, hi, sUnits, px, line);
                            for (int i = 1; i < labels.size(); ++i) {
                                const float pitch = (labels[i - 1].dbm - labels[i].dbm) / (hi - lo) * px;
                                QVERIFY2(pitch >= minPitch,
                                         qPrintable(QString("%1/%2 %3 px apart (min %4) ref %5 scale %6 px %7 line %8")
                                                        .arg(labels[i - 1].text, labels[i].text)
                                                        .arg(pitch)
                                                        .arg(minPitch)
                                                        .arg(ref)
                                                        .arg(scale)
                                                        .arg(px)
                                                        .arg(line)));
                            }
                            if (sUnits || labels.size() < 2)
                                continue;
                            const int step = static_cast<int>(labels[0].dbm - labels[1].dbm);
                            QVERIFY2(std::find(std::begin(kNiceSteps), std::end(kNiceSteps), step) !=
                                             std::end(kNiceSteps) ||
                                         step % 100 == 0,
                                     qPrintable(QString("step %1 dB is not a round value").arg(step)));
                            for (const auto &l : labels)
                                QCOMPARE(static_cast<int>(l.dbm) % step, 0);
                        }
                    }
                }
            }
        }
    }

    void theStepIsTheSmallestThatStaysReadable() {
        // Not just "far enough apart": one notch finer would have been too close. Pins that the
        // axis thins only as much as it must.
        static constexpr int kNiceSteps[] = {1, 2, 5, 10, 20, 25, 50, 100};
        for (int scale = 10; scale <= 150; scale += 5) {
            for (int px : {120, 300, 600, 1400}) {
                const float lo = -140.0f, hi = lo + static_cast<float>(scale);
                const auto labels = SpectrumScale::labelsFor(lo, hi, false, px, 11.0f);
                if (labels.size() < 2)
                    continue;
                const int step = static_cast<int>(labels[0].dbm - labels[1].dbm);
                const auto it = std::find(std::begin(kNiceSteps), std::end(kNiceSteps), step);
                if (it == std::end(kNiceSteps) || it == std::begin(kNiceSteps))
                    continue;
                const float finerPitch = static_cast<float>(*(it - 1)) / static_cast<float>(scale) * px;
                QVERIFY2(finerPitch < SpectrumScale::kLabelPitchLines * 11.0f,
                         qPrintable(QString("scale %1 px %2 chose %3 dB but %4 dB was already readable")
                                        .arg(scale)
                                        .arg(px)
                                        .arg(step)
                                        .arg(*(it - 1))));
            }
        }
    }

    void aTallerPaneNeverShowsFewerLabels() {
        for (int scale = 10; scale <= 150; scale += 10) {
            for (bool sUnits : {true, false}) {
                int previous = 0;
                for (int px = 60; px <= 1400; px += 20) {
                    const int count =
                        SpectrumScale::labelsFor(-140.0f, -140.0f + static_cast<float>(scale), sUnits, px, 11.0f)
                            .size();
                    QVERIFY2(count >= previous, qPrintable(QString("scale %1: %2 labels at %3 px after %4")
                                                               .arg(scale)
                                                               .arg(count)
                                                               .arg(px)
                                                               .arg(previous)));
                    previous = count;
                }
            }
        }
    }
};

QTEST_MAIN(TestSpectrumScale)
#include "test_spectrumscale.moc"
