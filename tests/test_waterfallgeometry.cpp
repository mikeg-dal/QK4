#include "dsp/waterfallcolorlut.h"
#include "dsp/waterfallgeometry.h"
#include "dsp/spectrumscale.h"
#include <QSet>
#include <QHash>
#include <QtTest>
#include <algorithm>
#include <cmath>
#include <utility>

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

    // One row per physical pixel when the OS downscales the framebuffer onto the panel.
    void devicePxPerRowHonoursOnlyWholeDownscales();
    void visibleRowsCountsPhysicalPixels();
    void fractionalViewportStaysOneToOne();
    void fractionalScaledModeCountsPanelPixelsNotFramebufferOnes();
    void oneDevicePxRowsShareAPhysicalPixelUnderDownscale();
    void twoDevicePxRowsScrollByWholePhysicalPixels();
    void rowShiftFollowsTheFramebufferParity();
    void shiftedRowsLandOnPhysicalPixelsAtDpr3();

    // SPEED: seconds of history on screen.
    void speedFillIsTodaysOneRowPerPixel();
    void speedEveryStepDiffersFromItsNeighbour();
    void speedPlanIsExactAndIndependentOfTheRate();
    void speedSecondsFollowHeightAndRate();
    void speedStepsAndLabels();
    void foldKeepsEachBinsPeak();

    // Resolution independence: the same setting must look the same on every display.
    void fastStepsAreLogicalPixelsSoMotionMatchesAcrossDisplays();
    void fillStaysOneRowPerPhysicalPixelOnEveryDisplay();
    void speedLadderShortensForAShortWindow();
    void aWithdrawnLevelStillDrawsAndIsNeverRewritten();
    void scrollPhaseClosesOverTheRowInterval();
    void aRowCountChangePreservesTheHistory();
    void fasterRowsAreWholePixelsTall();
    void everyDisplayAndSplitSweep();
    void namedGeometriesNeverRegress();

    // Colour window (WaterfallGeometry::colorWindowFor and the shader mirror).
    void noiseAtRefIsMidBlueNotBlack();
    void oldRefScaleWindowDrewNoiseBlack();
    void higherColorRangeIsBrighter();
    void unknownColorRangeUsesDefault();
    void noDataDrawsAtPaletteBottom();
    void capturedRowsKeepTheirColourWhenRefSteps();
    void smallerScaleIsBrighterLikeTheK4();
};

namespace {
struct Rgb {
    int r, g, b;
};

// The real table the waterfall draws with, at the position the shader would sample (nearest entry).
Rgb lutAt(float fraction) {
    QVector<quint8> lut;
    RhiUtils::generateWaterfallColorLUT(lut);
    const int i = qBound(0, static_cast<int>(std::lround(fraction * (RhiUtils::COLOR_LUT_SIZE - 1))),
                         RhiUtils::COLOR_LUT_SIZE - 1);
    return Rgb{lut[i * 4 + 0], lut[i * 4 + 1], lut[i * 4 + 2]};
}

bool isBlue(const Rgb &c) {
    return c.b >= 100 && c.r == 0 && c.g < c.b;
}

// Through the GPU path: the byte a row captures for a level, then the shader's pass-through uniforms.
float drawnFraction(float dbm, float refDbm, int colorRangeDb, int scaleDb = 80) {
    const auto window = WaterfallGeometry::colorWindowFor(refDbm, scaleDb, colorRangeDb);
    return WaterfallGeometry::shaderFractionFor(WaterfallGeometry::capturedByteFor(dbm, window),
                                                WaterfallGeometry::ColorUniforms{});
}
} // namespace

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

void TestWaterfallGeometry::devicePxPerRowHonoursOnlyWholeDownscales() {
    QCOMPARE(WaterfallGeometry::devicePxPerRowFor(1.0), 1);   // framebuffer is the panel (5K at 2560x1440)
    QCOMPARE(WaterfallGeometry::devicePxPerRowFor(2.0), 2);   // 7680x4320 halved onto a 4K panel
    QCOMPARE(WaterfallGeometry::devicePxPerRowFor(2.015), 2); // within 1%
    QCOMPARE(WaterfallGeometry::devicePxPerRowFor(3.0), 3);
    // Fractional macOS "scaled" modes land on no whole row height: unchanged.
    QCOMPARE(WaterfallGeometry::devicePxPerRowFor(1.125), 1); // 5K panel as "looks like 2880x1620"
    QCOMPARE(WaterfallGeometry::devicePxPerRowFor(1.75), 1);
    QCOMPARE(WaterfallGeometry::devicePxPerRowFor(2.2), 1);
    // Upscaled framebuffer (fewer pixels than the panel) and nonsense.
    QCOMPARE(WaterfallGeometry::devicePxPerRowFor(0.5), 1);
    QCOMPARE(WaterfallGeometry::devicePxPerRowFor(0.0), 1);
}

void TestWaterfallGeometry::visibleRowsCountsPhysicalPixels() {
    // Mike's panadapter, 2026-10-30: output 2092x924 px, spectrumRatio 0.4, on a 4K panel driven
    // with a 7680x4320 framebuffer.
    QCOMPARE(WaterfallGeometry::visibleRowsFor(554.4f, 2048, 64, 1), 554);
    QCOMPARE(WaterfallGeometry::visibleRowsFor(554.4f, 2048, 64, 2), 277);
    // The stored-rows clamp still applies to rows, not pixels.
    QCOMPARE(WaterfallGeometry::visibleRowsFor(8000.0f, 2048, 64, 2), 2047);
}

// The viewport is 554.4 px for 554 rows (pxPerRow 1.0007). That ratio is not a seam: with the V snap
// every whole pixel inside the viewport still resolves to exactly one row, in order, at every
// scroll position — so the fractional height is not what twinkles.
void TestWaterfallGeometry::fractionalViewportStaysOneToOne() {
    const int stored = 2048;
    // DPR 2: 554.4 px for 554 rows. DPR 3 at Mike's 150% interface scale: 831.6 px for 832 rows
    // (pxPerRow 0.9995) — 832 pixel centres lie inside 831.6, so that is still one row per pixel.
    for (const auto &[viewport, visible] : {std::pair{554.4f, 554}, std::pair{831.6f, 832}}) {
        QCOMPARE(WaterfallGeometry::visibleRowsFor(viewport, stored, 64), visible);
        for (int writeRow : {0, 1, 300, 1493, 2047}) {
            const int oldest = WaterfallGeometry::oldestVisibleRow(writeRow, visible, stored);
            for (int px = 0; px < visible; ++px)
                QCOMPARE(WaterfallGeometry::rowForPixel(px, viewport, writeRow, visible, stored),
                         (oldest + px) % stored);
        }
    }
}

