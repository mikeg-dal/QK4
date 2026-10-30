#include "dsp/spectrumgeometry.h"
#include <QtTest>

using namespace SpectrumGeometry;

class TestSpectrumGeometry : public QObject {
    Q_OBJECT

private slots:
    void modeParsesRadioStateSpelling();
    void unknownModeStringsAreUnknown();
    void dataSubModeOutOfRangeIsDataA();

    void cwShiftsTheDisplayCentreUp();
    void cwReverseShiftsTheDisplayCentreDown();
    void otherModesKeepTheDisplayCentre();
    void cwMarkerSitsAtThePassbandCentre();

    void ssbPassbandFollowsIfShift();
    void cwPassbandMatchesItsMarker();
    void rttyPassbandStraddlesMarkAndSpace();
    void pskPassbandIsCentredOnTheDial();
    void dataAPassbandIsUpperSideband();
    void amFmAndUnknownIgnoreIfShift();

    void normalFrameIsUsable();
    void binCountWiderThanTheTextureIsRejected();
    void binsPastThePayloadAreRejected();
    void degenerateFramesAreRejected();

    void frequencyLabelsTakeOneEvenStep();
    void frequencyLabelStepIsTheSmallestThatFits();
    void fillIsOpaqueDownToTheBottomEdge();
    void avgOneDrawsEveryFrameAsItArrives();
};

void TestSpectrumGeometry::modeParsesRadioStateSpelling() {
    QCOMPARE(modeFromString("LSB"), Mode::Lsb);
    QCOMPARE(modeFromString("USB"), Mode::Usb);
    QCOMPARE(modeFromString("CW"), Mode::Cw);
    QCOMPARE(modeFromString("CW-R"), Mode::CwR);
    QCOMPARE(modeFromString("DATA"), Mode::Data);
    QCOMPARE(modeFromString("DATA-R"), Mode::DataR);
    QCOMPARE(modeFromString("AM"), Mode::Am);
    QCOMPARE(modeFromString("FM"), Mode::Fm);
}

void TestSpectrumGeometry::unknownModeStringsAreUnknown() {
    QCOMPARE(modeFromString(""), Mode::Unknown);
    QCOMPARE(modeFromString("cw"), Mode::Unknown);
    QCOMPARE(modeFromString("AFSK"), Mode::Unknown);
}

void TestSpectrumGeometry::dataSubModeOutOfRangeIsDataA() {
    QCOMPARE(dataSubModeFromInt(0), DataSubMode::DataA);
    QCOMPARE(dataSubModeFromInt(1), DataSubMode::AfskA);
    QCOMPARE(dataSubModeFromInt(2), DataSubMode::FskD);
    QCOMPARE(dataSubModeFromInt(3), DataSubMode::PskD);
    QCOMPARE(dataSubModeFromInt(-1), DataSubMode::DataA);
    QCOMPARE(dataSubModeFromInt(7), DataSubMode::DataA);
}

void TestSpectrumGeometry::cwShiftsTheDisplayCentreUp() {
    // IF shift is in decahertz: 50 -> 500 Hz.
    QCOMPARE(displayCenterHz(7030000, 50, Mode::Cw), qint64(7030500));
}

void TestSpectrumGeometry::cwReverseShiftsTheDisplayCentreDown() {
    QCOMPARE(displayCenterHz(7030000, 50, Mode::CwR), qint64(7029500));
}

void TestSpectrumGeometry::otherModesKeepTheDisplayCentre() {
    for (Mode m : {Mode::Unknown, Mode::Lsb, Mode::Usb, Mode::Data, Mode::DataR, Mode::Am, Mode::Fm})
        QCOMPARE(displayCenterHz(14074000, 150, m), qint64(14074000));
}

void TestSpectrumGeometry::cwMarkerSitsAtThePassbandCentre() {
    QCOMPARE(markerFreqHz(7030000, 60, Mode::Cw), qint64(7030600));
    QCOMPARE(markerFreqHz(7030000, 60, Mode::CwR), qint64(7029400));
    QCOMPARE(markerFreqHz(14200000, 150, Mode::Usb), qint64(14200000));
}

