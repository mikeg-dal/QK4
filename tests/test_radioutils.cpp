#include <QTest>
#include "utils/radioutils.h"

namespace {
constexpr RadioUtils::FilterEdgeLimits kSsbLimits{5, 500, 30, 300, false};
int loDah(RadioUtils::FilterBwIs f) {
    return qMax(0, f.isDah - f.bwDah / 2);
}
int hiDah(RadioUtils::FilterBwIs f) {
    return loDah(f) + f.bwDah;
}
} // namespace

class TestRadioUtils : public QObject {
    Q_OBJECT

private slots:
    // tuningStepToHz
    void testTuningStep_0() { QCOMPARE(RadioUtils::tuningStepToHz(0), 1); }
    void testTuningStep_1() { QCOMPARE(RadioUtils::tuningStepToHz(1), 10); }
    void testTuningStep_2() { QCOMPARE(RadioUtils::tuningStepToHz(2), 100); }
    void testTuningStep_3() { QCOMPARE(RadioUtils::tuningStepToHz(3), 1000); }
    void testTuningStep_4() { QCOMPARE(RadioUtils::tuningStepToHz(4), 10000); }
    void testTuningStep_5() { QCOMPARE(RadioUtils::tuningStepToHz(5), 100); }
    void testTuningStep_outOfRange_negative() { QCOMPARE(RadioUtils::tuningStepToHz(-1), 1000); }
    void testTuningStep_outOfRange_high() { QCOMPARE(RadioUtils::tuningStepToHz(6), 1000); }

    // tuningStepForDigit
    void testDigitStep_1HzTo10kHz() {
        for (int digit = 0; digit <= 4; ++digit)
            QCOMPARE(RadioUtils::tuningStepForDigit(digit), digit);
    }
    void testDigitStep_eachDigitMatchesItsStepSize() {
        int hz = 1;
        for (int digit = 0; digit <= 4; ++digit, hz *= 10)
            QCOMPARE(RadioUtils::tuningStepToHz(RadioUtils::tuningStepForDigit(digit)), hz);
    }
    void testDigitStep_100kHzAndAboveHaveNoStep() {
        for (int digit = 5; digit <= 9; ++digit)
            QCOMPARE(RadioUtils::tuningStepForDigit(digit), -1);
    }
    void testDigitStep_negativeHasNoStep() { QCOMPARE(RadioUtils::tuningStepForDigit(-1), -1); }

    // getBandFromFrequency
    void testBand_160m() { QCOMPARE(RadioUtils::getBandFromFrequency(1900000), 0); }
    void testBand_80m() { QCOMPARE(RadioUtils::getBandFromFrequency(3573000), 1); }
    void testBand_60m() { QCOMPARE(RadioUtils::getBandFromFrequency(5357000), 2); }
    void testBand_40m() { QCOMPARE(RadioUtils::getBandFromFrequency(7074000), 3); }
    void testBand_30m() { QCOMPARE(RadioUtils::getBandFromFrequency(10136000), 4); }
    void testBand_20m() { QCOMPARE(RadioUtils::getBandFromFrequency(14074000), 5); }
    void testBand_17m() { QCOMPARE(RadioUtils::getBandFromFrequency(18100000), 6); }
    void testBand_15m() { QCOMPARE(RadioUtils::getBandFromFrequency(21074000), 7); }
    void testBand_12m() { QCOMPARE(RadioUtils::getBandFromFrequency(24915000), 8); }
    void testBand_10m() { QCOMPARE(RadioUtils::getBandFromFrequency(28074000), 9); }
    void testBand_6m() { QCOMPARE(RadioUtils::getBandFromFrequency(50313000), 10); }
    void testBand_xvtr() { QCOMPARE(RadioUtils::getBandFromFrequency(144174000), 16); }
    void testBand_outOfBand() { QCOMPARE(RadioUtils::getBandFromFrequency(100000), -1); }
    void testBand_gapBetweenBands() { QCOMPARE(RadioUtils::getBandFromFrequency(8000000), -1); }
    void testBand_lowerEdge_20m() { QCOMPARE(RadioUtils::getBandFromFrequency(14000000), 5); }
    void testBand_upperEdge_20m() { QCOMPARE(RadioUtils::getBandFromFrequency(14350000), 5); }
    void testBand_justAbove_20m() { QCOMPARE(RadioUtils::getBandFromFrequency(14350001), -1); }

    // getNextSpanUp
    void testSpanUp_min() { QCOMPARE(RadioUtils::getNextSpanUp(5000), 6000); }
    void testSpanUp_belowThreshold() { QCOMPARE(RadioUtils::getNextSpanUp(100000), 101000); }
    void testSpanUp_atThreshold() { QCOMPARE(RadioUtils::getNextSpanUp(144000), 148000); }
    void testSpanUp_aboveThreshold() { QCOMPARE(RadioUtils::getNextSpanUp(200000), 204000); }
    void testSpanUp_atMax() { QCOMPARE(RadioUtils::getNextSpanUp(368000), 368000); }
    void testSpanUp_nearMax() { QCOMPARE(RadioUtils::getNextSpanUp(366000), 368000); }