// Mike's test configuration, 2026-11-26: a 4K panel driven at 3008x1692, which macOS renders at
// 6016x3384 and the display pipeline scales down to 3840x2160 - a ratio of 1.567, not a whole
// number. Only a whole factor could be honoured before, so this mode fell through to one row per
// FRAMEBUFFER pixel: 0.64 of a panel pixel per row, so about three rows were merged into every two
// panel pixels and that detail was simply gone. Counting PANEL pixels instead gives one row per
// panel pixel, which is the most the display can actually show.
//
// This is only available because the shader reconstructs rows at a continuous phase; with the old
// texel snap a row 1.567 pixels tall alternated between one and two.
void TestWaterfallGeometry::fractionalScaledModeCountsPanelPixelsNotFramebufferOnes() {
    using namespace WaterfallGeometry;
    const qreal ratio = 6016.0 / 3840.0;   // 1.5667, as PanelScale reports it for that mode
    QCOMPARE(devicePxPerRowFor(ratio), 1); // still no whole factor to honour, and none is claimed

    // 809 framebuffer pixels of waterfall is 516 panel pixels, so 516 rows - not 809.
    QCOMPARE(visibleRowsFor(809.0f, 4096, 64, ratio), 516);
    QCOMPARE(visibleRowsFor(809.0f, 4096, 64, 1.0), 809); // native 4K, unchanged

    // The property that matters: never more than one row per panel pixel, at any height, so the
    // display pipeline can no longer merge two rows into one pixel.
    for (float framebufferPx = 100.0f; framebufferPx <= 3300.0f; framebufferPx += 13.0f) {
        const int rows = visibleRowsFor(framebufferPx, 4096, 64, ratio);
        const double panelPx = framebufferPx / ratio;
        QVERIFY2(rows <= static_cast<int>(std::ceil(panelPx)),
                 qPrintable(QStringLiteral("%1 framebuffer px is %2 panel px but asked for %3 rows")
                                .arg(framebufferPx)
                                .arg(panelPx)
                                .arg(rows)));
    }

    // A whole factor is unchanged by the switch to a real-valued ratio.
    QCOMPARE(visibleRowsFor(554.4f, 2048, 64, 2.0), visibleRowsFor(554.4f, 2048, 64, 2));

    // A fast level is still logical pixels, so it scales by dpr over the panel ratio: at dpr 2 on
    // this panel level 7's three logical pixels are 3 * 2/1.567 = 3.83 panel pixels.
    QVERIFY(std::abs(physicalRowHeightFor(7, 2.0, ratio) - 3.0f * 2.0f / float(ratio)) < 1e-4f);
    // And at dpr 1 on this panel a logical pixel is SMALLER than a panel pixel, so level 4 cannot
    // deliver its nominal height, clamps to Fill's, and is withdrawn rather than drawing Fill's
    // picture under another number.
    QCOMPARE(physicalRowHeightFor(4, 1.0, ratio), 1.0f);
    QVERIFY(!speedLevelFits(4, 800, 64, 1.0, ratio));
}

// The defect: at one device pixel per row under a 2:1 downscale, each physical pixel is two rows
// that the display's scaler blends, and which two changes with every scroll step.
void TestWaterfallGeometry::oneDevicePxRowsShareAPhysicalPixelUnderDownscale() {
    const int stored = 2048, visible = 554, writeRow = 700;
    for (int physical = 0; physical < visible / 2; ++physical) {
        const int lower = WaterfallGeometry::rowForPixel(2 * physical, 554.4f, writeRow, visible, stored);
        const int upper = WaterfallGeometry::rowForPixel(2 * physical + 1, 554.4f, writeRow, visible, stored);
        QVERIFY(lower != upper);
    }
}

// The fix: rows two device pixels tall. Each physical pixel (device pixels 2j, 2j+1 from the bottom,
// which is also the top-aligned pairing because the widget's device height is even at DPR 2) holds
// one row, and one step later every physical pixel holds what the one above it held: a rigid scroll
// by exactly one physical pixel, which the display's downscale leaves untouched.
void TestWaterfallGeometry::twoDevicePxRowsScrollByWholePhysicalPixels() {
    const int stored = 2048;
    const float viewport = 554.4f;
    const int visible = WaterfallGeometry::visibleRowsFor(viewport, stored, 64, 2);
    const int physicalPixels = visible; // 277 whole physical pixels, 554 device pixels

    for (int writeRow : {0, 1, 1000, 2046, 2047}) {
        const int next = (writeRow + 1) % stored;
        for (int physical = 0; physical < physicalPixels; ++physical) {
            const int lower = WaterfallGeometry::rowForPixel(2 * physical, viewport, writeRow, visible, stored);
            const int upper = WaterfallGeometry::rowForPixel(2 * physical + 1, viewport, writeRow, visible, stored);
            QCOMPARE(lower, upper);
            QVERIFY(lower != writeRow);
            if (physical > 0) {
                const int below = WaterfallGeometry::rowForPixel(2 * (physical - 1), viewport, next, visible, stored);
                QCOMPARE(below, lower);
            }
        }
        // Newest complete row on the top physical pixel.
        QCOMPARE(WaterfallGeometry::rowForPixel(2 * physicalPixels - 1, viewport, writeRow, visible, stored),
                 (writeRow - 1 + stored) % stored);
    }
}

void TestWaterfallGeometry::rowShiftFollowsTheFramebufferParity() {
    QCOMPARE(WaterfallGeometry::rowShiftFor(1386, 2), 0.0f);
    QCOMPARE(WaterfallGeometry::rowShiftFor(1131, 2), -0.5f);
    QCOMPARE(WaterfallGeometry::rowShiftFor(1131, 1), 0.0f);
    QCOMPARE(WaterfallGeometry::rowShiftFor(10, 3), -1.0f / 3.0f);
}

// Mike's session, 2026-10-30: DPR 3, waterfall 831.6 px, 7680x4320 framebuffer on a 4K panel (two
// framebuffer rows per physical pixel). For a widget whose bottom edge is at an even and at an odd
// framebuffer row, every physical pixel the waterfall covers completely shows one row, the newest
// complete row is at the top, the write head is never drawn, and one step later every physical
// pixel holds what the one above it held.
void TestWaterfallGeometry::shiftedRowsLandOnPhysicalPixelsAtDpr3() {
    const int stored = 2048, k = 2;
    const float viewport = 831.6f;
    const int pixels = 832; // pixel centres inside the viewport
    for (qint64 bottomRow : {qint64{1386}, qint64{1131}}) {
        const float shift = WaterfallGeometry::rowShiftFor(bottomRow, k);
        int visible = WaterfallGeometry::visibleRowsFor(viewport, stored, 64, k);
        if (shift != 0.0f)
            visible = (std::min)(visible, stored - 2);
        // Physical pixel of device pixel i (0 = bottom): framebuffer row bottomRow - 1 - i, halved.
        auto physicalOf = [&](int i) { return (bottomRow - 1 - i) / k; };

        for (int writeRow : {0, 1, 900, 2047}) {
            const int next = (writeRow + 1) % stored;
            auto row = [&](int i, int w) {
                return WaterfallGeometry::rowForPixel(i, viewport, w, visible, stored, shift);
            };
            for (int i = 0; i < pixels; ++i) {
                QVERIFY(row(i, writeRow) != writeRow);
                if (i + 1 < pixels && physicalOf(i + 1) == physicalOf(i))
                    QCOMPARE(row(i + 1, writeRow), row(i, writeRow));
                // Rigid scroll: the pixel k below now shows this pixel's row.
                if (i >= k)
                    QCOMPARE(row(i - k, next), row(i, writeRow));
            }
            QCOMPARE(row(pixels - 1, writeRow), (writeRow - 1 + stored) % stored);
        }
    }
}

