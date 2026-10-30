#include "dsp/spectrumgeometry.h"
#include <QtTest>

#include <cmath>
#include <cstdlib>

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

    void frequencyAndPositionRoundTrip();
    void miniPanCwFrameCentresThePassband();
    void miniPanDataFramePutsTheDialAtTheCentre();
    void miniPanPassbandIsPassbandForInEveryMode();
    void notchSitsOnTheDialsAudioSide();
    void oneDefaultCwPitch();

    void normalFrameIsUsable();
    void binCountWiderThanTheTextureIsRejected();
    void binsPastThePayloadAreRejected();
    void degenerateFramesAreRejected();

    void frequencyLabelsTakeOneEvenStep();
    void frequencyLabelStepIsTheSmallestThatFits();
    void fillIsOpaqueDownToTheBottomEdge();

    void miniPanOverlaysStayPutWhileTuning();
    void miniPanOverlaysLearnTheCwOffsetOnceSettled();
    void miniPanOverlaysUseTheFrameUntilAnOffsetIsKnown();
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

void TestSpectrumGeometry::frequencyAndPositionRoundTrip() {
    // Edges and centre of a 3 kHz mini-pan and a 50 kHz panadapter, then a sweep back and forth.
    QCOMPARE(normalizedXFor(9'999'000, 10'000'500, 3000), 0.0f);
    QCOMPARE(normalizedXFor(10'000'500, 10'000'500, 3000), 0.5f);
    QCOMPARE(normalizedXFor(10'002'000, 10'000'500, 3000), 1.0f);
    QVERIFY(normalizedXFor(9'998'000, 10'000'500, 3000) < 0.0f); // off-screen is not clamped
    for (int span : {3000, 12'000, 50'000}) {
        for (qint64 f = 14'000'000 - span / 2; f <= 14'000'000 + span / 2; f += span / 50) {
            const double x = normalizedXFor(f, 14'000'000, span);
            QVERIFY(std::abs(freqForNormalizedX(x, 14'000'000, span) - f) <= 1);
        }
    }
    QCOMPARE(normalizedXFor(14'000'000, 14'000'000, 0), 0.0f);
    QCOMPARE(freqForNormalizedX(0.3, 14'000'000, 0), qint64(14'000'000));
}

// The MiniPAN frame captured from a K4D in CW (protocol test): centre 10,000,500 = dial + pitch 500,
// span 3 kHz. With IS at the pitch the passband sits in the middle, 200 Hz = 1/15 of the width.
// The old fixed 2 kHz span drew it 1/10 wide.
void TestSpectrumGeometry::miniPanCwFrameCentresThePassband() {
    const qint64 centre = 10'000'500;
    const int span = 3000;
    const Passband pb = passbandFor(Mode::Cw, DataSubMode::DataA, 10'000'000, 200, 50, 170);
    QCOMPARE(normalizedXFor(pb.lowHz, centre, span), 1400.0f / 3000.0f);
    QCOMPARE(normalizedXFor(pb.highHz, centre, span), 1600.0f / 3000.0f);
    QCOMPARE(normalizedXFor(markerFreqHz(10'000'000, 50, Mode::Cw), centre, span), 0.5f);
    QCOMPARE(normalizedXFor(10'000'000, centre, span), 1.0f / 3.0f); // the dial, a pitch left of centre
}

// DATA-A frame from the same capture: centre = dial 10,131,000, span 12 kHz.
void TestSpectrumGeometry::miniPanDataFramePutsTheDialAtTheCentre() {
    const qint64 dial = 10'131'000;
    const int span = 12'000;
    QCOMPARE(normalizedXFor(markerFreqHz(dial, 150, Mode::Data), dial, span), 0.5f);
    const Passband pb = passbandFor(Mode::Data, DataSubMode::DataA, dial, 3200, 150, 170);
    QCOMPARE(normalizedXFor(pb.lowHz, dial, span), (6000.0f - 100.0f) / 12000.0f);
    QCOMPARE(normalizedXFor(pb.highHz, dial, span), (6000.0f + 3100.0f) / 12000.0f);
}