    // getNextSpanDown
    void testSpanDown_atMin() { QCOMPARE(RadioUtils::getNextSpanDown(5000), 5000); }
    void testSpanDown_nearMin() { QCOMPARE(RadioUtils::getNextSpanDown(6000), 5000); }
    void testSpanDown_belowThreshold() { QCOMPARE(RadioUtils::getNextSpanDown(100000), 99000); }
    void testSpanDown_aboveThreshold() { QCOMPARE(RadioUtils::getNextSpanDown(200000), 196000); }
    void testSpanDown_atThreshold() { QCOMPARE(RadioUtils::getNextSpanDown(140000), 139000); }
    void testSpanDown_justAboveThreshold() { QCOMPARE(RadioUtils::getNextSpanDown(141000), 137000); }

    // spanAfterZoom
    void testZoomIn_narrowsTheSpan() { QCOMPARE(RadioUtils::spanAfterZoom(100000, true), 99000); }
    void testZoomOut_widensTheSpan() { QCOMPARE(RadioUtils::spanAfterZoom(100000, false), 101000); }
    void testZoomIn_stopsAtMinimum() {
        QCOMPARE(RadioUtils::spanAfterZoom(RadioUtils::SPAN_MIN, true), RadioUtils::SPAN_MIN);
    }
    void testZoomOut_stopsAtMaximum() {
        QCOMPARE(RadioUtils::spanAfterZoom(RadioUtils::SPAN_MAX, false), RadioUtils::SPAN_MAX);
    }
    void testZoom_followsTheK4StepsAcrossTheTierThreshold() {
        for (int span : {139000, 140000, 141000, 144000, 148000}) {
            QCOMPARE(RadioUtils::spanAfterZoom(span, true), RadioUtils::getNextSpanDown(span));
            QCOMPARE(RadioUtils::spanAfterZoom(span, false), RadioUtils::getNextSpanUp(span));
        }
    }

    // nextDdcNoiseBlankerMode - DISPLAY NB key: OFF -> ON -> AUTO -> OFF
    void testDdcNbCycle() {
        QCOMPARE(RadioUtils::nextDdcNoiseBlankerMode(0), 1);
        QCOMPARE(RadioUtils::nextDdcNoiseBlankerMode(1), 2);
        QCOMPARE(RadioUtils::nextDdcNoiseBlankerMode(2), 0);
        QCOMPARE(RadioUtils::nextDdcNoiseBlankerMode(-1), 1); // not reported yet: taken as OFF
    }

    // nextFilterPreset
    void testFilterPreset_1GoesTo3() { QCOMPARE(RadioUtils::nextFilterPreset(1), 3); }
    void testFilterPreset_3GoesTo2() { QCOMPARE(RadioUtils::nextFilterPreset(3), 2); }
    void testFilterPreset_2GoesTo1() { QCOMPARE(RadioUtils::nextFilterPreset(2), 1); }
    void testFilterPreset_threeClicksComeBackToTheStart() {
        for (int start = 1; start <= 3; ++start) {
            int preset = start;
            for (int click = 0; click < 3; ++click)
                preset = RadioUtils::nextFilterPreset(preset);
            QCOMPARE(preset, start);
        }
    }
    void testFilterPreset_unknownHasNoNext() {
        QCOMPARE(RadioUtils::nextFilterPreset(-1), -1);
        QCOMPARE(RadioUtils::nextFilterPreset(0), -1);
        QCOMPARE(RadioUtils::nextFilterPreset(4), -1);
    }

    // filterAfterEdgeStep — HI/LO scroll keeps the opposite edge fixed
    void testEdgeStep_hiMovesHiOnly() {
        const auto next = RadioUtils::filterAfterEdgeStep({240, 150}, true, 1, kSsbLimits);
        QVERIFY(next);
        QCOMPARE(next->bwDah, 242);
        QCOMPARE(loDah(*next), 30);
        QCOMPARE(hiDah(*next), 272);
    }
    void testEdgeStep_hiAtMaxBandwidthIsIgnored() {
        // BW already at its limit: the old code still re-centred IS, dragging LO up.
        QVERIFY(!RadioUtils::filterAfterEdgeStep({500, 270}, true, 1, kSsbLimits));
    }
    void testEdgeStep_loPastMaxBandwidthKeepsHiFixed() {
        const RadioUtils::FilterBwIs current{498, 270};
        const auto next = RadioUtils::filterAfterEdgeStep(current, false, -2, kSsbLimits);
        QVERIFY(next);
        QCOMPARE(next->bwDah, 500);
        QCOMPARE(hiDah(*next), hiDah(current));
        QCOMPARE(loDah(*next), 19);
    }
    void testEdgeStep_loPastMinBandwidthKeepsHiFixed() {
        const RadioUtils::FilterBwIs current{6, 100};
        const auto next = RadioUtils::filterAfterEdgeStep(current, false, 1, kSsbLimits);
        QVERIFY(next);
        QCOMPARE(next->bwDah, 5);
        QCOMPARE(hiDah(*next), hiDah(current));
    }
    void testEdgeStep_isLimitWouldMoveFixedEdgeIsIgnored() {
        const auto ok = RadioUtils::filterAfterEdgeStep({40, 35}, true, -5, kSsbLimits);
        QVERIFY(ok);
        QCOMPARE(ok->isDah, 30);
        QCOMPARE(loDah(*ok), 15);
        // One more step needs IS 29: clamping it to 30 would move LO, so the tick is dropped.
        QVERIFY(!RadioUtils::filterAfterEdgeStep({40, 35}, true, -6, kSsbLimits));
    }
    void testEdgeStep_lockedShiftChangesBandwidthOnly() {
        const auto next = RadioUtils::filterAfterEdgeStep({50, 100}, true, 1, {5, 80, 30, 300, true});
        QVERIFY(next);
        QCOMPARE(next->bwDah, 52);
        QCOMPARE(next->isDah, 100);
    }
    void testEdgeStep_sweepFixedEdgeNeverMoves() {
        for (int bw = 5; bw <= 500; bw += 7) {
            for (int is = 30; is <= 300; is += 11) {
                for (int steps : {-25, -3, -1, 1, 3, 25}) {
                    for (bool adjustHi : {true, false}) {
                        const RadioUtils::FilterBwIs current{bw, is};
                        const auto next = RadioUtils::filterAfterEdgeStep(current, adjustHi, steps, kSsbLimits);
                        if (!next)
                            continue;
                        QVERIFY(next->bwDah >= 5 && next->bwDah <= 500);
                        QVERIFY(next->isDah >= 30 && next->isDah <= 300);
                        if (adjustHi)
                            QCOMPARE(loDah(*next), loDah(current));
                        else
                            QCOMPARE(hiDah(*next), hiDah(current));
                    }
                }
            }
        }
    }

