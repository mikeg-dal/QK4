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

    // ---- Spec conformance: the Mode A / Mode B decision rule -------------------------------
    //
    // These pin the one thing that distinguishes the modes: at an element boundary, Iambic B
    // counts a lever that was down at ANY point during the cycle, Iambic A counts only the
    // levers held at that instant. The suite above covers the long-squeeze case; these cover
    // the short squeeze that fits inside a single element, which is how a two-element letter
    // is actually sent, and which behaved differently.

    // Iambic B, the letter "A": press dit, squeeze dah while the first dit is still running,
    // release both. The dah latch survives the boundary and completes the letter — and nothing
    // more. Two elements, not three.
    //
    // Regression gate: this produced ".-." (the letter R) because enterElement() cleared only
    // the just-played element's latch, so the dit latch set during element 1 survived element 2
    // and fired a third element. iambicB_squeezeReleaseAppendsOneElement did not catch it —
    // squeezing across several elements consumes the latches on the way, so only the short
    // squeeze exposes it.
    void iambicB_shortSqueezeSendsExactlyTwoElements() {
        QCOMPARE(shortSqueeze(IambicKeyer::IambicB, /*startWithDit=*/true), QStringLiteral(".-"));
        QCOMPARE(shortSqueeze(IambicKeyer::IambicB, /*startWithDit=*/false), QStringLiteral("-."));
    }

    // Iambic A, same gesture: the element in progress finishes and the keyer stops. Nothing is
    // appended, because A does not consult the latch.
    void iambicA_shortSqueezeSendsOneElement() {
        QCOMPARE(shortSqueeze(IambicKeyer::IambicA, /*startWithDit=*/true), QStringLiteral("."));
        QCOMPARE(shortSqueeze(IambicKeyer::IambicA, /*startWithDit=*/false), QStringLiteral("-"));
    }

    // The levers never overlap here: one is released before the other is pressed, both inside
    // the first element. Nothing is squeezed, so only the latch can carry the second element —
    // which means B sends it and A must not.
    //
    // Regression gate: both modes sent it. onTimerFired() consulted the latches unconditionally
    // and the guard that corrected Iambic A keyed off a physical both-levers-down flag, so a
    // non-overlapping tap bypassed it entirely and Iambic A behaved exactly like Iambic B.
    void nonOverlappingTapDistinguishesTheModes() {
        QCOMPARE(nonOverlappingTap(IambicKeyer::IambicB, /*startWithDit=*/true), QStringLiteral(".-"));
        QCOMPARE(nonOverlappingTap(IambicKeyer::IambicA, /*startWithDit=*/true), QStringLiteral("."));
        QCOMPARE(nonOverlappingTap(IambicKeyer::IambicB, /*startWithDit=*/false), QStringLiteral("-."));
        QCOMPARE(nonOverlappingTap(IambicKeyer::IambicA, /*startWithDit=*/false), QStringLiteral("-"));
    }

    // A held lever repeats from live state alone, in both modes. Guards the same-side latch
    // clear in enterElement(): seeding that latch instead would double every single tap.
    void heldLeverRepeatsInBothModes() {
        for (auto mode : {IambicKeyer::IambicA, IambicKeyer::IambicB}) {
            IambicKeyer keyer;
            keyer.setEnabled(true);
            keyer.setSpeed(kWpm);
            keyer.setMode(mode);
            keyer.setHoldGateEnabled(false);
            QSignalSpy elem(&keyer, &IambicKeyer::elementStarted);
            keyer.setPaddleState(true, false);
            QTest::qWait(300);
            keyer.setPaddleState(false, false);
            QTest::qWait(200);
            QVERIFY2(elem.count() >= 3, qPrintable(QString("only %1 elements").arg(elem.count())));
            for (int i = 0; i < elem.count(); ++i)
                QVERIFY(elem.at(i).at(0).toBool()); // every one a dit
        }
    }

    // A sample that repeats the levers' CURRENT levels carries no information and must change
    // nothing. The MIDI HaliKey makes this a live concern rather than a hypothetical: it fires
    // note 31 on every dit (111 note-31 events against 111 note-20 in a bench capture), and
    // HalikeyDevice forwards all three line levels on any note — so setPaddleState() is called
    // repeatedly with levels that have not moved.
    //
    // Regression gate: with a level-driven latch, such a sample re-armed the same-side latch that
    // enterElement() had just cleared. In Iambic B that turned a single tap into two elements,
    // because the boundary then saw "same lever still pressed" from a latch rather than from the
    // lever. Serial never showed it — it has no spurious events to re-arm from.
    void repeatedLevelsDoNotDoubleATap() {
        for (auto mode : {IambicKeyer::IambicA, IambicKeyer::IambicB}) {
            IambicKeyer keyer;
            keyer.setEnabled(true);
            keyer.setSpeed(kWpm);
            keyer.setMode(mode);
            keyer.setHoldGateEnabled(false); // MIDI transport
            QSignalSpy elem(&keyer, &IambicKeyer::elementStarted);

            keyer.setPaddleState(true, false); // dit down -> one dit element begins
            QTest::qWait(20);
            keyer.setPaddleState(true, false); // redundant: same levels, as note 31 produces
            keyer.setPaddleState(true, false);
            keyer.setPaddleState(true, false);
            QTest::qWait(10);
            keyer.setPaddleState(false, false); // released, still inside the first element
            QTest::qWait(500);

            QCOMPARE(elementsOf(elem), QStringLiteral("."));
        }
    }

    // The same invariant stated generally: injecting redundant samples into a gesture must not
    // change what the keyer sends, in either mode.
    void redundantSamplesNeverChangeTheOutcome() {
        for (auto mode : {IambicKeyer::IambicA, IambicKeyer::IambicB}) {
            QCOMPARE(squeezeWithRepeats(mode, 4), squeezeWithRepeats(mode, 0));
        }
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
        QTest::qWait(300); // establish the squeeze and alternate a few times

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

    // Press dit, squeeze dah inside the first element, release both — with `repeats` redundant
    // same-level samples injected at each step, mimicking a transport that reports unchanged lines.
    QString squeezeWithRepeats(IambicKeyer::Mode mode, int repeats) {
        IambicKeyer keyer;
        keyer.setEnabled(true);
        keyer.setSpeed(kWpm);
        keyer.setMode(mode);
        keyer.setHoldGateEnabled(false);
        QSignalSpy elem(&keyer, &IambicKeyer::elementStarted);

        auto sample = [&](bool dit, bool dah) {
            keyer.setPaddleState(dit, dah);
            for (int i = 0; i < repeats; ++i)
                keyer.setPaddleState(dit, dah);
        };

        sample(true, false);
        QTest::qWait(15);
        sample(true, true);
        QTest::qWait(15);
        sample(false, false);
        QTest::qWait(700);
        return elementsOf(elem);
    }

    // Element sequence as a string: '.' = dit, '-' = dah. Hold gate off throughout, so a
    // deliberately short tap is never mistaken for contact bounce.
    static QString elementsOf(const QSignalSpy &elem) {
        QString out;
        for (int i = 0; i < elem.count(); ++i)
            out += elem.at(i).at(0).toBool() ? QLatin1Char('.') : QLatin1Char('-');
        return out;
    }

    // Press one lever, squeeze the other while the FIRST element is still running, release both.
    // At 30 WPM the first dit cycle is 80 ms, so the whole gesture fits inside it.
    QString shortSqueeze(IambicKeyer::Mode mode, bool startWithDit) {
        IambicKeyer keyer;
        keyer.setEnabled(true);
        keyer.setSpeed(kWpm);
        keyer.setMode(mode);
        keyer.setHoldGateEnabled(false);
        QSignalSpy elem(&keyer, &IambicKeyer::elementStarted);

        keyer.setPaddleState(startWithDit, !startWithDit);
        QTest::qWait(15);
        keyer.setPaddleState(true, true); // squeeze, still inside element 1
        QTest::qWait(15);
        keyer.setPaddleState(false, false); // release both, still inside element 1
        QTest::qWait(600);                  // long enough for a dah plus anything spurious
        return elementsOf(elem);
    }

    // Tap one lever and release it, then tap the other and release it, with NO overlap — both
    // taps inside the first element cycle.
    QString nonOverlappingTap(IambicKeyer::Mode mode, bool startWithDit) {
        IambicKeyer keyer;
        keyer.setEnabled(true);
        keyer.setSpeed(kWpm);
        keyer.setMode(mode);
        keyer.setHoldGateEnabled(false);
        QSignalSpy elem(&keyer, &IambicKeyer::elementStarted);

        keyer.setPaddleState(startWithDit, !startWithDit);
        QTest::qWait(12);
        keyer.setPaddleState(false, false); // first lever up before the second goes down
        QTest::qWait(8);
        keyer.setPaddleState(!startWithDit, startWithDit); // opposite lever, no overlap
        QTest::qWait(12);
        keyer.setPaddleState(false, false);
        QTest::qWait(800);
        return elementsOf(elem);
    }
};

QTEST_GUILESS_MAIN(TestIambicKeyer)
#include "test_iambickeyer.moc"
