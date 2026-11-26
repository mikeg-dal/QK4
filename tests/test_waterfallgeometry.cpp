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
    void secondsLabelHoldsAgainstRateDrift();
    void scrollPhaseClosesOverTheRowInterval();
    void fasterRowsAreWholePixelsTall();

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
        const SpeedPlan fill = speedPlanFor(rows, SpeedFill, 1.0, 1);
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
        for (std::size_t i = 1; i < SpeedSteps.size(); ++i) {
            const SpeedPlan a = speedPlanFor(rows, SpeedSteps[i - 1], 1.0, 1);
            const SpeedPlan b = speedPlanFor(rows, SpeedSteps[i], 1.0, 1);
            const bool differs = a.visibleRows != b.visibleRows || a.framesPerRow != b.framesPerRow ||
                                 a.pixelRowsPerRow != b.pixelRowsPerRow;
            QVERIFY2(differs, qPrintable(QStringLiteral("steps %1 and %2 draw the same at %3 rows")
                                             .arg(SpeedSteps[i - 1])
                                             .arg(SpeedSteps[i])
                                             .arg(rows)));
        }
    }
}

// The plan is exact, with no measured rate in it, so there is no boundary for it to hop across.
// The bench defect of 2026-11-25: 693 rows, 30 s and 14.9 Hz put the old ratio at 1.55 against a
// rounding boundary at 1.50, and the waterfall jumped up the screen and back down as the measured
// rate wandered. A plan that never sees the rate cannot do it.
void TestWaterfallGeometry::speedPlanIsExactAndIndependentOfTheRate() {
    using namespace WaterfallGeometry;
    for (int step : SpeedSteps) {
        const SpeedPlan plan = speedPlanFor(693, step, 1.0, 1);
        if (step >= SpeedFill) {
            QCOMPARE(plan.framesPerRow, 1);
            QCOMPARE(plan.pixelRowsPerRow, static_cast<float>(step));
            QCOMPARE(plan.visibleRows, step == SpeedFill ? 693 : qRound(693.0 / step));
        } else {
            QCOMPARE(plan.visibleRows, 693);
            QCOMPARE(plan.pixelRowsPerRow, 1.0f);
            QCOMPARE(plan.framesPerRow, -step);
        }
        QVERIFY(plan.visibleRows >= 2 && plan.visibleRows <= 693);
        QVERIFY(plan.framesPerRow >= 1 && plan.framesPerRow <= MaxFramesPerRow);
    }
}

// The seconds on the label are the seconds the plan actually shows, and they follow the window
// height and the measured rate rather than being the thing stored.
void TestWaterfallGeometry::speedSecondsFollowHeightAndRate() {
    using namespace WaterfallGeometry;
    // Mike's bench geometry: 693 rows at 14.9 Hz. Fill holds 47 s, half the rows hold half of it.
    QCOMPARE(qRound(secondsOnScreen(693, SpeedFill, 14.9, 1.0, 1)), 47);
    QCOMPARE(qRound(secondsOnScreen(693, 2, 14.9, 1.0, 1)), 23);
    QCOMPARE(qRound(secondsOnScreen(693, 3, 14.9, 1.0, 1)), 16);         // 231 rows at 14.9 Hz is 15.5 s
    QCOMPARE(qRound(secondsOnScreen(693, -2, 14.9, 1.0, 1)), 93);        // folding holds more than Fill can
    QCOMPARE(qRound(secondsOnScreen(346, SpeedFill, 14.9, 1.0, 1)), 23); // half the window, half the history
    // Nothing is claimed before the rate is known, or with no window.
    QCOMPARE(secondsOnScreen(693, 2, 0.0, 1.0, 1), 0.0);
    QCOMPARE(secondsOnScreen(0, 2, 14.9, 1.0, 1), 0.0);
}