    // agcLabel / agcActive
    void testAgcLabel() {
        QCOMPARE(RadioUtils::agcLabel(0), QString("AGC"));
        QCOMPARE(RadioUtils::agcLabel(1), QString("AGC-S"));
        QCOMPARE(RadioUtils::agcLabel(2), QString("AGC-F"));
        QCOMPARE(RadioUtils::agcLabel(-1), QString("AGC"));
    }
    void testAgcOffIsNotActive() {
        // The VFO indicator used to parse its own text and treated plain "AGC" (off) as active.
        QVERIFY(!RadioUtils::agcActive(0));
        QVERIFY(RadioUtils::agcActive(1));
        QVERIFY(RadioUtils::agcActive(2));
        QVERIFY(!RadioUtils::agcActive(-1));
    }
    void testTxBandwidthLabel() {
        QCOMPARE(RadioUtils::txBandwidthLabel(28), QString("2.8k"));
        QCOMPARE(RadioUtils::txBandwidthLabel(45), QString("4.5k"));
    }

    // buildEqCommand
    void testBuildEqCommand_flat() {
        QVector<int> flat(8, 0);
        QCOMPARE(RadioUtils::buildEqCommand("RE", flat), QString("RE+00+00+00+00+00+00+00+00"));
    }

    void testBuildEqCommand_mixed() {
        QVector<int> bands = {-16, -5, 0, 3, 8, 12, -1, 16};
        QCOMPARE(RadioUtils::buildEqCommand("TE", bands), QString("TE-16-05+00+03+08+12-01+16"));
    }

    void testBuildEqCommand_txPrefix() {
        QVector<int> bands(8, 5);
        QCOMPARE(RadioUtils::buildEqCommand("TE", bands), QString("TE+05+05+05+05+05+05+05+05"));
    }

    // Constants
    void testConstants() {
        QCOMPARE(RadioUtils::SPAN_MIN, 5000);
        QCOMPARE(RadioUtils::SPAN_MAX, 368000);
        QVERIFY(RadioUtils::SPAN_THRESHOLD_UP > RadioUtils::SPAN_THRESHOLD_DOWN);
    }

    // slTierToFrameSamples — verified via pcap at all SL tiers
    void testSlTier_SL0() { QCOMPARE(RadioUtils::slTierToFrameSamples(0), 240); }
    void testSlTier_SL1() { QCOMPARE(RadioUtils::slTierToFrameSamples(1), 480); }
    void testSlTier_SL2() { QCOMPARE(RadioUtils::slTierToFrameSamples(2), 480); }
    void testSlTier_SL3() { QCOMPARE(RadioUtils::slTierToFrameSamples(3), 720); }
    void testSlTier_SL4() { QCOMPARE(RadioUtils::slTierToFrameSamples(4), 720); }
    void testSlTier_SL5() { QCOMPARE(RadioUtils::slTierToFrameSamples(5), 720); }
    void testSlTier_SL6() { QCOMPARE(RadioUtils::slTierToFrameSamples(6), 1440); }
    void testSlTier_SL7() { QCOMPARE(RadioUtils::slTierToFrameSamples(7), 1440); }
    void testSlTier_outOfRange_negative() { QCOMPARE(RadioUtils::slTierToFrameSamples(-1), 720); }
    void testSlTier_outOfRange_high() { QCOMPARE(RadioUtils::slTierToFrameSamples(8), 720); }

    // fixedTuneModeFromCat — verified against hardware + K4 command reference 2026-05-28
    // FXT0 = Track (FXA ignored); FXT1: 0=Fixed1,1=Fixed2,2=Slide1,3=Static,4=Slide2
    void testFixedTune_track_fxt0() {
        QVERIFY(RadioUtils::fixedTuneModeFromCat(0, 0) == RadioUtils::FixedTuneMode::Track);
        QVERIFY(RadioUtils::fixedTuneModeFromCat(0, 4) == RadioUtils::FixedTuneMode::Track); // FXA ignored
    }
    void testFixedTune_fixed1_fxa0() {
        QVERIFY(RadioUtils::fixedTuneModeFromCat(1, 0) == RadioUtils::FixedTuneMode::Fixed1);
    }
    void testFixedTune_fixed2_fxa1() {
        QVERIFY(RadioUtils::fixedTuneModeFromCat(1, 1) == RadioUtils::FixedTuneMode::Fixed2);
    }
    void testFixedTune_slide1_fxa2() {
        QVERIFY(RadioUtils::fixedTuneModeFromCat(1, 2) == RadioUtils::FixedTuneMode::Slide1);
    }
    void testFixedTune_static_fxa3() {
        QVERIFY(RadioUtils::fixedTuneModeFromCat(1, 3) == RadioUtils::FixedTuneMode::Static);
    }
    void testFixedTune_slide2_fxa4() {
        QVERIFY(RadioUtils::fixedTuneModeFromCat(1, 4) == RadioUtils::FixedTuneMode::Slide2);
    }
    void testFixedTune_unknownFxa_fallsBackToTrack() {
        QVERIFY(RadioUtils::fixedTuneModeFromCat(1, 9) == RadioUtils::FixedTuneMode::Track);
    }

