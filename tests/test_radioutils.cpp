#include <QTest>
#include "utils/radioutils.h"

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

    // RX jitter-buffer watermarks — MUST scale with packet size (the SL bundle), never a fixed ms.
    // SL0 packet = 1920 bytes (20ms), SL7 packet = 11520 bytes (120ms) at 12kHz stereo Float32.
    void testJitterTarget_scalesWithPacket() {
        QCOMPARE(RadioUtils::jitterTargetBytes(1920), 1920 * RadioUtils::JITTER_TARGET_PACKETS);
        QCOMPARE(RadioUtils::jitterTargetBytes(11520), 11520 * RadioUtils::JITTER_TARGET_PACKETS);
    }
    void testJitterHighWater_scalesWithPacket() {
        QCOMPARE(RadioUtils::jitterHighWaterBytes(1920), 1920 * RadioUtils::JITTER_HIGH_WATER_PACKETS);
        QCOMPARE(RadioUtils::jitterHighWaterBytes(11520), 11520 * RadioUtils::JITTER_HIGH_WATER_PACKETS);
    }
    // The SL7 target must NOT be a fixed ms — it must exceed one SL7 packet, or high SL would drop
    // every packet. Guards against a fixed-millisecond regression.
    void testJitterTarget_highSlExceedsOnePacket() {
        QVERIFY(RadioUtils::jitterTargetBytes(11520) > 11520);
        QVERIFY(RadioUtils::jitterHighWaterBytes(11520) > RadioUtils::jitterTargetBytes(11520));
    }
    void testJitterWatermarks_nonPositiveSafe() {
        QCOMPARE(RadioUtils::jitterTargetBytes(0), 0);
        QCOMPARE(RadioUtils::jitterHighWaterBytes(0), 0);
        QCOMPARE(RadioUtils::jitterTargetBytes(-5), 0);
    }

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
};

QTEST_MAIN(TestRadioUtils)
#include "test_radioutils.moc"