void TestWaterfallGeometry::speedStepsAndLabels() {
    using namespace WaterfallGeometry;
    QCOMPARE(stepSpeed(SpeedFill, +1), 2);
    QCOMPARE(stepSpeed(2, +1), 3);
    QCOMPARE(stepSpeed(SpeedFill, -1), -2); // below Fill is the folding side
    QCOMPARE(stepSpeed(-2, -1), -3);
    QCOMPARE(stepSpeed(-4, -1), -4); // held at the ends
    QCOMPARE(stepSpeed(12, +1), 12);
    QCOMPARE(stepSpeed(7, +1), 2); // not a step: counts as Fill, so + moves one off Fill
    QVERIFY(isSpeedStep(SpeedFill));
    QVERIFY(isSpeedStep(-4));
    QVERIFY(!isSpeedStep(0));
    QVERIFY2(!isSpeedStep(30) && !isSpeedStep(15), "the old ladder held seconds; these are not steps");

    // The label says what the step comes to, and names Fill so it can be found again.
    QCOMPARE(speedLabel(SpeedFill, 693, 14.9, 1.0, 1), QStringLiteral("Fill 47s"));
    QCOMPARE(speedLabel(2, 693, 14.9, 1.0, 1), QStringLiteral("23s"));
    QCOMPARE(speedLabel(-2, 693, 14.9, 1.0, 1), QStringLiteral("93s"));
    // Before the rate is known, the shape rather than a number that would be wrong.
    QCOMPARE(speedLabel(SpeedFill, 693, 0.0, 1.0, 1), QStringLiteral("Fill"));
    QCOMPARE(speedLabel(3, 693, 0.0, 1.0, 1), QStringLiteral("1/3"));
    QCOMPARE(speedLabel(-2, 693, 0.0, 1.0, 1), QStringLiteral("x2"));
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
        for (int step : SpeedSteps) {
            {
                const SpeedPlan plan = speedPlanFor(height, step, 1.0, 1);
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
        for (const int perPanel : {1, 2}) {
            QCOMPARE(physicalRowHeightFor(SpeedFill, dpr, perPanel), 1.0f);
            for (const int fold : {-2, -3, -4})
                QCOMPARE(physicalRowHeightFor(fold, dpr, perPanel), 1.0f);
            const SpeedPlan plan = speedPlanFor(800, SpeedFill, dpr, perPanel);
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
    for (const int step : {2, 3, 4, 5, 6, 8, 10, 12}) {
        // One logical pixel is dpr framebuffer pixels; with one framebuffer pixel per panel pixel
        // that is dpr physical pixels, so the step's physical height scales exactly with dpr.
        QCOMPARE(physicalRowHeightFor(step, 1.0, 1), static_cast<float>(step));
        QCOMPARE(physicalRowHeightFor(step, 2.0, 1), static_cast<float>(step * 2));
        QCOMPARE(physicalRowHeightFor(step, 1.5, 1), static_cast<float>(step) * 1.5f);

        // The travel per frame in LOGICAL pixels is what must match across displays. A window of
        // the same logical height shows the same seconds on every one of them.
        const double logicalHeight = 400.0;
        double firstSeconds = 0.0;
        for (const qreal dpr : {1.0, 1.25, 1.5, 2.0, 3.0}) {
            const int physicalRows = static_cast<int>(std::lround(logicalHeight * dpr));
            const double seconds = secondsOnScreen(physicalRows, step, 15.0, dpr, 1);
            if (firstSeconds == 0.0)
                firstSeconds = seconds;
            // Within a row of rounding: the row count is an integer at every scale.
            QVERIFY2(std::abs(seconds - firstSeconds) <= 1.0 / 15.0 * 2.0,
                     qPrintable(QStringLiteral("step %1 at dpr %2 shows %3 s, not %4 s")
                                    .arg(step)
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

    // A tall window holds the whole ladder.
    for (const int step : SpeedSteps)
        QVERIFY2(speedStepFits(step, 1512, kMinRows, 1.0, 1), qPrintable(QStringLiteral("step %1").arg(step)));

    // A 150-pixel waterfall cannot hold 64 rows past 1/2, so the fast end is withdrawn.
    QVERIFY(speedStepFits(SpeedFill, 150, kMinRows, 1.0, 1));
    QVERIFY(speedStepFits(-4, 150, kMinRows, 1.0, 1));
    QVERIFY(speedStepFits(2, 150, kMinRows, 1.0, 1));  // 75 rows
    QVERIFY(!speedStepFits(3, 150, kMinRows, 1.0, 1)); // 50 rows
    QVERIFY(!speedStepFits(12, 150, kMinRows, 1.0, 1));

    // Stepping up stops on the last step that fits instead of landing on one that does not.
    QCOMPARE(stepSpeedFitting(2, +1, 150, kMinRows, 1.0, 1), 2);
    QCOMPARE(stepSpeedFitting(SpeedFill, +1, 150, kMinRows, 1.0, 1), 2);
    // Downward is never withdrawn: folding adds history, it never costs rows.
    QCOMPARE(stepSpeedFitting(2, -1, 150, kMinRows, 1.0, 1), SpeedFill);
    QCOMPARE(stepSpeedFitting(SpeedFill, -1, 150, kMinRows, 1.0, 1), -2);

    // Every step the ladder offers, at any height, actually leaves a waterfall behind.
    for (int height = 64; height <= 2047; height += 7) {
        for (const qreal dpr : {1.0, 1.5, 2.0}) {
            int step = SpeedFill;
            for (int i = 0; i < 20; ++i) {
                const int next = stepSpeedFitting(step, +1, height, kMinRows, dpr, 1);
                QVERIFY(speedStepFits(next, height, kMinRows, dpr, 1));
                if (next == step)
                    break;
                step = next;
            }
        }
    }
}

// The label is damped and the plan is not. SPEED 1/3 over 693 rows at 14.9 Hz computes 15.50 s
// exactly, so a fraction of a percent of drift in the measured rate flipped the key between 15 and
// 16 with nothing touched (bench, 2026-11-26).
void TestWaterfallGeometry::secondsLabelHoldsAgainstRateDrift() {
    using namespace WaterfallGeometry;
    // The boundary case itself: the rate wanders either side of the value that rounds to 15.5.
    const double a = secondsOnScreen(693, 3, 14.88, 1.0, 1);
    const double b = secondsOnScreen(693, 3, 14.93, 1.0, 1);
    QVERIFY(qRound(a) != qRound(b)); // the numbers really do differ - this is the defect
    QVERIFY2(!secondsLabelShouldChange(a, b), "a rate wobble must not move the key");

    // A real change still gets through: one step of the ladder is far more than the hold.
    const double oneStepAway = secondsOnScreen(693, 4, 14.9, 1.0, 1);
    QVERIFY(secondsLabelShouldChange(a, oneStepAway));
    // So does appearing or disappearing, which is the switch between a number and the shape.
    QVERIFY(secondsLabelShouldChange(0.0, a));
    QVERIFY(secondsLabelShouldChange(a, 0.0));
    QVERIFY(!secondsLabelShouldChange(0.0, 0.0));
    // The hold is a fraction of what is shown, so it scales with the value.
    QVERIFY(!secondsLabelShouldChange(100.0, 103.0));
    QVERIFY(secondsLabelShouldChange(100.0, 110.0));
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

    // Several rows at once (a burst after a stall) travel together, never more than they advanced.
    QCOMPARE(scrollPhaseFor(3, kStored, 0.0, msPerRow), -3.0f / kStored);
    QCOMPARE(scrollPhaseFor(3, kStored, msPerRow * 3.0, msPerRow), 0.0f);

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

QTEST_MAIN(TestWaterfallGeometry)
#include "test_waterfallgeometry.moc"
