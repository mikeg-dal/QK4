// SPDX-License-Identifier: GPL-3.0-or-later
//
// PaddleKeyer behaviour outside the recorded sweeps: MoMIDI time decoding, straight-key KZD/KZU,
// the keyer modes QK4 adds (Plain Iambic, Iambic A, Ultimatic), weighting, the radio monitor mute
// and every way keying ends. The port is of K4-Companion's Keyer (Dale Farnsworth, GPL-3.0);
// k4companion line numbers are cited where a case pins a specific rule.

#include "audio/sidetonescheduler.h"
#include "hardware/paddlekeyer.h"
#include "hardware/halikeysafety.h"
#include "paddlekeyersim.h"
#include "utils/rtsan.h"

#include <QtTest/QtTest>

using namespace PaddleSim;

namespace {

// A clock that only moves when told to; sleeps move it.
class ManualClock : public CwKeying::Clock {
public:
    double t = 10.0;
    double now() override { return t; }
    void sleep(double s) override {
        if (s > 0)
            t += s;
    }
};

class CaptureLink : public CwKeying::Link {
public:
    std::vector<std::string> sent;
    void send(std::string_view c) override {
        // WHY allowed: a test double recording what the keyer sends. The real link,
        // KzEgress::post, is checked on its own.
        RtSan::Allow allow;
        sent.emplace_back(c);
    }
};

// A straight-key keyer with no audio stream: sidetone requests are refused, as they are while no
// output device is open, so only the K4 commands are observed.
struct StraightRig {
    ManualClock clock;
    CaptureLink link;
    SidetoneScheduler sidetone{clock, []() -> std::optional<double> { return std::nullopt; }};
    PaddleKeyer keyer{clock, link, sidetone};

    StraightRig() {
        keyer.setStraightKeying(true);
        keyer.setDitNotes({20});
        keyer.setDitSignal(PaddleKeyer::Signal::DSR);
        keyer.setMuteRadioMonitor(false);
        keyer.setSpeed(30);
        // Switching to straight keying asks the keyer for a release, which it carries out on its
        // own thread; let it, so each case starts from a keyer with nothing pending.
        drain();
    }
    void drain() {
        while (keyer.hasPendingEvents())
            keyer.processNextEvent();
    }
};

std::vector<std::string> elements(const Result &r, int wpm) {
    return elementsFrom(r.sent, wpm);
}

int countOf(const std::vector<std::string> &v, const std::string &x) {
    return static_cast<int>(std::count(v.begin(), v.end(), x));
}

Result run(Config c, int tapMs) {
    Run r(c, tapMs / 1000.0);
    return r.go();
}

} // namespace

class TestPaddleKeyer : public QObject {
    Q_OBJECT

private slots:
    void midiArmingGestureNeverTransmits() {
        StraightRig rig;
        HaliKeySafety guard;
        auto feed = [&](int status) {
            if (guard.midiMessage(status, 20))
                rig.keyer.midiMessage(status, 20, 127, 0.05);
            rig.drain();
        };
        feed(0x90); // held at connection
        guard.enable(true);
        feed(0x80); // release arms, without sending a key-up or key-down
        QVERIFY(guard.armed());
        QVERIFY(rig.link.sent.empty());
        feed(0x90);
        rig.clock.t += 0.05;
        feed(0x80);
        QCOMPARE(rig.link.sent.size(), std::size_t{2});
        QVERIFY(rig.link.sent[0].compare(0, 3, "KZD") == 0);
        QVERIFY(rig.link.sent[1].compare(0, 3, "KZU") == 0);
    }

    // ---- MoMIDI timing (k4companion:19924-20008) ----