    // fixedTuneSetCommand — exact SET strings (FXA before FXT; Track sends only FXT0)
    void testFixedTuneCmd_track() {
        QCOMPARE(RadioUtils::fixedTuneSetCommand(RadioUtils::FixedTuneMode::Track), QString("#FXT0;"));
    }
    void testFixedTuneCmd_fixed1() {
        QCOMPARE(RadioUtils::fixedTuneSetCommand(RadioUtils::FixedTuneMode::Fixed1), QString("#FXA0;#FXT1;"));
    }
    void testFixedTuneCmd_fixed2() {
        QCOMPARE(RadioUtils::fixedTuneSetCommand(RadioUtils::FixedTuneMode::Fixed2), QString("#FXA1;#FXT1;"));
    }
    void testFixedTuneCmd_slide1() {
        QCOMPARE(RadioUtils::fixedTuneSetCommand(RadioUtils::FixedTuneMode::Slide1), QString("#FXA2;#FXT1;"));
    }
    void testFixedTuneCmd_static() {
        QCOMPARE(RadioUtils::fixedTuneSetCommand(RadioUtils::FixedTuneMode::Static), QString("#FXA3;#FXT1;"));
    }
    void testFixedTuneCmd_slide2() {
        QCOMPARE(RadioUtils::fixedTuneSetCommand(RadioUtils::FixedTuneMode::Slide2), QString("#FXA4;#FXT1;"));
    }

    // Round-trip: every fixed mode's SET command re-decodes to the same mode.
    void testFixedTune_roundTrip() {
        const RadioUtils::FixedTuneMode modes[] = {RadioUtils::FixedTuneMode::Fixed1, RadioUtils::FixedTuneMode::Fixed2,
                                                   RadioUtils::FixedTuneMode::Slide1, RadioUtils::FixedTuneMode::Static,
                                                   RadioUtils::FixedTuneMode::Slide2};
        for (auto mode : modes) {
            QString cmd = RadioUtils::fixedTuneSetCommand(mode); // e.g. "#FXA2;#FXT1;"
            int fxa = cmd.mid(4, 1).toInt();                     // digit after "#FXA"
            QVERIFY(RadioUtils::fixedTuneModeFromCat(1, fxa) == mode);
        }
    }

    // snapFreqToStep — zero sub-step digits (KPOD/scroll/drag land on step boundary)
    void testSnap_artifact100Hz() { QCOMPARE(RadioUtils::snapFreqToStep(7277380, 100), Q_INT64_C(7277300)); }
    void testSnap_alreadyAligned() { QCOMPARE(RadioUtils::snapFreqToStep(7277400, 100), Q_INT64_C(7277400)); }
    void testSnap_1HzStepUnchanged() { QCOMPARE(RadioUtils::snapFreqToStep(7277380, 1), Q_INT64_C(7277380)); }
    void testSnap_1kHzStep() { QCOMPARE(RadioUtils::snapFreqToStep(7277380, 1000), Q_INT64_C(7277000)); }
    void testSnap_otherArtifact() { QCOMPARE(RadioUtils::snapFreqToStep(14286750, 100), Q_INT64_C(14286700)); }
    void testSnap_zeroFreq() { QCOMPARE(RadioUtils::snapFreqToStep(0, 100), Q_INT64_C(0)); }
    void testSnap_guardZeroStep() { QCOMPARE(RadioUtils::snapFreqToStep(7277380, 0), Q_INT64_C(7277380)); }

    // vfoWheelTuneTarget — wheel over a VFO's frequency display; a locked VFO (LK / LK$) never tunes
    void testVfoWheel_tunesUnlocked() {
        QCOMPARE(RadioUtils::vfoWheelTuneTarget(14074000, 3, 100, false), Q_INT64_C(14074300));
        QCOMPARE(RadioUtils::vfoWheelTuneTarget(14074000, -2, 1000, false), Q_INT64_C(14072000));
    }
    void testVfoWheel_lockedNeverTunes() {
        for (int steps : {-1000, -1, 1, 1000})
            QCOMPARE(RadioUtils::vfoWheelTuneTarget(14074000, steps, 100, true), Q_INT64_C(0));
    }
    void testVfoWheel_nonPositiveRefused() {
        QCOMPARE(RadioUtils::vfoWheelTuneTarget(500, -1, 1000, false), Q_INT64_C(0));
        QCOMPARE(RadioUtils::vfoWheelTuneTarget(1000, -1, 1000, false), Q_INT64_C(0));
    }

