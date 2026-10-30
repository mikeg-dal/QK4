#include "dsp/waterfallcolorlut.h"
#include "dsp/waterfallgeometry.h"
#include "dsp/spectrumscale.h"
#include <QSet>
#include <QtTest>
#include <algorithm>
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

    // Colour window (WaterfallGeometry::colorWindowFor and the shader mirror).
    void noiseAtRefIsMidBlueNotBlack();
    void oldRefScaleWindowDrewNoiseBlack();
    void higherColorRangeIsBrighter();
    void unknownColorRangeUsesDefault();
    void shaderMirrorMatchesPaletteFraction();
    void storedByteEncodesTheRadiosLevel();
    void noDataDrawsAtPaletteBottomUnderAnyWindow();
    void capturedRowsKeepTheirColourWhenRefSteps();
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

// Through the GPU path: the byte stored for a level, then the shader's colour uniforms.
float drawnFraction(float dbm, float refDbm, int colorRangeDb) {
    const auto window = WaterfallGeometry::colorWindowFor(refDbm, colorRangeDb);
    return WaterfallGeometry::shaderFractionFor(WaterfallGeometry::storedByteFor(dbm),
                                                WaterfallGeometry::colorUniformsFor(window));
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
    // A carrier 20 dB over the noise reaches the warm end, as WWV does on the K4.
    const Rgb carrier = lutAt(drawnFraction(-110.0f + 23.0f, -110.0f, 14));
    QVERIFY(carrier.r == 255);
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
            const float lo = WaterfallGeometry::paletteFractionFor(dbm, WaterfallGeometry::colorWindowFor(ref, wbs));
            const float hi =
                WaterfallGeometry::paletteFractionFor(dbm, WaterfallGeometry::colorWindowFor(ref, wbs + 1));
            QVERIFY(hi >= lo);
        }
    }
    QVERIFY(drawnFraction(ref, ref, 30) > drawnFraction(ref, ref, 5));
}

void TestWaterfallGeometry::unknownColorRangeUsesDefault() {
    const auto def = WaterfallGeometry::colorWindowFor(-110.0f, WaterfallGeometry::DefaultColorRangeDb);
    for (int bad : {-1, 0, 4, 31, 100}) {
        const auto w = WaterfallGeometry::colorWindowFor(-110.0f, bad);
        QCOMPARE(w.lowDbm, def.lowDbm);
        QCOMPARE(w.spanDb, def.spanDb);
    }
    // The hypothesis, stated as numbers so a change to it is a deliberate test edit.
    const auto w = WaterfallGeometry::colorWindowFor(-110.0f, 14);
    QCOMPARE(w.lowDbm, -124.0f);
    QCOMPARE(w.spanDb, WaterfallGeometry::PaletteSpanDb);
}

// storedByteFor + colorUniformsFor + the shader line must agree with the direct dBm mapping, to
// within the byte's 1 dB step, for every REF and colour range the radio can send.
void TestWaterfallGeometry::shaderMirrorMatchesPaletteFraction() {
    const float tolerance = 0.5f / WaterfallGeometry::PaletteSpanDb + 1e-4f;
    for (int ref = -160; ref <= 50; ref += 7) {
        for (int wbs = 5; wbs <= 30; wbs += 5) {
            const auto window = WaterfallGeometry::colorWindowFor(static_cast<float>(ref), wbs);
            for (float dbm = -161.0f; dbm <= 93.0f; dbm += 0.37f) {
                const float direct = WaterfallGeometry::paletteFractionFor(dbm, window);
                const float drawn = drawnFraction(dbm, static_cast<float>(ref), wbs);
                QVERIFY2(
                    std::abs(direct - drawn) <= tolerance,
                    qPrintable(
                        QString("ref %1 wbs %2 dbm %3: %4 vs %5").arg(ref).arg(wbs).arg(dbm).arg(direct).arg(drawn)));
            }
        }
    }
}

void TestWaterfallGeometry::storedByteEncodesTheRadiosLevel() {
    // The K4's own encoding: byte = dBm + K4_DBM_OFFSET.
    QCOMPARE(int(WaterfallGeometry::storedByteFor(-110.0f)), 52);
    QCOMPARE(int(WaterfallGeometry::storedByteFor(-110.4f)), 52);
    QCOMPARE(int(WaterfallGeometry::storedByteFor(-109.6f)), 52);
    QCOMPARE(int(WaterfallGeometry::storedByteFor(93.0f)), 255);
    QCOMPARE(int(WaterfallGeometry::storedByteFor(500.0f)), 255);
    // Real data never collides with "no data".
    QCOMPARE(int(WaterfallGeometry::storedByteFor(-162.0f)), 1);
    QCOMPARE(int(WaterfallGeometry::storedByteFor(-500.0f)), 1);
}

void TestWaterfallGeometry::noDataDrawsAtPaletteBottomUnderAnyWindow() {
    for (int ref = -200; ref <= 50; ref += 10) {
        for (int wbs = 5; wbs <= 30; ++wbs) {
            const auto u = WaterfallGeometry::colorUniformsFor(WaterfallGeometry::colorWindowFor(ref, wbs));
            QCOMPARE(WaterfallGeometry::shaderFractionFor(WaterfallGeometry::NoDataByte, u), 0.0f);
        }
    }
    // The mini-pan's pass-through uniforms leave a stored palette position unchanged.
    const WaterfallGeometry::ColorUniforms passThrough;
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
        const ColorWindow window = colorWindowFor(ref, 14);
        for (float dbm = ref - 20.0f; dbm <= ref + 40.0f; dbm += 0.5f) {
            const quint8 byte = capturedByteFor(dbm, window);
            QVERIFY(byte != NoDataByte);
            QVERIFY(std::abs(shaderFractionFor(byte, passThrough) - paletteFractionFor(dbm, window)) <= 1.0f / 255.0f);
        }
    }
    // The same noise level captured before and after a 1 dB REF step stores different bytes: only the
    // new row changes colour, because the uniforms no longer follow REF.
    const float noise = -104.0f;
    QVERIFY(capturedByteFor(noise, colorWindowFor(-106.0f, 14)) != capturedByteFor(noise, colorWindowFor(-107.0f, 14)));
}

QTEST_MAIN(TestWaterfallGeometry)
#include "test_waterfallgeometry.moc"