    // A note's velocity carries the milliseconds since the device's previous event; poly
    // aftertouch carries whole 126 ms steps. With a straight key those deltas are the KZD/KZU
    // intervals directly.
    void midiVelocityAndAftertouchCarryDeviceTime() {
        StraightRig rig;
        rig.keyer.midiMessage(0x90, 20, 10, 0); // down, 10 ms after the previous event
        rig.drain();
        rig.keyer.midiMessage(0xa0, 20, 1, 0); // 1 x 126 ms ...
        rig.keyer.midiMessage(0x80, 20, 4, 0); // ... + 4 ms = 130 ms down
        rig.drain();
        QCOMPARE(rig.link.sent, (std::vector<std::string>{"KZD0010;", "KZU0130;"}));
    }

    // Velocity 0 or 127 means "no time in this message": use RtMidi's own delta instead. Note-on
    // with velocity 0 is still a key-down on these devices; only 0x80 releases.
    void midiVelocityZeroUsesHostDeltaAndIsStillKeyDown() {
        StraightRig rig;
        rig.keyer.midiMessage(0x90, 20, 0, 0.250);
        rig.drain();
        rig.keyer.midiMessage(0x80, 20, 127, 0.050);
        rig.drain();
        QCOMPARE(rig.link.sent, (std::vector<std::string>{"KZD0250;", "KZU0050;"}));
    }

    // An up for a paddle that was never down is not forwarded.
    void midiUpWithoutDownIsDropped() {
        StraightRig rig;
        rig.keyer.midiMessage(0x80, 20, 5, 0);
        QVERIFY(!rig.keyer.hasPendingEvents());
        QVERIFY(rig.link.sent.empty());
    }

    // Notes that aren't mapped to a paddle are ignored.
    void midiUnmappedNoteIgnored() {
        StraightRig rig;
        rig.keyer.midiMessage(0x90, 31, 10, 0);
        QVERIFY(!rig.keyer.hasPendingEvents());
    }

    // ---- straight key (k4companion:20189-20236) ----

    // A gap longer than 5 s is no gap at all: KZD0000.
    void straightKeyLongGapSendsZero() {
        StraightRig rig;
        rig.keyer.midiMessage(0x90, 20, 0, 6.0);
        rig.drain();
        QCOMPARE(rig.link.sent.front(), std::string("KZD0000;"));
    }

    // No interval is shorter than the sidetone's shortest tone.
    void straightKeyRespectsMinimumTone() {
        StraightRig rig;
        rig.sidetone.setMinimumSidetoneMs(4);
        rig.keyer.midiMessage(0x90, 20, 2, 0);
        rig.drain();
        QCOMPARE(rig.link.sent.front(), std::string("KZD0004;"));
    }

    // A second release cannot send a second KZU.
    void straightKeyDuplicateReleaseSendsOneKzu() {
        StraightRig rig;
        rig.keyer.serialSignalChanged(PaddleKeyer::Signal::DSR, true, 0);
        rig.keyer.serialSignalChanged(PaddleKeyer::Signal::DSR, false, 0.100);
        rig.keyer.serialSignalChanged(PaddleKeyer::Signal::DSR, false, 0.050);
        rig.drain();
        QCOMPARE(rig.link.sent, (std::vector<std::string>{"KZD0000;", "KZU0100;"}));
    }

    // Releasing (Esc, disconnect, device lost) while a straight key is down closes the KZD with
    // the time to the release, so the K4 is never left keyed. The KZU is the keyer thread's to
    // send: the release only asks for it, and the keyer's delay in getting to it adds nothing.
    void releaseWhileStraightKeyDownSendsKzu() {
        StraightRig rig;
        rig.keyer.serialSignalChanged(PaddleKeyer::Signal::DSR, true, 0);
        rig.drain();
        rig.clock.t += 0.250;
        rig.keyer.releasePaddles();
        QCOMPARE(rig.link.sent, (std::vector<std::string>{"KZD0000;"}));
        rig.clock.t += 0.100;
        rig.drain();
        QCOMPARE(rig.link.sent, (std::vector<std::string>{"KZD0000;", "KZU0250;"}));
    }

