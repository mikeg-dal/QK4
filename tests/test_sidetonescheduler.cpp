// SPDX-License-Identifier: GPL-3.0-or-later
//
// SidetoneScheduler: where a sidetone edge lands in the output block, and how the keying-to-
// hearing delay is held. Ported from K4-Companion's Audio class (Dale Farnsworth, GPL-3.0;
// k4companion:9122-9440, 10473-10727).
//
// Every case uses a stream clock 1000 s ahead of the keyer clock, 48 kHz, 4 ms (192-frame)
// blocks filled 10 ms ahead of their DAC time. The fixed first-edge delay is then lead + block:
// 14 ms.

#include "audio/sidetonescheduler.h"

#include <QtTest/QtTest>
#include <functional>

namespace {

constexpr int kRate = 48000;
constexpr int kFrames = 192;
constexpr double kBlock = 0.004;
constexpr double kLead = 0.010;

bool near(double a, double b) {
    return std::fabs(a - b) < 1e-9;
}

class HookClock : public CwKeying::Clock {
public:
    double t = 10.0;
    std::function<void()> onSleep;
    double now() override { return t; }
    void sleep(double s) override {
        t += s;
        if (onSleep)
            onSleep();
    }
};

struct Rig {
    HookClock clock;
    SidetoneScheduler s{clock, [this]() -> std::optional<double> { return clock.t + 1000.0; }};
    // An output block whose first frame plays at dac (stream clock).
    std::vector<SidetoneScheduler::Edge> block(double dac) {
        s.noteBlockTiming(dac, dac - kLead, kBlock);
        std::vector<SidetoneScheduler::Edge> out;
        int start = 0;
        while (auto e = s.nextKeyEdge(start, kFrames, dac, kRate)) {
            out.push_back(*e);
            start = e->offset;
        }
        return out;
    }
};

} // namespace

class TestSidetoneScheduler : public QObject {
    Q_OBJECT

private slots:
    void refusedWithNoStream() {
        HookClock c;
        SidetoneScheduler none(c, []() -> std::optional<double> { return std::nullopt; });
        QVERIFY(!none.scheduleDown(10.0));
    }

    // The first edge of a stream is always late, since the keyer asks for "now". Rather than
    // ratchet, it fixes the delay at lead + block, and every first edge sounds the same 14 ms
    // after it was keyed. (K4-Companion: "14.00 every time" against 10.00-13.75.)
    void firstEdgeFixesDelayAtLeadPlusBlock() {
        Rig r;
        QVERIFY(r.s.scheduleDown(10.0));
        QVERIFY(r.block(1010.010).empty()); // fixed delay puts it at frame 192: next block
        QVERIFY(r.s.pendingEdgeHeld());
        const auto e = r.block(1010.014);
        QCOMPARE(e.size(), size_t(1));
        QCOMPARE(e[0].offset, 0);
        QVERIFY(e[0].down);
        QVERIFY(near(r.s.lag(), kLead + kBlock));
        QVERIFY(near(r.s.delay(), 0.014));
        QVERIFY(r.s.downHeardAt().has_value());
        QVERIFY(near(*r.s.downHeardAt(), 10.014));
    }

    // An edge the keyer itself was late with is placed at once, without raising the delay.
    void keyerLateDownIsDraggedNotRatcheted() {
        Rig r;
        r.s.setLag(0.014);
        QVERIFY(r.s.scheduleDown(9.990, /*late=*/true));
        const auto e = r.block(1010.010);
        QCOMPARE(e.size(), size_t(1));
        QCOMPARE(e[0].offset, 0);
        QVERIFY(near(r.s.lag(), 0.014));
    }

    // When the host skipped blocks, the lateness is the host's, not the audio's: dragged too.
    void skippedBlocksDragNotRatchet() {
        Rig r;
        r.s.setLag(0.014);
        r.s.noteBlockTiming(1010.000, 1010.000 - kLead, kBlock);
        QVERIFY(r.s.scheduleDown(9.990));
        const auto e = r.block(1010.010); // 10 ms after the previous block: > 1.5 blocks
        QCOMPARE(e.size(), size_t(1));
        QVERIFY(near(r.s.lag(), 0.014));
    }

