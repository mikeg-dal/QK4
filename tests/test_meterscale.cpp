#include <QtTest/QtTest>
#include <cmath>

#include "ui/widgets/txmeterwidget.h"

// Scale marks on the TX/RX meter bars must sit where their values actually fall.
//
// WHY this suite exists. Labels used to be spread evenly across the bar regardless of what they
// said — `x = barStartX + (barWidth * i) / (numLabels - 1)` — while the bar fill is linear in
// value/max. That is only correct when the label values are themselves evenly spaced across the
// full range. Three of six meters were not:
//
//   ALC   marked 1/3/5/7: gaps of 1, 2, 2, 2. The "3" mark sat at 50% of a bar where an ALC of 3
//         fills to 43% — about half a division out, on the meter an operator sets drive by.
//   SWR   the infinity label consumed a tick slot of its own, re-spacing every numbered mark;
//         the "2" mark sat where 1.8 was drawn.
//   S     S1 belongs at 1/15 of the bar, not at the left end, so every S-unit was shifted and
//         S9 was drawn at 57% where it fills to 60%.
//
// Po, COMP and Id were correct only because their label values happen to be evenly spaced.
//
// Each mark now carries the value it denotes alongside its position, so the two can be checked
// against each other — which is what these tests do.
class TestMeterScale : public QObject {
    Q_OBJECT

private slots:
    void everyMarkSitsAtItsOwnValue();
    void marksStayWithinTheBar();
    void sMeterStartsAtS1NotAtZero();
    void alcMarksAreNotEvenlySpaced();
    void swrSpansOneToThree();
    void evenlySpacedMetersAreUnchanged();

private:
    // The affine mapping every meter's fill uses: ratio = (value - min) / (max - min).
    static double expectedPos(double value, double minValue, double maxValue) {
        return (value - minValue) / (maxValue - minValue);
    }

    static void checkAll(const QList<TxMeterWidget::ScaleMark> &marks, double minValue, double maxValue,
                         const char *what) {
        QVERIFY2(!marks.isEmpty(), what);
        for (const auto &m : marks) {
            const double want = expectedPos(m.value, minValue, maxValue);
            QVERIFY2(std::fabs(m.pos - want) < 1e-9,
                     qPrintable(QString("%1: mark '%2' (value %3) sits at %4, should be %5")
                                    .arg(what)
                                    .arg(m.text)
                                    .arg(m.value)
                                    .arg(m.pos)
                                    .arg(want)));
        }
    }
};

void TestMeterScale::everyMarkSitsAtItsOwnValue() {
    checkAll(TxMeterWidget::sMeterMarks(), 0.0, TxMeterWidget::MaxSMeter, "S-meter");
    checkAll(TxMeterWidget::powerMarks(false), 0.0, TxMeterWidget::MaxPowerQRO, "Po (QRO)");
    checkAll(TxMeterWidget::powerMarks(true), 0.0, TxMeterWidget::MaxPowerQRP, "Po (QRP)");
    checkAll(TxMeterWidget::alcMarks(), 0.0, TxMeterWidget::MaxAlcBars, "ALC");
    checkAll(TxMeterWidget::compMarks(), 0.0, TxMeterWidget::MaxCompressionDb, "COMP");
    checkAll(TxMeterWidget::currentMarks(), 0.0, TxMeterWidget::MaxCurrentAmps, "Id");
    checkAll(TxMeterWidget::swrMarks(), TxMeterWidget::MinSwr, TxMeterWidget::MinSwr + TxMeterWidget::MaxSwrScale,
             "SWR");
}

void TestMeterScale::marksStayWithinTheBar() {
    const QList<QList<TxMeterWidget::ScaleMark>> all = {
        TxMeterWidget::sMeterMarks(), TxMeterWidget::powerMarks(false), TxMeterWidget::powerMarks(true),
        TxMeterWidget::alcMarks(),    TxMeterWidget::compMarks(),       TxMeterWidget::currentMarks(),
        TxMeterWidget::swrMarks(),
    };
    for (const auto &marks : all) {
        for (const auto &m : marks) {
            QVERIFY2(m.pos >= 0.0 && m.pos <= 1.0, "a mark fell outside the bar");
            QVERIFY2(!m.text.isEmpty(), "a mark has no text, so it draws a tick nobody can read");
        }
        // The top of every scale must be marked, or the operator cannot tell what full scale means.
        QVERIFY2(std::fabs(marks.last().pos - 1.0) < 1e-9, "the last mark must sit at the right end of the bar");
    }
}