void TestSpectrumGeometry::ssbPassbandFollowsIfShift() {
    const Passband usb = passbandFor(Mode::Usb, DataSubMode::DataA, 14200000, 2400, 150, 170);
    QCOMPARE(usb.lowHz, qint64(14200300));
    QCOMPARE(usb.highHz, qint64(14202700));
    const Passband lsb = passbandFor(Mode::Lsb, DataSubMode::DataA, 7200000, 2400, 150, 170);
    QCOMPARE(lsb.lowHz, qint64(7197300));
    QCOMPARE(lsb.highHz, qint64(7199700));
}

void TestSpectrumGeometry::cwPassbandMatchesItsMarker() {
    for (Mode m : {Mode::Cw, Mode::CwR}) {
        const Passband pb = passbandFor(m, DataSubMode::DataA, 7030000, 400, 55, 170);
        QCOMPARE((pb.lowHz + pb.highHz) / 2, markerFreqHz(7030000, 55, m));
        QCOMPARE(pb.highHz - pb.lowHz, qint64(400));
    }
}

void TestSpectrumGeometry::rttyPassbandStraddlesMarkAndSpace() {
    // The dial is the mark tone; space is one 170 Hz shift below. Centre is half a shift below.
    for (Mode m : {Mode::Data, Mode::DataR}) {
        for (DataSubMode s : {DataSubMode::AfskA, DataSubMode::FskD}) {
            const Passband pb = passbandFor(m, s, 14080000, 500, 150, 170);
            QCOMPARE(pb.lowHz, qint64(14080000 - 85 - 250));
            QCOMPARE(pb.highHz, qint64(14080000 - 85 + 250));
        }
    }
}

void TestSpectrumGeometry::pskPassbandIsCentredOnTheDial() {
    const Passband pb = passbandFor(Mode::Data, DataSubMode::PskD, 14070000, 300, 150, 170);
    QCOMPARE(pb.lowHz, qint64(14069850));
    QCOMPARE(pb.highHz, qint64(14070150));
}

void TestSpectrumGeometry::dataAPassbandIsUpperSideband() {
    for (Mode m : {Mode::Data, Mode::DataR}) {
        const Passband pb = passbandFor(m, DataSubMode::DataA, 14074000, 3000, 150, 170);
        QCOMPARE(pb.lowHz, qint64(14074000));
        QCOMPARE(pb.highHz, qint64(14077000));
    }
}

void TestSpectrumGeometry::amFmAndUnknownIgnoreIfShift() {
    for (Mode m : {Mode::Am, Mode::Fm, Mode::Unknown}) {
        const Passband pb = passbandFor(m, DataSubMode::DataA, 29000000, 6000, 150, 170);
        QCOMPARE(pb.lowHz, qint64(28997000));
        QCOMPARE(pb.highHz, qint64(29003000));
    }
}

void TestSpectrumGeometry::normalFrameIsUsable() {
    QCOMPARE(usableBinCount(16 + 1024, 16, 1024, 4096), 1024);
    QCOMPARE(usableBinCount(4096, 0, 4096, 4096), 4096);
}

void TestSpectrumGeometry::binCountWiderThanTheTextureIsRejected() {
    // A count above the texture width centred the bins at a negative offset and wrote outside the
    // waterfall and spectrum buffers.
    QCOMPARE(usableBinCount(16 + 5000, 16, 5000, 4096), 0);
    QCOMPARE(usableBinCount(16 + 4097, 16, 4097, 4096), 0);
}

void TestSpectrumGeometry::binsPastThePayloadAreRejected() {
    QCOMPARE(usableBinCount(1000, 16, 1024, 4096), 0);
    QCOMPARE(usableBinCount(1040, 17, 1024, 4096), 0);
}

void TestSpectrumGeometry::degenerateFramesAreRejected() {
    QCOMPARE(usableBinCount(1040, 16, 0, 4096), 0);
    QCOMPARE(usableBinCount(1040, 16, -5, 4096), 0);
    QCOMPARE(usableBinCount(1040, -1, 1024, 4096), 0);
}

