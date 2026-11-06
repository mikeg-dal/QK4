// SPDX-License-Identifier: GPL-3.0-or-later
//
// SerialLinePoller: modem-line change detection for a serial paddle, ported from K4-Companion's
// SerialInput.signal_loop (Dale Farnsworth, GPL-3.0; k4companion:18890-18970).

#include "hardware/seriallinepoller.h"

#include <QtTest/QtTest>
#include <map>

using Signal = PaddleKeyer::Signal;

namespace {

struct Lines {
    std::map<Signal, bool> state;
    SerialLinePoller::ReadLine reader() {
        return [this](Signal s) { return state[s]; };
    }
};

} // namespace

class TestSerialLinePoller : public QObject {
    Q_OBJECT

private slots:
    // The lines as found at open are rest, not edges: an adapter idling with a line asserted
    // must not look like a held paddle.
    void restStateAtOpenIsNotReported() {
        Lines l;
        l.state[Signal::CTS] = true;
        l.state[Signal::DCD] = true;
        SerialLinePoller p(Signal::CTS, Signal::DCD, false);
        QVERIFY(p.start(l.reader()));
        QVERIFY(p.poll(l.reader(), 1.000).empty());
        QVERIFY(p.poll(l.reader(), 1.001).empty());
    }

    // A paddle is reported on the poll that sees it change. The first report of a line carries
    // 0, each later one the time since that line's previous change.
    void paddleChangeReportedOnSamePollWithPerLineDelta() {
        Lines l;
        SerialLinePoller p(Signal::CTS, Signal::DCD, false);
        p.start(l.reader());

        l.state[Signal::CTS] = true;
        auto c = p.poll(l.reader(), 1.000);
        QCOMPARE(c.size(), size_t(1));
        QCOMPARE(c[0].signal, Signal::CTS);
        QCOMPARE(c[0].asserted, true);
        QCOMPARE(c[0].deltaTime, 0.0);

        l.state[Signal::DCD] = true;
        c = p.poll(l.reader(), 1.020);
        QCOMPARE(c.size(), size_t(1));
        QCOMPARE(c[0].signal, Signal::DCD);
        QCOMPARE(c[0].deltaTime, 0.0);

        l.state[Signal::CTS] = false;
        c = p.poll(l.reader(), 1.050);
        QCOMPARE(c.size(), size_t(1));
        QCOMPARE(c[0].asserted, false);
        QVERIFY(std::fabs(c[0].deltaTime - 0.050) < 1e-9);
    }

    // Both lines changing on one poll are both reported, dit first.
    void bothLinesOnOnePollDitFirst() {
        Lines l;
        SerialLinePoller p(Signal::DSR, Signal::DCD, false);
        p.start(l.reader());
        l.state[Signal::DSR] = true;
        l.state[Signal::DCD] = true;
        const auto c = p.poll(l.reader(), 2.0);
        QCOMPARE(c.size(), size_t(2));
        QCOMPARE(c[0].signal, Signal::DSR);
        QCOMPARE(c[1].signal, Signal::DCD);
    }

    // A straight key waits for 20 ms without a change, then reports the state it settled on.
    void straightKeyDebouncesTwentyMs() {
        Lines l;
        SerialLinePoller p(Signal::DSR, Signal::DCD, true);
        p.start(l.reader());
        l.state[Signal::DSR] = true;
        QVERIFY(p.poll(l.reader(), 2.000).empty());
        QVERIFY(p.poll(l.reader(), 2.010).empty());
        const auto c = p.poll(l.reader(), 2.025);
        QCOMPARE(c.size(), size_t(1));
        QCOMPARE(c[0].asserted, true);
    }

    // Contact bounce restarts the wait. Only the settled state is reported.
    void straightKeyBounceRestartsTheWait() {
        Lines l;
        SerialLinePoller p(Signal::DSR, Signal::DCD, true);
        p.start(l.reader());
        l.state[Signal::DSR] = true;
        QVERIFY(p.poll(l.reader(), 2.000).empty());
        l.state[Signal::DSR] = false;
        QVERIFY(p.poll(l.reader(), 2.005).empty());
        l.state[Signal::DSR] = true;
        QVERIFY(p.poll(l.reader(), 2.010).empty());
        QVERIFY(p.poll(l.reader(), 2.025).empty());
        const auto c = p.poll(l.reader(), 2.035);
        QCOMPARE(c.size(), size_t(1));
        QCOMPARE(c[0].asserted, true);
    }

    // With a straight key, the dah line is not watched at all.
    void straightKeyIgnoresDahLine() {
        Lines l;
        SerialLinePoller p(Signal::DSR, Signal::DCD, true);
        p.start(l.reader());
        l.state[Signal::DCD] = true;
        QVERIFY(p.poll(l.reader(), 3.0).empty());
        QVERIFY(p.poll(l.reader(), 3.1).empty());
    }
};

QTEST_GUILESS_MAIN(TestSerialLinePoller)
#include "test_seriallinepoller.moc"