    // A release that lands after the key-down is queued but before the keyer has sent its KZD: the
    // KZU follows the KZD, never precedes it, and the radio is not left keyed.
    void releaseBeforeKzdIsSentFollowsIt() {
        StraightRig rig;
        rig.keyer.serialSignalChanged(PaddleKeyer::Signal::DSR, true, 0);
        rig.clock.t += 0.040;
        rig.keyer.releasePaddles();
        QVERIFY(rig.link.sent.empty());
        rig.drain();
        QCOMPARE(rig.link.sent, (std::vector<std::string>{"KZD0000;", "KZU0040;"}));
    }

    // The same with an inhibit, where the key's own DitUp is dropped at the door: the keyer's
    // release is the only thing that can unkey the radio.
    void inhibitBeforeKzdIsSentFollowsIt() {
        StraightRig rig;
        rig.keyer.serialSignalChanged(PaddleKeyer::Signal::DSR, true, 0);
        rig.clock.t += 0.040;
        rig.keyer.setInhibited(true);
        rig.keyer.serialSignalChanged(PaddleKeyer::Signal::DSR, false, 0.050);
        rig.drain();
        QCOMPARE(rig.link.sent, (std::vector<std::string>{"KZD0000;", "KZU0040;"}));
    }

    // The dah line is not a key in straight mode.
    void straightKeyIgnoresDahLine() {
        StraightRig rig;
        rig.keyer.setDahSignal(PaddleKeyer::Signal::DCD);
        rig.keyer.serialSignalChanged(PaddleKeyer::Signal::DCD, true, 0);
        rig.drain();
        QVERIFY(rig.link.sent.empty());
    }

    // ---- close ----

    // After close() the keyer loop sees Close and nothing later keys the radio.
    void closeRefusesLaterEvents() {
        StraightRig rig;
        rig.keyer.close();
        QCOMPARE(rig.keyer.processNextEvent(), PaddleKeyer::Event::Close);
        rig.keyer.serialSignalChanged(PaddleKeyer::Signal::DSR, true, 0);
        QVERIFY(!rig.keyer.hasPendingEvents());
        QVERIFY(rig.link.sent.empty());
    }

    // ---- invariants over every scenario ----

    // Every KZD is closed by a KZU, in order, in every run of every scenario at both speeds. A
    // KZD left open is a transmitter left keyed.
    void everyKzdIsClosed() {
        for (int wpm : {30, 40}) {
            for (Scenario s : {Scenario::Squeeze, Scenario::Swap, Scenario::Tap, Scenario::DitTap, Scenario::DahFirst,
                               Scenario::Stall}) {
                Config c;
                c.scenario = s;
                c.wpm = wpm;
                for (int tapMs : Geometry(c).taps()) {
                    int open = 0;
                    for (const auto &cmd : run(c, tapMs).sent) {
                        if (cmd.rfind("KZD", 0) == 0)
                            QVERIFY(++open == 1);
                        else if (cmd.rfind("KZU", 0) == 0)
                            QVERIFY(--open == 0);
                    }
                    QCOMPARE(open, 0);
                }
            }
        }
    }

    // releasePaddles mid-squeeze (Esc, disconnect, mode change): the radio is left unkeyed, and the
    // keying is exactly what letting go of both paddles at that instant would have sent.
    void releaseMidSqueezeEndsCleanly() {
        Config held;
        held.scenario = Scenario::Squeeze;
        for (int releaseMs : {50, 150, 250, 350}) {
            Config c = held;
            c.releaseAt = releaseMs / 1000.0;
            const Result r = run(c, 500);
            int open = 0;
            for (const auto &cmd : r.sent)
                open += cmd.rfind("KZD", 0) == 0 ? 1 : cmd.rfind("KZU", 0) == 0 ? -1 : 0;
            QCOMPARE(open, 0);
            // Released at the same instant as a physical release, the same keying results.
            QCOMPARE(elements(r, 30), elements(run(held, releaseMs), 30));
        }
    }

    // ---- modes ----