    // Any other late key-down means the audio needs more delay than it had: the shortfall is added
    // to the delay, so later edges land on time.
    void otherLateDownRaisesTheDelay() {
        Rig r;
        r.s.setLag(0.014);
        QVERIFY(r.s.scheduleDown(9.990));
        const auto e = r.block(1010.010);
        QCOMPARE(e.size(), size_t(1));
        QCOMPARE(e[0].offset, 0);
        QVERIFY(near(r.s.lag(), 0.020)); // 6 ms short
    }

    // A late key-up is placed at once and never raises the delay.
    void lateUpIsDraggedOnly() {
        Rig r;
        r.s.setLag(0.014);
        QVERIFY(r.s.scheduleDown(9.996));
        r.s.scheduleUp(9.990);
        const auto e = r.block(1010.010);
        QCOMPARE(e.size(), size_t(2));
        QVERIFY(e[0].down);
        QVERIFY(!e[1].down);
        QVERIFY(near(r.s.lag(), 0.014));
    }

    // An edge lands on the exact frame its time falls on: 2 ms into a 48 kHz block is frame 96.
    void edgeLandsOnItsExactFrame() {
        Rig r;
        r.s.setLag(0.014);
        QVERIFY(r.s.scheduleDown(9.998));
        const auto e = r.block(1010.010);
        QCOMPARE(e.size(), size_t(1));
        QCOMPARE(e[0].offset, 96);
    }

    // A key-down retracted before the callback reached it is dropped, and the keyer is told.
    void cancelBeforePlacementDrops() {
        Rig r;
        r.s.setLag(0.014);
        r.s.noteBlockTiming(1010.000, 1010.000 - kLead, kBlock);
        QVERIFY(r.s.scheduleDown(10.000));
        double dac = 1010.000;
        r.clock.onSleep = [&] {
            dac += kBlock;
            r.block(dac);
        };
        QVERIFY(r.s.cancelDown());
        r.clock.onSleep = nullptr;
        QVERIFY(r.block(dac + kBlock).empty());
        QVERIFY(!r.s.downHeardAt().has_value());
    }

    // Too late to retract: the edge was placed, and the keyer must send the element whole.
    void cancelAfterPlacementFails() {
        Rig r;
        r.s.setLag(0.014);
        QVERIFY(r.s.scheduleDown(9.996));
        QCOMPARE(r.block(1010.010).size(), size_t(1));
        QVERIFY(!r.s.cancelDown());
    }

    // While idle the delay eases back 1 ms per second, never below lead + block.
    void delayEasesInSilence() {
        Rig r;
        r.s.noteBlockTiming(1010.000, 1010.000 - kLead, kBlock);
        r.s.setLag(0.030);
        r.s.easeLag(1.0);
        QVERIFY(near(r.s.lag(), 0.029));
        r.s.easeLag(100.0);
        QVERIFY(near(r.s.lag(), kLead + kBlock));
        QVERIFY(r.s.delayFloor().has_value());
        QVERIFY(near(*r.s.delayFloor(), kLead + kBlock));
    }

    // The latest the keyer can queue an edge and still make the block it falls in: that block's
    // fill time, on the keyer clock. Heard at 10.020 falls in the block playing at 1010.018, filled
    // at 1010.008, which is 10.008 on the keyer clock.
    void edgeDeadlineIsTheFillTimeOfItsBlock() {
        Rig r;
        QVERIFY(r.s.scheduleDown(10.0));
        r.s.noteBlockTiming(1010.010, 1010.010 - kLead, kBlock);
        const auto d = r.s.edgeDeadline(10.020);
        QVERIFY(d.has_value());
        QVERIFY(near(*d, 10.008));
    }
};

QTEST_GUILESS_MAIN(TestSidetoneScheduler)
#include "test_sidetonescheduler.moc"
