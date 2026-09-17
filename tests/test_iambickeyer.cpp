// Behavioral tests for IambicKeyer — the CW keyer state machine that turns paddle
// dit/dah edges into timed elements (elementStarted / characterSpace / keyingFinished).
//
// These are integration-style timing tests: the keyer is driven through its public
// paddle API and observed via signals, with the QtTest event loop pumping the internal
// QTimer (element clock) and the queued handlePaddleChange() posts. A deliberately slow
// keyer speed (30 WPM → 40 ms dit unit) keeps element windows comfortably larger than
// qWait() jitter and the 8 ms bounce-hold gate.
//
// The headline coverage is the Iambic A vs B distinction, which is ONLY observable on a
// squeeze release: A completes the in-progress element and stops; B appends exactly one
// additional opposite element (the "B insert" / squeeze memory). See the trace recipe in
// docs/halikey-cw-trace.md and the branch at iambickeyer.cpp (squeeze-release A→idle vs
// B→opposite element).

#include "hardware/iambickeyer.h"
#include <QSignalSpy>
#include <QtTest/QtTest>

class TestIambicKeyer : public QObject {
    Q_OBJECT

private:
    // 30 WPM → ditMs = 40. dit element = 80 ms, dah element = 160 ms. Single taps below
    // release at ~25 ms: past the 8 ms hold gate, well inside the element window.
    static constexpr int kWpm = 30;

private slots:
    // A disabled keyer (default state, before radioReady) must ignore paddles entirely.
    void disabledKeyerIgnoresPaddles() {
        IambicKeyer keyer;
        keyer.setSpeed(kWpm); // not enabled
        QSignalSpy elem(&keyer, &IambicKeyer::elementStarted);
        keyer.setDitPaddle(true);
        QTest::qWait(120);
        keyer.setDitPaddle(false);
        QTest::qWait(120);
        QCOMPARE(elem.count(), 0);
    }

    // A single dit tap produces exactly one dit element, then the keyer idles.
    void singleDitTapProducesOneDit() {
        IambicKeyer keyer;
        keyer.setEnabled(true);
        keyer.setSpeed(kWpm);
        keyer.setMode(IambicKeyer::IambicA);
        QSignalSpy elem(&keyer, &IambicKeyer::elementStarted);
        QSignalSpy finished(&keyer, &IambicKeyer::keyingFinished);

        keyer.setDitPaddle(true);
        QTest::qWait(25); // > 8 ms hold gate, << 80 ms dit element
        keyer.setDitPaddle(false);

        QVERIFY(finished.wait(2000));
        QCOMPARE(elem.count(), 1);
        QCOMPARE(elem.at(0).at(0).toBool(), true); // dit
    }

    // A single dah tap produces exactly one dah element, then idles.
    void singleDahTapProducesOneDah() {
        IambicKeyer keyer;
        keyer.setEnabled(true);
        keyer.setSpeed(kWpm);
        keyer.setMode(IambicKeyer::IambicA);
        QSignalSpy elem(&keyer, &IambicKeyer::elementStarted);
        QSignalSpy finished(&keyer, &IambicKeyer::keyingFinished);

        keyer.setDahPaddle(true);
        QTest::qWait(25); // < 160 ms dah element
        keyer.setDahPaddle(false);

        QVERIFY(finished.wait(2000));
        QCOMPARE(elem.count(), 1);
        QCOMPARE(elem.at(0).at(0).toBool(), false); // dah
    }

    // With paddles reversed, a press on the DIT line must play a DAH (and vice-versa).
    void reversedSwapsDitAndDah() {
        IambicKeyer keyer;
        keyer.setEnabled(true);
        keyer.setSpeed(kWpm);
        keyer.setMode(IambicKeyer::IambicA);
        keyer.setReversed(true);
        QSignalSpy elem(&keyer, &IambicKeyer::elementStarted);
        QSignalSpy finished(&keyer, &IambicKeyer::keyingFinished);

        keyer.setDitPaddle(true); // physical dit line
        QTest::qWait(25);
        keyer.setDitPaddle(false);

        QVERIFY(finished.wait(2000));
        QCOMPARE(elem.count(), 1);
        QCOMPARE(elem.at(0).at(0).toBool(), false); // reversed: dit line → dah element
    }