// The K4 shows band noise as mid-blue speckle at its #WBS14 with AutoRef parking REF on the noise.
// Noise a few dB above REF, and its usual +-6 dB excursions, must land in the palette's blue band —
// never in the black dead zone below 0.10 that used to swallow it.
void TestWaterfallGeometry::noiseAtRefIsMidBlueNotBlack() {
    for (float ref : {-129.0f, -110.0f, -95.0f}) {
        const float noise = ref + 3.0f;
        const float f = drawnFraction(noise, ref, 14);
        QVERIFY2(f > 0.10f, qPrintable(QString("noise at %1 is in the black zone (%2)").arg(noise).arg(f)));
        const Rgb mid = lutAt(f);
        QVERIFY2(isBlue(mid), qPrintable(QString("noise drawn %1,%2,%3").arg(mid.r).arg(mid.g).arg(mid.b)));
        // A noise peak stays blue-ish (no green/yellow), a noise trough is darker but the peak is not.
        const Rgb peak = lutAt(drawnFraction(noise + 6.0f, ref, 14));
        QVERIFY(peak.r == 0 && peak.b >= mid.b);
        QVERIFY(lutAt(drawnFraction(noise - 6.0f, ref, 14)).b < mid.b);
    }
    // The fit to Mike's K4 photo of WWV (2026-10-30, REF -106, #WBS 14): the sidebands (~-75 dBm)
    // are green / yellow-green, not red, and only the carrier (~-55 dBm) reaches red.
    const Rgb sideband = lutAt(drawnFraction(-75.0f, -106.0f, 14));
    QVERIFY2(sideband.g == 255 && sideband.r < 128,
             qPrintable(QString("sideband %1,%2,%3").arg(sideband.r).arg(sideband.g).arg(sideband.b)));
    const Rgb carrier = lutAt(drawnFraction(-55.0f, -106.0f, 14));
    QVERIFY2(carrier.r == 255, qPrintable(QString("carrier %1,%2,%3").arg(carrier.r).arg(carrier.g).arg(carrier.b)));
}

// The regression this fixes: palette = REF..REF+SCALE (80 dB) put the same noise at ~0.04.
void TestWaterfallGeometry::oldRefScaleWindowDrewNoiseBlack() {
    const float ref = -110.0f;
    const float oldFraction = SpectrumScale::normalizedForDb(ref + 3.0f, ref, ref + 80.0f);
    const Rgb old = lutAt(oldFraction);
    QCOMPARE(old.r + old.g + old.b, 0);
    QVERIFY(drawnFraction(ref + 3.0f, ref, 14) > oldFraction + 0.25f);
}

// K4 manual rev D6, WTR CLRS: "A higher number will use brighter colors for a given amount of
// waterfall signal intensity."
void TestWaterfallGeometry::higherColorRangeIsBrighter() {
    const float ref = -110.0f;
    for (float dbm = ref - 20.0f; dbm <= ref + 40.0f; dbm += 1.0f) {
        for (int wbs = 5; wbs < 30; ++wbs) {
            const float lo =
                WaterfallGeometry::paletteFractionFor(dbm, WaterfallGeometry::colorWindowFor(ref, 80, wbs));
            const float hi =
                WaterfallGeometry::paletteFractionFor(dbm, WaterfallGeometry::colorWindowFor(ref, 80, wbs + 1));
            QVERIFY(hi >= lo);
        }
    }
    QVERIFY(drawnFraction(ref, ref, 30) > drawnFraction(ref, ref, 5));
}

void TestWaterfallGeometry::unknownColorRangeUsesDefault() {
    const auto def = WaterfallGeometry::colorWindowFor(-110.0f, 80, WaterfallGeometry::DefaultColorRangeDb);
    for (int bad : {-1, 0, 4, 31, 100}) {
        const auto w = WaterfallGeometry::colorWindowFor(-110.0f, 80, bad);
        QCOMPARE(w.lowDbm, def.lowDbm);
        QCOMPARE(w.spanDb, def.spanDb);
    }
    // The hypothesis, stated as numbers so a change to it is a deliberate test edit.
    const auto w = WaterfallGeometry::colorWindowFor(-110.0f, 80, 14);
    QCOMPARE(w.lowDbm, -138.0f); // REF - 2 x #WBS
    QCOMPARE(w.spanDb, 80.0f);   // span = SCALE
}

void TestWaterfallGeometry::noDataDrawsAtPaletteBottom() {
    // The pass-through uniforms both widgets send leave a stored palette position unchanged, and
    // "no data" draws at the bottom of the palette.
    const WaterfallGeometry::ColorUniforms passThrough;
    QCOMPARE(WaterfallGeometry::shaderFractionFor(WaterfallGeometry::NoDataByte, passThrough), 0.0f);
    QCOMPARE(WaterfallGeometry::shaderFractionFor(255, passThrough), 1.0f);
    QVERIFY(std::abs(WaterfallGeometry::shaderFractionFor(128, passThrough) - 128.0f / 255.0f) < 1e-6f);
}

// A row's colour is fixed when it is captured. Through pass-through uniforms the stored byte draws
// at the palette position it was captured with (within one byte), and a later 1 dB AutoRef REF step
// leaves rows already drawn alone - the whole-history flicker seen on the bench.
void TestWaterfallGeometry::capturedRowsKeepTheirColourWhenRefSteps() {
    using namespace WaterfallGeometry;
    const ColorUniforms passThrough{};
    for (float ref : {-130.0f, -110.0f, -106.0f}) {
        const ColorWindow window = colorWindowFor(ref, 80, 14);
        for (float dbm = ref - 20.0f; dbm <= ref + 40.0f; dbm += 0.5f) {
            const quint8 byte = capturedByteFor(dbm, window);
            QVERIFY(byte != NoDataByte);
            QVERIFY(std::abs(shaderFractionFor(byte, passThrough) - paletteFractionFor(dbm, window)) <= 1.0f / 255.0f);
        }
    }
    // The same noise level captured before and after a 1 dB REF step stores different bytes: only the
    // new row changes colour, because the uniforms no longer follow REF.
    const float noise = -104.0f;
    QVERIFY(capturedByteFor(noise, colorWindowFor(-106.0f, 80, 14)) !=
            capturedByteFor(noise, colorWindowFor(-107.0f, 80, 14)));
}

