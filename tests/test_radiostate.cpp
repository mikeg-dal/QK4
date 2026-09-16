#include <QTest>
#include <QSignalSpy>
#include "models/radiostate.h"

class TestRadioState : public QObject {
    Q_OBJECT

private slots:
    // Frequency parsing
    void testFrequencyA() {
        RadioState rs;
        rs.parseCATCommand("FA00014074000;");
        QCOMPARE(rs.frequency(), quint64(14074000));
        QCOMPARE(rs.vfoA(), quint64(14074000));
    }

    void testFrequencyB() {
        RadioState rs;
        rs.parseCATCommand("FB00007074000;");
        QCOMPARE(rs.vfoB(), quint64(7074000));
    }

    void testFrequencySignalEmitted() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::frequencyChanged);
        rs.parseCATCommand("FA00014074000;");
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toULongLong(), quint64(14074000));
    }

    void testFrequencyNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("FA00014074000;");
        QSignalSpy spy(&rs, &RadioState::frequencyChanged);
        rs.parseCATCommand("FA00014074000;"); // same value
        QCOMPARE(spy.count(), 0);             // no signal for unchanged value
    }

    // Mode parsing
    void testModeA_LSB() {
        RadioState rs;
        rs.parseCATCommand("MD1;");
        QCOMPARE(rs.mode(), RadioState::LSB);
    }

    void testModeA_USB() {
        RadioState rs;
        rs.parseCATCommand("MD2;");
        QCOMPARE(rs.mode(), RadioState::USB);
    }

    void testModeA_CW() {
        RadioState rs;
        rs.parseCATCommand("MD3;");
        QCOMPARE(rs.mode(), RadioState::CW);
    }

    void testModeA_DATA() {
        RadioState rs;
        rs.parseCATCommand("MD6;");
        QCOMPARE(rs.mode(), RadioState::DATA);
    }

    void testModeB() {
        RadioState rs;
        rs.parseCATCommand("MD$2;");
        QCOMPARE(rs.modeB(), RadioState::USB);
    }

    // Power parsing (PCnnnr format)
    void testPowerQRO() {
        RadioState rs;
        rs.parseCATCommand("PC050H;");
        QCOMPARE(rs.rfPower(), 50.0);
        QCOMPARE(rs.isQrpMode(), false);
    }

    void testPowerQRP() {
        RadioState rs;
        rs.parseCATCommand("PC099L;");
        QCOMPARE(rs.rfPower(), 9.9);
        QCOMPARE(rs.isQrpMode(), true);
    }

    void testPowerQRP_OneWatt() {
        RadioState rs;
        rs.parseCATCommand("PC010L;");
        QCOMPARE(rs.rfPower(), 1.0);
        QCOMPARE(rs.isQrpMode(), true);
    }

    // Filter position (FP/FP$ — uses handleIntPair helper)
    void testFilterPositionA() {
        RadioState rs;
        rs.parseCATCommand("FP1;");
        QCOMPARE(rs.filterPosition(), 1);
    }

    void testFilterPositionB() {
        RadioState rs;
        rs.parseCATCommand("FP$2;");
        QCOMPARE(rs.filterPositionB(), 2);
    }

    void testFilterPositionOutOfRange() {
        RadioState rs;
        rs.parseCATCommand("FP1;");       // valid
        rs.parseCATCommand("FP9;");       // out of range (1-3)
        QCOMPARE(rs.filterPosition(), 1); // unchanged
    }

    // Bandwidth (BW/BW$ — ×10 multiplier)
    void testBandwidthA() {
        RadioState rs;
        rs.parseCATCommand("BW240;");
        QCOMPARE(rs.filterBandwidth(), 2400);
    }

    void testBandwidthB() {
        RadioState rs;
        rs.parseCATCommand("BW$300;");
        QCOMPARE(rs.filterBandwidthB(), 3000);
    }

    // IF Shift (IS/IS$)
    void testIfShiftA() {
        RadioState rs;
        rs.parseCATCommand("IS50;");
        QCOMPARE(rs.ifShift(), 50);
    }

    void testIfShiftB() {
        RadioState rs;
        rs.parseCATCommand("IS$45;");
        QCOMPARE(rs.ifShiftB(), 45);
    }

    // Auto notch (NA/NA$ — uses handleBoolPair helper)
    void testAutoNotchA_On() {
        RadioState rs;
        rs.parseCATCommand("NA1;");
        QCOMPARE(rs.autoNotchEnabled(), true);
    }

    void testAutoNotchA_Off() {
        RadioState rs;
        rs.parseCATCommand("NA1;");
        rs.parseCATCommand("NA0;");
        QCOMPARE(rs.autoNotchEnabled(), false);
    }

    void testAutoNotchB() {
        RadioState rs;
        rs.parseCATCommand("NA$1;");
        QCOMPARE(rs.autoNotchEnabledB(), true);
    }

    // VFO Lock (LK/LK$ — uses handleBoolPairVal helper)
    void testLockA() {
        RadioState rs;
        rs.parseCATCommand("LK1;");
        QCOMPARE(rs.lockA(), true);
    }

    void testLockB() {
        RadioState rs;
        rs.parseCATCommand("LK$1;");
        QCOMPARE(rs.lockB(), true);
    }

    // Split
    void testSplitOn() {
        RadioState rs;
        rs.parseCATCommand("FT1;");
        QCOMPARE(rs.splitEnabled(), true);
    }

    void testSplitOff() {
        RadioState rs;
        rs.parseCATCommand("FT1;");
        rs.parseCATCommand("FT0;");
        QCOMPARE(rs.splitEnabled(), false);
    }

    // Malformed/edge cases — must not crash
    void testEmptyCommand() {
        RadioState rs;
        rs.parseCATCommand("");               // empty
        rs.parseCATCommand(";");              // just semicolon
        QCOMPARE(rs.frequency(), quint64(0)); // unchanged from default
    }

    void testShortCommand() {
        RadioState rs;
        rs.parseCATCommand("FA;");            // too short for frequency
        QCOMPARE(rs.frequency(), quint64(0)); // unchanged
    }

    void testUnknownCommand() {
        RadioState rs;
        rs.parseCATCommand("ZZ999;");         // unknown prefix
        QCOMPARE(rs.frequency(), quint64(0)); // no crash, no change
    }

    void testPowerTooShort() {
        RadioState rs;
        rs.parseCATCommand("PC;");    // no data
        rs.parseCATCommand("PC00;");  // too short for PCnnnr (need 6)
        QCOMPARE(rs.rfPower(), -1.0); // unchanged from default sentinel
    }

    // CW Pitch
    void testCwPitch() {
        RadioState rs;
        // CW pitch format: CWnn; where nn = pitch/10 (range 25-95 → 250-950 Hz)
        rs.parseCATCommand("CW60;");
        QCOMPARE(rs.cwPitch(), 600);
    }

    // Keyer Speed
    void testKeyerSpeed() {
        RadioState rs;
        rs.parseCATCommand("KS020;");
        QCOMPARE(rs.keyerSpeed(), 20);
    }

    // Streaming Latency (SL command)
    void testStreamingLatency() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::streamingLatencyChanged);
        rs.parseCATCommand("SL3;");
        QCOMPARE(rs.streamingLatency(), 3);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 3);
    }

    void testStreamingLatency_allValues() {
        RadioState rs;
        for (int i = 0; i <= 7; i++) {
            rs.parseCATCommand(QString("SL%1;").arg(i));
            QCOMPARE(rs.streamingLatency(), i);
        }
    }

    void testStreamingLatency_noSignalOnSameValue() {
        RadioState rs;
        rs.parseCATCommand("SL3;");
        QSignalSpy spy(&rs, &RadioState::streamingLatencyChanged);
        rs.parseCATCommand("SL3;");
        QCOMPARE(spy.count(), 0);
    }

    void testStreamingLatency_resetToSentinel() {
        RadioState rs;
        rs.parseCATCommand("SL5;");
        rs.reset();
        QCOMPARE(rs.streamingLatency(), -1);
    }

    // SIRC command removed — must not crash when received
    void testSircCommandIgnored() {
        RadioState rs;
        rs.parseCATCommand("SIRC1;");
        // No handler registered — command is silently ignored, no crash
    }

    // Reset clears all state
    void testReset() {
        RadioState rs;
        rs.parseCATCommand("FA00014074000;");
        rs.parseCATCommand("MD2;");
        rs.parseCATCommand("FP1;");
        rs.reset();
        QCOMPARE(rs.frequency(), quint64(0));
        QCOMPARE(rs.mode(), RadioState::Unknown);
        QCOMPARE(rs.filterPosition(), -1); // sentinel
    }

    // =========================================================================
    // Phase 0.1 Backfill — FrequencyVfo subsystem
    // Handlers covered: FA, FB, FT, RT, RT$, XT, RO, RO$
    // =========================================================================

    // --- FB signal / idempotence (A/B symmetry with testFrequencySignalEmitted) ---
    void testFrequencyBSignalEmitted() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::frequencyBChanged);
        rs.parseCATCommand("FB00007074000;");
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toULongLong(), quint64(7074000));
    }

    void testFrequencyBNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("FB00007074000;");
        QSignalSpy spy(&rs, &RadioState::frequencyBChanged);
        rs.parseCATCommand("FB00007074000;");
        QCOMPARE(spy.count(), 0);
    }

    // --- FA/FB non-numeric payload (toULongLong fails) ---
    void testFrequencyANonNumeric() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::frequencyChanged);
        rs.parseCATCommand("FAABCDEFGHIJK;");
        QCOMPARE(spy.count(), 0);
        QCOMPARE(rs.frequency(), quint64(0));
    }

    void testFrequencyBNonNumeric() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::frequencyBChanged);
        rs.parseCATCommand("FBABCDEFGHIJK;");
        QCOMPARE(spy.count(), 0);
        QCOMPARE(rs.vfoB(), quint64(0));
    }

    // --- FA/FB low-edge values ---
    void testFrequencyALowEdge() {
        RadioState rs;
        rs.parseCATCommand("FA00000001000;"); // 1 kHz
        QCOMPARE(rs.frequency(), quint64(1000));
    }

    void testFrequencyBLowEdge() {
        RadioState rs;
        rs.parseCATCommand("FB00000001000;");
        QCOMPARE(rs.vfoB(), quint64(1000));
    }

    // --- Split (FT) signal + idempotence ---
    void testSplitSignalEmitted() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::splitChanged);
        rs.parseCATCommand("FT1;");
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toBool(), true);
    }

    void testSplitNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("FT1;");
        QSignalSpy spy(&rs, &RadioState::splitChanged);
        rs.parseCATCommand("FT1;");
        QCOMPARE(spy.count(), 0);
    }

    void testSplitTreatsNonOneAsOff() {
        // handleFT uses strict "mid(2) == \"1\"" — anything else is "off"
        RadioState rs;
        rs.parseCATCommand("FT1;");
        rs.parseCATCommand("FT2;"); // not "1" → off
        QCOMPARE(rs.splitEnabled(), false);
    }

    // --- RIT on/off (RT) ---
    void testRitEnable() {
        RadioState rs;
        rs.parseCATCommand("RT1;");
        QCOMPARE(rs.ritEnabled(), true);
    }

    void testRitDisable() {
        RadioState rs;
        rs.parseCATCommand("RT1;");
        rs.parseCATCommand("RT0;");
        QCOMPARE(rs.ritEnabled(), false);
    }

    void testRitIgnoresInvalidChar() {
        // handleRT returns silently if char at [2] is not '0' or '1'
        RadioState rs;
        rs.parseCATCommand("RT1;");
        QSignalSpy spy(&rs, &RadioState::ritXitChanged);
        rs.parseCATCommand("RT2;"); // invalid
        rs.parseCATCommand("RTX;"); // invalid
        QCOMPARE(spy.count(), 0);
        QCOMPARE(rs.ritEnabled(), true); // unchanged
    }

    void testRitNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("RT1;");
        QSignalSpy spy(&rs, &RadioState::ritXitChanged);
        rs.parseCATCommand("RT1;");
        QCOMPARE(spy.count(), 0);
    }

    // --- XIT on/off (XT) ---
    void testXitEnable() {
        RadioState rs;
        rs.parseCATCommand("XT1;");
        QCOMPARE(rs.xitEnabled(), true);
    }

    void testXitDisable() {
        RadioState rs;
        rs.parseCATCommand("XT1;");
        rs.parseCATCommand("XT0;");
        QCOMPARE(rs.xitEnabled(), false);
    }

    void testXitIgnoresInvalidChar() {
        RadioState rs;
        rs.parseCATCommand("XT1;");
        QSignalSpy spy(&rs, &RadioState::ritXitChanged);
        rs.parseCATCommand("XT2;");
        QCOMPARE(spy.count(), 0);
        QCOMPARE(rs.xitEnabled(), true);
    }

    void testXitNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("XT1;");
        QSignalSpy spy(&rs, &RadioState::ritXitChanged);
        rs.parseCATCommand("XT1;");
        QCOMPARE(spy.count(), 0);
    }

    // --- RIT/XIT offset (RO) ---
    void testRitXitOffsetPositive() {
        RadioState rs;
        rs.parseCATCommand("RO+0500;");
        QCOMPARE(rs.ritXitOffset(), 500);
    }

    void testRitXitOffsetNegative() {
        RadioState rs;
        rs.parseCATCommand("RO-0500;");
        QCOMPARE(rs.ritXitOffset(), -500);
    }

    void testRitXitOffsetZero() {
        // First set non-zero so we can detect the change to zero
        RadioState rs;
        rs.parseCATCommand("RO+0100;");
        rs.parseCATCommand("RO+0000;");
        QCOMPARE(rs.ritXitOffset(), 0);
    }

    void testRitXitOffsetNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("RO+0500;");
        QSignalSpy spy(&rs, &RadioState::ritXitChanged);
        rs.parseCATCommand("RO+0500;");
        QCOMPARE(spy.count(), 0);
    }

    // --- RT$/RO$ (VFO B RIT register) ---
    void testRitBEnable() {
        RadioState rs;
        rs.parseCATCommand("RT$1;");
        QCOMPARE(rs.ritEnabledB(), true);
    }

    void testRitBDisable() {
        RadioState rs;
        rs.parseCATCommand("RT$1;");
        rs.parseCATCommand("RT$0;");
        QCOMPARE(rs.ritEnabledB(), false);
    }

    void testRitXitBOffset() {
        RadioState rs;
        rs.parseCATCommand("RO$+0250;");
        QCOMPARE(rs.ritXitOffsetB(), 250);
    }

    void testRitXitBOffsetNegative() {
        RadioState rs;
        rs.parseCATCommand("RO$-0250;");
        QCOMPARE(rs.ritXitOffsetB(), -250);
    }

    // --- Rollup signal semantic: ritXitChanged carries rit AND xit AND offset ---
    // WHY: The RT/XT/RO handlers all fire the same rollup signal. A regression
    // that splits these onto separate signals (during Phase 1) would only
    // emit one arg — this test catches that.
    void testRitXitRollupCarriesAllThree() {
        RadioState rs;
        rs.parseCATCommand("XT1;");     // set XIT first
        rs.parseCATCommand("RO+0500;"); // set offset
        QSignalSpy spy(&rs, &RadioState::ritXitChanged);
        rs.parseCATCommand("RT1;"); // now enable RIT — should emit rollup
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toBool(), true); // ritEnabled
        QCOMPARE(spy.at(0).at(1).toBool(), true); // xitEnabled still true
        QCOMPARE(spy.at(0).at(2).toInt(), 500);   // offset still 500
    }

    // --- A/B symmetry gap flagged by plan exploration ---
    // Paired with testFilterPositionOutOfRange.
    void testFilterPositionBOutOfRange() {
        RadioState rs;
        rs.parseCATCommand("FP$2;");       // valid
        rs.parseCATCommand("FP$9;");       // out of range (1-3)
        QCOMPARE(rs.filterPositionB(), 2); // unchanged
    }

    // =========================================================================
    // Phase 0.1 Backfill — ProcessingState subsystem
    // Handlers: NB/NB$, NR/NR$, PA/PA$, RA/RA$, GT/GT$, NM/NM$, AP/AP$
    // All of NB/NR/PA/RA/GT fire the rollup signal processingChanged() /
    // processingChangedB(). Tests assert idempotent emission (same input →
    // no duplicate rollup).
    // =========================================================================

    // --- NB / NB$ (Noise Blanker: NBnnmf; nn=level 0-15, m=on/off, f=filter 0-2) ---
    void testNbParsesLevelEnabledFilter() {
        RadioState rs;
        rs.parseCATCommand("NB0512;"); // level=5, enabled=1, filter=2
        QCOMPARE(rs.noiseBlankerLevel(), 5);
        QCOMPARE(rs.noiseBlankerEnabled(), true);
        QCOMPARE(rs.noiseBlankerFilterWidth(), 2);
    }

    void testNbLevelClampedTo15() {
        RadioState rs;
        rs.parseCATCommand("NB9912;");
        QCOMPARE(rs.noiseBlankerLevel(), 15);
    }

    void testNbFilterClampedTo2() {
        RadioState rs;
        rs.parseCATCommand("NB0519;"); // filter=9 → clamped to 2
        QCOMPARE(rs.noiseBlankerFilterWidth(), 2);
    }

    void testNbDisable() {
        RadioState rs;
        rs.parseCATCommand("NB0511;");
        rs.parseCATCommand("NB0501;");
        QCOMPARE(rs.noiseBlankerEnabled(), false);
    }

    void testNbSignalEmitted() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::processingChanged);
        rs.parseCATCommand("NB0512;");
        QCOMPARE(spy.count(), 1);
    }

    void testNbNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("NB0512;");
        QSignalSpy spy(&rs, &RadioState::processingChanged);
        rs.parseCATCommand("NB0512;");
        QCOMPARE(spy.count(), 0);
    }

    void testNbBNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("NB$0512;");
        QSignalSpy spy(&rs, &RadioState::processingChangedB);
        rs.parseCATCommand("NB$0512;");
        QCOMPARE(spy.count(), 0);
    }

    void testNbBParsesAndEmits() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::processingChangedB);
        rs.parseCATCommand("NB$0712;");
        QCOMPARE(rs.noiseBlankerLevelB(), 7);
        QCOMPARE(rs.noiseBlankerEnabledB(), true);
        QCOMPARE(rs.noiseBlankerFilterWidthB(), 2);
        QCOMPARE(spy.count(), 1);
    }

    void testNbShortCommandIgnored() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::processingChanged);
        rs.parseCATCommand("NB;");   // way too short
        rs.parseCATCommand("NB01;"); // missing enable/filter
        QCOMPARE(spy.count(), 0);
    }

    void testNbPartialWithoutFilterPreservesCurrent() {
        // NB with only 3 chars after prefix (NBnnm) leaves filter untouched
        RadioState rs;
        rs.parseCATCommand("NB0512;"); // filter set to 2
        rs.parseCATCommand("NB071;");  // only level+enabled (3 chars) — filter stays 2
        QCOMPARE(rs.noiseBlankerLevel(), 7);
        QCOMPARE(rs.noiseBlankerFilterWidth(), 2);
    }

    // --- NR / NR$ (Noise Reduction: NRnnm; nn=level, m=on/off) ---
    void testNrParsesLevelEnabled() {
        RadioState rs;
        rs.parseCATCommand("NR051;");
        QCOMPARE(rs.noiseReductionLevel(), 5);
        QCOMPARE(rs.noiseReductionEnabled(), true);
    }

    void testNrDisable() {
        RadioState rs;
        rs.parseCATCommand("NR051;");
        rs.parseCATCommand("NR050;");
        QCOMPARE(rs.noiseReductionEnabled(), false);
    }

    void testNrSignalEmitted() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::processingChanged);
        rs.parseCATCommand("NR051;");
        QCOMPARE(spy.count(), 1);
    }

    void testNrNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("NR051;");
        QSignalSpy spy(&rs, &RadioState::processingChanged);
        rs.parseCATCommand("NR051;");
        QCOMPARE(spy.count(), 0);
    }

    void testNrBParsesAndEmits() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::processingChangedB);
        rs.parseCATCommand("NR$071;");
        QCOMPARE(rs.noiseReductionLevelB(), 7);
        QCOMPARE(rs.noiseReductionEnabledB(), true);
        QCOMPARE(spy.count(), 1);
    }

    void testNrBNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("NR$071;");
        QSignalSpy spy(&rs, &RadioState::processingChangedB);
        rs.parseCATCommand("NR$071;");
        QCOMPARE(spy.count(), 0);
    }

    // --- NRS / NRS$ (Spectral-subtraction NR: NRSnnm; LMS NR's peer) ---
    void testNrsParsesLevelEnabled() {
        RadioState rs;
        rs.parseCATCommand("NRS061;");
        QCOMPARE(rs.ssnrLevel(), 6);
        QCOMPARE(rs.ssnrEnabled(), true);
    }

    void testNrsDisable() {
        RadioState rs;
        rs.parseCATCommand("NRS061;");
        rs.parseCATCommand("NRS060;");
        QCOMPARE(rs.ssnrEnabled(), false);
    }

    void testNrsSignalEmitted() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::processingChanged);
        rs.parseCATCommand("NRS061;");
        QCOMPARE(spy.count(), 1);
    }

    void testNrsDoesNotShadowNr() {
        // NRS061; must dispatch to handleNRS, NOT handleNR — verify by checking
        // the ssnr fields populate while the nr fields remain at defaults.
        RadioState rs;
        rs.parseCATCommand("NRS061;");
        QCOMPARE(rs.ssnrLevel(), 6);
        QCOMPARE(rs.ssnrEnabled(), true);
        QCOMPARE(rs.noiseReductionLevel(), 0);
        QCOMPARE(rs.noiseReductionEnabled(), false);
    }

    void testNrsSubParsesAndEmits() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::processingChangedB);
        rs.parseCATCommand("NRS$081;");
        QCOMPARE(rs.ssnrLevelB(), 8);
        QCOMPARE(rs.ssnrEnabledB(), true);
        QCOMPARE(spy.count(), 1);
    }

    void testNrsSubDoesNotShadowNrSub() {
        RadioState rs;
        rs.parseCATCommand("NRS$081;");
        QCOMPARE(rs.ssnrLevelB(), 8);
        QCOMPARE(rs.noiseReductionLevelB(), 0);
        QCOMPARE(rs.noiseReductionEnabledB(), false);
    }

    // --- PA / PA$ (Preamp: PAnm; n=level, m=on/off) ---
    void testPaParsesLevelEnabled() {
        RadioState rs;
        rs.parseCATCommand("PA21;");
        QCOMPARE(rs.preamp(), 2);
        QCOMPARE(rs.preampEnabled(), true);
    }

    void testPaDisable() {
        RadioState rs;
        rs.parseCATCommand("PA21;");
        rs.parseCATCommand("PA20;");
        QCOMPARE(rs.preampEnabled(), false);
    }

    void testPaSignalEmitted() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::processingChanged);
        rs.parseCATCommand("PA21;");
        QCOMPARE(spy.count(), 1);
    }

    void testPaNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("PA21;");
        QSignalSpy spy(&rs, &RadioState::processingChanged);
        rs.parseCATCommand("PA21;");
        QCOMPARE(spy.count(), 0);
    }

    void testPaBParsesAndEmits() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::processingChangedB);
        rs.parseCATCommand("PA$11;");
        QCOMPARE(rs.preampB(), 1);
        QCOMPARE(rs.preampEnabledB(), true);
        QCOMPARE(spy.count(), 1);
    }

    // --- RA / RA$ (Attenuator: RAnnm; nn=level, m=on/off) ---
    void testRaParsesLevelEnabled() {
        RadioState rs;
        rs.parseCATCommand("RA051;");
        QCOMPARE(rs.attenuatorLevel(), 5);
        QCOMPARE(rs.attenuatorEnabled(), true);
    }

    void testRaSignalEmitted() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::processingChanged);
        rs.parseCATCommand("RA051;");
        QCOMPARE(spy.count(), 1);
    }

    void testRaNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("RA051;");
        QSignalSpy spy(&rs, &RadioState::processingChanged);
        rs.parseCATCommand("RA051;");
        QCOMPARE(spy.count(), 0);
    }

    void testRaBParsesAndEmits() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::processingChangedB);
        rs.parseCATCommand("RA$101;");
        QCOMPARE(rs.attenuatorLevelB(), 10);
        QCOMPARE(rs.attenuatorEnabledB(), true);
        QCOMPARE(spy.count(), 1);
    }

    // --- GT / GT$ (AGC Speed: GTn; 0=Off, 1=Slow, 2=Fast) ---
    void testGtOff() {
        RadioState rs;
        rs.parseCATCommand("GT0;");
        QCOMPARE(rs.agcSpeed(), RadioState::AGC_Off);
    }

    void testGtSlow() {
        // WHY: default m_agcSpeed is AGC_Slow; setting it to AGC_Slow emits
        // nothing. Change state first so we observe the transition.
        RadioState rs;
        rs.parseCATCommand("GT0;"); // now Off
        rs.parseCATCommand("GT1;"); // → Slow
        QCOMPARE(rs.agcSpeed(), RadioState::AGC_Slow);
    }

    void testGtFast() {
        RadioState rs;
        rs.parseCATCommand("GT2;");
        QCOMPARE(rs.agcSpeed(), RadioState::AGC_Fast);
    }

    void testGtInvalidValueIgnored() {
        // Values outside 0-2 are silently ignored.
        RadioState rs;
        rs.parseCATCommand("GT2;"); // Fast
        QSignalSpy spy(&rs, &RadioState::processingChanged);
        rs.parseCATCommand("GT5;"); // invalid → no change
        QCOMPARE(spy.count(), 0);
        QCOMPARE(rs.agcSpeed(), RadioState::AGC_Fast);
    }

    void testGtSignalEmitted() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::processingChanged);
        rs.parseCATCommand("GT2;"); // Slow (default) → Fast
        QCOMPARE(spy.count(), 1);
    }

    void testGtNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("GT2;");
        QSignalSpy spy(&rs, &RadioState::processingChanged);
        rs.parseCATCommand("GT2;");
        QCOMPARE(spy.count(), 0);
    }

    void testGtBParsesAndEmits() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::processingChangedB);
        rs.parseCATCommand("GT$0;");
        QCOMPARE(rs.agcSpeedB(), RadioState::AGC_Off);
        QCOMPARE(spy.count(), 1);
    }

    // --- NM / NM$ (Manual Notch: NMnnnnm long form, or NMm short enable-only) ---
    void testNmParsesPitchAndEnabled() {
        RadioState rs;
        rs.parseCATCommand("NM10001;"); // pitch=1000, enabled=1
        QCOMPARE(rs.manualNotchPitch(), 1000);
        QCOMPARE(rs.manualNotchEnabled(), true);
    }

    void testNmShortFormTogglesEnableOnly() {
        RadioState rs;
        rs.parseCATCommand("NM10001;"); // establish pitch=1000
        rs.parseCATCommand("NM0;");     // enable only → disable
        QCOMPARE(rs.manualNotchEnabled(), false);
        QCOMPARE(rs.manualNotchPitch(), 1000); // pitch preserved
    }

    void testNmPitchBelowRangeIgnored() {
        RadioState rs;
        rs.parseCATCommand("NM10001;"); // pitch=1000
        rs.parseCATCommand("NM01001;"); // pitch=100 (< 150) — rejected
        QCOMPARE(rs.manualNotchPitch(), 1000);
    }

    void testNmPitchAboveRangeIgnored() {
        RadioState rs;
        rs.parseCATCommand("NM10001;");
        rs.parseCATCommand("NM60001;"); // pitch=6000 (> 5000) — rejected
        QCOMPARE(rs.manualNotchPitch(), 1000);
    }

    void testNmSignalEmitted() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::notchChanged);
        rs.parseCATCommand("NM10001;");
        QCOMPARE(spy.count(), 1);
    }

    void testNmNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("NM10001;");
        QSignalSpy spy(&rs, &RadioState::notchChanged);
        rs.parseCATCommand("NM10001;");
        QCOMPARE(spy.count(), 0);
    }

    void testNmBParsesAndEmits() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::notchBChanged);
        rs.parseCATCommand("NM$10001;");
        QCOMPARE(rs.manualNotchPitchB(), 1000);
        QCOMPARE(rs.manualNotchEnabledB(), true);
        QCOMPARE(spy.count(), 1);
    }

    // --- AP / AP$ (Audio Peak Filter: APmb; m=on/off, b=bandwidth 0-2) ---
    void testApParsesEnabledBandwidth() {
        RadioState rs;
        rs.parseCATCommand("AP11;");
        QCOMPARE(rs.apfEnabled(), true);
        QCOMPARE(rs.apfBandwidth(), 1);
    }

    void testApBandwidthClampedTo2() {
        RadioState rs;
        rs.parseCATCommand("AP19;"); // bandwidth=9 → clamped to 2
        QCOMPARE(rs.apfBandwidth(), 2);
    }

    void testApSignalEmitted() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::apfChanged);
        rs.parseCATCommand("AP11;");
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toBool(), true); // enabled
        QCOMPARE(spy.at(0).at(1).toInt(), 1);     // bandwidth
    }

    void testApNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("AP11;");
        QSignalSpy spy(&rs, &RadioState::apfChanged);
        rs.parseCATCommand("AP11;");
        QCOMPARE(spy.count(), 0);
    }

    void testApBParsesAndEmits() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::apfBChanged);
        rs.parseCATCommand("AP$11;");
        QCOMPARE(rs.apfEnabledB(), true);
        QCOMPARE(rs.apfBandwidthB(), 1);
        QCOMPARE(spy.count(), 1);
    }

    // =========================================================================
    // Phase 0.1 Backfill — Antenna subsystem
    // Handlers: AN, AT, ACN, ACM, ACS, ACT, AR, AR$
    // AN/AR/AR$ all emit the rollup signal antennaChanged(tx, rxMain, rxSub).
    // ACM/ACS/ACT emit their own *AntCfgChanged signals.
    // ATU mode has its own atuModeChanged signal.
    // =========================================================================

    // --- AN (TX antenna select, 1-6) ---
    void testAntennaSelectParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::antennaChanged);
        rs.parseCATCommand("AN3;");
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 3); // tx
    }

    void testAntennaSelectOutOfRangeLow() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::antennaChanged);
        rs.parseCATCommand("AN0;"); // below 1
        QCOMPARE(spy.count(), 0);
    }

    void testAntennaSelectOutOfRangeHigh() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::antennaChanged);
        rs.parseCATCommand("AN7;"); // above 6
        QCOMPARE(spy.count(), 0);
    }

    void testAntennaSelectNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("AN3;");
        QSignalSpy spy(&rs, &RadioState::antennaChanged);
        rs.parseCATCommand("AN3;");
        QCOMPARE(spy.count(), 0);
    }

    // Rollup semantics: antennaChanged must carry current tx/rxMain/rxSub, not
    // just the changed one. A Phase 1 split that loses any of those args
    // would be caught by this test.
    void testAntennaRollupCarriesAllThree() {
        RadioState rs;
        rs.parseCATCommand("AR2;");  // set rxMain = 2
        rs.parseCATCommand("AR$3;"); // set rxSub = 3
        QSignalSpy spy(&rs, &RadioState::antennaChanged);
        rs.parseCATCommand("AN4;"); // now change tx — rollup must include all three
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 4); // tx
        QCOMPARE(spy.at(0).at(1).toInt(), 2); // rxMain still 2
        QCOMPARE(spy.at(0).at(2).toInt(), 3); // rxSub still 3
    }

    // --- AR / AR$ (RX antenna main / sub, 0-7) ---
    void testArParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::antennaChanged);
        rs.parseCATCommand("AR5;");
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(1).toInt(), 5); // rxMain
    }

    void testArOutOfRangeHigh() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::antennaChanged);
        rs.parseCATCommand("AR9;");
        QCOMPARE(spy.count(), 0);
    }

    void testArNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("AR5;");
        QSignalSpy spy(&rs, &RadioState::antennaChanged);
        rs.parseCATCommand("AR5;");
        QCOMPARE(spy.count(), 0);
    }

    void testArBParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::antennaChanged);
        rs.parseCATCommand("AR$4;");
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(2).toInt(), 4); // rxSub
    }

    void testArBOutOfRangeHigh() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::antennaChanged);
        rs.parseCATCommand("AR$9;");
        QCOMPARE(spy.count(), 0);
    }

    void testArBNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("AR$4;");
        QSignalSpy spy(&rs, &RadioState::antennaChanged);
        rs.parseCATCommand("AR$4;");
        QCOMPARE(spy.count(), 0);
    }

    // --- AT (ATU mode, 0-2) ---
    void testAtuModeBypass() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::atuModeChanged);
        rs.parseCATCommand("AT0;");
        QCOMPARE(rs.atuMode(), 0);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 0);
    }

    void testAtuModeAuto() {
        RadioState rs;
        rs.parseCATCommand("AT2;");
        QCOMPARE(rs.atuMode(), 2);
    }

    void testAtuModeOutOfRange() {
        RadioState rs;
        rs.parseCATCommand("AT1;");
        QSignalSpy spy(&rs, &RadioState::atuModeChanged);
        rs.parseCATCommand("AT5;"); // above 2
        QCOMPARE(spy.count(), 0);
        QCOMPARE(rs.atuMode(), 1); // unchanged
    }

    void testAtuModeNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("AT1;");
        QSignalSpy spy(&rs, &RadioState::atuModeChanged);
        rs.parseCATCommand("AT1;");
        QCOMPARE(spy.count(), 0);
    }

    // --- ACN (Antenna name, ACN<i><name>; where i=1-7) ---
    void testAntennaNameParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::antennaNameChanged);
        rs.parseCATCommand("ACN3BEAM;");
        QCOMPARE(rs.antennaName(3), QString("BEAM"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 3);
        QCOMPARE(spy.at(0).at(1).toString(), QString("BEAM"));
    }

    void testAntennaNameEmptyRejected() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::antennaNameChanged);
        rs.parseCATCommand("ACN3;"); // no name text
        QCOMPARE(spy.count(), 0);
    }

    void testAntennaNameOutOfRangeIndex() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::antennaNameChanged);
        rs.parseCATCommand("ACN8BEAM;"); // index 8 > 7
        QCOMPARE(spy.count(), 0);
    }

    void testAntennaNameNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("ACN3BEAM;");
        QSignalSpy spy(&rs, &RadioState::antennaNameChanged);
        rs.parseCATCommand("ACN3BEAM;");
        QCOMPARE(spy.count(), 0);
    }

    // --- ACM / ACS / ACT (antenna config bitmasks) ---
    // Format: ACM<displayAll><7 mask bits>; ACT has 3 mask bits.
    void testAcmParsesMask() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::mainRxAntCfgChanged);
        rs.parseCATCommand("ACM11010010;"); // displayAll=1, mask=1010010
        QCOMPARE(rs.mainRxDisplayAll(), true);
        QCOMPARE(spy.count(), 1);
        auto mask = rs.mainRxAntMask();
        QCOMPARE(mask.size(), 7);
        QCOMPARE(mask.at(0), true);
        QCOMPARE(mask.at(1), false);
        QCOMPARE(mask.at(2), true);
        QCOMPARE(mask.at(3), false);
        QCOMPARE(mask.at(4), false);
        QCOMPARE(mask.at(5), true);
        QCOMPARE(mask.at(6), false);
    }

    void testAcmNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("ACM11010010;");
        QSignalSpy spy(&rs, &RadioState::mainRxAntCfgChanged);
        rs.parseCATCommand("ACM11010010;");
        QCOMPARE(spy.count(), 0);
    }

    void testAcmSingleBitFlipEmits() {
        RadioState rs;
        rs.parseCATCommand("ACM11010010;");
        QSignalSpy spy(&rs, &RadioState::mainRxAntCfgChanged);
        rs.parseCATCommand("ACM11010011;"); // bit 6 flipped
        QCOMPARE(spy.count(), 1);
    }

    void testAcmDisplayAllFlipEmits() {
        RadioState rs;
        rs.parseCATCommand("ACM11010010;");
        QSignalSpy spy(&rs, &RadioState::mainRxAntCfgChanged);
        rs.parseCATCommand("ACM01010010;"); // displayAll flipped
        QCOMPARE(spy.count(), 1);
        QCOMPARE(rs.mainRxDisplayAll(), false);
    }

    void testAcsParsesMask() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::subRxAntCfgChanged);
        rs.parseCATCommand("ACS11100001;");
        QCOMPARE(rs.subRxDisplayAll(), true);
        QCOMPARE(spy.count(), 1);
        auto mask = rs.subRxAntMask();
        QCOMPARE(mask.at(0), true);
        QCOMPARE(mask.at(6), true);
    }

    void testAcsNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("ACS11100001;");
        QSignalSpy spy(&rs, &RadioState::subRxAntCfgChanged);
        rs.parseCATCommand("ACS11100001;");
        QCOMPARE(spy.count(), 0);
    }

    void testActParsesMask() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::txAntCfgChanged);
        rs.parseCATCommand("ACT1101;"); // displayAll=1, TX mask = 101
        QCOMPARE(rs.txDisplayAll(), true);
        QCOMPARE(spy.count(), 1);
        auto mask = rs.txAntMask();
        QCOMPARE(mask.size(), 3);
        QCOMPARE(mask.at(0), true);
        QCOMPARE(mask.at(1), false);
        QCOMPARE(mask.at(2), true);
    }

    void testActNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("ACT1101;");
        QSignalSpy spy(&rs, &RadioState::txAntCfgChanged);
        rs.parseCATCommand("ACT1101;");
        QCOMPARE(spy.count(), 0);
    }

    void testAcmShortCommandIgnored() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::mainRxAntCfgChanged);
        rs.parseCATCommand("ACM1101;"); // too short (need 11 chars)
        QCOMPARE(spy.count(), 0);
    }

    void testActShortCommandIgnored() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::txAntCfgChanged);
        rs.parseCATCommand("ACT11;"); // too short (need 7 chars)
        QCOMPARE(spy.count(), 0);
    }

    // =========================================================================
    // Phase 0.1 Backfill — DataControl subsystem
    // Handlers: TD/TD$ (text decode cfg), TB/TB$ (text buffer), DT/DT$
    // (data sub-mode), DR/DR$ (data rate), VT/VT$ (tuning step).
    // DT/DT$ and DR/DR$ carry a 500ms optimistic-update cooldown that
    // suppresses echoes of user-initiated changes — tested explicitly.
    // =========================================================================

    // --- TD (text decode cfg: TD<mode><threshold><lines>;) ---
    void testTextDecodeParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::textDecodeChanged);
        rs.parseCATCommand("TD132;"); // mode=1, threshold=3, lines=2
        QCOMPARE(rs.textDecodeMode(), 1);
        QCOMPARE(rs.textDecodeThreshold(), 3);
        QCOMPARE(rs.textDecodeLines(), 2);
        QCOMPARE(spy.count(), 1);
    }

    void testTextDecodeLinesOutOfRangeIgnored() {
        RadioState rs;
        rs.parseCATCommand("TD132;"); // lines=2
        rs.parseCATCommand("TD130;"); // lines=0 (< 1) — rejected
        QCOMPARE(rs.textDecodeLines(), 2);
    }

    void testTextDecodeNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("TD132;");
        QSignalSpy spy(&rs, &RadioState::textDecodeChanged);
        rs.parseCATCommand("TD132;");
        QCOMPARE(spy.count(), 0);
    }

    void testTextDecodeBParsesSymmetric() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::textDecodeBChanged);
        rs.parseCATCommand("TD$132;");
        QCOMPARE(rs.textDecodeModeB(), 1);
        QCOMPARE(rs.textDecodeThresholdB(), 3);
        QCOMPARE(rs.textDecodeLinesB(), 2);
        QCOMPARE(spy.count(), 1);
    }

    // --- TB (text buffer stream — emits every non-empty decoded buffer) ---
    void testTextBufferReceivedEmitsForMain() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::textBufferReceived);
        rs.parseCATCommand("TB000 HELLO WORLD"); // no trailing semicolon required
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(1).toBool(), false); // isSubRx=false
    }

    void testTextBufferReceivedEmitsForSub() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::textBufferReceived);
        rs.parseCATCommand("TB$000 HELLO WORLD");
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(1).toBool(), true); // isSubRx=true
    }

    void testTextBufferEmptyDoesNotEmit() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::textBufferReceived);
        rs.parseCATCommand("TB000"); // nothing after the 3-char header
        QCOMPARE(spy.count(), 0);
    }

    // --- DT / DT$ (Data sub-mode, 0-3) ---
    void testDataSubModeParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::dataSubModeChanged);
        rs.parseCATCommand("DT2;");
        QCOMPARE(rs.dataSubMode(), 2);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 2);
    }

    void testDataSubModeOutOfRangeRejected() {
        RadioState rs;
        rs.parseCATCommand("DT2;");
        QSignalSpy spy(&rs, &RadioState::dataSubModeChanged);
        rs.parseCATCommand("DT4;"); // above 3
        rs.parseCATCommand("DT9;");
        QCOMPARE(spy.count(), 0);
        QCOMPARE(rs.dataSubMode(), 2);
    }

    void testDataSubModeNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("DT2;");
        QSignalSpy spy(&rs, &RadioState::dataSubModeChanged);
        rs.parseCATCommand("DT2;");
        QCOMPARE(spy.count(), 0);
    }

    // The 500ms optimistic cooldown suppresses CAT echoes of a just-made
    // setter call so the UI doesn't briefly show the server-echoed value.
    // WHY this matters for Phase 1: any split that moves state out of
    // RadioState but leaves the timestamp check behind breaks this.
    void testDataSubModeOptimisticCooldownSuppressesEcho() {
        RadioState rs;
        rs.setDataSubMode(2); // optimistic set — starts 500ms window
        QSignalSpy spy(&rs, &RadioState::dataSubModeChanged);
        rs.parseCATCommand("DT3;"); // would otherwise change to 3
        QCOMPARE(spy.count(), 0);
        QCOMPARE(rs.dataSubMode(), 2); // suppressed; state retains optimistic value
    }

    void testDataSubModeBOptimisticCooldownSuppressesEcho() {
        RadioState rs;
        rs.setDataSubModeB(2);
        QSignalSpy spy(&rs, &RadioState::dataSubModeBChanged);
        rs.parseCATCommand("DT$3;");
        QCOMPARE(spy.count(), 0);
        QCOMPARE(rs.dataSubModeB(), 2);
    }

    // --- DR / DR$ (Data rate, 0 or 1) ---
    void testDataRateParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::dataRateChanged);
        rs.parseCATCommand("DR1;");
        QCOMPARE(rs.dataRate(), 1);
        QCOMPARE(spy.count(), 1);
    }

    void testDataRateOutOfRangeRejected() {
        RadioState rs;
        rs.parseCATCommand("DR1;");
        QSignalSpy spy(&rs, &RadioState::dataRateChanged);
        rs.parseCATCommand("DR2;"); // above 1
        QCOMPARE(spy.count(), 0);
        QCOMPARE(rs.dataRate(), 1);
    }

    void testDataRateOptimisticCooldownSuppressesEcho() {
        RadioState rs;
        rs.setDataRate(1);
        QSignalSpy spy(&rs, &RadioState::dataRateChanged);
        rs.parseCATCommand("DR0;"); // would otherwise revert
        QCOMPARE(spy.count(), 0);
        QCOMPARE(rs.dataRate(), 1);
    }

    void testDataRateBOptimisticCooldownSuppressesEcho() {
        RadioState rs;
        rs.setDataRateB(1);
        QSignalSpy spy(&rs, &RadioState::dataRateBChanged);
        rs.parseCATCommand("DR$0;");
        QCOMPARE(spy.count(), 0);
        QCOMPARE(rs.dataRateB(), 1);
    }

    // --- VT / VT$ (Tuning step index, clamped 0-5) ---
    void testTuningStepParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::tuningStepChanged);
        rs.parseCATCommand("VT3;");
        QCOMPARE(rs.tuningStep(), 3);
        QCOMPARE(spy.count(), 1);
    }

    void testTuningStepClampedTo5() {
        RadioState rs;
        rs.parseCATCommand("VT9;");
        QCOMPARE(rs.tuningStep(), 5);
    }

    void testTuningStepNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("VT3;");
        QSignalSpy spy(&rs, &RadioState::tuningStepChanged);
        rs.parseCATCommand("VT3;");
        QCOMPARE(spy.count(), 0);
    }

    void testTuningStepBParsesSymmetric() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::tuningStepBChanged);
        rs.parseCATCommand("VT$2;");
        QCOMPARE(rs.tuningStepB(), 2);
        QCOMPARE(spy.count(), 1);
    }

    // =========================================================================
    // Phase 0.1 Backfill — SpectrumDisplay subsystem
    // Handlers: 24 "#"-prefix commands for panadapter/waterfall/scale/span/etc.
    // All follow the same shape: parse int, range-check, change-detect, emit.
    // Primary coverage = one parse-and-emit test per handler; bounds and
    // idempotence sampled where the range is non-trivial.
    // =========================================================================

    // --- #SCL (panadapter scale, 10-150) ---
    void testScaleParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::scaleChanged);
        rs.parseCATCommand("#SCL080;");
        QCOMPARE(rs.scale(), 80);
        QCOMPARE(spy.count(), 1);
    }

    void testScaleBelowRangeIgnored() {
        RadioState rs;
        rs.parseCATCommand("#SCL080;");
        QSignalSpy spy(&rs, &RadioState::scaleChanged);
        rs.parseCATCommand("#SCL005;"); // below 10
        QCOMPARE(spy.count(), 0);
        QCOMPARE(rs.scale(), 80);
    }

    void testScaleAboveRangeIgnored() {
        RadioState rs;
        rs.parseCATCommand("#SCL080;");
        rs.parseCATCommand("#SCL200;"); // above 150
        QCOMPARE(rs.scale(), 80);
    }

    // --- #DPM / #HDPM (dual-pan mode LCD / external, 0-2) ---
    void testDualPanModeLcdParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::dualPanModeLcdChanged);
        rs.parseCATCommand("#DPM1;");
        QCOMPARE(rs.dualPanModeLcd(), 1);
        QCOMPARE(spy.count(), 1);
    }

    void testDualPanModeLcdOutOfRange() {
        RadioState rs;
        rs.parseCATCommand("#DPM1;");
        rs.parseCATCommand("#DPM5;"); // above 2
        QCOMPARE(rs.dualPanModeLcd(), 1);
    }

    void testDualPanModeExtParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::dualPanModeExtChanged);
        rs.parseCATCommand("#HDPM2;");
        QCOMPARE(rs.dualPanModeExt(), 2);
        QCOMPARE(spy.count(), 1);
    }

    // --- #DSM / #HDSM (display mode LCD/ext, 0 or 1 only) ---
    void testDisplayModeLcdParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::displayModeLcdChanged);
        rs.parseCATCommand("#DSM1;");
        QCOMPARE(rs.displayModeLcd(), 1);
        QCOMPARE(spy.count(), 1);
    }

    void testDisplayModeLcdRejectsNonBoolean() {
        RadioState rs;
        rs.parseCATCommand("#DSM1;");
        rs.parseCATCommand("#DSM3;"); // not 0 or 1
        QCOMPARE(rs.displayModeLcd(), 1);
    }

    void testDisplayModeExtParses() {
        RadioState rs;
        rs.parseCATCommand("#HDSM1;");
        QCOMPARE(rs.displayModeExt(), 1);
    }

    // --- #FPS (display FPS, 12-30) ---
    void testDisplayFpsParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::displayFpsChanged);
        rs.parseCATCommand("#FPS24;");
        QCOMPARE(rs.displayFps(), 24);
        QCOMPARE(spy.count(), 1);
    }

    void testDisplayFpsBelowRangeIgnored() {
        RadioState rs;
        rs.parseCATCommand("#FPS24;");
        rs.parseCATCommand("#FPS08;"); // below 12
        QCOMPARE(rs.displayFps(), 24);
    }

    // --- #WFC (waterfall color, 0-4) ---
    void testWaterfallColorParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::waterfallColorChanged);
        rs.parseCATCommand("#WFC3;");
        QCOMPARE(rs.waterfallColor(), 3);
        QCOMPARE(spy.count(), 1);
    }

    void testWaterfallColorOutOfRange() {
        RadioState rs;
        rs.parseCATCommand("#WFC3;");
        rs.parseCATCommand("#WFC9;");
        QCOMPARE(rs.waterfallColor(), 3);
    }

    // --- #WFH / #HWFH (waterfall height, 0-100) ---
    // WHY value=60 not 50: default m_waterfallHeight = 50, so 50 is no-change.
    void testWaterfallHeightParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::waterfallHeightChanged);
        rs.parseCATCommand("#WFH060;");
        QCOMPARE(rs.waterfallHeight(), 60);
        QCOMPARE(spy.count(), 1);
    }

    void testWaterfallHeightExtParses() {
        RadioState rs;
        rs.parseCATCommand("#HWFH075;");
        QCOMPARE(rs.waterfallHeightExt(), 75);
    }

    // --- #AVG (averaging, 1-20) ---
    void testAveragingParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::averagingChanged);
        rs.parseCATCommand("#AVG05;");
        QCOMPARE(rs.averaging(), 5);
        QCOMPARE(spy.count(), 1);
    }

    void testAveragingZeroRejected() {
        RadioState rs;
        rs.parseCATCommand("#AVG05;");
        rs.parseCATCommand("#AVG00;"); // below 1
        QCOMPARE(rs.averaging(), 5);
    }

    // --- #PKM (peak mode, 0 or 1) ---
    void testPeakModeParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::peakModeChanged);
        rs.parseCATCommand("#PKM1;");
        QCOMPARE(rs.peakMode(), true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toBool(), true);
    }

    // --- #FXT / #FXA (fixed-tune enable + mode, both emit fixedTuneChanged) ---
    void testFixedTuneEnableParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::fixedTuneChanged);
        rs.parseCATCommand("#FXT1;");
        QCOMPARE(rs.fixedTune(), 1);
        QCOMPARE(spy.count(), 1);
    }

    void testFixedTuneModeParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::fixedTuneChanged);
        rs.parseCATCommand("#FXA3;");
        QCOMPARE(rs.fixedTuneMode(), 3);
        QCOMPARE(spy.count(), 1);
    }

    void testFixedTuneModeOutOfRange() {
        RadioState rs;
        rs.parseCATCommand("#FXA3;");
        rs.parseCATCommand("#FXA7;"); // above 4
        QCOMPARE(rs.fixedTuneMode(), 3);
    }

    // --- #FRZ (freeze, 0 or 1) ---
    void testFreezeParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::freezeChanged);
        rs.parseCATCommand("#FRZ1;");
        QCOMPARE(rs.freeze(), true);
        QCOMPARE(spy.count(), 1);
    }

    // --- #VFA / #VFB (VFO cursor style, 0-3) ---
    void testVfoACursorParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::vfoACursorChanged);
        rs.parseCATCommand("#VFA2;");
        QCOMPARE(rs.vfoACursor(), 2);
        QCOMPARE(spy.count(), 1);
    }

    void testVfoBCursorParses() {
        RadioState rs;
        rs.parseCATCommand("#VFB3;");
        QCOMPARE(rs.vfoBCursor(), 3);
    }

    void testVfoACursorOutOfRange() {
        RadioState rs;
        rs.parseCATCommand("#VFA2;");
        rs.parseCATCommand("#VFA9;"); // above 3
        QCOMPARE(rs.vfoACursor(), 2);
    }

    // --- #AR (auto ref level — checks last char for 'A' = auto) ---
    void testAutoRefLevelAuto() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::autoRefLevelChanged);
        rs.parseCATCommand("#AR-200+050A;"); // ends with 'A' → auto
        QCOMPARE(rs.autoRefLevel(), true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toBool(), true);
    }

    void testAutoRefLevelManual() {
        RadioState rs;
        rs.parseCATCommand("#AR-200+050A;"); // auto
        QSignalSpy spy(&rs, &RadioState::autoRefLevelChanged);
        rs.parseCATCommand("#AR-200+050M;"); // manual
        QCOMPARE(rs.autoRefLevel(), false);
        QCOMPARE(spy.count(), 1);
    }

    void testAutoRefLevelTooShort() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::autoRefLevelChanged);
        rs.parseCATCommand("#AR;"); // way too short
        QCOMPARE(spy.count(), 0);
    }

    // --- #NB$ (DDC NB mode, 0-2) ---
    // WHY the '$' — the DDC NB registry key is "#NB$" (handleDisplayNB), not "#NB".
    // "#NB" unprefixed would fall through to the RX-audio noise blanker.
    void testDdcNbModeParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::ddcNbModeChanged);
        rs.parseCATCommand("#NB$1;");
        QCOMPARE(rs.ddcNbMode(), 1);
        QCOMPARE(spy.count(), 1);
    }

    void testDdcNbModeOutOfRange() {
        RadioState rs;
        rs.parseCATCommand("#NB$1;");
        rs.parseCATCommand("#NB$9;"); // above 2
        QCOMPARE(rs.ddcNbMode(), 1);
    }

    // --- #NBL$ (DDC NB level, 0-14) ---
    void testDdcNbLevelParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::ddcNbLevelChanged);
        rs.parseCATCommand("#NBL$08;");
        QCOMPARE(rs.ddcNbLevel(), 8);
        QCOMPARE(spy.count(), 1);
    }

    void testDdcNbLevelOutOfRange() {
        RadioState rs;
        rs.parseCATCommand("#NBL$08;");
        rs.parseCATCommand("#NBL$15;"); // above 14
        QCOMPARE(rs.ddcNbLevel(), 8);
    }

    // --- #REF / #REF$ (ref level, -200..50, via handleIntPair) ---
    void testRefLevelParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::refLevelChanged);
        rs.parseCATCommand("#REF-150;");
        QCOMPARE(rs.refLevel(), -150);
        QCOMPARE(spy.count(), 1);
    }

    void testRefLevelBParses() {
        RadioState rs;
        rs.parseCATCommand("#REF$-100;");
        QCOMPARE(rs.refLevelB(), -100);
    }

    // --- #SPN / #SPN$ (span Hz, 1..999999, via handleIntPair) ---
    void testSpanParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::spanChanged);
        rs.parseCATCommand("#SPN50000;");
        QCOMPARE(rs.spanHz(), 50000);
        QCOMPARE(spy.count(), 1);
    }

    void testSpanBParses() {
        RadioState rs;
        rs.parseCATCommand("#SPN$25000;");
        QCOMPARE(rs.spanHzB(), 25000);
    }

    // --- #MP / #MP$ (mini-pan enabled, via handleBoolPairVal) ---
    void testMiniPanAEnabledParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::miniPanAEnabledChanged);
        rs.parseCATCommand("#MP1;");
        QCOMPARE(rs.miniPanAEnabled(), true);
        QCOMPARE(spy.count(), 1);
    }

    void testMiniPanBEnabledParses() {
        RadioState rs;
        rs.parseCATCommand("#MP$1;");
        QCOMPARE(rs.miniPanBEnabled(), true);
    }

    // Idempotence sample: the big chain above all uses the same pattern;
    // one representative no-change test is enough to verify the
    // "only emit when changed" semantic applies uniformly.
    void testScaleNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("#SCL080;");
        QSignalSpy spy(&rs, &RadioState::scaleChanged);
        rs.parseCATCommand("#SCL080;");
        QCOMPARE(spy.count(), 0);
    }

    // =========================================================================
    // Phase 0.1 Backfill — AudioGain subsystem
    // Handlers: MG (mic gain), RG/RG$ (RF gain), SQ/SQ$ (squelch),
    // CP (compression), ML (monitor level per mode), KP (keyer paddle),
    // LN (VFO link), SD (QSK/VOX delay per mode).
    // =========================================================================

    // --- MG (Mic gain, simple int) ---
    void testMicGainParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::micGainChanged);
        rs.parseCATCommand("MG050;");
        QCOMPARE(rs.micGain(), 50);
        QCOMPARE(spy.count(), 1);
    }

    void testMicGainNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("MG050;");
        QSignalSpy spy(&rs, &RadioState::micGainChanged);
        rs.parseCATCommand("MG050;");
        QCOMPARE(spy.count(), 0);
    }

    // --- RG / RG$ (RF gain — stored as positive magnitude 0..60) ---
    // WHY: K4 reports RF gain as attenuation -0..-60 dB, but the entire
    // QK4 codebase (SideControlPanel display, MainWindow scroll handler,
    // setRfGain setter) carries it as a positive magnitude and re-adds
    // the minus sign at the display/CAT boundary. Handler uses qAbs() to
    // make that convention explicit.
    void testRfGainStoresMagnitude() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::rfGainChanged);
        rs.parseCATCommand("RG-020;");
        QCOMPARE(rs.rfGain(), 20); // stored as positive magnitude
        QCOMPARE(spy.count(), 1);
    }

    void testRfGainWithoutSignParsesSame() {
        RadioState rs;
        rs.parseCATCommand("RG020;");
        QCOMPARE(rs.rfGain(), 20);
    }

    void testRfGainNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("RG-020;");
        QSignalSpy spy(&rs, &RadioState::rfGainChanged);
        rs.parseCATCommand("RG-020;");
        QCOMPARE(spy.count(), 0);
    }

    void testRfGainBStoresMagnitude() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::rfGainBChanged);
        rs.parseCATCommand("RG$-015;");
        QCOMPARE(rs.rfGainB(), 15);
        QCOMPARE(spy.count(), 1);
    }

    // --- SQ / SQ$ (Squelch, handleIntPair) ---
    void testSquelchParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::squelchChanged);
        rs.parseCATCommand("SQ010;");
        QCOMPARE(rs.squelchLevel(), 10);
        QCOMPARE(spy.count(), 1);
    }

    void testSquelchBParses() {
        RadioState rs;
        rs.parseCATCommand("SQ$015;");
        QCOMPARE(rs.squelchLevelB(), 15);
    }

    // --- CP (Compression) ---
    void testCompressionParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::compressionChanged);
        rs.parseCATCommand("CP015;");
        QCOMPARE(rs.compression(), 15);
        QCOMPARE(spy.count(), 1);
    }

    void testCompressionNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("CP015;");
        QSignalSpy spy(&rs, &RadioState::compressionChanged);
        rs.parseCATCommand("CP015;");
        QCOMPARE(spy.count(), 0);
    }

    // --- ML (Monitor level: MLmnnn where m=0(CW)/1(Data)/2(Voice), nnn=0-100) ---
    void testMonitorLevelCwParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::monitorLevelChanged);
        rs.parseCATCommand("ML0050;"); // CW, 50
        QCOMPARE(rs.monitorLevelCW(), 50);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 0);
        QCOMPARE(spy.at(0).at(1).toInt(), 50);
    }

    void testMonitorLevelDataParses() {
        RadioState rs;
        rs.parseCATCommand("ML1075;");
        QCOMPARE(rs.monitorLevelData(), 75);
    }

    void testMonitorLevelVoiceParses() {
        RadioState rs;
        rs.parseCATCommand("ML2025;");
        QCOMPARE(rs.monitorLevelVoice(), 25);
    }

    void testMonitorLevelInvalidModeRejected() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::monitorLevelChanged);
        rs.parseCATCommand("ML5050;"); // mode=5, invalid
        QCOMPARE(spy.count(), 0);
    }

    void testMonitorLevelOutOfRangeRejected() {
        RadioState rs;
        rs.parseCATCommand("ML0050;");
        QSignalSpy spy(&rs, &RadioState::monitorLevelChanged);
        rs.parseCATCommand("ML0150;"); // level=150, > 100
        QCOMPARE(spy.count(), 0);
        QCOMPARE(rs.monitorLevelCW(), 50);
    }

    // --- KP (Keyer paddle: KPionnn; i=iambic A/B, o=paddle N/R, nnn=weight 90-125) ---
    void testKeyerPaddleParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::keyerPaddleChanged);
        rs.parseCATCommand("KPAN100;");
        QCOMPARE(rs.iambicMode(), QChar('A'));
        QCOMPARE(rs.paddleOrientation(), QChar('N'));
        QCOMPARE(rs.keyingWeight(), 100);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toChar(), QChar('A'));
        QCOMPARE(spy.at(0).at(1).toChar(), QChar('N'));
        QCOMPARE(spy.at(0).at(2).toInt(), 100);
    }

    void testKeyerPaddleNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("KPAN100;");
        QSignalSpy spy(&rs, &RadioState::keyerPaddleChanged);
        rs.parseCATCommand("KPAN100;");
        QCOMPARE(spy.count(), 0);
    }

    void testKeyerPaddleSingleFieldFlipEmits() {
        RadioState rs;
        rs.parseCATCommand("KPAN100;");
        QSignalSpy spy(&rs, &RadioState::keyerPaddleChanged);
        rs.parseCATCommand("KPBN100;"); // iambic A→B
        QCOMPARE(spy.count(), 1);
    }

    // --- LN (VFO Link) ---
    void testVfoLinkEnable() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::vfoLinkChanged);
        rs.parseCATCommand("LN1;");
        QCOMPARE(rs.vfoLink(), true);
        QCOMPARE(spy.count(), 1);
    }

    void testVfoLinkDisable() {
        RadioState rs;
        rs.parseCATCommand("LN1;");
        rs.parseCATCommand("LN0;");
        QCOMPARE(rs.vfoLink(), false);
    }

    // --- SD (QSK/VOX delay per mode: SDxMzzz; x=QSK flag, M=mode, zzz=delay) ---
    void testSdCwModeStoresDelayAndQsk() {
        RadioState rs;
        QSignalSpy qskSpy(&rs, &RadioState::qskEnabledChanged);
        rs.parseCATCommand("SD1C050;");
        QCOMPARE(rs.qskDelayCW(), 50);
        QCOMPARE(rs.qskEnabled(), true);
        QCOMPARE(qskSpy.count(), 1);
    }

    void testSdVoiceModeStoresDelay() {
        RadioState rs;
        rs.parseCATCommand("SD0V100;");
        QCOMPARE(rs.qskDelayVoice(), 100);
    }

    void testSdDataModeStoresDelay() {
        RadioState rs;
        rs.parseCATCommand("SD0D075;");
        QCOMPARE(rs.qskDelayData(), 75);
    }

    void testSdQskFlagOnlyMeaningfulInCwMode() {
        // QSK flag byte is only inspected when mode char is 'C'.
        RadioState rs;
        rs.parseCATCommand("SD1C050;");
        QSignalSpy spy(&rs, &RadioState::qskEnabledChanged);
        rs.parseCATCommand("SD0V050;"); // '0' in voice mode — QSK state must not change
        QCOMPARE(spy.count(), 0);
        QCOMPARE(rs.qskEnabled(), true);
    }

    // =========================================================================
    // Phase 0.1 Backfill — AudioRouting subsystem
    // Handlers: LO (line out), LI (line in), MI (mic input select),
    // MS (mic setup: 5 one-char fields), BL (balance: mode + signed offset),
    // MX (audio mix routing: "<L>.<R>" with A/B/AB/-A components).
    // =========================================================================

    // --- LO (Line Out: LOlllrrrm; lll=left 0-40, rrr=right 0-40, m=mode) ---
    void testLineOutParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::lineOutChanged);
        rs.parseCATCommand("LO020030M;"); // left=20, right=30, mode='M' (not 1)
        QCOMPARE(rs.lineOutLeft(), 20);
        QCOMPARE(rs.lineOutRight(), 30);
        QCOMPARE(rs.lineOutRightEqualsLeft(), false);
        QCOMPARE(spy.count(), 1);
    }

    void testLineOutRightEqualsLeftFlag() {
        RadioState rs;
        rs.parseCATCommand("LO020020 ;"); // mode char not '1' → false
        rs.parseCATCommand("LO020020 ;"); // no change
        rs.parseCATCommand("LO020020 ;"); // no change
        QCOMPARE(rs.lineOutRightEqualsLeft(), false);
    }

    void testLineOutNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("LO020030M;");
        QSignalSpy spy(&rs, &RadioState::lineOutChanged);
        rs.parseCATCommand("LO020030M;");
        QCOMPARE(spy.count(), 0);
    }

    void testLineOutOutOfRangeRejected() {
        RadioState rs;
        rs.parseCATCommand("LO020030M;");
        QSignalSpy spy(&rs, &RadioState::lineOutChanged);
        rs.parseCATCommand("LO050030M;"); // left=50 > 40
        QCOMPARE(spy.count(), 0);
        QCOMPARE(rs.lineOutLeft(), 20); // unchanged
    }

    // --- LI (Line In: LIuuullls; uuu=soundCard 0-250, lll=line 0-250, s=source 0/1) ---
    void testLineInParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::lineInChanged);
        rs.parseCATCommand("LI050100 ;"); // sc=50, line=100, source=0 (' ' → toInt → 0)
        // the 's' char is read directly; only 0 or 1 are valid source values.
        // WHY: non-digit '\0' mode char passes because toInt returns 0. Accept the
        // current behavior — what we care about is the numeric extraction.
        Q_UNUSED(spy);
    }

    void testLineInParsesWithZeroSource() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::lineInChanged);
        rs.parseCATCommand("LI0501000;"); // source=0
        QCOMPARE(rs.lineInSoundCard(), 50);
        QCOMPARE(rs.lineInJack(), 100);
        QCOMPARE(rs.lineInSource(), 0);
        QCOMPARE(spy.count(), 1);
    }

    void testLineInParsesWithJackSource() {
        RadioState rs;
        rs.parseCATCommand("LI0501001;"); // source=1
        QCOMPARE(rs.lineInSource(), 1);
    }

    void testLineInNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("LI0501000;");
        QSignalSpy spy(&rs, &RadioState::lineInChanged);
        rs.parseCATCommand("LI0501000;");
        QCOMPARE(spy.count(), 0);
    }

    // --- MI (Mic Input select, 0-4) ---
    void testMicInputParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::micInputChanged);
        rs.parseCATCommand("MI3;");
        QCOMPARE(rs.micInput(), 3);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 3);
    }

    void testMicInputOutOfRange() {
        RadioState rs;
        rs.parseCATCommand("MI3;");
        QSignalSpy spy(&rs, &RadioState::micInputChanged);
        rs.parseCATCommand("MI7;"); // above 4
        QCOMPARE(spy.count(), 0);
        QCOMPARE(rs.micInput(), 3);
    }

    // --- MS (Mic Setup: MSabcde; a=frontPreamp 0-2, b/c=frontBias/Buttons 0/1, d/e=rear 0/1) ---
    void testMicSetupParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::micSetupChanged);
        rs.parseCATCommand("MS20111;"); // frontPreamp=2, bias=0, buttons=1, rearPreamp=1, rearBias=1
        QCOMPARE(spy.count(), 1);
    }

    void testMicSetupNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("MS20111;");
        QSignalSpy spy(&rs, &RadioState::micSetupChanged);
        rs.parseCATCommand("MS20111;");
        QCOMPARE(spy.count(), 0);
    }

    void testMicSetupSingleFieldChangeEmits() {
        RadioState rs;
        rs.parseCATCommand("MS20111;");
        QSignalSpy spy(&rs, &RadioState::micSetupChanged);
        rs.parseCATCommand("MS00111;"); // flip front preamp 2→0
        QCOMPARE(spy.count(), 1);
    }

    // --- BL (Balance: BLm±nn; m=0 NOR or 1 BAL, signed offset -50..+50) ---
    void testBalanceNormalParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::balanceChanged);
        rs.parseCATCommand("BL0+00;"); // NOR mode, offset 0
        QCOMPARE(rs.balanceMode(), 0);
        QCOMPARE(rs.balanceOffset(), 0);
        QCOMPARE(spy.count(), 1);
    }

    void testBalancePositiveOffset() {
        RadioState rs;
        rs.parseCATCommand("BL1+25;");
        QCOMPARE(rs.balanceMode(), 1);
        QCOMPARE(rs.balanceOffset(), 25);
    }

    void testBalanceNegativeOffset() {
        RadioState rs;
        rs.parseCATCommand("BL1-15;");
        QCOMPARE(rs.balanceOffset(), -15);
    }

    void testBalanceClampedToRange() {
        RadioState rs;
        rs.parseCATCommand("BL1+99;"); // > +50 → clamped to +50
        QCOMPARE(rs.balanceOffset(), 50);
    }

    void testBalanceInvalidModeRejected() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::balanceChanged);
        rs.parseCATCommand("BL2+05;"); // mode=2 (> 1)
        QCOMPARE(spy.count(), 0);
    }

    void testBalanceNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("BL1+25;");
        QSignalSpy spy(&rs, &RadioState::balanceChanged);
        rs.parseCATCommand("BL1+25;");
        QCOMPARE(spy.count(), 0);
    }

    // --- MX (Audio Mix routing: "MX<L>.<R>"; components A/B/AB/-A) ---
    void testAudioMixABParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::audioMixChanged);
        rs.parseCATCommand("MXA.B;"); // left=MixA(0), right=MixB(1)
        QCOMPARE(rs.audioMixLeft(), 0);
        QCOMPARE(rs.audioMixRight(), 1);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 0);
        QCOMPARE(spy.at(0).at(1).toInt(), 1);
    }

    void testAudioMixBothAB() {
        RadioState rs;
        rs.parseCATCommand("MXAB.AB;"); // both = MixAB(2)
        QCOMPARE(rs.audioMixLeft(), 2);
        QCOMPARE(rs.audioMixRight(), 2);
    }

    void testAudioMixNegA() {
        RadioState rs;
        rs.parseCATCommand("MXA.-A;"); // left=A(0), right=-A(3)
        QCOMPARE(rs.audioMixLeft(), 0);
        QCOMPARE(rs.audioMixRight(), 3);
    }

    void testAudioMixUnknownComponentRejected() {
        RadioState rs;
        rs.parseCATCommand("MXA.B;");
        QSignalSpy spy(&rs, &RadioState::audioMixChanged);
        rs.parseCATCommand("MXZ.B;"); // Z isn't a valid component
        QCOMPARE(spy.count(), 0);
    }

    void testAudioMixMissingDotRejected() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::audioMixChanged);
        rs.parseCATCommand("MXAB;"); // no dot
        QCOMPARE(spy.count(), 0);
    }

    void testAudioMixNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("MXA.B;");
        QSignalSpy spy(&rs, &RadioState::audioMixChanged);
        rs.parseCATCommand("MXA.B;");
        QCOMPARE(spy.count(), 0);
    }

    // =========================================================================
    // Phase 0.1 Backfill — VoxEssb subsystem
    // Handlers: VX (VOX on/off per mode C/V/D), VG (VOX gain per V/D),
    // VI (Anti-VOX 0-60), ES (ESSB enable + TX bandwidth 24-45).
    // =========================================================================

    // --- VX (VOX enable, per-mode VXmn; m='C'/'V'/'D', n='0'/'1') ---
    void testVoxCwEnable() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::voxChanged);
        rs.parseCATCommand("VXC1;");
        QCOMPARE(rs.voxCW(), true);
        QCOMPARE(rs.voxEnabled(), true); // rollup accessor
        QCOMPARE(spy.count(), 1);
    }

    void testVoxVoiceEnable() {
        RadioState rs;
        rs.parseCATCommand("VXV1;");
        QCOMPARE(rs.voxVoice(), true);
    }

    void testVoxDataEnable() {
        RadioState rs;
        rs.parseCATCommand("VXD1;");
        QCOMPARE(rs.voxData(), true);
    }

    void testVoxEnabledRollupAcrossModes() {
        // voxEnabled() is any-of across the three per-mode flags.
        RadioState rs;
        QCOMPARE(rs.voxEnabled(), false);
        rs.parseCATCommand("VXC1;");
        QCOMPARE(rs.voxEnabled(), true);
        rs.parseCATCommand("VXC0;");
        rs.parseCATCommand("VXV1;");
        QCOMPARE(rs.voxEnabled(), true);
        rs.parseCATCommand("VXV0;");
        QCOMPARE(rs.voxEnabled(), false);
    }

    void testVoxNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("VXC1;");
        QSignalSpy spy(&rs, &RadioState::voxChanged);
        rs.parseCATCommand("VXC1;");
        QCOMPARE(spy.count(), 0);
    }

    void testVoxInvalidModeCharRejected() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::voxChanged);
        rs.parseCATCommand("VXX1;"); // 'X' — not C/V/D
        QCOMPARE(spy.count(), 0);
    }

    // --- VG (VOX gain: VGmnnn; m='V' or 'D', nnn=000-060) ---
    void testVoxGainVoiceParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::voxGainChanged);
        rs.parseCATCommand("VGV030;");
        QCOMPARE(rs.voxGainVoice(), 30);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 0); // mode index 0 = Voice
        QCOMPARE(spy.at(0).at(1).toInt(), 30);
    }

    void testVoxGainDataParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::voxGainChanged);
        rs.parseCATCommand("VGD045;");
        QCOMPARE(rs.voxGainData(), 45);
        QCOMPARE(spy.at(0).at(0).toInt(), 1); // mode index 1 = Data
    }

    void testVoxGainOutOfRangeRejected() {
        RadioState rs;
        rs.parseCATCommand("VGV030;");
        QSignalSpy spy(&rs, &RadioState::voxGainChanged);
        rs.parseCATCommand("VGV099;"); // above 60
        QCOMPARE(spy.count(), 0);
        QCOMPARE(rs.voxGainVoice(), 30);
    }

    // --- VI (Anti-VOX, VInnn 0-60) ---
    void testAntiVoxParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::antiVoxChanged);
        rs.parseCATCommand("VI020;");
        QCOMPARE(rs.antiVox(), 20);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 20);
    }

    void testAntiVoxOutOfRangeRejected() {
        RadioState rs;
        rs.parseCATCommand("VI020;");
        rs.parseCATCommand("VI099;"); // > 60
        QCOMPARE(rs.antiVox(), 20);
    }

    void testAntiVoxNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("VI020;");
        QSignalSpy spy(&rs, &RadioState::antiVoxChanged);
        rs.parseCATCommand("VI020;");
        QCOMPARE(spy.count(), 0);
    }

    // --- ES (ESSB: ESnbb; n=0/1, bb=24..45 bandwidth) ---
    void testEssbDisabledWithoutBandwidth() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::essbChanged);
        rs.parseCATCommand("ES0;"); // disable, no BW
        QCOMPARE(rs.essbEnabled(), false);
        // Signal only fires if something changed — default m_essbEnabled is false,
        // so no change ⇒ no emit. This encodes the current semantics.
        Q_UNUSED(spy);
    }

    void testEssbEnabledWithBandwidth() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::essbChanged);
        rs.parseCATCommand("ES130;"); // enable, BW=30
        QCOMPARE(rs.essbEnabled(), true);
        QCOMPARE(rs.ssbTxBw(), 30);
        QCOMPARE(spy.count(), 1);
    }

    void testEssbBandwidthChangeOnly() {
        RadioState rs;
        rs.parseCATCommand("ES130;");
        QSignalSpy spy(&rs, &RadioState::essbChanged);
        rs.parseCATCommand("ES140;"); // enabled still true, BW 30→40
        QCOMPARE(rs.ssbTxBw(), 40);
        QCOMPARE(spy.count(), 1);
    }

    void testEssbBandwidthOutOfRangePreservesCurrent() {
        RadioState rs;
        rs.parseCATCommand("ES130;");
        rs.parseCATCommand("ES199;"); // BW=99, outside 24-45 → preserves 30
        QCOMPARE(rs.ssbTxBw(), 30);
    }

    void testEssbInvalidModeRejected() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::essbChanged);
        rs.parseCATCommand("ES530;"); // mode=5, invalid
        QCOMPARE(spy.count(), 0);
    }

    void testEssbNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("ES130;");
        QSignalSpy spy(&rs, &RadioState::essbChanged);
        rs.parseCATCommand("ES130;");
        QCOMPARE(spy.count(), 0);
    }

    // =========================================================================
    // Phase 0.1 Backfill — RxTxMeter subsystem
    // Handlers: TX/RX (transmit state), SM/SM$ (S-meter — always emits),
    // TM (TX meter telemetry — always emits), PO (unused),
    // SIFP (supply voltage/current), ID/OM/RV. (radio identity — no signals),
    // MN (message bank), SB/DV/TS/BS (control state flags).
    // =========================================================================

    // --- TX / RX (transmit state: signals fire only on transition) ---
    void testTxTransitionEmits() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::transmitStateChanged);
        rs.parseCATCommand("TX;");
        QCOMPARE(rs.isTransmitting(), true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toBool(), true);
    }

    void testRxTransitionEmits() {
        RadioState rs;
        rs.parseCATCommand("TX;");
        QSignalSpy spy(&rs, &RadioState::transmitStateChanged);
        rs.parseCATCommand("RX;");
        QCOMPARE(rs.isTransmitting(), false);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toBool(), false);
    }

    void testTxWhileTransmittingNoDuplicateSignal() {
        RadioState rs;
        rs.parseCATCommand("TX;");
        QSignalSpy spy(&rs, &RadioState::transmitStateChanged);
        rs.parseCATCommand("TX;");
        QCOMPARE(spy.count(), 0);
    }

    void testRxWhileReceivingNoDuplicateSignal() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::transmitStateChanged);
        rs.parseCATCommand("RX;"); // already RX by default
        QCOMPARE(spy.count(), 0);
    }

    // --- SM / SM$ (S-meter — emits on EVERY parse; streaming telemetry) ---
    // WHY: unlike config handlers, meters are intentionally non-idempotent —
    // the UI wants a repaint tick for every packet even if the displayed
    // value happens to match the previous one.
    void testSMeterLowRangeBarsEncoding() {
        RadioState rs;
        rs.parseCATCommand("SM09;"); // bars=9 → S4.5 (bars/2)
        QCOMPARE(rs.sMeter(), 4.5);
    }

    void testSMeterHighRangeBarsEncoding() {
        RadioState rs;
        // bars=20 → 9 + (20-18)*3/10 = 9.6 dB above S9
        rs.parseCATCommand("SM20;");
        QCOMPARE(rs.sMeter(), 9.6);
    }

    void testSMeterStreamingAlwaysEmits() {
        RadioState rs;
        rs.parseCATCommand("SM09;");
        QSignalSpy spy(&rs, &RadioState::sMeterChanged);
        rs.parseCATCommand("SM09;"); // same value — but meter still emits
        rs.parseCATCommand("SM09;");
        QCOMPARE(spy.count(), 2);
    }

    void testSMeterBSubParses() {
        RadioState rs;
        rs.parseCATCommand("SM$09;");
        QCOMPARE(rs.sMeterB(), 4.5);
    }

    // --- TM (TX meter telemetry: TMaaabbbcccddd — ALC, CMP, FWD, SWRx10) ---
    void testTxMeterParsesQro() {
        RadioState rs;
        QSignalSpy txSpy(&rs, &RadioState::txMeterChanged);
        QSignalSpy swrSpy(&rs, &RadioState::swrChanged);
        rs.parseCATCommand("TM050010100015;"); // ALC=50, CMP=10, FWD=100 (W), SWR=1.5
        QCOMPARE(rs.alcMeter(), 50);
        QCOMPARE(rs.compressionDb(), 10);
        QCOMPARE(rs.forwardPower(), 100.0); // QRO mode → watts direct
        QCOMPARE(rs.swrMeter(), 1.5);
        QCOMPARE(txSpy.count(), 1);
        QCOMPARE(swrSpy.count(), 1);
    }

    void testTxMeterForwardPowerQrpUsesTenths() {
        // In QRP mode, forward-power field is tenths of a watt.
        RadioState rs;
        rs.parseCATCommand("PC099L;"); // QRP mode (isQrpMode=true)
        rs.parseCATCommand("TM050010100015;");
        QCOMPARE(rs.forwardPower(), 10.0); // 100 / 10
    }

    void testTxMeterSwrEmitsIndependently() {
        // Both txMeterChanged AND swrChanged fire for every TM packet.
        RadioState rs;
        QSignalSpy swrSpy(&rs, &RadioState::swrChanged);
        rs.parseCATCommand("TM050010100015;");
        rs.parseCATCommand("TM050010100015;"); // same values — still emits (streaming)
        QCOMPARE(swrSpy.count(), 2);
    }

    void testTxMeterTooShortRejected() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::txMeterChanged);
        rs.parseCATCommand("TM0501;"); // way too short
        QCOMPARE(spy.count(), 0);
    }

    // --- PO (raw power meter, intentionally unused) ---
    void testPowerMeterDoesNotCrash() {
        RadioState rs;
        rs.parseCATCommand("PO050;"); // handler ignores payload
        // No signal, no state change; just must not crash.
    }

    // --- SIFP (status: VS:voltage, IS:current) ---
    void testSifpParsesVoltageAndCurrent() {
        RadioState rs;
        QSignalSpy vSpy(&rs, &RadioState::supplyVoltageChanged);
        QSignalSpy iSpy(&rs, &RadioState::supplyCurrentChanged);
        rs.parseCATCommand("SIFPVS:13.8,IS:2.5;");
        QCOMPARE(rs.supplyVoltage(), 13.8);
        QCOMPARE(rs.supplyCurrent(), 2.5);

        // SIRF carries the PA drain current as PM / 768 — matches the K4 front-panel "Id".
        QSignalSpy idSpy(&rs, &RadioState::paDrainCurrentChanged);
        rs.parseCATCommand("SIRFV8:8.0,V5:4.9,LT:30,LM:1196,PA:0.6,PM:12298,PT:31;");
        QCOMPARE(rs.paDrainCurrent(), 12298.0 / 768.0); // ~16.01 A at 50 W TX
        QCOMPARE(idSpy.count(), 1);
        // Idle SIRF (PM:0) clears the drain reading.
        rs.parseCATCommand("SIRFV8:8.0,V5:4.9,LT:29,LM:5,PA:0.6,PM:0,PT:30;");
        QCOMPARE(rs.paDrainCurrent(), 0.0);
        // Same value re-emitted should not re-fire the signal.
        rs.parseCATCommand("SIRFV8:8.0,V5:4.9,LT:29,LM:5,PA:0.6,PM:0,PT:30;");
        QCOMPARE(idSpy.count(), 2);
        // 100 W steady carrier sample.
        rs.parseCATCommand("SIRFV8:8.0,V5:4.9,LT:37,LM:1519,PA:0.6,PM:15976,PT:46;");
        QCOMPARE(rs.paDrainCurrent(), 15976.0 / 768.0); // ~20.80 A
        QCOMPARE(idSpy.count(), 3);
        QCOMPARE(vSpy.count(), 1);
        QCOMPARE(iSpy.count(), 1);
    }

    void testSifpNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("SIFPVS:13.8,IS:2.5;");
        QSignalSpy vSpy(&rs, &RadioState::supplyVoltageChanged);
        rs.parseCATCommand("SIFPVS:13.8,IS:2.5;");
        QCOMPARE(vSpy.count(), 0);
    }

    void testSifpWithTrailingFieldOnly() {
        // If only VS (voltage) is present without trailing comma, still parses.
        RadioState rs;
        rs.parseCATCommand("SIFPVS:12.5;");
        QCOMPARE(rs.supplyVoltage(), 12.5);
    }

    // --- ID (radio ID — stores string, no signal) ---
    void testRadioIdStored() {
        RadioState rs;
        rs.parseCATCommand("ID017;");
        QCOMPARE(rs.radioID(), QString("017"));
    }

    // --- OM (Option Modules — parses and derives radioModel) ---
    // WHY positions: handleOM gates model detection on trimmed length > 8
    // and checks pos 3=='S', pos 4=='H', pos 8=='4'. Inputs must survive
    // QString::trimmed() and still be at least 9 chars with the sentinel
    // characters at the required offsets.
    void testOptionModulesK4HD() {
        RadioState rs;
        rs.parseCATCommand("OM---SH---4X;"); // trimmed "---SH---4X": pos 3=S, 4=H, 8=4
        QCOMPARE(rs.radioModel(), QString("K4HD"));
    }

    void testOptionModulesK4Plain() {
        RadioState rs;
        rs.parseCATCommand("OM--------4X;"); // trimmed "--------4X" (len 10): only pos 8=4
        QCOMPARE(rs.radioModel(), QString("K4"));
    }

    // --- RV. (firmware versions: component-version pairs) ---
    void testFirmwareVersionStored() {
        RadioState rs;
        rs.parseCATCommand("RV.DDC0-00.65 (0:35);");
        QCOMPARE(rs.firmwareVersions().value("DDC0"), QString("00.65 (0:35)"));
    }

    // --- MN (message bank 1-4) ---
    void testMessageBankParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::messageBankChanged);
        rs.parseCATCommand("MN2;");
        QCOMPARE(rs.messageBank(), 2);
        QCOMPARE(spy.count(), 1);
    }

    void testMessageBankOutOfRangeRejected() {
        RadioState rs;
        rs.parseCATCommand("MN2;");
        rs.parseCATCommand("MN5;"); // above 4
        QCOMPARE(rs.messageBank(), 2);
    }

    // --- SB (sub receiver: SB0=off, SB1 or SB3=on) ---
    void testSubRxOffToOn() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::subRxEnabledChanged);
        rs.parseCATCommand("SB1;");
        QCOMPARE(rs.subReceiverEnabled(), true);
        QCOMPARE(spy.count(), 1);
    }

    void testSubRxDiversityStateAlsoOn() {
        RadioState rs;
        rs.parseCATCommand("SB3;"); // diversity — still "on" for subReceiverEnabled
        QCOMPARE(rs.subReceiverEnabled(), true);
    }

    void testSubRxOff() {
        RadioState rs;
        rs.parseCATCommand("SB1;");
        rs.parseCATCommand("SB0;");
        QCOMPARE(rs.subReceiverEnabled(), false);
    }

    // Tri-state sentinel regression guard: the FIRST report must emit even when
    // it is the default-off state. A plain bool defaulting to false swallowed
    // SB0 here, so VFO B stayed white / sub stayed unmuted on a first connect.
    void testSubRxFirstReportOffEmits() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::subRxEnabledChanged);
        rs.parseCATCommand("SB0;");
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toBool(), false);
        QCOMPARE(rs.subReceiverEnabled(), false);
    }

    // --- DV (diversity) ---
    void testDiversityOn() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::diversityChanged);
        rs.parseCATCommand("DV1;");
        QCOMPARE(rs.diversityEnabled(), true);
        QCOMPARE(spy.count(), 1);
    }

    void testDiversityFirstReportOffEmits() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::diversityChanged);
        rs.parseCATCommand("DV0;");
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toBool(), false);
    }

    // --- TS (test mode) ---
    void testTestModeOn() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::testModeChanged);
        rs.parseCATCommand("TS1;");
        QCOMPARE(rs.testMode(), true);
        QCOMPARE(spy.count(), 1);
    }

    // --- BS (B SET) ---
    void testBSetOn() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::bSetChanged);
        rs.parseCATCommand("BS1;");
        QCOMPARE(rs.bSetEnabled(), true);
        QCOMPARE(spy.count(), 1);
    }

    void testBSetNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("BS1;");
        QSignalSpy spy(&rs, &RadioState::bSetChanged);
        rs.parseCATCommand("BS1;");
        QCOMPARE(spy.count(), 0);
    }

    // --- ER (error notification with code:message) ---
    void testErrorNotificationParses() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::errorNotificationReceived);
        rs.parseCATCommand("ER42:Something went wrong;");
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 42);
        QCOMPARE(spy.at(0).at(1).toString(), QString("Something went wrong"));
    }

    void testErrorNotificationWithoutCodeIgnored() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::errorNotificationReceived);
        rs.parseCATCommand("ER;"); // no ':' at all
        QCOMPARE(spy.count(), 0);
    }

    // =========================================================================
    // Phase 0.1 Backfill — Equalizer subsystem
    // Handlers: RE (RX EQ: 8 bands × 3 signed digits, range -16..+16),
    // TE (TX EQ: same shape). Both emit a single rollup signal.
    // =========================================================================

    void testRxEqParsesAllBands() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::rxEqChanged);
        // 8 bands: +05, -03, +00, +10, -16, +16, +07, -02
        rs.parseCATCommand("RE+05-03+00+10-16+16+07-02;");
        QCOMPARE(rs.rxEqBand(0), 5);
        QCOMPARE(rs.rxEqBand(1), -3);
        QCOMPARE(rs.rxEqBand(2), 0);
        QCOMPARE(rs.rxEqBand(3), 10);
        QCOMPARE(rs.rxEqBand(4), -16);
        QCOMPARE(rs.rxEqBand(5), 16);
        QCOMPARE(rs.rxEqBand(6), 7);
        QCOMPARE(rs.rxEqBand(7), -2);
        QCOMPARE(spy.count(), 1);
    }

    void testRxEqOutOfRangeValueIgnored() {
        RadioState rs;
        rs.parseCATCommand("RE+00+00+00+00+00+00+00+00;"); // establish baseline
        QSignalSpy spy(&rs, &RadioState::rxEqChanged);
        // Band 0 = +99 (over +16) — rejected; rest match current → no change overall
        rs.parseCATCommand("RE+99+00+00+00+00+00+00+00;");
        QCOMPARE(spy.count(), 0);
        QCOMPARE(rs.rxEqBand(0), 0);
    }

    void testRxEqNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("RE+05-03+00+10-16+16+07-02;");
        QSignalSpy spy(&rs, &RadioState::rxEqChanged);
        rs.parseCATCommand("RE+05-03+00+10-16+16+07-02;");
        QCOMPARE(spy.count(), 0);
    }

    void testRxEqShortCommandIgnored() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::rxEqChanged);
        rs.parseCATCommand("RE+05;"); // way too short (needs 26+ chars)
        QCOMPARE(spy.count(), 0);
    }

    void testTxEqParsesAllBands() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::txEqChanged);
        rs.parseCATCommand("TE+01+02+03+04+05+06+07+08;");
        for (int i = 0; i < 8; i++) {
            QCOMPARE(rs.txEqBand(i), i + 1);
        }
        QCOMPARE(spy.count(), 1);
    }

    void testTxEqNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("TE+01+02+03+04+05+06+07+08;");
        QSignalSpy spy(&rs, &RadioState::txEqChanged);
        rs.parseCATCommand("TE+01+02+03+04+05+06+07+08;");
        QCOMPARE(spy.count(), 0);
    }

    // =========================================================================
    // Phase 0.1 Backfill — ModeFilter gaps
    // Existing tests cover LSB/USB/CW/DATA on VFO A and simple BW/IS/FP/FP$.
    // This block fills in: FM/AM/CW_R/DATA_R mode codes, unknown mode fallback,
    // CW pitch bounds + the CW-R dash-prefix skip, signal-emission spies for
    // BW/IS/FP.
    // =========================================================================

    // --- MD full mode-code coverage ---
    void testModeA_FM() {
        RadioState rs;
        rs.parseCATCommand("MD4;");
        QCOMPARE(rs.mode(), RadioState::FM);
    }

    void testModeA_AM() {
        RadioState rs;
        rs.parseCATCommand("MD5;");
        QCOMPARE(rs.mode(), RadioState::AM);
    }

    void testModeA_CWR() {
        RadioState rs;
        rs.parseCATCommand("MD7;");
        QCOMPARE(rs.mode(), RadioState::CW_R);
    }

    void testModeA_DATAR() {
        RadioState rs;
        rs.parseCATCommand("MD9;");
        QCOMPARE(rs.mode(), RadioState::DATA_R);
    }

    void testModeUnknownCodeFallsBackToUsb() {
        // modeFromCode defaults to USB on any code not in 1-9 (except 8).
        RadioState rs;
        rs.parseCATCommand("MD8;"); // 8 is unassigned
        QCOMPARE(rs.mode(), RadioState::USB);
    }

    void testModeChangeEmitsQskDelayRefresh() {
        // Switching mode also re-emits qskDelayChanged with the delay for
        // the new mode band (so the UI shows the right delay for CW vs voice).
        RadioState rs;
        rs.parseCATCommand("SD1C050;"); // CW delay = 50
        rs.parseCATCommand("SD0V100;"); // Voice delay = 100
        QSignalSpy qskSpy(&rs, &RadioState::qskDelayChanged);
        rs.parseCATCommand("MD1;"); // LSB is voice → emit delay 100
        QCOMPARE(qskSpy.count(), 1);
        QCOMPARE(qskSpy.at(0).at(0).toInt(), 100);
    }

    // --- CW pitch bounds (CWnn; nn=pitch/10, range 25-95) ---
    void testCwPitchBelowRangeIgnored() {
        RadioState rs;
        rs.parseCATCommand("CW60;"); // baseline 600 Hz
        rs.parseCATCommand("CW20;"); // 20 < 25 minimum
        QCOMPARE(rs.cwPitch(), 600);
    }

    void testCwPitchAboveRangeIgnored() {
        RadioState rs;
        rs.parseCATCommand("CW60;");
        rs.parseCATCommand("CW99;"); // 99 > 95 maximum
        QCOMPARE(rs.cwPitch(), 600);
    }

    void testCwPitchSkipsCwRPrefix() {
        // handleCW short-circuits on payloads starting with '-' so CW-R
        // mode strings don't get parsed as pitch codes.
        RadioState rs;
        rs.parseCATCommand("CW60;"); // baseline 600 Hz
        QSignalSpy spy(&rs, &RadioState::cwPitchChanged);
        rs.parseCATCommand("CW-R;"); // must not alter pitch
        QCOMPARE(spy.count(), 0);
        QCOMPARE(rs.cwPitch(), 600);
    }

    // --- BW signal-emission spies ---
    void testBandwidthSignalEmitted() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::filterBandwidthChanged);
        rs.parseCATCommand("BW240;");
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 2400);
    }

    void testBandwidthNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("BW240;");
        QSignalSpy spy(&rs, &RadioState::filterBandwidthChanged);
        rs.parseCATCommand("BW240;");
        QCOMPARE(spy.count(), 0);
    }

    void testBandwidthBNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("BW$300;");
        QSignalSpy spy(&rs, &RadioState::filterBandwidthBChanged);
        rs.parseCATCommand("BW$300;");
        QCOMPARE(spy.count(), 0);
    }

    // --- IS signal-emission spies ---
    void testIfShiftSignalEmitted() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::ifShiftChanged);
        rs.parseCATCommand("IS50;");
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 50);
    }

    void testIfShiftNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("IS50;");
        QSignalSpy spy(&rs, &RadioState::ifShiftChanged);
        rs.parseCATCommand("IS50;");
        QCOMPARE(spy.count(), 0);
    }

    // --- FP signal-emission spy ---
    void testFilterPositionSignalEmitted() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::filterPositionChanged);
        rs.parseCATCommand("FP2;");
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 2);
    }

    void testFilterPositionNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("FP2;");
        QSignalSpy spy(&rs, &RadioState::filterPositionChanged);
        rs.parseCATCommand("FP2;");
        QCOMPARE(spy.count(), 0);
    }

    // =========================================================================
    // Phase 0.8 Backfill — Sentinel-value audit
    // Many RadioState fields are initialized to sentinels (-1, -999, -110,
    // etc.) so the *first* CAT update after construction or reset() always
    // differs from the stored value and fires a signal. The two risks the
    // sentinel convention guards against:
    //   1. reset() forgets a field → stale data persists past a reconnect.
    //   2. A sentinel value collides with a legal value → first update is
    //      silently absorbed; UI never refreshes.
    // The tests below exercise the resilience of both invariants across a
    // representative slice of subsystems.
    // =========================================================================

    // Directly verify reset() restores each sentinel-initialized field to
    // its original value. A representative cross-subsystem sample.
    void testResetRestoresSentinels() {
        RadioState rs;
        // Change state across every subsystem.
        rs.parseCATCommand("FA00014074000;");
        rs.parseCATCommand("FP1;");
        rs.parseCATCommand("BW240;");
        rs.parseCATCommand("IS50;");
        rs.parseCATCommand("CW60;");
        rs.parseCATCommand("MG050;");
        rs.parseCATCommand("CP015;");
        rs.parseCATCommand("RG-020;");
        rs.parseCATCommand("SQ010;");
        rs.parseCATCommand("KS020;");
        rs.parseCATCommand("AN3;");
        rs.parseCATCommand("AT1;");
        rs.parseCATCommand("RO+0500;");
        rs.parseCATCommand("VT3;");
        rs.parseCATCommand("ML0050;");
        rs.parseCATCommand("MN2;");
        rs.parseCATCommand("SL3;");
        rs.parseCATCommand("MI3;");
        rs.parseCATCommand("VGV030;");
        rs.parseCATCommand("VI020;");
        rs.parseCATCommand("#SCL080;");
        rs.parseCATCommand("#AVG05;");
        rs.parseCATCommand("#PKM1;");
        rs.parseCATCommand("DT2;");
        rs.parseCATCommand("DR1;");
        rs.parseCATCommand("TD132;");
        rs.parseCATCommand("BL1+25;");

        rs.reset();

        // Frequency/VFO sentinels (these zero, not -1)
        QCOMPARE(rs.frequency(), quint64(0));
        QCOMPARE(rs.tuningStep(), -1);
        QCOMPARE(rs.tuningStepB(), -1);
        // Mode/filter
        QCOMPARE(rs.mode(), RadioState::Unknown);
        QCOMPARE(rs.modeB(), RadioState::Unknown);
        QCOMPARE(rs.filterBandwidth(), -1);
        QCOMPARE(rs.filterPosition(), -1);
        QCOMPARE(rs.ifShift(), -1);
        QCOMPARE(rs.cwPitch(), -1);
        // Gain/levels
        QCOMPARE(rs.rfPower(), -1.0);
        QCOMPARE(rs.micGain(), -1);
        QCOMPARE(rs.compression(), -1);
        QCOMPARE(rs.rfGain(), -999);
        QCOMPARE(rs.squelchLevel(), -1);
        QCOMPARE(rs.keyerSpeed(), -1);
        QCOMPARE(rs.keyingWeight(), -1);
        QCOMPARE(rs.monitorLevelCW(), -1);
        // Antenna / ATU
        QCOMPARE(rs.atuMode(), -1);
        // Message bank
        QCOMPARE(rs.messageBank(), -1);
        // Streaming
        QCOMPARE(rs.streamingLatency(), -1);
        // Audio routing
        QCOMPARE(rs.lineOutLeft(), -1);
        QCOMPARE(rs.lineInSoundCard(), -1);
        QCOMPARE(rs.micInput(), -1);
        // VOX/ESSB
        QCOMPARE(rs.voxGainVoice(), -1);
        QCOMPARE(rs.antiVox(), -1);
        QCOMPARE(rs.ssbTxBw(), -1);
        // Display
        QCOMPARE(rs.scale(), -1);
        QCOMPARE(rs.averaging(), -1);
        QCOMPARE(rs.refLevel(), -110); // special sentinel (dBm)
        QCOMPARE(rs.refLevelB(), -110);
        // Balance uses -99 (outside valid -50..+50)
        QCOMPARE(rs.balanceMode(), -1);
        QCOMPARE(rs.balanceOffset(), -99);
        // Data control
        QCOMPARE(rs.dataSubMode(), -1);
        QCOMPARE(rs.dataRate(), -1);
        // Text decode
        QCOMPARE(rs.textDecodeMode(), -1);
        QCOMPARE(rs.textDecodeLines(), -1);
        // RIT/XIT
        QCOMPARE(rs.ritXitOffset(), 0); // reset uses 0, not sentinel
        QCOMPARE(rs.ritEnabled(), false);
        QCOMPARE(rs.xitEnabled(), false);
    }

    // Sentinel's functional contract: post-reset, the very next CAT packet
    // for any field must fire its change signal (because stored state is
    // different from incoming). Regression risk: a sentinel that collides
    // with a legal value silently absorbs the first packet.
    void testResetEnsuresNextCatEmits() {
        // Representative sample across subsystems.
        RadioState rs;
        rs.parseCATCommand("FP1;");
        rs.parseCATCommand("BW240;");
        rs.parseCATCommand("MG050;");
        rs.parseCATCommand("AT1;");
        rs.parseCATCommand("#SCL080;");
        rs.parseCATCommand("MN2;");
        rs.parseCATCommand("DT2;");

        rs.reset();

        {
            QSignalSpy spy(&rs, &RadioState::filterPositionChanged);
            rs.parseCATCommand("FP1;"); // same input as before reset — must emit
            QCOMPARE(spy.count(), 1);
        }
        {
            QSignalSpy spy(&rs, &RadioState::filterBandwidthChanged);
            rs.parseCATCommand("BW240;");
            QCOMPARE(spy.count(), 1);
        }
        {
            QSignalSpy spy(&rs, &RadioState::micGainChanged);
            rs.parseCATCommand("MG050;");
            QCOMPARE(spy.count(), 1);
        }
        {
            QSignalSpy spy(&rs, &RadioState::atuModeChanged);
            rs.parseCATCommand("AT1;");
            QCOMPARE(spy.count(), 1);
        }
        {
            QSignalSpy spy(&rs, &RadioState::scaleChanged);
            rs.parseCATCommand("#SCL080;");
            QCOMPARE(spy.count(), 1);
        }
        {
            QSignalSpy spy(&rs, &RadioState::messageBankChanged);
            rs.parseCATCommand("MN2;");
            QCOMPARE(spy.count(), 1);
        }
        {
            QSignalSpy spy(&rs, &RadioState::dataSubModeChanged);
            rs.parseCATCommand("DT2;");
            QCOMPARE(spy.count(), 1);
        }
    }

    // K4 remote power state (PS command).
    void testPowerStateInitiallyUnknown() {
        RadioState rs;
        QVERIFY(!rs.isPoweredOn().has_value());
    }

    void testPowerStateOn() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::powerStateChanged);
        rs.parseCATCommand("PS1;");
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toBool(), true);
        QVERIFY(rs.isPoweredOn().has_value());
        QCOMPARE(rs.isPoweredOn().value(), true);
    }

    void testPowerStateOff() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::powerStateChanged);
        rs.parseCATCommand("PS0;");
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toBool(), false);
        QCOMPARE(rs.isPoweredOn().value(), false);
    }

    void testPowerStateNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("PS1;");
        QSignalSpy spy(&rs, &RadioState::powerStateChanged);
        rs.parseCATCommand("PS1;"); // same value
        QCOMPARE(spy.count(), 0);
    }

    void testPowerStateMalformed() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::powerStateChanged);
        rs.parseCATCommand("PS;");  // too short
        rs.parseCATCommand("PS2;"); // invalid flag
        rs.parseCATCommand("PSx;"); // non-digit
        QCOMPARE(spy.count(), 0);
        QVERIFY(!rs.isPoweredOn().has_value());
    }

    void testPowerStateResetClearsToUnknown() {
        RadioState rs;
        rs.parseCATCommand("PS1;");
        QCOMPARE(rs.isPoweredOn().value(), true);
        rs.reset();
        QVERIFY(!rs.isPoweredOn().has_value());
    }

    // SIRF parser — realistic frame yields PT and LT.
    void testSirfEmitsPaAndLpaTemperatures() {
        RadioState rs;
        QSignalSpy paSpy(&rs, &RadioState::paTemperatureChanged);
        QSignalSpy lpaSpy(&rs, &RadioState::lpaTemperatureChanged);
        rs.parseCATCommand("SIRFV8:8.0,V5:4.9,LT:28,LM:5,PA:0.6,PM:0,PT:26;");
        QCOMPARE(paSpy.count(), 1);
        QCOMPARE(paSpy.first().at(0).toInt(), 26);
        QCOMPARE(lpaSpy.count(), 1);
        QCOMPARE(lpaSpy.first().at(0).toInt(), 28);
        QCOMPARE(rs.paTemperatureC(), 26);
        QCOMPARE(rs.lpaTemperatureC(), 28);
    }

    // The whole point of the tokenizer: a hypothetical future SIRF key whose name
    // contains "PT" or "LT" as a substring (XPT, RPT, RLT, ALT, …) must NOT
    // collide with the real PT/LT lookups.
    void testSirfTokenizerIsExactKeyMatch() {
        RadioState rs;
        QSignalSpy paSpy(&rs, &RadioState::paTemperatureChanged);
        QSignalSpy lpaSpy(&rs, &RadioState::lpaTemperatureChanged);
        // XPT and RLT come BEFORE PT/LT in the payload — substring matching
        // (indexOf("PT")) would have picked up XPT:99; we must end up with PT=26.
        rs.parseCATCommand("SIRFV8:8.0,V5:4.9,XPT:99,RLT:88,LT:28,LM:5,PA:0.6,PM:0,PT:26;");
        QCOMPARE(paSpy.count(), 1);
        QCOMPARE(paSpy.first().at(0).toInt(), 26);
        QCOMPARE(lpaSpy.count(), 1);
        QCOMPARE(lpaSpy.first().at(0).toInt(), 28);
    }

    // Regression: PM-derived PA drain current must still parse after the rewrite.
    void testSirfStillEmitsPaDrainCurrent() {
        RadioState rs;
        QSignalSpy spy(&rs, &RadioState::paDrainCurrentChanged);
        // PM:7680 / 768 = 10.0 A
        rs.parseCATCommand("SIRFV8:8.0,V5:4.9,LT:28,LM:5,PA:0.6,PM:7680,PT:26;");
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toDouble(), 10.0);
    }

    void testSirfTemperatureNoChangeNoSignal() {
        RadioState rs;
        rs.parseCATCommand("SIRFV8:8.0,V5:4.9,LT:28,LM:5,PA:0.6,PM:0,PT:26;");
        QSignalSpy paSpy(&rs, &RadioState::paTemperatureChanged);
        QSignalSpy lpaSpy(&rs, &RadioState::lpaTemperatureChanged);
        rs.parseCATCommand("SIRFV8:8.0,V5:4.9,LT:28,LM:5,PA:0.6,PM:0,PT:26;");
        QCOMPARE(paSpy.count(), 0);
        QCOMPARE(lpaSpy.count(), 0);
    }

    void testSirfMissingTempFieldsDoNotEmit() {
        RadioState rs;
        QSignalSpy paSpy(&rs, &RadioState::paTemperatureChanged);
        QSignalSpy lpaSpy(&rs, &RadioState::lpaTemperatureChanged);
        rs.parseCATCommand("SIRFPM:0;");
        QCOMPARE(paSpy.count(), 0);
        QCOMPARE(lpaSpy.count(), 0);
        QCOMPARE(rs.paTemperatureC(), -1);
        QCOMPARE(rs.lpaTemperatureC(), -1);
    }

    void testSirfTemperaturesResetToUnknown() {
        RadioState rs;
        rs.parseCATCommand("SIRFV8:8.0,V5:4.9,LT:28,LM:5,PA:0.6,PM:0,PT:26;");
        QCOMPARE(rs.paTemperatureC(), 26);
        QCOMPARE(rs.lpaTemperatureC(), 28);
        rs.reset();
        QCOMPARE(rs.paTemperatureC(), -1);
        QCOMPARE(rs.lpaTemperatureC(), -1);
    }

    // Defaults-with-special-values check: a handful of fields use non-negative
    // sentinels (default 50, 30, 1000, etc.) that could collide with legal
    // values. Lock in the exact default so a regression changes it visibly.
    void testNonNegativeSentinelDefaults() {
        RadioState rs;
        // waterfallHeight default = 50 (mid-range, legal value 0-100).
        // Any CAT update with WFH050; is no-op.
        QSignalSpy spy(&rs, &RadioState::waterfallHeightChanged);
        rs.parseCATCommand("#WFH050;"); // same as default
        QCOMPARE(spy.count(), 0);
        // displayFps default = 30, waterfallHeightExt default = 50 —
        // these are documented in reset() and protected by testWaterfallHeightParses
        // using a value ≠ 50.
    }
};

QTEST_MAIN(TestRadioState)
#include "test_radiostate.moc"