    // While both paddles are squeezed, the keyer alternates dit/dah continuously.
    void squeezeAlternatesElements() {
        IambicKeyer keyer;
        keyer.setEnabled(true);
        keyer.setSpeed(kWpm);
        keyer.setMode(IambicKeyer::IambicA);
        QSignalSpy elem(&keyer, &IambicKeyer::elementStarted);

        keyer.setDitPaddle(true);
        keyer.setDahPaddle(true);
        QTest::qWait(300); // several element periods
        keyer.setDitPaddle(false);
        keyer.setDahPaddle(false);

        // At least a couple of elements, and they alternate dit/dah/dit...
        QVERIFY(elem.count() >= 3);
        bool first = elem.at(0).at(0).toBool();
        for (int i = 0; i < elem.count(); ++i)
            QCOMPARE(elem.at(i).at(0).toBool(), (i % 2 == 0) ? first : !first);
    }

    // Hold gate ON (default / V1.4 serial): a press+release delivered back-to-back with no
    // event-loop turn between them measures a sub-8ms hold — treated as contact bounce, the
    // latch is cleared, and no element plays. Pins the V1.4 bounce filter against regression.
    void holdGateOn_burstTapIsFiltered() {
        IambicKeyer keyer;
        keyer.setEnabled(true);
        keyer.setSpeed(kWpm);
        keyer.setMode(IambicKeyer::IambicA);
        QSignalSpy elem(&keyer, &IambicKeyer::elementStarted);

        // No qWait between the calls: both land before the queued handlePaddleChange runs,
        // simulating a WinMM-style burst or a real serial bounce.
        keyer.setDitPaddle(true);
        keyer.setDitPaddle(false);

        QTest::qWait(150);
        QCOMPARE(elem.count(), 0);
    }

    // Hold gate OFF (MIDI transport): the same burst keeps its latch — the press is
    // firmware-debounced and real — so exactly one dit element plays, then the keyer idles.
    void holdGateOff_burstTapProducesOneDit() {
        IambicKeyer keyer;
        keyer.setEnabled(true);
        keyer.setSpeed(kWpm);
        keyer.setMode(IambicKeyer::IambicA);
        keyer.setHoldGateEnabled(false);
        QSignalSpy elem(&keyer, &IambicKeyer::elementStarted);
        QSignalSpy finished(&keyer, &IambicKeyer::keyingFinished);

        keyer.setDitPaddle(true);
        keyer.setDitPaddle(false);

        QVERIFY(finished.wait(2000));
        QCOMPARE(elem.count(), 1);
        QCOMPARE(elem.at(0).at(0).toBool(), true); // dit
    }

    // Iambic A: releasing a squeeze appends NO further element — the in-progress element
    // finishes and the keyer idles.
    void iambicA_squeezeReleaseAppendsNoElement() {
        int delta = squeezeReleaseExtraElements(IambicKeyer::IambicA);
        QCOMPARE(delta, 0);
    }

    // Iambic B: releasing a squeeze appends exactly one opposite element (the B insert),
    // then idles.
    void iambicB_squeezeReleaseAppendsOneElement() {
        int delta = squeezeReleaseExtraElements(IambicKeyer::IambicB);
        QCOMPARE(delta, 1);
    }

    // setPaddleState is the entry point the transports use, since a HaliKey sample yields both
    // levers at once. It must produce exactly the behaviour the two single-lever calls do.
    void setPaddleStateMatchesTwoCallSqueeze() {
        QCOMPARE(squeezeReleaseExtraElements(IambicKeyer::IambicA, /*usePair=*/true), 0);
        QCOMPARE(squeezeReleaseExtraElements(IambicKeyer::IambicB, /*usePair=*/true), 1);
    }