// Mike's K4 photos of WWV at #WBS 14 (2026-10-30): the K4's waterfall is brighter at a smaller
// SCALE - 25: blue with green speckle; 50: royal blue; 80: blue, sidebands green, carrier red.
// Noise a few dB above REF climbs the palette as SCALE shrinks, and its +6 dB peaks reach green
// only at SCALE 25.
void TestWaterfallGeometry::smallerScaleIsBrighterLikeTheK4() {
    const float ref = -108.0f, noise = ref + 3.0f;
    const float at25 = drawnFraction(noise, ref, 14, 25);
    const float at50 = drawnFraction(noise, ref, 14, 50);
    const float at80 = drawnFraction(noise, ref, 14, 80);
    QVERIFY(at25 > at50 && at50 > at80);
    QVERIFY(isBlue(lutAt(at50)) && isBlue(lutAt(at80)));
    const Rgb peak25 = lutAt(drawnFraction(noise + 6.0f, ref, 14, 25));
    QVERIFY2(peak25.g >= 128,
             qPrintable(QString("SCALE 25 noise peak %1,%2,%3").arg(peak25.r).arg(peak25.g).arg(peak25.b)));
    const Rgb peak80 = lutAt(drawnFraction(noise + 6.0f, ref, 14, 80));
    QVERIFY(peak80.g < 128);
    QCOMPARE(WaterfallGeometry::colorWindowFor(ref, 25, 14).spanDb, 25.0f);
}

// Fill (the default) is exactly the waterfall before SPEED existed: one row per pixel row, one
// frame per row, whatever the line rate.
void TestWaterfallGeometry::speedFillIsTodaysOneRowPerPixel() {
    using namespace WaterfallGeometry;
    for (int rows : {64, 300, 607, 2047}) {
        const SpeedPlan fill = speedPlanFor(rows, SpeedLevelFill, 1.0, 1.0);
        QCOMPARE(fill.visibleRows, rows);
        QCOMPARE(fill.framesPerRow, 1);
        QCOMPARE(fill.pixelRowsPerRow, 1.0f);
    }
}

// Every step draws something different from the one beside it, at any window height. That is the
// point of choosing the row height directly instead of rounding a number of seconds to it: the old
// ladder put several of its steps on the identical picture (25 s and 30 s both landed on 2 pixel
// rows per row at Mike's geometry) and left nothing at all between 23 s and 46 s.
void TestWaterfallGeometry::speedEveryStepDiffersFromItsNeighbour() {
    using namespace WaterfallGeometry;
    for (int rows : {200, 607, 693, 1100}) {
        for (const qreal dpr : {1.0, 2.0}) {
            int previous = -1;
            for (int level = SpeedLevelMin; level <= SpeedLevelMax; ++level) {
                if (!speedLevelFits(level, rows, 2, dpr, 1.0))
                    continue; // withdrawn here; the ladder is shorter, not duplicated
                const SpeedPlan plan = speedPlanFor(rows, level, dpr, 1.0);
                const int signature = plan.visibleRows * 100 + plan.framesPerRow;
                QVERIFY2(signature != previous, qPrintable(QStringLiteral("level %1 draws the same as the one "
                                                                          "before it at %2 rows, dpr %3")
                                                               .arg(level)
                                                               .arg(rows)
                                                               .arg(dpr)));
                previous = signature;
            }
        }
    }
}

// The plan is exact, with no measured rate in it, so there is no boundary for it to hop across.
// The bench defect of 2026-11-25: 693 rows, 30 s and 14.9 Hz put the old ratio at 1.55 against a
// rounding boundary at 1.50, and the waterfall jumped up the screen and back down as the measured
// rate wandered. A plan that never sees the rate cannot do it.
void TestWaterfallGeometry::speedPlanIsExactAndIndependentOfTheRate() {
    using namespace WaterfallGeometry;
    for (int level = SpeedLevelMin; level <= SpeedLevelMax; ++level) {
        const SpeedPlan plan = speedPlanFor(693, level, 1.0, 1.0);
        const SpeedLevel &entry = SpeedLevels[static_cast<std::size_t>(level - 1)];
        if (entry.framesPerRow > 1) {
            QCOMPARE(plan.visibleRows, 693);
            QCOMPARE(plan.pixelRowsPerRow, 1.0f);
            QCOMPARE(plan.framesPerRow, entry.framesPerRow);
        } else {
            QCOMPARE(plan.framesPerRow, 1);
            const float height = physicalRowHeightFor(level, 1.0, 1.0);
            QCOMPARE(plan.pixelRowsPerRow, height);
            QCOMPARE(plan.visibleRows, height <= 1.001f ? 693 : qRound(693.0 / height));
        }
        QVERIFY(plan.visibleRows >= 2 && plan.visibleRows <= 693);
        QVERIFY(plan.framesPerRow >= 1 && plan.framesPerRow <= MaxFramesPerRow);
    }
}

// The seconds on the label are the seconds the plan actually shows, and they follow the window
// height and the measured rate rather than being the thing stored.
void TestWaterfallGeometry::speedSecondsFollowHeightAndRate() {
    using namespace WaterfallGeometry;
    // Mike's bench geometry: 693 rows at 14.9 Hz. These are DIAGNOSTIC numbers - the SPEED key
    // shows a bare level, precisely because these move with the height and the rate.
    QCOMPARE(qRound(secondsOnScreen(693, SpeedLevelFill, 14.9, 1.0, 1.0)), 47);
    QCOMPARE(qRound(secondsOnScreen(693, 2, 14.9, 1.0, 1.0)), 93);              // the 2x fold
    QCOMPARE(qRound(secondsOnScreen(693, 1, 14.9, 1.0, 1.0)), 140);             // the 3x fold
    QCOMPARE(qRound(secondsOnScreen(346, SpeedLevelFill, 14.9, 1.0, 1.0)), 23); // half the window
    // Nothing is claimed before the rate is known, or with no window.
    QCOMPARE(secondsOnScreen(693, 6, 0.0, 1.0, 1.0), 0.0);
    QCOMPARE(secondsOnScreen(0, 6, 14.9, 1.0, 1.0), 0.0);
}

void TestWaterfallGeometry::speedStepsAndLabels() {
    using namespace WaterfallGeometry;
    // The ladder is ten levels, slowest first, with Fill in the middle of the two folds and the
    // eight magnifications.
    QCOMPARE(SpeedLevelMin, 1);
    QCOMPARE(SpeedLevelMax, 10);
    QCOMPARE(SpeedLevels[static_cast<std::size_t>(SpeedLevelFill - 1)].framesPerRow, 1);
    QCOMPARE(SpeedLevels[static_cast<std::size_t>(SpeedLevelFill - 1)].unit, RowUnit::PanelPixel);

    // Folds below Fill, magnifications above, and they only ever get faster going up.
    for (int level = SpeedLevelMin; level < SpeedLevelFill; ++level)
        QVERIFY(SpeedLevels[static_cast<std::size_t>(level - 1)].framesPerRow > 1);
    for (int level = SpeedLevelFill + 1; level <= SpeedLevelMax; ++level) {
        const SpeedLevel &entry = SpeedLevels[static_cast<std::size_t>(level - 1)];
        QCOMPARE(entry.framesPerRow, 1);
        QCOMPARE(entry.unit, RowUnit::LogicalPixel);
        QVERIFY(entry.rowHeight > SpeedLevels[static_cast<std::size_t>(level - 2)].rowHeight ||
                SpeedLevels[static_cast<std::size_t>(level - 2)].unit == RowUnit::PanelPixel);
    }
    // Folds get slower going down.
    QVERIFY(SpeedLevels[0].framesPerRow > SpeedLevels[1].framesPerRow);

    // Anything off the ladder reads as Fill, so an older build's saved value lands somewhere sane.
    for (const int outside : {0, -1, -4, 11, 30, 120})
        QCOMPARE(clampSpeedLevel(outside), SpeedLevelFill);
    for (int level = SpeedLevelMin; level <= SpeedLevelMax; ++level)
        QCOMPARE(clampSpeedLevel(level), level);
}

