#include "dsp/panadapter_constants.h"
#include "dsp/dashgeometry.h"

#include <QtTest>

// The dashed overlay lines cannot be tested through the renderer, so these assert the arithmetic
// that sizes their vertex buffers — the same approach WaterfallGeometry takes for the shader math.
//
// The property that matters: dashFloatsFor() must upper-bound the loop it sizes for, and
// appendDashedVerticalLine() must never exceed the cap whatever it is asked to draw. Before this,
// five buffers were sized at 1200 floats (100 dashes = 1000 device px) and the mini-pan's at 512
// (420 px), while the loops spanned the full render-target height — so a Retina panadapter wrote
// past the end of a GPU buffer on every frame.
class TestPanadapterDashes : public QObject {
    Q_OBJECT

private slots:
    void dashFloatsUpperBoundsTheRenderLoop();
    void dashFloatsCoversEveryReachableSpectrumHeight();
    void maxDashFloatsCoversTheStatedCeiling();
    void builderNeverExceedsTheCap();
    void builderEmitsWholeDashesAndClipsTheLast();
    void emptyAndNegativeSpansProduceNothing();
};

// Mirrors the loop in PanadapterRhiWidget::render()/MiniPanRhiWidget::render() exactly, including
// the float accumulation in `y += stride` that the +1 in dashFloatsFor exists to absorb.
static int floatsTheLoopWouldProduce(float top, float bottom) {
    int floats = 0;
    for (float y = top; y < bottom; y += PanadapterConstants::DashStridePx)
        floats += PanadapterConstants::FloatsPerDash;
    return floats;
}

void TestPanadapterDashes::dashFloatsUpperBoundsTheRenderLoop() {
    // Exact multiples of the stride are the interesting case: a span of exactly 10 px runs the
    // loop once, and one of 10.0001 twice.
    for (float span = 0.0f; span <= 5000.0f; span += 0.5f) {
        QVERIFY2(floatsTheLoopWouldProduce(0.0f, span) <= PanadapterConstants::dashFloatsFor(span),
                 qPrintable(QString("span %1").arg(double(span))));
    }
}

void TestPanadapterDashes::dashFloatsCoversEveryReachableSpectrumHeight() {
    // DELIBERATELY LITERAL heights, not MaxDisplayHeightPx. Sweeping up to the constant under test
    // would make this pass for any ceiling however small — which is the shape of the defect being
    // fixed: a buffer quietly sized for less display than exists. These are device-pixel heights
    // of real panels, so lowering the ceiling below one of them fails here.
    const float panels[] = {1080.0f,  // 1080p at DPR 1
                            1600.0f,  // 1440p-class laptop
                            2160.0f,  // 4K at DPR 1, or 1080p at DPR 2
                            2880.0f,  // 5K iMac at DPR 2
                            3384.0f,  // Pro Display XDR at DPR 2
                            4320.0f}; // 8K at DPR 1, 4K at DPR 2

    // m_spectrumRatio is clamped to [0.1, 0.9] and the spectrum starts at the band-plan strip's
    // lower edge, so the reachable span is ratio * height with a non-zero top offset.
    for (float height : panels) {
        for (int pct = 10; pct <= 90; pct += 10) {
            const float bottom = height * (float(pct) / 100.0f);
            for (float top : {0.0f, 36.0f}) {
                if (bottom <= top)
                    continue;
                QVERIFY2(floatsTheLoopWouldProduce(top, bottom) <= PanadapterConstants::MaxDashFloats,
                         qPrintable(QString("panel %1 px at %2%%").arg(double(height)).arg(pct)));
            }
        }
    }

    // The mini-pan spans its whole widget, not a ratio of it, so the full height must fit too.
    for (float height : panels)
        QVERIFY(floatsTheLoopWouldProduce(0.0f, height) <= PanadapterConstants::MaxDashFloats);
}

void TestPanadapterDashes::maxDashFloatsCoversTheStatedCeiling() {
    QCOMPARE(PanadapterConstants::MaxDashFloats,
             PanadapterConstants::dashFloatsFor(PanadapterConstants::MaxDisplayHeightPx));
    QVERIFY(floatsTheLoopWouldProduce(0.0f, PanadapterConstants::MaxDisplayHeightPx) <=
            PanadapterConstants::MaxDashFloats);
    // A whole number of dashes, so a partial dash can never be uploaded.
    QCOMPARE(PanadapterConstants::MaxDashFloats % PanadapterConstants::FloatsPerDash, 0);
}

void TestPanadapterDashes::builderNeverExceedsTheCap() {
    // A display far taller than the ceiling must clip rather than overrun. This is the backstop
    // that makes a fixed ceiling safe.
    QVector<float> verts;
    DashGeometry::appendDashedVerticalLine(verts, 100.0f, 1.5f, 0.0f, 1000000.0f);
    QVERIFY(verts.size() <= PanadapterConstants::MaxDashFloats);
    QCOMPARE(verts.size() % PanadapterConstants::FloatsPerDash, 0);

    // An explicit smaller cap is honoured too.
    QVector<float> small;
    DashGeometry::appendDashedVerticalLine(small, 10.0f, 1.0f, 0.0f, 10000.0f, 24);
    QCOMPARE(small.size(), 24);
}

void TestPanadapterDashes::builderEmitsWholeDashesAndClipsTheLast() {
    QVector<float> verts;
    DashGeometry::appendDashedVerticalLine(verts, 50.0f, 2.0f, 0.0f, 25.0f);

    // 0..25 at a 10 px stride: dashes at y=0, 10, 20.
    QCOMPARE(verts.size(), 3 * PanadapterConstants::FloatsPerDash);

    // First dash spans its full length; the last is clipped to the bottom edge rather than
    // running past it.
    QCOMPARE(verts.at(1), 0.0f);
    QCOMPARE(verts.at(5), PanadapterConstants::DashLengthPx);
    QCOMPARE(verts.at(verts.size() - 1), 25.0f);
}

void TestPanadapterDashes::emptyAndNegativeSpansProduceNothing() {
    QCOMPARE(PanadapterConstants::dashFloatsFor(0.0f), 0);
    QCOMPARE(PanadapterConstants::dashFloatsFor(-100.0f), 0);

    QVector<float> verts;
    DashGeometry::appendDashedVerticalLine(verts, 10.0f, 1.0f, 40.0f, 40.0f);
    QVERIFY(verts.isEmpty());
    DashGeometry::appendDashedVerticalLine(verts, 10.0f, 1.0f, 40.0f, 10.0f);
    QVERIFY(verts.isEmpty());
}

QTEST_APPLESS_MAIN(TestPanadapterDashes)
#include "test_panadapterdashes.moc"