void TestMeterScale::sMeterStartsAtS1NotAtZero() {
    const auto marks = TxMeterWidget::sMeterMarks();
    QCOMPARE(marks.first().text, QString("1"));

    // S1 is one unit up a fifteen-unit scale. Evenly spaced labels put it at 0.0, which is the
    // specific error this test exists to catch.
    QVERIFY(std::fabs(marks.first().pos - 1.0 / 15.0) < 1e-9);
    QVERIFY2(marks.first().pos > 0.0, "S1 must not sit at the left end of the bar");

    // S9 is the reference every operator reads, and it belongs at 60%, not the 57% that evenly
    // spaced labels produced.
    bool foundS9 = false;
    for (const auto &m : marks) {
        if (m.text == QLatin1String("9")) {
            foundS9 = true;
            QVERIFY(std::fabs(m.pos - 0.6) < 1e-9);
        }
    }
    QVERIFY2(foundS9, "the S9 mark is missing");
}

void TestMeterScale::alcMarksAreNotEvenlySpaced() {
    const auto marks = TxMeterWidget::alcMarks();
    QCOMPARE(marks.size(), 4);

    // 1, 3, 5, 7 over a 0..7 scale: the first gap is half the others. Even spacing would place
    // them at 0, 1/3, 2/3, 1 — assert we are not doing that.
    QVERIFY(std::fabs(marks[0].pos - 1.0 / 7.0) < 1e-9);
    QVERIFY(std::fabs(marks[3].pos - 1.0) < 1e-9);

    const double firstGap = marks[1].pos - marks[0].pos;
    const double secondGap = marks[2].pos - marks[1].pos;
    QVERIFY2(std::fabs(firstGap - secondGap) < 1e-9, "gaps between 1-3 and 3-5 are equal in value terms");
    QVERIFY2(marks[0].pos < firstGap, "the gap from 0 to the '1' mark is smaller than the rest");
}

void TestMeterScale::swrSpansOneToThree() {
    const auto marks = TxMeterWidget::swrMarks();
    QCOMPARE(marks.first().value, 1.0);
    QCOMPARE(marks.first().pos, 0.0); // SWR 1.0 is the left end, not zero-SWR
    QCOMPARE(marks.last().value, 3.0);
    QVERIFY(std::fabs(marks.last().pos - 1.0) < 1e-9);

    // SWR 2.0 is the midpoint of a 1..3 bar. With the infinity label taking a tick slot it landed
    // where 1.8 is drawn instead.
    for (const auto &m : marks) {
        if (std::fabs(m.value - 2.0) < 1e-9)
            QVERIFY(std::fabs(m.pos - 0.5) < 1e-9);
    }
}

void TestMeterScale::evenlySpacedMetersAreUnchanged() {
    // Po, COMP and Id were already correct. Their marks must still land on even spacing, so this
    // change cannot have moved a meter that was right to begin with.
    const auto po = TxMeterWidget::powerMarks(false);
    QCOMPARE(po.size(), 6);
    for (int i = 0; i < po.size(); i++)
        QVERIFY(std::fabs(po[i].pos - i / 5.0) < 1e-9);

    const auto comp = TxMeterWidget::compMarks();
    for (int i = 0; i < comp.size(); i++)
        QVERIFY(std::fabs(comp[i].pos - i / 5.0) < 1e-9);

    const auto id = TxMeterWidget::currentMarks();
    for (int i = 0; i < id.size(); i++)
        QVERIFY(std::fabs(id[i].pos - i / 5.0) < 1e-9);
}

// APPLESS: every assertion is on static functions, so no QApplication and no display server.
// CI runs headless; QTEST_MAIN would pull in QApplication and fail there.
QTEST_APPLESS_MAIN(TestMeterScale)
#include "test_meterscale.moc"