    // Ultimatic: with both paddles held, the one closed last wins and repeats. The dah closes
    // first and is heard; the dit closes at 40 ms and takes over (k4companion:20407-20440).
    void ultimaticLastClosedWins() {
        Config c;
        c.scenario = Scenario::DahFirst;
        c.mode = PaddleKeyer::Mode::Ultimatic;
        const auto e = elements(run(c, 40), 30);
        QVERIFY(e.size() >= 3);
        QCOMPARE(e.front(), std::string("dah"));
        for (size_t i = 1; i < e.size(); ++i)
            QCOMPARE(e[i], std::string("dit"));
    }

    // Ultimatic has no memories. Both paddles closed together: neither closed last, so it keys
    // dits for as long as they're held, and a release owes nothing. It sends no more dits than
    // there are 80 ms dit periods (30 WPM) begun before the release.
    void ultimaticSqueezeReleaseAddsNothing() {
        Config u;
        u.scenario = Scenario::Squeeze;
        u.mode = PaddleKeyer::Mode::Ultimatic;
        for (int tapMs : Geometry(u).taps()) {
            const auto e = elements(run(u, tapMs), 30);
            QVERIFY(!e.empty());
            QCOMPARE(countOf(e, "dah"), 0);
            QVERIFY2(static_cast<int>(e.size()) <= tapMs / 80 + 1,
                     qPrintable(QStringLiteral("released at %1 ms, %2 dits").arg(tapMs).arg(e.size())));
        }
    }

    // Plain Iambic has no memories: a 5 ms dah tap that falls between readings is lost, where
    // Iambic B remembers it. It never sends more than B does.
    void plainIambicHasNoMemory() {
        Config b;
        b.scenario = Scenario::Tap;
        b.wpm = 40;
        Config p = b;
        p.mode = PaddleKeyer::Mode::PlainIambic;
        int dropped = 0;
        for (int tapMs : Geometry(b).taps()) {
            const int dahsB = countOf(elements(run(b, tapMs), 40), "dah");
            const int dahsP = countOf(elements(run(p, tapMs), 40), "dah");
            QCOMPARE(dahsB, 1);
            QVERIFY(dahsP <= dahsB);
            dropped += dahsP == 0;
        }
        QVERIFY(dropped > 0);
    }

    // Iambic A keeps the dit memory (a dit closed after a dah was heard is sent) but has no dah
    // memory and no squeeze-release memory (k4companion:20341-20375).
    void iambicAKeepsDitMemoryOnly() {
        Config b;
        b.scenario = Scenario::DitTap;
        Config a = b;
        a.mode = PaddleKeyer::Mode::IambicA;
        for (int tapMs : Geometry(b).taps())
            QCOMPARE(countOf(elements(run(a, tapMs), 30), "dit"), 1);

        Config bt;
        bt.scenario = Scenario::Tap;
        bt.wpm = 40;
        Config at = bt;
        at.mode = PaddleKeyer::Mode::IambicA;
        int dropped = 0;
        for (int tapMs : Geometry(bt).taps()) {
            const int dahsA = countOf(elements(run(at, tapMs), 40), "dah");
            QVERIFY(dahsA <= 1);
            dropped += dahsA == 0;
        }
        QVERIFY(dropped > 0);

        Config bs;
        bs.scenario = Scenario::Squeeze;
        Config as = bs;
        as.mode = PaddleKeyer::Mode::IambicA;
        int shorter = 0;
        for (int tapMs : Geometry(bs).taps()) {
            const auto ea = elements(run(as, tapMs), 30);
            const auto eb = elements(run(bs, tapMs), 30);
            QVERIFY(ea.size() <= eb.size());
            shorter += ea.size() < eb.size();
        }
        QVERIFY(shorter > 0);
    }

    // ---- weighting (k4companion:21312-21326) ----

