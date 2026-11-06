// Main-window sizing (src/utils/windowfit.h): the size the window opens at, and the interface
// scale at which it fits a screen. Screens are given in logical pixels, the units Qt lays out in —
// a 1920x1080 panel at 150% OS scaling is 1280x720 logical.

#include "utils/windowfit.h"

#include <QTest>
#include <QVariant>

namespace {

// What the main window needed when this was written, in its largest everyday form: KPA1500 enabled,
// menu bar inside the window (Windows, Linux), plus title bar and frame. Exact values do not matter to
// the rules under test; realistic ones keep the cases readable.
const QSize kRequired(1214, 847);

// Available area of a screen at the given OS scaling, minus a 48 px taskbar.
QSize screenAt(int width, int height, double osScale = 1.0) {
    return {int(width / osScale), int(height / osScale) - 48};
}

QSize screen1080(double osScale) {
    return screenAt(1920, 1080, osScale);
}

// What Qt reports as a screen's logical size under QT_SCALE_FACTOR: native size / factor, rounded to
// the nearest pixel per dimension (QHighDpi scales a QSize by a qreal, which rounds).
QSize logicalUnder(QSize native, double factor) {
    return {qRound(native.width() / factor), qRound(native.height() / factor)};
}

bool fits(QSize required, QSize available, double fromScale, double toScale) {
    const double shrink = toScale / fromScale;
    return required.width() * shrink <= available.width() + 1e-9 &&
           required.height() * shrink <= available.height() + 1e-9;
}

} // namespace

class WindowFitTests : public QObject {
    Q_OBJECT

private slots:
    void initialSizeIsPreferredWhenItFits() {
        QCOMPARE(WindowFit::initialWindowSize({1157, 862}, {1340, 0}, {1920, 1032}), QSize(1340, 862));
    }

    void initialSizeNeverBelowLayoutMinimum() {
        // A preferred size smaller than the layout is the original bug: the window opened at 840
        // tall, below its own 862 px layout, and the side panel was squeezed.
        QCOMPARE(WindowFit::initialWindowSize({1157, 862}, {1340, 840}, {1920, 1032}), QSize(1340, 862));
    }

    void initialSizeBoundedByScreen() {
        QCOMPARE(WindowFit::initialWindowSize({1157, 700}, {1340, 0}, {1280, 900}), QSize(1280, 700));
    }

    void layoutWinsOverATooSmallScreen() {
        // Too small a screen gets a window that hangs off the edge, never one smaller than its
        // contents. Shrinking the interface to fit is fitScale's job, not this one's.
        QCOMPARE(WindowFit::initialWindowSize({1157, 862}, {1340, 0}, {1280, 672}), QSize(1280, 862));
    }

    void nativeOnA1080pScreen() {
        // 1032 / 847 leaves room for 121%, but growth moves only in 25% steps and 125% does not fit.
        QCOMPARE(WindowFit::fitScale(kRequired, screen1080(1.0), 1.0), 1.0);
    }

    void growthTakesOnly25PercentSteps() {
        // Room for 120%, 124.9% and 125% exactly.
        QCOMPARE(WindowFit::fitScale({1000, 1000}, {1200, 1200}, 1.0), 1.0);
        QCOMPARE(WindowFit::fitScale({1000, 1000}, {1249, 1249}, 1.0), 1.0);
        QCOMPARE(WindowFit::fitScale({1000, 1000}, {1250, 1250}, 1.0), 1.25);
        // Shrinking keeps its 5% steps.
        QCOMPARE(WindowFit::fitScale({1000, 1000}, {950, 950}, 1.0), 0.95);
    }

    void cappedOnA1440pScreen() {
        // A 27" 2560x1440 monitor has room for 164%; Auto stops at its ceiling.
        QCOMPARE(WindowFit::fitScale(kRequired, screenAt(2560, 1440), 1.0), WindowFit::kMaxScale);
        QCOMPARE(WindowFit::kMaxScale, 1.25);
    }