// Folding frames into one row keeps each bin's brightest moment: a one-frame FT8 or CW burst is not
// averaged away when SPEED folds several frames together.
void TestWaterfallGeometry::foldKeepsEachBinsPeak() {
    quint8 row[4] = {10, 200, 30, 0};
    const quint8 burst[4] = {250, 20, 30, 7};
    WaterfallGeometry::foldPeak(row, burst, 4);
    QCOMPARE(row[0], quint8(250));
    QCOMPARE(row[1], quint8(200));
    QCOMPARE(row[2], quint8(30));
    QCOMPARE(row[3], quint8(7));
}

// The spectrum expert's review, 2026-11-02: rows of 1 and 2 px in a pattern fixed on screen made a
// trace change thickness as it scrolled. Faster than Fill, every row covers k pixel rows, or k + 1
// for at most k - 1 of them where the height does not divide (never 1 and 2 alternating).
//
// k now comes from the plan's own row count rather than from the step, because the row height is no
// longer required to be a whole number of pixels: the shader reconstructs rows with a cubic kernel
// at a continuous phase, so a height of 12.16 is drawn as 12.16 instead of alternating 12 and 13 in
// a pattern that slid through the picture. What still has to hold, on any integer pixel grid, is
// that the rows differ by at most one pixel and that every row gets some.
void TestWaterfallGeometry::fasterRowsAreWholePixelsTall() {
    using namespace WaterfallGeometry;
    constexpr int kStored = 2048;
    for (int height : {200, 450, 607, 1100}) {
        for (int level = SpeedLevelMin; level <= SpeedLevelMax; ++level) {
            {
                const SpeedPlan plan = speedPlanFor(height, level, 1.0, 1.0);
                if (plan.visibleRows >= height)
                    continue; // Fill or folding: one row per pixel row, covered elsewhere
                const int k = height / plan.visibleRows;
                QHash<int, int> pixelsPerRow;
                for (int pixel = 0; pixel < height; ++pixel)
                    ++pixelsPerRow[rowForPixel(pixel, float(height), 900, plan.visibleRows, kStored)];
                QCOMPARE(pixelsPerRow.size(), plan.visibleRows);
                int taller = 0;
                for (const int count : pixelsPerRow) {
                    QVERIFY2(count == k || count == k + 1,
                             qPrintable(QStringLiteral("a row of %1 px with k = %2").arg(count).arg(k)));
                    taller += count == k + 1;
                }
                // Exactly the remainder of the division is a pixel taller - the most even split an
                // integer pixel grid allows, and no alternating pattern beyond one pixel.
                QCOMPARE(taller, height % plan.visibleRows);
            }
        }
    }
}

// Fill means one row per PHYSICAL pixel on every display: the sharpest picture and the longest
// history the panel can hold. A 4K panel is meant to get more of both than a 1080p one, which is
// why Fill's seconds differ between displays and the label says what they came to.
void TestWaterfallGeometry::fillStaysOneRowPerPhysicalPixelOnEveryDisplay() {
    using namespace WaterfallGeometry;
    for (const qreal dpr : {1.0, 1.25, 1.5, 2.0, 3.0}) {
        for (const qreal ratio : {1.0, 2.0}) {
            QCOMPARE(physicalRowHeightFor(SpeedLevelFill, dpr, ratio), 1.0f);
            for (int fold = SpeedLevelMin; fold < SpeedLevelFill; ++fold)
                QCOMPARE(physicalRowHeightFor(fold, dpr, ratio), 1.0f);
            const SpeedPlan plan = speedPlanFor(800, SpeedLevelFill, dpr, ratio);
            QCOMPARE(plan.visibleRows, 800);
            QCOMPARE(plan.pixelRowsPerRow, 1.0f);
        }
    }
}

// The bench defect of 2026-11-26: the step was counted in FRAMEBUFFER pixels, so the identical
// window scrolled at half the speed and held twice the history on a 2x display. Counted in logical
// pixels, a step covers the same visible distance per frame everywhere - which is what the operator
// is judging when they pick a speed.
void TestWaterfallGeometry::fastStepsAreLogicalPixelsSoMotionMatchesAcrossDisplays() {
    using namespace WaterfallGeometry;
    for (int level = SpeedLevelFill + 1; level <= SpeedLevelMax; ++level) {
        const float logical = SpeedLevels[static_cast<std::size_t>(level - 1)].rowHeight;
        // One logical pixel is dpr framebuffer pixels; with one framebuffer pixel per panel pixel
        // that is dpr physical pixels, so the level's physical height scales exactly with dpr.
        QCOMPARE(physicalRowHeightFor(level, 2.0, 1.0), logical * 2.0f);
        QVERIFY(std::abs(physicalRowHeightFor(level, 1.5, 1.0) - logical * 1.5f) < 1e-4f);

        // The travel per frame in LOGICAL pixels is what must match across displays, so a window of
        // the same logical height shows the same seconds on every one of them.
        const double logicalHeight = 500.0;
        double firstSeconds = 0.0;
        for (const qreal dpr : {1.0, 1.25, 1.5, 2.0, 3.0}) {
            const int physicalRows = static_cast<int>(std::lround(logicalHeight * dpr));
            const double seconds = secondsOnScreen(physicalRows, level, 15.0, dpr, 1.0);
            if (firstSeconds == 0.0)
                firstSeconds = seconds;
            QVERIFY2(std::abs(seconds - firstSeconds) <= 2.0 / 15.0,
                     qPrintable(QStringLiteral("level %1 at dpr %2 shows %3 s, not %4 s")
                                    .arg(level)
                                    .arg(dpr)
                                    .arg(seconds)
                                    .arg(firstSeconds)));
        }
    }
}