    // Weight 1.25 at 30 WPM: d = (1 - w) / (1 + w); dit 40 x (1 - d) = 44.4 ms, rounded up to
    // 45; dah 134; the gap between elements 40 x (1 + d) = 35.6, sent as 36.
    void weightingLengthensElementsAndShortensGaps() {
        Config c;
        c.scenario = Scenario::Squeeze;
        c.weighting = 1.25;
        const Result r = run(c, 400);
        bool first = true;
        for (const auto &cmd : r.sent) {
            if (cmd.rfind("KZU", 0) == 0)
                QVERIFY2(cmd == "KZU0045;" || cmd == "KZU0134;", cmd.c_str());
            if (cmd.rfind("KZD", 0) == 0) {
                if (!first)
                    QCOMPARE(cmd, std::string("KZD0036;"));
                first = false;
            }
        }
    }

    // ---- radio monitor mute (k4companion:21331-21352) ----

    // With the mute on, each burst sends ML0000 straight after its first KZD, then restores the
    // saved level once the K4 has finished: after KZL, the sidetone delay, the last element and
    // half a second.
    void monitorMuteRestoresSavedLevelAfterBurst() {
        Config c;
        c.scenario = Scenario::Squeeze;
        c.monitorLevel = 10;
        c.keyDownInitialDelayMs = 100;
        Run r(c, .200);
        const Result res = r.go();
        QVERIFY(res.sent.size() >= 2);
        QCOMPARE(res.sent[0].substr(0, 3), std::string("KZD"));
        QCOMPARE(res.sent[1], std::string("ML0000;"));
        QCOMPARE(countOf(res.sent, "ML0000;"), 1);
        // The burst cannot end before the release at .200, and KZL alone is .100 of the delay.
        QVERIFY(r.monitorRestoreDue().has_value());
        QVERIFY(*r.monitorRestoreDue() > Run::kT0 + 0.200 + 0.100 + 0.5);
        QCOMPARE(r.fireUnmute(), (std::vector<std::string>{"ML0010;"}));
        QVERIFY(!r.monitorRestoreDue().has_value());
        QVERIFY(r.fireUnmute().empty());
    }

    // Keying again before the restore is due voids it; the next key-up arms a new one, and the
    // keyer sends it once the clock reaches it, without being woken by anything else.
    void monitorRestoreVoidedByKeyingAgain() {
        StraightRig rig;
        rig.keyer.setMuteRadioMonitor(true);
        rig.keyer.setMonitorLevel(7);
        rig.keyer.paddleEvent(PaddleKeyer::Event::DitDown, 0.100);
        rig.drain();
        rig.keyer.paddleEvent(PaddleKeyer::Event::DitUp, 0.050);
        rig.drain();
        QVERIFY(rig.keyer.monitorRestoreDue().has_value());
        rig.keyer.paddleEvent(PaddleKeyer::Event::DitDown, 0.100);
        rig.drain();
        QVERIFY(!rig.keyer.monitorRestoreDue().has_value());
        rig.keyer.paddleEvent(PaddleKeyer::Event::DitUp, 0.050);
        rig.drain();
        const auto due = rig.keyer.monitorRestoreDue();
        QVERIFY(due.has_value());
        QCOMPARE(countOf(rig.link.sent, "ML0000;"), 1);
        QCOMPARE(countOf(rig.link.sent, "ML0007;"), 0);
        rig.clock.t = *due;
        QCOMPARE(rig.keyer.processNextEvent(), PaddleKeyer::Event::RestoreMonitor);
        QCOMPARE(rig.link.sent.back(), std::string("ML0007;"));
        QVERIFY(!rig.keyer.monitorRestoreDue().has_value());
    }

    // With the mute off, no restore is ever armed.
    void monitorMuteOffArmsNoRestore() {
        Config c;
        c.scenario = Scenario::Squeeze;
        c.muteRadioMonitor = false;
        Run r(c, .200);
        r.go();
        QVERIFY(!r.monitorRestoreDue().has_value());
    }
};

QTEST_GUILESS_MAIN(TestPaddleKeyer)
#include "test_paddlekeyer.moc"