void TestSpectrumGeometry::frequencyLabelsTakeOneEvenStep() {
    // The reported case: a 10 kHz span across ~1580 px with a 1 kHz grid and 4-decimal labels about
    // 70 px wide. Labels were dropped one by one where the wider "-0.00x0" collided, leaving
    // -0.004, -0.002, 0.000, 0.001, 0.002 ... One step now serves the whole scale.
    const int step = frequencyLabelStepHz(1000, 10000, 1580.0, 70.0, 12.0);
    QCOMPARE(step, 1000);
    // Narrow pane: the step widens to the next round multiple of the grid, still one step.
    QCOMPARE(frequencyLabelStepHz(1000, 10000, 600.0, 70.0, 12.0), 2000);
    QCOMPARE(frequencyLabelStepHz(1000, 10000, 300.0, 70.0, 12.0), 5000);
}

void TestSpectrumGeometry::frequencyLabelStepIsTheSmallestThatFits() {
    static constexpr int kGrids[] = {100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000};
    for (int grid : kGrids) {
        for (int span : {1000, 2000, 5000, 10000, 27000, 50000, 100000, 200000, 368000, 1000000}) {
            if (grid > span)
                continue;
            for (double width : {200.0, 400.0, 800.0, 1200.0, 1600.0, 2400.0}) {
                for (double widest : {30.0, 50.0, 70.0, 90.0}) {
                    const double needed = widest + 12.0;
                    const int step = frequencyLabelStepHz(grid, span, width, widest, 12.0);
                    QVERIFY2(step % grid == 0, "step must stay on the grid");
                    int m = step / grid;
                    while (m % 10 == 0)
                        m /= 10;
                    QVERIFY2(m == 1 || m == 2 || m == 5, "step must be grid x 1, 2 or 5 x 10^n");
                    const double pitch = static_cast<double>(step) * width / span;
                    if (step < span)
                        QVERIFY2(pitch >= needed, qPrintable(QString("grid %1 span %2 width %3: %4 px < %5")
                                                                 .arg(grid)
                                                                 .arg(span)
                                                                 .arg(width)
                                                                 .arg(pitch)
                                                                 .arg(needed)));
                    if (step > grid) {
                        // The next finer step on the 1-2-5 series would not have fitted.
                        const int finer = (m == 2 ? step / 2 : m == 5 ? step * 2 / 5 : step / 2);
                        QVERIFY(static_cast<double>(finer) * width / span < needed);
                    }
                }
            }
        }
    }
}

// The band noise sits at the bottom of the spectrum. The fill used to fade there to 10% alpha, which
// made the noise nearly invisible; below the trace it must be as opaque at the bottom as anywhere.
// Values are the panadapter's glow uniforms (width 0.04, intensity 0.8, cyan alpha 1.0).
void TestSpectrumGeometry::fillIsOpaqueDownToTheBottomEdge() {
    for (float peakY = 0.0f; peakY <= 0.98f; peakY += 0.02f) {
        for (float y = peakY + 0.01f; y <= 1.0f; y += 0.005f)
            QVERIFY2(fillAlphaFor(y, peakY, 0.04f, 0.8f, 1.0f) >= 0.9f,
                     qPrintable(QString("peak %1 y %2").arg(peakY).arg(y)));
    }
    // Well above the trace and its glow nothing is drawn.
    QCOMPARE(fillAlphaFor(0.1f, 0.9f, 0.04f, 0.8f, 1.0f), 0.0f);
}

// #AVG 1 means no averaging on the K4; QK4 must not smooth either, or the noise grass sits about one
// S-unit low (bench 2026-10-30). Higher levels smooth, more with each level, and stay in (0, 1].
void TestSpectrumGeometry::avgOneDrawsEveryFrameAsItArrives() {
    const EmaAlphas one = emaAlphasFor(1);
    QCOMPARE(one.attack, 1.0f);
    QCOMPARE(one.decay, 1.0f);
    QCOMPARE(emaAlphasFor(0).attack, 1.0f); // clamped up to 1
    float prevAttack = 1.0f, prevDecay = 1.0f;
    for (int level = 2; level <= 20; ++level) {
        const EmaAlphas a = emaAlphasFor(level);
        QVERIFY(a.attack > 0.0f && a.attack < prevAttack);
        QVERIFY(a.decay > 0.0f && a.decay < prevDecay);
        prevAttack = a.attack;
        prevDecay = a.decay;
    }
    QCOMPARE(emaAlphasFor(25).attack, emaAlphasFor(20).attack); // clamped down to 20
}

QTEST_APPLESS_MAIN(TestSpectrumGeometry)
#include "test_spectrumgeometry.moc"