// A step that would leave a handful of thick colour bands is not a waterfall, so a short window or
// a spectrum-heavy #WFH split gets a SHORTER LADDER rather than settings that cannot work. Fill and
// the folds always fit: they keep every row the height can hold.
void TestWaterfallGeometry::speedLadderShortensForAShortWindow() {
    using namespace WaterfallGeometry;
    constexpr int kMinRows = 64;

    // A tall window at 2x holds the whole ladder.
    for (int level = SpeedLevelMin; level <= SpeedLevelMax; ++level)
        QVERIFY2(speedLevelFits(level, 1512, kMinRows, 2.0, 1.0), qPrintable(QStringLiteral("level %1").arg(level)));

    // Fill and the folds always fit, however short the window.
    for (int level = SpeedLevelMin; level <= SpeedLevelFill; ++level)
        QVERIFY(speedLevelFits(level, 80, kMinRows, 1.0, 1.0));

    // A 150-pixel waterfall at 1x cannot hold 64 rows past two logical pixels per row.
    QVERIFY(speedLevelFits(6, 150, kMinRows, 1.0, 1.0));  // 2 px -> 75 rows
    QVERIFY(!speedLevelFits(7, 150, kMinRows, 1.0, 1.0)); // 3 px -> 50 rows
    QVERIFY(!speedLevelFits(10, 150, kMinRows, 1.0, 1.0));
    QCOMPARE(stepSpeedLevel(6, +1, 150, kMinRows, 1.0, 1.0), 6);              // stops where it fits
    QCOMPARE(stepSpeedLevel(SpeedLevelFill, -1, 150, kMinRows, 1.0, 1.0), 2); // folds never withdraw

    // At 1x, one logical pixel IS Fill, so level 4 is withdrawn rather than drawing Fill's picture
    // under another number. At 2x it is a real step and is offered.
    QVERIFY(!speedLevelFits(4, 1512, kMinRows, 1.0, 1.0));
    QVERIFY(speedLevelFits(4, 1512, kMinRows, 2.0, 1.0));
    QCOMPARE(stepSpeedLevel(SpeedLevelFill, +1, 1512, kMinRows, 1.0, 1.0), 5);
    QCOMPARE(stepSpeedLevel(SpeedLevelFill, +1, 1512, kMinRows, 2.0, 1.0), 4);

    // Every level the ladder walks onto, at any height and scale, actually leaves a waterfall.
    for (int height = 64; height <= 2047; height += 7) {
        for (const qreal dpr : {1.0, 1.5, 2.0}) {
            int level = SpeedLevelFill;
            for (int i = 0; i < 20; ++i) {
                const int next = stepSpeedLevel(level, +1, height, kMinRows, dpr, 1.0);
                if (next == level)
                    break;
                QVERIFY(speedLevelFits(next, height, kMinRows, dpr, 1.0));
                level = next;
            }
        }
    }
}

// A level the window is too short for is withdrawn from the LADDER, not from the renderer, and the
// saved level is never rewritten behind the operator's back (decided 2026-11-26).
//
// WHY the two floors differ. `speedLevelFits` refuses anything under minRows, so the key will not
// step onto a setting that is no longer a waterfall. `speedPlanFor` has no such floor: a level the
// operator already chose keeps its row height and simply shows fewer rows. The alternative -
// clamping in the plan - would silently pull a saved 9 down to whatever fits on every frame of a
// window drag and hand back a different setting than the one that was set, which is the same mistake
// a431e01 made with the history. Growing the window back restores the level exactly, because nothing
// ever rewrote it.
void TestWaterfallGeometry::aWithdrawnLevelStillDrawsAndIsNeverRewritten() {
    using namespace WaterfallGeometry;
    constexpr int kMinRows = 64;
    constexpr qreal kDpr = 2.0; // level 9 is 12 panel pixels per row here, level 10 is 16

    // Tall enough for the whole ladder: both top levels are offered and their rows are the height
    // over the row height.
    // NB speedPlanFor takes the row count FIRST, unlike every other function here.
    const SpeedPlan tall9 = speedPlanFor(1536, 9, kDpr, 1.0);
    const SpeedPlan tall10 = speedPlanFor(1536, 10, kDpr, 1.0);
    QVERIFY(speedLevelFits(9, 1536, kMinRows, kDpr, 1.0));
    QVERIFY(speedLevelFits(10, 1536, kMinRows, kDpr, 1.0));
    QCOMPARE(tall9.visibleRows, 128);
    QCOMPARE(tall10.visibleRows, 96);

    // Short window: neither is offered any more, and both still draw - same row height, fewer rows.
    QVERIFY(!speedLevelFits(9, 324, kMinRows, kDpr, 1.0));
    QVERIFY(!speedLevelFits(10, 324, kMinRows, kDpr, 1.0));
    const SpeedPlan short9 = speedPlanFor(324, 9, kDpr, 1.0);
    QCOMPARE(short9.visibleRows, 27);
    QCOMPARE(short9.pixelRowsPerRow, tall9.pixelRowsPerRow);
    QCOMPARE(short9.framesPerRow, 1);

    // The level itself survives every clamp on the path from disk to the renderer.
    QCOMPARE(clampSpeedLevel(9), 9);
    QCOMPARE(clampSpeedLevel(10), 10);

    // The key is one-way while it does not fit: + runs off the top and stays put, - skips every
    // level that does not fit and lands on the first that does.
    QCOMPARE(stepSpeedLevel(9, +1, 324, kMinRows, kDpr, 1.0), 9);
    QCOMPARE(stepSpeedLevel(10, +1, 324, kMinRows, kDpr, 1.0), 10);
    QCOMPARE(stepSpeedLevel(9, -1, 324, kMinRows, kDpr, 1.0), 6); // 8 and 7 are too tall as well

    // Grow the window back and the level is exactly what it was - no renumbering, no drift.
    QCOMPARE(speedPlanFor(1536, 9, kDpr, 1.0).visibleRows, tall9.visibleRows);
    QCOMPARE(speedPlanFor(1536, 9, kDpr, 1.0).pixelRowsPerRow, tall9.pixelRowsPerRow);

    // However short the waterfall gets, a withdrawn level still produces a drawable plan: at least
    // two rows, never more rows than pixel rows, and never a row height that drifts with the height.
    for (int height = 2; height <= 2047; height += 7) {
        for (const int level : {9, 10}) {
            const SpeedPlan plan = speedPlanFor(height, level, kDpr, 1.0);
            const QString what = QStringLiteral("level %1 at %2 rows").arg(level).arg(height);
            QVERIFY2(plan.visibleRows >= 2, qPrintable(what));
            QVERIFY2(plan.visibleRows <= height || height < 2, qPrintable(what));
            QCOMPARE(plan.pixelRowsPerRow, physicalRowHeightFor(level, kDpr, 1.0));
        }
    }
}