// The mini-pan places a passband by mapping passbandFor's edges onto the frame's axis, so in every
// mode its width is BW / span and its centre is passbandFor's centre: no mode has a rule of its own.
void TestSpectrumGeometry::miniPanPassbandIsPassbandForInEveryMode() {
    const qint64 dial = 7'030'000;
    const int span = 3000;
    const qint64 centre = dial + 500;
    for (Mode m : {Mode::Lsb, Mode::Usb, Mode::Cw, Mode::CwR, Mode::Data, Mode::DataR, Mode::Am, Mode::Fm}) {
        for (DataSubMode sub : {DataSubMode::DataA, DataSubMode::AfskA, DataSubMode::FskD, DataSubMode::PskD}) {
            const Passband pb = passbandFor(m, sub, dial, 400, 55, 170);
            const float left = normalizedXFor(pb.lowHz, centre, span);
            const float right = normalizedXFor(pb.highHz, centre, span);
            QVERIFY(std::abs((right - left) - 400.0f / span) < 1e-6f);
            const qint64 mid = freqForNormalizedX((left + right) / 2.0, centre, span);
            QVERIFY(std::abs(mid - (pb.lowHz + pb.highHz) / 2) <= 1);
        }
    }
}

void TestSpectrumGeometry::notchSitsOnTheDialsAudioSide() {
    for (Mode m : {Mode::Usb, Mode::Cw, Mode::Data, Mode::DataR, Mode::Am, Mode::Fm})
        QCOMPARE(notchFreqHz(m, 14'000'000, 800), qint64(14'000'800));
    for (Mode m : {Mode::Lsb, Mode::CwR})
        QCOMPARE(notchFreqHz(m, 14'000'000, 800), qint64(13'999'200));
}

void TestSpectrumGeometry::oneDefaultCwPitch() {
    QCOMPARE(DefaultCwPitchHz, 500); // the K4's default; the mini-pan once assumed 600
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

// Bench 2026-10-30: the mini-pan's markers slid while tuning, because they were placed on the last
// frame's centre and frames lag the dial. Locked to the dial, a marker at the dial stays at the same
// x for every dial value, whatever the lagging frames say.
void TestSpectrumGeometry::miniPanOverlaysStayPutWhileTuning() {
    DialLockedAxis axis;
    const int span = 3000;
    axis.dialChanged(0);
    axis.frameArrived(14'200'000, 14'200'000, 0); // first frame sets the offset (0)
    const float settledX = normalizedXFor(14'200'000, axis.centerFor(14'200'000, 14'200'000), span);
    qint64 dial = 14'200'000;
    for (int step = 1; step <= 50; ++step) {
        const qint64 now = step * 20; // knob turning every 20 ms
        dial += 10;
        axis.dialChanged(now);
        axis.frameArrived(dial - 200, dial, now); // the frame is still 20 steps behind
        QCOMPARE(normalizedXFor(dial, axis.centerFor(dial, dial - 200), span), settledX);
    }
}

// In CW the frame is centred off the dial (on the pitch side). The offset is learned from a frame
// that arrives after the dial has been still for SettleMs; frames during tuning do not move it.
void TestSpectrumGeometry::miniPanOverlaysLearnTheCwOffsetOnceSettled() {
    DialLockedAxis axis;
    axis.dialChanged(0);
    axis.frameArrived(7'030'500, 7'030'000, 0); // CW: frame 500 Hz above the dial
    QCOMPARE(axis.centerFor(7'030'000, 0), qint64(7'030'500));
    axis.dialChanged(1000);
    axis.frameArrived(7'029'000, 7'030'100, 1000 + DialLockedAxis::SettleMs - 1); // stale frame mid-tune
    QCOMPARE(axis.centerFor(7'030'100, 0), qint64(7'030'600));                    // offset unchanged
    axis.frameArrived(7'030'350, 7'030'100, 1000 + DialLockedAxis::SettleMs);     // settled; mode now DATA-ish
    QCOMPARE(axis.centerFor(7'030'100, 0), qint64(7'030'350));
}

void TestSpectrumGeometry::miniPanOverlaysUseTheFrameUntilAnOffsetIsKnown() {
    DialLockedAxis axis;
    QCOMPARE(axis.centerFor(14'000'000, 14'001'000), qint64(14'001'000));
    axis.frameArrived(14'001'000, 0, 0); // dial not reported yet: nothing learned
    QCOMPARE(axis.centerFor(14'000'000, 14'001'000), qint64(14'001'000));
    axis.frameArrived(14'001'000, 14'000'000, 0);
    axis.reset();
    QCOMPARE(axis.centerFor(14'000'000, 14'002'000), qint64(14'002'000));
}

QTEST_APPLESS_MAIN(TestSpectrumGeometry)
#include "test_spectrumgeometry.moc"