    // vfosMovedByCat / catMovesLockedVfo — every frequency SET respects LK (A) and LK$ (B) on its own
    void testVfosMoved_frequencySets() {
        QCOMPARE(RadioUtils::vfosMovedByCat("FA00014074000;"), int(RadioUtils::VfoA));
        QCOMPARE(RadioUtils::vfosMovedByCat("FB7100"), int(RadioUtils::VfoB));
        QCOMPARE(RadioUtils::vfosMovedByCat("UP;"), int(RadioUtils::VfoA));
        QCOMPARE(RadioUtils::vfosMovedByCat("DN;"), int(RadioUtils::VfoA));
        QCOMPARE(RadioUtils::vfosMovedByCat("UPB;"), int(RadioUtils::VfoB));
        QCOMPARE(RadioUtils::vfosMovedByCat("DNB;"), int(RadioUtils::VfoB));
        QCOMPARE(RadioUtils::vfosMovedByCat("VO+00100;"), int(RadioUtils::VfoA));
        QCOMPARE(RadioUtils::vfosMovedByCat("VO$-00100;"), int(RadioUtils::VfoB));
    }
    void testVfosMoved_copyAndSwap() {
        QCOMPARE(RadioUtils::vfosMovedByCat("AB0;"), int(RadioUtils::VfoB));
        QCOMPARE(RadioUtils::vfosMovedByCat("AB1;"), int(RadioUtils::VfoA));
        QCOMPARE(RadioUtils::vfosMovedByCat("AB2;"), int(RadioUtils::VfoA | RadioUtils::VfoB));
        QCOMPARE(RadioUtils::vfosMovedByCat("AB3;"), int(RadioUtils::VfoB));
        QCOMPARE(RadioUtils::vfosMovedByCat("AB4;"), int(RadioUtils::VfoA));
        QCOMPARE(RadioUtils::vfosMovedByCat("AB5;"), int(RadioUtils::VfoA | RadioUtils::VfoB));
        QCOMPARE(RadioUtils::vfosMovedByCat("AB9;"), int(RadioUtils::VfoNone));
    }
    void testVfosMoved_notFrequencySets() {
        for (const char *c : {"FA;", "FB;", "FT1;", "FR0;", "LK1;", "LK$1;", "MD3;", "SW72;", "RU;", "VO;", "VO$;", ""})
            QVERIFY2(RadioUtils::vfosMovedByCat(c) == RadioUtils::VfoNone, c);
    }
    void testLockedVfo_eachLockOnItsOwn() {
        QVERIFY(!RadioUtils::catMovesLockedVfo("FA14074000;", false, true));
        QVERIFY(RadioUtils::catMovesLockedVfo("FA14074000;", true, false));
        QVERIFY(!RadioUtils::catMovesLockedVfo("FB14074000;", true, false));
        QVERIFY(RadioUtils::catMovesLockedVfo("FB14074000;", false, true));
        // A copy into B is refused only when B is locked; a swap when either is.
        QVERIFY(!RadioUtils::catMovesLockedVfo("AB0;", true, false));
        QVERIFY(RadioUtils::catMovesLockedVfo("AB0;", false, true));
        QVERIFY(RadioUtils::catMovesLockedVfo("AB2;", true, false));
        QVERIFY(RadioUtils::catMovesLockedVfo("AB2;", false, true));
        QVERIFY(!RadioUtils::catMovesLockedVfo("AB2;", false, false));
    }

    // textDecodeSetAllowed — TD is decode only in CW/DATA; in SSB TD1 arms 2-TONE (transmit safety)
    void testTextDecode_onlyCwAndData() {
        for (int md : {3, 7, 6, 9})
            QVERIFY2(RadioUtils::textDecodeSetAllowed(md), qPrintable(QString::number(md)));
        for (int md : {0, 1, 2, 4, 5, 8}) // unknown, LSB, USB, FM, AM, reserved
            QVERIFY2(!RadioUtils::textDecodeSetAllowed(md), qPrintable(QString::number(md)));
    }

    // apfAvailable — APF exists only in CW / CW-R; elsewhere the RX row key reads "N/A"
    void testApf_onlyCw() {
        QVERIFY(RadioUtils::apfAvailable(3));
        QVERIFY(RadioUtils::apfAvailable(7));
        for (int md : {0, 1, 2, 4, 5, 6, 9})
            QVERIFY2(!RadioUtils::apfAvailable(md), qPrintable(QString::number(md)));
    }

    // isValidIpv4 — strict dotted-quad
    void testIpv4_simple() { QVERIFY(RadioUtils::isValidIpv4("192.168.1.10")); }
    void testIpv4_zeros() { QVERIFY(RadioUtils::isValidIpv4("0.0.0.0")); }
    void testIpv4_max() { QVERIFY(RadioUtils::isValidIpv4("255.255.255.255")); }
    void testIpv4_loopback() { QVERIFY(RadioUtils::isValidIpv4("127.0.0.1")); }
    void testIpv4_threeOctets() { QVERIFY(!RadioUtils::isValidIpv4("192.168.1")); }
    void testIpv4_fiveOctets() { QVERIFY(!RadioUtils::isValidIpv4("1.2.3.4.5")); }
    void testIpv4_octetTooLarge() { QVERIFY(!RadioUtils::isValidIpv4("192.168.100.500")); }
    void testIpv4_signedOctet() { QVERIFY(!RadioUtils::isValidIpv4("192.168.1.-1")); }

