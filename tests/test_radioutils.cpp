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

    // Constants
    void testConstants() {
        QCOMPARE(RadioUtils::SPAN_MIN, 5000);
        QCOMPARE(RadioUtils::SPAN_MAX, 368000);
        QVERIFY(RadioUtils::SPAN_THRESHOLD_UP > RadioUtils::SPAN_THRESHOLD_DOWN);
    }
};

QTEST_MAIN(TestRadioUtils)
#include "test_radioutils.moc"