    void commonScreens_data() {
        QTest::addColumn<QSize>("available");
        QTest::addColumn<double>("expected");
        QTest::newRow("2560x1440") << screenAt(2560, 1440) << 1.25;
        QTest::newRow("1920x1080") << screenAt(1920, 1080) << 1.0;
        QTest::newRow("1440x900") << screenAt(1440, 900) << 1.0;
        QTest::newRow("1366x768") << screenAt(1366, 768) << 0.85;
        QTest::newRow("1024x768") << screenAt(1024, 768) << 0.8;
        QTest::newRow("1920x1080 at 150%") << screenAt(1920, 1080, 1.5) << 0.75;
        QTest::newRow("3840x2160 at 150%") << screenAt(3840, 2160, 1.5) << 1.25;
    }

    void commonScreens() {
        QFETCH(QSize, available);
        QFETCH(double, expected);
        const double s = WindowFit::fitScale(kRequired, available, 1.0);
        QCOMPARE(s, expected);
        QVERIFY(fits(kRequired, available, 1.0, s));
    }

    void laptopAt150Percent() {
        const double s = WindowFit::fitScale(kRequired, screen1080(1.5), 1.0);
        QVERIFY2(s <= 0.75, qPrintable(QString::number(s)));
        QVERIFY(fits(kRequired, screen1080(1.5), 1.0, s));
    }

    void laptopAt125Percent() {
        const double s = WindowFit::fitScale(kRequired, screen1080(1.25), 1.0);
        QCOMPARE(s, 0.95);
        QVERIFY(fits(kRequired, screen1080(1.25), 1.0, s));
    }

    void exactFitAtAStepIsNotRoundedDown() { QCOMPARE(WindowFit::fitScale({1000, 1000}, {750, 750}, 1.0), 0.75); }

    void factorIsSavedAsItsDecimal() {
        // QSettings writes the shortest text that round-trips. 14 x 0.05 computed directly is
        // 0.7000000000000001, which is what the preferences file showed before.
        for (int percent = 50; percent <= 125; percent += percent < 100 ? 5 : 25) {
            const double s = WindowFit::fitScale({1000, 1000}, {percent * 10, percent * 10}, 1.0);
            QCOMPARE(QVariant(s).toString(), QString::number(percent / 100.0));
        }
    }

    void clampedAtTheFloor() { QCOMPARE(WindowFit::fitScale(kRequired, {400, 300}, 1.0), WindowFit::kMinScale); }

    void growsOnABiggerScreen() {
        // Launched last time on the laptop at 0.75, now docked to a 2560x1440 monitor. Under 0.75 the
        // layout is the same size in logical pixels and the screen is larger: 2560/0.75 wide.
        QCOMPARE(WindowFit::fitScale(kRequired, {3413, 1856}, 0.75), WindowFit::kMaxScale);
    }

    void shrinksBackFromAGrownFactor() {
        // Launched last time at 1.25 on the big monitor, now on the 1080p laptop at 150%: under 1.25
        // its 1280x672 logical screen reports 1024x538.
        const double s = WindowFit::fitScale(kRequired, {1024, 538}, 1.25);
        QCOMPARE(s, 0.75);
    }

    void ceilingLimitsGrowthButNotShrinking() {
        QCOMPARE(WindowFit::fitScale(kRequired, screenAt(2560, 1440), 1.0, WindowFit::kNativeScale), 1.0);
        QCOMPARE(WindowFit::fitScale(kRequired, {2048, 1114}, 1.25, WindowFit::kNativeScale), 1.0);
        QCOMPARE(WindowFit::fitScale(kRequired, screen1080(1.5), 1.0, WindowFit::kNativeScale), 0.75);
    }

    void noGrowthWhereTheWorkAreaIsUnknown() {
        // X11 with several monitors, and Wayland, report the whole screen as available.
        QCOMPARE(WindowFit::autoScaleCeiling({0, 0, 2560, 1440}, {0, 0, 2560, 1440}), WindowFit::kNativeScale);
        QCOMPARE(WindowFit::autoScaleCeiling({0, 0, 2560, 1440}, {0, 0, 2560, 1392}), WindowFit::kMaxScale);
        QCOMPARE(WindowFit::autoScaleCeiling({0, 0, 1440, 900}, {0, 25, 1440, 875}), WindowFit::kMaxScale);
    }