    // Releasing ONE lever of a squeeze must not stop keying — the held lever keeps sending. This
    // is the case a released squeeze can be mistaken for, so it has to stay distinct: pairing the
    // levers must not turn a partial release into a full one.
    void partialReleaseKeepsSendingHeldLever() {
        IambicKeyer keyer;
        keyer.setEnabled(true);
        keyer.setSpeed(kWpm);
        keyer.setMode(IambicKeyer::IambicA);
        QSignalSpy elem(&keyer, &IambicKeyer::elementStarted);
        QSignalSpy finished(&keyer, &IambicKeyer::keyingFinished);

        keyer.setPaddleState(true, true);
        QTest::qWait(300);

        // Drop dit, keep dah held: the keyer must carry on, and every further element is a dah.
        const int afterSqueeze = elem.count();
        keyer.setPaddleState(false, true);
        QTest::qWait(400);
        const int duringHold = elem.count();
        QVERIFY2(duringHold > afterSqueeze, "held dah lever stopped producing elements");
        for (int i = afterSqueeze; i < duringHold; ++i)
            QCOMPARE(elem.at(i).at(0).toBool(), false); // false == dah

        // Now release the held lever: the keyer idles.
        keyer.setPaddleState(false, false);
        if (finished.count() == 0)
            finished.wait(2000);
        QVERIFY(finished.count() > 0);
    }

    // A lever released and re-pressed with no overlap is two separate taps, never a squeeze —
    // so Iambic B must not append its trailing element.
    void sequentialTapsAreNotASqueeze() {
        IambicKeyer keyer;
        keyer.setEnabled(true);
        keyer.setSpeed(kWpm);
        keyer.setMode(IambicKeyer::IambicB);
        QSignalSpy elem(&keyer, &IambicKeyer::elementStarted);
        QSignalSpy finished(&keyer, &IambicKeyer::keyingFinished);

        keyer.setPaddleState(true, false); // dit only
        QTest::qWait(25);
        keyer.setPaddleState(false, false);
        if (finished.count() == 0)
            finished.wait(2000);

        // One dit, nothing appended: no squeeze ever existed.
        QCOMPARE(elem.count(), 1);
        QCOMPARE(elem.at(0).at(0).toBool(), true);
    }

private:
    // Squeeze both paddles, let the keyer alternate, then release both at once and count
    // how many NEW elements start strictly after release before the keyer idles. This is
    // the A/B discriminator: A → 0, B → 1.
    int squeezeReleaseExtraElements(IambicKeyer::Mode mode, bool usePair = false) {
        IambicKeyer keyer;
        keyer.setEnabled(true);
        keyer.setSpeed(kWpm);
        keyer.setMode(mode);
        QSignalSpy elem(&keyer, &IambicKeyer::elementStarted);
        QSignalSpy finished(&keyer, &IambicKeyer::keyingFinished);

        if (usePair) {
            keyer.setPaddleState(true, true);
        } else {
            keyer.setDitPaddle(true);
            keyer.setDahPaddle(true);
        }
        QTest::qWait(300); // establish the squeeze (m_squeezed) and alternate a few times

        // Capture the count and release synchronously: no event loop runs between these
        // two statements, so the element timer cannot fire in the gap and the in-progress
        // element is already accounted for in `before`.
        const int before = elem.count();
        if (usePair) {
            keyer.setPaddleState(false, false);
        } else {
            keyer.setDitPaddle(false);
            keyer.setDahPaddle(false);
        }

        // The keyer must return to idle (no paddles held).
        if (finished.count() == 0)
            finished.wait(2000);

        return elem.count() - before;
    }
};

QTEST_GUILESS_MAIN(TestIambicKeyer)
#include "test_iambickeyer.moc"
