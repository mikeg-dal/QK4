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
};

QTEST_MAIN(TestRadioState)
#include "test_radiostate.moc"
