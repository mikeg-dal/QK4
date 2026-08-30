#include "dsp/waterfallgeometry.h"
#include <QSet>
#include <QtTest>

// The shaders cannot be unit tested, so these assert the arithmetic that feeds them via the mirror
// in WaterfallGeometry::rowForPixel. The property that matters is that the visible pixels map onto
// exactly the newest visibleRows stored rows — each once, in order, and never the in-flight row.
class TestWaterfallGeometry : public QObject {
    Q_OBJECT

private slots:
    void visibleRowsFollowsPixelHeight();
    void visibleRowsClampsToStoredRows();
    void visibleRowsClampsToMinimum();

    void oldestVisibleWrapsAroundTheRing();

    void visibleFractionCarriesNoOffByOne();

    void everyPixelMapsInsideTheVisibleWindow();
    void topPixelIsTheNewestCompleteRow();
    void inFlightRowIsNeverSampled();
    void mappingIsOntoAndOrdered();
    void mappingHoldsAcrossTheRingWrap();
};

void TestWaterfallGeometry::visibleRowsFollowsPixelHeight() {
    QCOMPARE(WaterfallGeometry::visibleRowsFor(770.0f, 2048, 64), 770);
    QCOMPARE(WaterfallGeometry::visibleRowsFor(1540.0f, 2048, 64), 1540);
    // Rounds rather than truncates, so a half-pixel does not silently cost a row.
    QCOMPARE(WaterfallGeometry::visibleRowsFor(769.6f, 2048, 64), 770);
}

void TestWaterfallGeometry::visibleRowsClampsToStoredRows() {
    // Clamps to storedRows - 1, not storedRows: one row is always reserved for the write head, or
    // the window wraps onto the whole ring and the in-flight row reappears at the bottom. Past the
    // clamp pxPerRow goes fractional and the vertical blur returns, which the diagnostic reports.
    QCOMPARE(WaterfallGeometry::visibleRowsFor(4000.0f, 2048, 64), 2047);
}

void TestWaterfallGeometry::visibleRowsClampsToMinimum() {
    QCOMPARE(WaterfallGeometry::visibleRowsFor(10.0f, 2048, 64), 64);
    // A ring smaller than the floor cannot honour the floor; stored rows win.
    QCOMPARE(WaterfallGeometry::visibleRowsFor(10.0f, 32, 64), 31);
}

void TestWaterfallGeometry::oldestVisibleWrapsAroundTheRing() {
    QCOMPARE(WaterfallGeometry::oldestVisibleRow(500, 100, 2048), 400);
    // Write head behind the window start: must wrap forward, not go negative.
    QCOMPARE(WaterfallGeometry::oldestVisibleRow(50, 100, 2048), 1998);
    QCOMPARE(WaterfallGeometry::oldestVisibleRow(0, 100, 2048), 1948);
}

void TestWaterfallGeometry::visibleFractionCarriesNoOffByOne() {
    // The regression this file exists for: the fraction must be visibleRows/storedRows. The old
    // (visibleRows - 1)/storedRows form compressed the window by one row, which the V snap turned
    // into the newest row never being drawn.
    QCOMPARE(WaterfallGeometry::visibleFractionFor(1024, 2048), 0.5f);
    QCOMPARE(WaterfallGeometry::visibleFractionFor(2048, 2048), 1.0f);
}

void TestWaterfallGeometry::everyPixelMapsInsideTheVisibleWindow() {
    const int stored = 2048, visible = 770, writeRow = 1234;
    const int oldest = WaterfallGeometry::oldestVisibleRow(writeRow, visible, stored);

    for (int px = 0; px < visible; ++px) {
        const int row = WaterfallGeometry::rowForPixel(px, visible, writeRow, visible, stored);
        QVERIFY(row >= 0 && row < stored);
        int offset = row - oldest;
        if (offset < 0)
            offset += stored;
        QVERIFY2(offset < visible, qPrintable(QString("pixel %1 -> row %2 outside window").arg(px).arg(row)));
    }
}

void TestWaterfallGeometry::topPixelIsTheNewestCompleteRow() {
    const int stored = 2048, visible = 770, writeRow = 1234;
    const int top = WaterfallGeometry::rowForPixel(visible - 1, visible, writeRow, visible, stored);
    QCOMPARE(top, writeRow - 1);

    // Bottom pixel is the oldest row still on display.
    const int bottom = WaterfallGeometry::rowForPixel(0, visible, writeRow, visible, stored);
    QCOMPARE(bottom, WaterfallGeometry::oldestVisibleRow(writeRow, visible, stored));
}

void TestWaterfallGeometry::inFlightRowIsNeverSampled() {
    const int stored = 2048;
    for (int writeRow : {0, 1, 777, 2047}) {
        for (int visible : {64, 513, 770, 2047}) {
            for (int px = 0; px < visible; ++px) {
                const int row = WaterfallGeometry::rowForPixel(px, visible, writeRow, visible, stored);
                QVERIFY2(row != writeRow, qPrintable(QString("writeRow %1 sampled at pixel %2").arg(writeRow).arg(px)));
            }
        }
    }
}

void TestWaterfallGeometry::mappingIsOntoAndOrdered() {
    const int stored = 2048, visible = 770, writeRow = 1234;
    QSet<int> seen;
    int previousOffset = -1;

    for (int px = 0; px < visible; ++px) {
        const int row = WaterfallGeometry::rowForPixel(px, visible, writeRow, visible, stored);
        QVERIFY2(!seen.contains(row), qPrintable(QString("row %1 drawn twice").arg(row)));
        seen.insert(row);

        int offset = row - WaterfallGeometry::oldestVisibleRow(writeRow, visible, stored);
        if (offset < 0)
            offset += stored;
        QCOMPARE(offset, previousOffset + 1); // strictly ascending, no gaps
        previousOffset = offset;
    }
    QCOMPARE(seen.size(), visible); // onto: every visible row drawn
}

void TestWaterfallGeometry::mappingHoldsAcrossTheRingWrap() {
    const int stored = 2048;
    // writeRow values where the window straddles the end of the ring.
    for (int writeRow : {0, 5, 100, 2040, 2047}) {
        const int visible = 770;
        QSet<int> seen;
        for (int px = 0; px < visible; ++px)
            seen.insert(WaterfallGeometry::rowForPixel(px, visible, writeRow, visible, stored));
        QCOMPARE(seen.size(), visible);
        QVERIFY(!seen.contains(writeRow));
        QCOMPARE(WaterfallGeometry::rowForPixel(visible - 1, visible, writeRow, visible, stored),
                 (writeRow - 1 + stored) % stored);
    }
}

QTEST_MAIN(TestWaterfallGeometry)
#include "test_waterfallgeometry.moc"