// The stutter fix. The ring jumps a whole row the instant one arrives; the residual starts at that
// whole distance and closes to zero over the row interval, so the travel is spread across the
// display refreshes in between instead of landing in one lurch of `step` pixels.
void TestWaterfallGeometry::scrollPhaseClosesOverTheRowInterval() {
    using namespace WaterfallGeometry;
    constexpr int kStored = 2048;
    const double msPerRow = 1000.0 / 15.0; // ~66.7 ms, Mike's measured rate

    // At the instant the row lands the picture has the whole row still to travel, and it is pulled
    // BACK toward the older rows (negative, like rowShiftFor).
    const float atArrival = scrollPhaseFor(1, kStored, 0.0, msPerRow);
    QCOMPARE(atArrival, -1.0f / kStored);
    // Halfway through the interval, half of it remains.
    QVERIFY(std::abs(scrollPhaseFor(1, kStored, msPerRow / 2.0, msPerRow) - -0.5f / kStored) < 1e-6f);
    // By the end it has closed, and it does not overshoot past it.
    QCOMPARE(scrollPhaseFor(1, kStored, msPerRow, msPerRow), 0.0f);
    QCOMPARE(scrollPhaseFor(1, kStored, msPerRow * 4.0, msPerRow), 0.0f);

    // Several rows at once travel together, but never further back than the slack the shader
    // reserves. Bench 2026-11-26: the caller accumulated a row per frame while restarting the clock,
    // so the residual grew without bound, dragged V through the ring, and the tap clamp pinned every
    // row to one - the waterfall drew as vertical streaks of a single spectrum. The cap means no
    // caller can do that again.
    QCOMPARE(scrollPhaseFor(2, kStored, 0.0, msPerRow), -2.0f / kStored);
    QCOMPARE(scrollPhaseFor(2, kStored, msPerRow * 2.0, msPerRow), 0.0f);
    for (const int runaway : {3, 17, 400, 100000})
        QCOMPARE(scrollPhaseFor(runaway, kStored, 0.0, msPerRow), -float(MaxScrollLagRows) / kStored);

    // Nothing is animated against a guess: before a rate is measured, or with no advance, the
    // picture simply sits where the ring puts it.
    QCOMPARE(scrollPhaseFor(1, kStored, 10.0, 0.0), 0.0f);
    QCOMPARE(scrollPhaseFor(0, kStored, 10.0, msPerRow), 0.0f);
    QCOMPARE(scrollPhaseFor(1, 0, 10.0, msPerRow), 0.0f);

    // Monotone and bounded at every phase, so the picture never jitters backwards.
    float previous = -2.0f;
    for (double ms = 0.0; ms <= msPerRow * 1.5; ms += msPerRow / 32.0) {
        const float phase = scrollPhaseFor(1, kStored, ms, msPerRow);
        QVERIFY(phase >= -1.0f / kStored && phase <= 0.0f);
        QVERIFY(phase >= previous);
        previous = phase;
    }
}

// Resizing the window, dragging the #WFH split or moving to another display changes how many rows
// are on screen - and must NOT throw the history away. Both the pixels and the rows scale with the
// height, so a row stays at the same distance from the newest whatever that height is: a taller
// window REVEALS older rows, a shorter one hides them, and no row is ever redrawn at a scale it was
// not captured at.
//
// This is the gate on a fix that was wrong. a431e01 restarted the history on any row-count change,
// believing a change rescaled the stored rows; it does not, and restarting blanked the waterfall on
// every frame of a window drag. The slide that prompted it was the old plan's rounding boundary
// (693/447 at 1.55 against a boundary at 1.50), which the explicit step removed.
void TestWaterfallGeometry::aRowCountChangePreservesTheHistory() {
    using namespace WaterfallGeometry;
    constexpr int kStored = 2048;
    constexpr int kWrite = 900;

    // Fill: one row per physical pixel, so the newest N rows are the same N rows at any height.
    for (const auto [from, to] :
         {std::pair{809, 693}, std::pair{693, 809}, std::pair{1512, 693}, std::pair{400, 1200}, std::pair{64, 2000}}) {
        for (int distance = 0; distance < (std::min)(from, to); ++distance) {
            const int before = rowForPixel(from - 1 - distance, float(from), kWrite, from, kStored);
            const int after = rowForPixel(to - 1 - distance, float(to), kWrite, to, kStored);
            QCOMPARE(after, before);
        }
    }

    // And at the fast steps, where the rows are taller: the row count scales with the height, so
    // the same row is still the same distance back through the history.
    for (const int level : {5, 6, 7, 10}) {
        for (const auto [from, to] : {std::pair{809, 693}, std::pair{1512, 693}, std::pair{693, 1512}}) {
            const SpeedPlan a = speedPlanFor(from, level, 1.0, 1.0);
            const SpeedPlan b = speedPlanFor(to, level, 1.0, 1.0);
            const int shared = (std::min)(a.visibleRows, b.visibleRows);
            for (int distance = 0; distance < shared; ++distance) {
                // The pixel that sits `distance` rows back from the newest, in each geometry.
                const int pixelA = static_cast<int>((from - 1) - distance * double(from) / a.visibleRows);
                const int pixelB = static_cast<int>((to - 1) - distance * double(to) / b.visibleRows);
                const int before = rowForPixel(pixelA, float(from), kWrite, a.visibleRows, kStored);
                const int after = rowForPixel(pixelB, float(to), kWrite, b.visibleRows, kStored);
                QVERIFY2(after == before, qPrintable(QStringLiteral("level %1, %2 -> %3 rows: distance %4 moved "
                                                                    "from row %5 to row %6")
                                                         .arg(level)
                                                         .arg(from)
                                                         .arg(to)
                                                         .arg(distance)
                                                         .arg(before)
                                                         .arg(after)));
            }
        }
    }
}

namespace {

// What the renderer does with a window, so the sweep exercises the same path as render():
// the split's remainder in framebuffer pixels, then rows at one per panel pixel.
struct Geometry {
    int framebufferHeightPx = 0;
    qreal dpr = 1.0;
    qreal framebufferPerPanel = 1.0;
    int waterfallPercent = 100;

    float waterfallFramebufferPx() const {
        // The remainder of the split, exactly as render() derives it - never a second truncation.
        const float spectrumRatio = static_cast<float>(100 - waterfallPercent) / 100.0f;
        const float spectrum = std::floor(static_cast<float>(framebufferHeightPx) * spectrumRatio);
        return static_cast<float>(framebufferHeightPx) - spectrum;
    }
    int pixelRows(int storedRows, int minRows) const {
        return WaterfallGeometry::visibleRowsFor(waterfallFramebufferPx(), storedRows, minRows, framebufferPerPanel);
    }
};

} // namespace