    // isValidHostOrIp — valid IPv4
    void testHost_ipv4Simple() { QVERIFY(RadioUtils::isValidHostOrIp("1.2.3.4")); }
    void testHost_ipv4Real() { QVERIFY(RadioUtils::isValidHostOrIp("192.168.1.10")); }
    void testHost_ipv4Edges() {
        QVERIFY(RadioUtils::isValidHostOrIp("0.0.0.0"));
        QVERIFY(RadioUtils::isValidHostOrIp("255.255.255.255"));
        QVERIFY(RadioUtils::isValidHostOrIp("127.0.0.1"));
    }

    // isValidHostOrIp — invalid IP-ish (all digits/dots must be a valid IPv4)
    void testHost_octet500() { QVERIFY(!RadioUtils::isValidHostOrIp("192.168.100.500")); }
    void testHost_octet256() { QVERIFY(!RadioUtils::isValidHostOrIp("256.1.1.1")); }
    void testHost_tooFewOctets() { QVERIFY(!RadioUtils::isValidHostOrIp("192.168.1")); }
    void testHost_tooManyOctets() { QVERIFY(!RadioUtils::isValidHostOrIp("1.2.3.4.5")); }
    void testHost_emptyOctet() { QVERIFY(!RadioUtils::isValidHostOrIp("192.168..1")); }
    void testHost_trailingDotNumeric() { QVERIFY(!RadioUtils::isValidHostOrIp("1.2.3.")); }
    void testHost_leadingDotNumeric() { QVERIFY(!RadioUtils::isValidHostOrIp(".1.2.3")); }
    void testHost_allNines() { QVERIFY(!RadioUtils::isValidHostOrIp("999.999.999.999")); }

    // isValidHostOrIp — valid hostnames
    void testHost_local() { QVERIFY(RadioUtils::isValidHostOrIp("k4.local")); }
    void testHost_bare() { QVERIFY(RadioUtils::isValidHostOrIp("myradio")); }
    void testHost_hyphenated() { QVERIFY(RadioUtils::isValidHostOrIp("K4-SN00045.local")); }
    void testHost_fqdn() { QVERIFY(RadioUtils::isValidHostOrIp("host.example.com")); }
    void testHost_singleChar() { QVERIFY(RadioUtils::isValidHostOrIp("a")); }

    // isValidHostOrIp — invalid hostnames
    void testHost_space() { QVERIFY(!RadioUtils::isValidHostOrIp("host name")); }
    void testHost_underscore() { QVERIFY(!RadioUtils::isValidHostOrIp("my_radio")); }
    void testHost_leadingHyphen() { QVERIFY(!RadioUtils::isValidHostOrIp("-leading.local")); }
    void testHost_trailingHyphen() { QVERIFY(!RadioUtils::isValidHostOrIp("trailing-.local")); }
    void testHost_emptyLabel() { QVERIFY(!RadioUtils::isValidHostOrIp("a..b")); }
    void testHost_trailingDotFqdn() { QVERIFY(!RadioUtils::isValidHostOrIp("k4.local.")); }
    void testHost_illegalChar() { QVERIFY(!RadioUtils::isValidHostOrIp("host!.local")); }

    // isValidHostOrIp — IPv6 (accepted)
    void testHost_ipv6Loopback() { QVERIFY(RadioUtils::isValidHostOrIp("::1")); }
    void testHost_ipv6LinkLocal() { QVERIFY(RadioUtils::isValidHostOrIp("fe80::1")); }
    void testHost_ipv6Doc() { QVERIFY(RadioUtils::isValidHostOrIp("2001:db8::1")); }
    void testHost_ipv6Garbage() { QVERIFY(!RadioUtils::isValidHostOrIp("fe80::zz")); }

    // isValidHostOrIp — edges
    void testHost_empty() { QVERIFY(!RadioUtils::isValidHostOrIp("")); }
    void testHost_whitespaceOnly() { QVERIFY(!RadioUtils::isValidHostOrIp("   ")); }
    void testHost_trimmed() { QVERIFY(RadioUtils::isValidHostOrIp("  k4.local  ")); }
    void testHost_tooLong() { QVERIFY(!RadioUtils::isValidHostOrIp(QString(254, 'a'))); }

    // Leading zeros. inet_aton() and several resolvers read a leading-zero octet as OCTAL, so
    // "010.0.0.1" would mean 8.0.0.1 to them and 10.0.0.1 here. Accepting the form would let QK4
    // store an address another part of the stack resolves to a different host.
    void testIpv4_leadingZeroOctet() { QVERIFY(!RadioUtils::isValidIpv4("010.0.0.1")); }
    void testIpv4_leadingZeroPadded() { QVERIFY(!RadioUtils::isValidIpv4("192.168.001.001")); }
    void testIpv4_leadingZeroDoubleZero() { QVERIFY(!RadioUtils::isValidIpv4("00.1.1.1")); }
    void testIpv4_leadingZeroLastOctet() { QVERIFY(!RadioUtils::isValidIpv4("1.1.1.01")); }
    // A bare zero is still a legal octet - the rule is about padding, not about the value.
    void testIpv4_bareZeroOctetsStillValid() { QVERIFY(RadioUtils::isValidIpv4("0.0.0.0")); }
    void testIpv4_zeroInsideAddress() { QVERIFY(RadioUtils::isValidIpv4("10.0.0.1")); }

    // ...and the same rule reached through the host validator, which is what the dialog calls.
    void testHost_leadingZeroRejected() { QVERIFY(!RadioUtils::isValidHostOrIp("010.0.0.1")); }
    // A leading-zero address must NOT fall through to the hostname branch and pass as a name.
    void testHost_leadingZeroNotTreatedAsHostname() { QVERIFY(!RadioUtils::isValidHostOrIp("192.168.001.001")); }

