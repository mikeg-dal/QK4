#include <QtTest>

#include <algorithm>

#include "dsp/spectrumscale.h"
#include "dsp/waterfallgeometry.h"

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

    // K4_MINIPAN_DBM_OFFSET pinned to its bench pairs (2026-10-30, WWV 10 MHz in CW, 3 kHz mini-pan,
    // peak MiniPAN byte vs SMH within 30 ms). WWV was fading, so single pairs scatter over 13 dB: the
    // median must sit within 2 dB of the radio and every pair within 8 dB. byte / 10, the old reading,
    // misses every pair by over 50 dB.
    void miniPanByteOffsetMatchesTheRadiosDbmMeter() {
        struct Pair {
            int peakByte;
            int smhDbm;
        };
        const Pair pairs[] = {{53, -66}, {65, -54}, {55, -67}, {65, -55}, {64, -56},  // capture 1
                              {60, -50}, {70, -52}, {64, -50}, {71, -52}, {63, -59}}; // capture 2
        QVector<float> errors;
        for (const Pair &p : pairs) {
            const float dbm = static_cast<float>(p.peakByte) - SpectrumScale::K4_MINIPAN_DBM_OFFSET;
            const float error = dbm - static_cast<float>(p.smhDbm);
            QVERIFY2(std::abs(error) <= 8.0f,
                     qPrintable(QStringLiteral("byte %1 -> %2 dBm, radio %3").arg(p.peakByte).arg(dbm).arg(p.smhDbm)));
            errors.append(error);
        }
        std::sort(errors.begin(), errors.end());
        const float median = (errors[4] + errors[5]) / 2.0f;
        QVERIFY2(std::abs(median) <= 2.0f, qPrintable(QStringLiteral("median error %1 dB").arg(median)));
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

    // The radio's own SMH reading wins whenever it has sent one; the bar estimate only fills the gap
    // before the first SMH. On a K4D the bars (SM04) gave -115 dBm where SMH said -110.
    void rxLevelPrefersTheRadiosDbm() {
        QCOMPARE(SpectrumScale::rxLevelDbm(-110, 2.0), -110.0f);
        QCOMPARE(SpectrumScale::rxLevelDbm(5, 0.0), 5.0f);
        QCOMPARE(SpectrumScale::rxLevelDbm(-127, 13.0), -127.0f); // never blended with the bars
    }

    void rxLevelFallsBackToTheBarsUntilSmhArrives() {
        QCOMPARE(SpectrumScale::rxLevelDbm(std::nullopt, 9.0), SpectrumScale::dbmForSMeterReading(9.0));
        QCOMPARE(SpectrumScale::rxLevelDbm(std::nullopt, 2.0), -115.0f); // SM04
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
    // ---- REF across two resolution bandwidths -------------------------------------------------

    // A bin's level is power in that bin, so it follows the bin's width: the ratio in dB is
    // 10*log10, not 20*log10 and not the ratio itself.
    void refOffsetIsTenLogTheBinWidthRatio() {
        using SpectrumScale::refOffsetForBinWidth;
        // Same width: REF already means what it says, so nothing moves.
        QCOMPARE(refOffsetForBinWidth(187.5, 187.5), 0.0f);
        // The bench case: 12 kHz over 1024 bins against a 192 kHz tier over 1024. Ratio 16.
        QVERIFY(std::abs(refOffsetForBinWidth(12000.0 / 1024, 192000.0 / 1024) - -12.041f) < 0.01f);
        // Halving the bin width is -3 dB, doubling it +3, decades are 10.
        QVERIFY(std::abs(refOffsetForBinWidth(50.0, 100.0) - -3.0103f) < 0.001f);
        QVERIFY(std::abs(refOffsetForBinWidth(100.0, 50.0) - 3.0103f) < 0.001f);
        QVERIFY(std::abs(refOffsetForBinWidth(10.0, 100.0) - -10.0f) < 0.001f);
        // Narrower bins always LOWER the window and wider ones raise it. The sign being backwards
        // would darken the display it was written to brighten.
        QVERIFY(refOffsetForBinWidth(11.7, 187.5) < 0.0f);
        QVERIFY(refOffsetForBinWidth(187.5, 11.7) > 0.0f);
    }

    // An unknown width leaves the display exactly as it was, and no width can slide the window far
    // enough to blank it.
    void refOffsetIsInertWhenAWidthIsUnknownAndIsBounded() {
        using SpectrumScale::refOffsetForBinWidth;
        QCOMPARE(refOffsetForBinWidth(0.0, 187.5), 0.0f);  // before the first mini-pan frame
        QCOMPARE(refOffsetForBinWidth(11.7, 0.0), 0.0f);   // before the first PAN frame
        QCOMPARE(refOffsetForBinWidth(0.0, 0.0), 0.0f);    // at construction
        QCOMPARE(refOffsetForBinWidth(-5.0, 187.5), 0.0f); // nonsense in, nothing out
        QCOMPARE(refOffsetForBinWidth(11.7, -1.0), 0.0f);
        const auto bounded = static_cast<float>(SpectrumScale::MaxRefOffsetDb);
        QCOMPARE(refOffsetForBinWidth(1.0, 1.0e9), -bounded);
        QCOMPARE(refOffsetForBinWidth(1.0e9, 1.0), bounded);
    }

    // The bench case this was written for, end to end through the colour window (K4D SN1641, 40 m
    // SSB, 2026-11-26). Uncorrected, the mini-pan's noise median landed half a dB BELOW the bottom
    // of the window and the whole floor drew black while an S9+ voice peak reached two thirds of
    // the palette. Corrected, the noise sits about where the main pan's noise sits and the peak
    // reaches the top. Every number here came off the qk4.spectrum.diag MINIPAN line, including
    // both bin widths - the main tier was READ, not inferred from the span.
    void miniPanNoiseLandsWhereTheMainPansDoesOnceRefIsCorrected() {
        constexpr int kRef = -101;                  // AutoRef, so the MAIN pan's noise is here
        constexpr int kScale = 51;                  // SCALE
        constexpr int kWbs = 9;                     // #WBS
        constexpr float kNoise = -113.0f;           // mini-pan median, measured
        constexpr float kPeak = -78.0f;             // mini-pan max on an S9+ voice peak, measured
        const double miniHzPerBin = 12000.0 / 1024; // 11.7
        const double mainHzPerBin = 375.0;          // a 384 kHz tier over 1024 bins

        // Where the main pan puts its own noise floor: that is the target.
        const WaterfallGeometry::ColorWindow mainWindow =
            WaterfallGeometry::colorWindowFor(static_cast<float>(kRef), kScale, kWbs);
        const float mainNoise = WaterfallGeometry::paletteFractionFor(static_cast<float>(kRef), mainWindow);
        QVERIFY(mainNoise > 0.15f && mainNoise < 0.30f); // visibly blue, not black

        // Before: the mini-pan borrows REF untouched and its noise falls out of the bottom.
        const WaterfallGeometry::ColorWindow before =
            WaterfallGeometry::colorWindowFor(static_cast<float>(kRef), kScale, kWbs);
        QCOMPARE(WaterfallGeometry::paletteFractionFor(kNoise, before), 0.0f);
        QVERIFY(WaterfallGeometry::paletteFractionFor(kPeak, before) < 0.75f); // S9+ only two thirds up

        // After: the same REF, corrected for the bin-width gap. 32x the bin width is 15.05 dB.
        const float offset = SpectrumScale::refOffsetForBinWidth(miniHzPerBin, mainHzPerBin);
        QVERIFY(std::abs(offset - -15.05f) < 0.01f);
        const WaterfallGeometry::ColorWindow after =
            WaterfallGeometry::colorWindowFor(static_cast<float>(kRef) + offset, kScale, kWbs);
        const float noiseAfter = WaterfallGeometry::paletteFractionFor(kNoise, after);
        QVERIFY(noiseAfter > 0.20f); // out of the black, which is the whole point

        // Within about 4 dB of where the main pan puts its own noise - not closer, and the gap is
        // expected rather than slop: the radio clips a quarter to a third of the mini-pan's bins at
        // byte 0, which truncates the bottom of the distribution and biases the measured median
        // upward. Tightening this below the clipping bias would pin noise, not arithmetic.
        QVERIFY2(std::abs(noiseAfter - mainNoise) < 0.08f,
                 qPrintable(QStringLiteral("mini-pan noise at %1, main pan at %2").arg(noiseAfter).arg(mainNoise)));
        QVERIFY(WaterfallGeometry::paletteFractionFor(kPeak, after) > 0.88f); // S9+ near the top

        // That clipped tail carries no information, but it must still be drawn as dim colour rather
        // than as the palette's "no data" black, or the correction has only moved the problem.
        QVERIFY(WaterfallGeometry::paletteFractionFor(-118.0f, after) > 0.05f);
    }
};

QTEST_MAIN(TestSpectrumScale)
#include "test_spectrumscale.moc"