    void stableUnderTheScaleItChose() {
        // Relaunched at the factor it chose, the same screen must produce the same factor —
        // otherwise Auto would relaunch again. Shrinking and growing alike.
        for (const QSize available : {screen1080(1.5), screen1080(1.0), screenAt(2560, 1440)}) {
            const double first = WindowFit::fitScale(kRequired, available, 1.0);
            QCOMPARE(WindowFit::fitScale(kRequired, logicalUnder(available, first), first), first);
        }
    }

    void sweepNeverLoopsAcrossRelaunchesWithQtRounding() {
        // Every launch measures the screen under the factor the last one chose, rounded to whole
        // logical pixels as Qt reports it. That rounding is up to half a pixel of noise, so a screen
        // that fits a step to within a pixel can move one step on a later launch (about 1 in 3000 of
        // these geometries). It must then stay there: at most one change, by one step, never back.
        for (int rw = 900; rw <= 1400; rw += 50)
            for (int rh = 700; rh <= 1000; rh += 7)
                for (int nw = 1024; nw <= 3840; nw += 128)
                    for (int nh = 600; nh <= 2160; nh += 3) {
                        const QSize req(rw, rh), native(nw, nh);
                        const double first = WindowFit::fitScale(req, native, 1.0);
                        double scale = first;
                        int changes = 0;
                        for (int launch = 0; launch < 6; ++launch) {
                            const double next = WindowFit::fitScale(req, logicalUnder(native, scale), scale);
                            if (next != scale)
                                ++changes;
                            scale = next;
                        }
                        if (changes > 1 || qAbs(scale - first) > WindowFit::kScaleStep + 1e-9)
                            QFAIL(qPrintable(QString("%1x%2 on %3x%4: %5 then %6 after %7 changes")
                                                 .arg(rw)
                                                 .arg(rh)
                                                 .arg(nw)
                                                 .arg(nh)
                                                 .arg(first)
                                                 .arg(scale)
                                                 .arg(changes)));
                    }
    }

    void degenerateInputLeavesFullSize() {
        QCOMPARE(WindowFit::fitScale({}, screen1080(1.0), 1.0), 1.0);
        QCOMPARE(WindowFit::fitScale(kRequired, {}, 1.0), 1.0);
        QCOMPARE(WindowFit::fitScale(kRequired, screen1080(1.0), 0.0), 1.0);
    }

    void sweepResultAlwaysFitsAndIsAStep() {
        for (int rw = 600; rw <= 2400; rw += 150)
            for (int rh = 500; rh <= 1600; rh += 110)
                for (int aw = 640; aw <= 3840; aw += 320)
                    for (int ah = 400; ah <= 2160; ah += 160)
                        for (double cur : {0.5, 0.75, 1.0, 1.25}) {
                            const QSize req(rw, rh), avail(aw, ah);
                            const double s = WindowFit::fitScale(req, avail, cur);
                            QVERIFY(s >= WindowFit::kMinScale && s <= WindowFit::kMaxScale);
                            const double steps = s / WindowFit::kScaleStep;
                            QVERIFY(qAbs(steps - qRound(steps)) < 1e-6);
                            if (s > WindowFit::kNativeScale)
                                QCOMPARE(s, WindowFit::kMaxScale);
                            if (s > WindowFit::kMinScale)
                                QVERIFY2(fits(req, avail, cur, s), qPrintable(QString("%1x%2 in %3x%4 at %5 -> %6")
                                                                                  .arg(rw)
                                                                                  .arg(rh)
                                                                                  .arg(aw)
                                                                                  .arg(ah)
                                                                                  .arg(cur)
                                                                                  .arg(s)));
                        }
    }
};

QTEST_GUILESS_MAIN(WindowFitTests)
#include "test_windowfit.moc"