    // celsiusToFahrenheit - display conversion for the status-bar temperature toggle.
    void testTemp_freezing() { QCOMPARE(RadioUtils::celsiusToFahrenheit(0), 32); }
    void testTemp_boiling() { QCOMPARE(RadioUtils::celsiusToFahrenheit(100), 212); }
    void testTemp_bodyRounds() { QCOMPARE(RadioUtils::celsiusToFahrenheit(37), 99); }
    void testTemp_negative() { QCOMPARE(RadioUtils::celsiusToFahrenheit(-40), -40); }
    // The two thresholds the status bar colours on, so a unit change cannot shift a warning.
    void testTemp_warnThreshold() { QCOMPARE(RadioUtils::celsiusToFahrenheit(60), 140); }
    void testTemp_critThreshold() { QCOMPARE(RadioUtils::celsiusToFahrenheit(75), 167); }

    // filterLimits - the one BW/IS table for the filter knobs (APP-004).
    void testFilterLimits_voiceAndCw() {
        const auto usb = RadioUtils::filterLimits(2, -1);
        QCOMPARE(usb.bwMinDah, 5);
        QCOMPARE(usb.bwMaxDah, 500);
        QCOMPARE(usb.isMinDah, 30);
        QCOMPARE(usb.isMaxDah, 300);
        QVERIFY(!usb.isLocked);
        for (const int cw : {3, 7}) {
            QCOMPARE(RadioUtils::filterLimits(cw, -1).isMaxDah, 200);
            QVERIFY(!RadioUtils::filterLimits(cw, -1).isLocked);
        }
    }
    void testFilterLimits_amAndFmFixIs() {
        QVERIFY(RadioUtils::filterLimits(4, -1).isLocked);
        QVERIFY(RadioUtils::filterLimits(5, -1).isLocked);
    }
    void testFilterLimits_dataSubModes() {
        for (const int data : {6, 9}) {
            const auto fsk = RadioUtils::filterLimits(data, 2);
            QCOMPARE(fsk.bwMinDah, 15);
            QCOMPARE(fsk.bwMaxDah, 80);
            QVERIFY(fsk.isLocked);
            const auto psk = RadioUtils::filterLimits(data, 3);
            QCOMPARE(psk.bwMinDah, 5);
            QCOMPARE(psk.bwMaxDah, 20);
            QVERIFY(psk.isLocked);
            for (const int afData : {-1, 0, 1}) {
                QCOMPARE(RadioUtils::filterLimits(data, afData).bwMaxDah, 500);
                QVERIFY(!RadioUtils::filterLimits(data, afData).isLocked);
            }
        }
    }
    // DT outside DATA is not the operating mode ("the returned value may not be valid").
    void testFilterLimits_dataSubModeIgnoredOutsideData() {
        for (const int md : {1, 2, 3, 7}) {
            QCOMPARE(RadioUtils::filterLimits(md, 2).bwMaxDah, 500);
            QVERIFY(!RadioUtils::filterLimits(md, 3).isLocked);
        }
    }

    // Knob limits come from the K4 (APP-003): KS 8-100, CW 25-95 x10 Hz, SQ 0-40. The knobs stopped
    // at 50 WPM, went past 950 Hz to 990 (CW99 is out of range) and stopped squelch at 29.
    void testKeyerSpeed_stepsPastFifty() { QCOMPARE(RadioUtils::nextKeyerSpeed(50, 1), std::optional<int>(51)); }
    void testKeyerSpeed_stopsAtTheRadioLimits() {
        QVERIFY(!RadioUtils::nextKeyerSpeed(100, 1));
        QVERIFY(!RadioUtils::nextKeyerSpeed(8, -1));
        QCOMPARE(RadioUtils::nextKeyerSpeed(98, 5), std::optional<int>(100));
    }
    void testCwPitch_staysAtTheTopOfTheRange() { QVERIFY(!RadioUtils::nextCwPitch(950, 1)); }
    void testCwPitch_reachesTheBottomOfTheRange() {
        QCOMPARE(RadioUtils::nextCwPitch(260, -1), std::optional<int>(250));
        QVERIFY(!RadioUtils::nextCwPitch(250, -1));
    }
    void testSquelch_stepsPastTwentyNine() { QCOMPARE(RadioUtils::nextSquelch(35, 1), std::optional<int>(36)); }
    void testSquelch_stopsAtForty() {
        QCOMPARE(RadioUtils::nextSquelch(29, 1), std::optional<int>(30));
        QVERIFY(!RadioUtils::nextSquelch(40, 1));
    }
    // A value the radio has not reported yet is never stepped: from the -1 sentinel the old knobs
    // sent KS008, CW30, SQ000, MG000 or a 0 ms delay.
    void testKnobs_unknownValueSendsNothing() {
        QVERIFY(!RadioUtils::nextKeyerSpeed(-1, 1));
        QVERIFY(!RadioUtils::nextCwPitch(-1, 1));
        QVERIFY(!RadioUtils::nextSquelch(-1, 1));
        QVERIFY(!RadioUtils::stepWithin(-999, -1, 0, RadioUtils::RF_GAIN_MAX));
        QVERIFY(!RadioUtils::stepWithin(-1, 1, 0, RadioUtils::QSK_DELAY_MAX));
    }
    // Sweep: from every legal value, any tick lands on a legal value (and on the pitch grid).
    void testKnobs_neverLeaveTheRadioRange() {
        for (int steps = -120; steps <= 120; ++steps) {
            for (int wpm = RadioUtils::KEYER_WPM_MIN; wpm <= RadioUtils::KEYER_WPM_MAX; ++wpm) {
                const auto next = RadioUtils::nextKeyerSpeed(wpm, steps);
                QVERIFY(!next || (*next >= 8 && *next <= 100));
            }
            for (int hz = RadioUtils::CW_PITCH_MIN_HZ; hz <= RadioUtils::CW_PITCH_MAX_HZ; hz += 10) {
                const auto next = RadioUtils::nextCwPitch(hz, steps);
                QVERIFY(!next || (*next >= 250 && *next <= 950 && *next % 10 == 0));
            }
            for (int sq = 0; sq <= RadioUtils::SQUELCH_MAX; ++sq) {
                const auto next = RadioUtils::nextSquelch(sq, steps);
                QVERIFY(!next || (*next >= 0 && *next <= 40));
            }
        }
    }