// The resolution gate. QK4 is developed on one Mac at one resolution; this is what stops that
// machine deciding whether the display is correct. Every combination of window height, device pixel
// ratio, framebuffer-to-panel ratio, #WFH split and SPEED step, against the invariants that have to
// hold for the picture to look the same everywhere.
void TestWaterfallGeometry::everyDisplayAndSplitSweep() {
    using namespace WaterfallGeometry;
    constexpr int kStored = 4096;
    constexpr int kMinRows = 64;
    const double rate = 15.0; // a measured frame rate; the invariants hold at any

    for (const int framebufferHeight : {600, 900, 1080, 1692, 2160, 3384}) {
        for (const qreal dpr : {1.0, 1.25, 1.5, 2.0, 3.0}) {
            for (const qreal ratio : {1.0, 6016.0 / 3840.0, 2.0}) {
                for (const int percent : {0, 25, 50, 75, 100}) {
                    const Geometry g{framebufferHeight, dpr, ratio, percent};
                    const int pixelRows = g.pixelRows(kStored, kMinRows);
                    const QString where = QStringLiteral("fb %1 dpr %2 ratio %3 wfh %4")
                                              .arg(framebufferHeight)
                                              .arg(dpr)
                                              .arg(ratio)
                                              .arg(percent);
                    if (percent == 0)
                        continue; // spectrum only: there is no waterfall to judge

                    // Never more rows than panel pixels, so the display pipeline cannot merge two
                    // rows into one pixel and take the detail with them.
                    QVERIFY2(pixelRows <= static_cast<int>(std::ceil(g.waterfallFramebufferPx() / ratio)),
                             qPrintable(where));
                    // And never clamped by the ring: past that the rows stretch and 1:1 is gone.
                    QVERIFY2(pixelRows < kStored - 1, qPrintable(where + " hit the stored-row ceiling"));

                    for (int level = SpeedLevelMin; level <= SpeedLevelMax; ++level) {
                        const SpeedPlan plan = speedPlanFor(pixelRows, level, dpr, ratio);
                        const QString at = where + QStringLiteral(" level %1").arg(level);

                        QVERIFY2(plan.visibleRows >= 1 && plan.visibleRows <= pixelRows, qPrintable(at));
                        QVERIFY2(plan.framesPerRow >= 1 && plan.framesPerRow <= MaxFramesPerRow, qPrintable(at));
                        QVERIFY2(plan.pixelRowsPerRow >= 1.0f, qPrintable(at));

                        // Fill and the folds are panel-native: one row per panel pixel, whatever the
                        // display. That is Fill's whole point - the sharpest the panel can show.
                        if (SpeedLevels[static_cast<std::size_t>(level - 1)].unit == RowUnit::PanelPixel) {
                            QCOMPARE(plan.pixelRowsPerRow, 1.0f);
                            QCOMPARE(plan.visibleRows, pixelRows);
                        } else if (speedLevelFits(level, pixelRows, kMinRows, dpr, ratio)) {
                            // A fast level is `rowHeight` LOGICAL pixels per frame, so the picture
                            // travels at rowHeight * rate logical pixels a second on every display.
                            // This is the invariant the framebuffer-pixel version broke: the same
                            // window scrolled at half the speed on a 2x panel.
                            //
                            // Only for levels this display actually offers: where a logical pixel is
                            // smaller than a panel pixel the level clamps to Fill's height and is
                            // withdrawn, so it has no nominal speed to keep.
                            const double nominal = SpeedLevels[static_cast<std::size_t>(level - 1)].rowHeight;
                            const double panelPxPerLogical = ratio > 0.0 ? dpr / ratio : dpr;
                            const double logicalPxPerSecond = plan.pixelRowsPerRow / panelPxPerLogical * rate;
                            QVERIFY2(std::abs(logicalPxPerSecond - nominal * rate) < 0.01 * nominal * rate,
                                     qPrintable(at + QStringLiteral(" travels %1 logical px/s, not %2")
                                                         .arg(logicalPxPerSecond)
                                                         .arg(nominal * rate)));
                        }

                        // Every step the ladder actually offers leaves a waterfall behind.
                        if (speedLevelFits(level, pixelRows, kMinRows, dpr, ratio))
                            QVERIFY2(plan.visibleRows >= kMinRows, qPrintable(at));

                        // The seconds reported are the seconds the plan shows, and are positive
                        // wherever a rate is known.
                        const double seconds = secondsOnScreen(pixelRows, level, rate, dpr, ratio);
                        QVERIFY2(seconds > 0.0, qPrintable(at));

                        // The scroll residual is bounded and closes within one row interval at every
                        // geometry, so the picture can never jitter backwards or stall part-way.
                        const double msPerRow = 1000.0 * plan.framesPerRow / rate;
                        QCOMPARE(scrollPhaseFor(1, kStored, msPerRow, msPerRow), 0.0f);
                        for (double ms = 0.0; ms < msPerRow; ms += msPerRow / 8.0) {
                            const float phase = scrollPhaseFor(1, kStored, ms, msPerRow);
                            QVERIFY2(phase <= 0.0f && phase >= -float(MaxScrollLagRows) / kStored, qPrintable(at));
                        }
                    }
                }
            }
        }
    }
}

// The four geometries that must never regress quietly: the window QK4 opens at, the smallest it
// allows, and both of Mike's test configurations. A sweep can drift away from the cases a person
// actually looks at; these are those cases, by name.
void TestWaterfallGeometry::namedGeometriesNeverRegress() {
    using namespace WaterfallGeometry;
    constexpr int kStored = 4096;
    constexpr int kMinRows = 64;

    struct Named {
        const char *name;
        Geometry geometry;
    };
    // 3008x1692 is a macOS scaled mode on a 4K panel: rendered at 6016x3384, scaled to 3840x2160.
    const Named cases[] = {
        {"3008x1692 scaled on 4K, DPR 2, full window, 60% waterfall", {3384, 2.0, 6016.0 / 3840.0, 60}},
        {"3840x2160 native, DPR 1, full window, 60% waterfall", {2160, 1.0, 1.0, 60}},
        {"default window, DPR 2, 60% waterfall", {1200, 2.0, 1.0, 60}},
        {"smallest allowed window, DPR 1, 25% waterfall", {768, 1.0, 1.0, 25}},
    };

    for (const auto &[name, g] : cases) {
        const int pixelRows = g.pixelRows(kStored, kMinRows);
        const QString where = QString::fromLatin1(name);

        // A waterfall worth the name, and never clamped by the ring.
        QVERIFY2(pixelRows >= kMinRows, qPrintable(where + " has too few rows"));
        QVERIFY2(pixelRows < kStored - 1, qPrintable(where + " hit the stored-row ceiling"));

        // Fill fills it, one row per panel pixel.
        const SpeedPlan fill = speedPlanFor(pixelRows, SpeedLevelFill, g.dpr, g.framebufferPerPanel);
        QCOMPARE(fill.visibleRows, pixelRows);
        QCOMPARE(fill.pixelRowsPerRow, 1.0f);

        // At least one step faster than Fill is offered in every one of them: a window this size
        // must be able to speed up, or the ladder's floor is too aggressive.
        QVERIFY2(stepSpeedLevel(SpeedLevelFill, +1, pixelRows, kMinRows, g.dpr, g.framebufferPerPanel) !=
                     SpeedLevelFill,
                 qPrintable(where + " offers no step faster than Fill"));

        // And the fast steps travel at the same logical speed in all four, which is the whole point.
        const SpeedPlan plan = speedPlanFor(pixelRows, 7, g.dpr, g.framebufferPerPanel);
        const double panelPxPerLogical = g.dpr / g.framebufferPerPanel;
        const double nominal = SpeedLevels[6].rowHeight; // level 7
        QVERIFY2(std::abs(plan.pixelRowsPerRow / panelPxPerLogical - nominal) < 0.01,
                 qPrintable(where + QStringLiteral(" level 7 travels %1 logical px per frame, not 3")
                                        .arg(plan.pixelRowsPerRow / panelPxPerLogical)));
    }

    // The two configurations Mike compares directly: the scaled mode must no longer ask for more
    // rows than the panel can show, which is what made it look softer than 4K native.
    const Geometry scaled{3384, 2.0, 6016.0 / 3840.0, 60};
    const Geometry native{2160, 1.0, 1.0, 60};
    QCOMPARE(scaled.pixelRows(kStored, kMinRows), native.pixelRows(kStored, kMinRows));
}

QTEST_MAIN(TestWaterfallGeometry)
#include "test_waterfallgeometry.moc"