    // nextRfPower - the PWR knob steps within the range the radio reported (APP-002 / CAT-003).
    void testRfPower_qrpTopStepsUpIntoQro() {
        const auto next = RadioUtils::nextRfPower(10.0, LevelsState::PowerRange::Qrp, 1);
        QVERIFY(next);
        QCOMPARE(next->value, 11.0);
        QCOMPARE(next->range, LevelsState::PowerRange::Qro);
    }
    void testRfPower_qroElevenStepsDownIntoQrp() {
        const auto next = RadioUtils::nextRfPower(11.0, LevelsState::PowerRange::Qro, -1);
        QVERIFY(next);
        QCOMPARE(next->value, 10.0);
        QCOMPARE(next->range, LevelsState::PowerRange::Qrp);
    }
    // The K4's QRO range is 1-110 W. A QRO 5 W the radio reported is not QRP 5.0 W.
    void testRfPower_lowQroStaysQro() {
        for (const int steps : {-1, 1}) {
            const auto next = RadioUtils::nextRfPower(5.0, LevelsState::PowerRange::Qro, steps);
            QVERIFY(next);
            QCOMPARE(next->value, 5.0 + steps);
            QCOMPARE(next->range, LevelsState::PowerRange::Qro);
        }
        QVERIFY(!RadioUtils::nextRfPower(1.0, LevelsState::PowerRange::Qro, -1));
    }
    void testRfPower_qrpStepsTenths() {
        const auto next = RadioUtils::nextRfPower(9.9, LevelsState::PowerRange::Qrp, -1);
        QVERIFY(next);
        QCOMPARE(next->value, 9.8);
        QCOMPARE(next->range, LevelsState::PowerRange::Qrp);
        QVERIFY(!RadioUtils::nextRfPower(0.1, LevelsState::PowerRange::Qrp, -1));
    }
    void testRfPower_qroTopStops() { QVERIFY(!RadioUtils::nextRfPower(110.0, LevelsState::PowerRange::Qro, 1)); }
    void testRfPower_unknownSendsNothing() {
        QVERIFY(!RadioUtils::nextRfPower(-1.0, LevelsState::PowerRange::Qro, 1));
        QVERIFY(!RadioUtils::nextRfPower(-1.0, LevelsState::PowerRange::Qrp, -1));
    }
    // Sweep: from every XVTR setting, any tick stays XVTR (mW) and inside 0.1-10.0 mW.
    void testRfPower_xvtrNeverLeavesXvtr() {
        for (int tenths = 1; tenths <= 100; ++tenths) {
            for (int steps = -120; steps <= 120; ++steps) {
                const auto next = RadioUtils::nextRfPower(tenths / 10.0, LevelsState::PowerRange::Xvtr, steps);
                if (!next)
                    continue;
                QCOMPARE(next->range, LevelsState::PowerRange::Xvtr);
                QVERIFY(next->value >= 0.1 - 1e-9 && next->value <= 10.0 + 1e-9);
            }
        }
    }
    // Sweep: every result is on its range's grid and inside it, and a range change happens only at
    // the QRP 10.0 W / QRO 11 W boundary.
    void testRfPower_resultsStayInRangeAndCrossOnlyAtTheBoundary() {
        using R = LevelsState::PowerRange;
        struct Start {
            double value;
            R range;
        };
        QVector<Start> starts;
        for (int t = 1; t <= 100; ++t)
            starts.append({t / 10.0, R::Qrp});
        for (int w = 1; w <= 110; ++w)
            starts.append({static_cast<double>(w), R::Qro});
        for (const Start &start : starts) {
            for (int steps = -150; steps <= 150; ++steps) {
                const auto next = RadioUtils::nextRfPower(start.value, start.range, steps);
                if (!next)
                    continue;
                if (next->range == R::Qrp) {
                    const double tenths = next->value * 10.0;
                    QCOMPARE(tenths, static_cast<double>(qRound(tenths)));
                    QVERIFY(tenths >= 1.0 && tenths <= 100.0);
                } else {
                    QCOMPARE(next->range, R::Qro);
                    QCOMPARE(next->value, static_cast<double>(qRound(next->value)));
                    QVERIFY(next->value >= 1.0 && next->value <= 110.0);
                }
                if (next->range != start.range) {
                    if (next->range == R::Qro)
                        QCOMPARE(next->value, 11.0);
                    else
                        QCOMPARE(next->value, 10.0);
                }
            }
        }
    }
};

QTEST_MAIN(TestRadioUtils)
#include "test_radioutils.moc"
