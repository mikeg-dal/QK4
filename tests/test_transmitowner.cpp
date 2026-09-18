// Tests for the transmit arbiter — the owner of "are we transmitting".
//
// The defect class these guard against is the one the 2026-09-17 audit called the G1 violation:
// several producers each writing an audio gate and a button independently, with no owner. Every
// assertion below is a rule that was NOT true before the arbiter existed, so they read as a
// specification rather than as coverage.
//
// Pure logic — no radio, no audio device, no event loop.
#include <QtTest/QtTest>
#include "models/transmitowner.h"

using namespace TransmitOwner;

class TestTransmitOwner : public QObject {
    Q_OBJECT

private slots:
    // ---- Taking it when it is free ----------------------------------------------------------

    // Streaming from here opens the gate and sets the source, and puts NOTHING on the wire.
    // The tunnel header is what keys the K4; a TX; here would key it expecting its own mic.
    void streamedEngageOpensTheGateAndSendsNoCat() {
        State s;
        const Effects e = engage(s, Owner::PttButton, Route::StreamedFromHere);
        QVERIFY(!e.refused);
        QVERIFY(e.setGate);
        QVERIFY(e.gateActive);
        QVERIFY(e.setSource);
        QCOMPARE(e.source, AudioSource::Microphone);
        QVERIFY(!e.sendTx);
        QVERIFY(!e.sendRx);
        QVERIFY(e.transmitting);
    }

    // The radio-local route is the opposite: TX; on the wire, and the gate stays shut. Opening it
    // would stream the room at a radio that is already transmitting from its own input.
    void radioLocalEngageSendsTxAndLeavesTheGateShut() {
        State s;
        const Effects e = engage(s, Owner::Xmit, Route::RadioLocal);
        QVERIFY(e.sendTx);
        QVERIFY(!e.sendRx);
        QVERIFY(!e.setGate);
        QVERIFY(!e.setSource);
        QVERIFY(e.transmitting);
    }

    // A TCI client streams its own audio, not the microphone.
    void tciEngageSelectsTheTciSource() {
        State s;
        const Effects e = engage(s, Owner::TciClient, Route::StreamedFromHere);
        QVERIFY(e.setSource);
        QCOMPARE(e.source, AudioSource::Tci);
        QVERIFY(e.gateActive);
    }

    // ---- Release is the inverse of the engage, never a fixed action -------------------------

    // The rule INT-001 was about. A gate-only release after a CAT-keyed transmit leaves the K4
    // keyed with no modulation — an unattended carrier into an amplifier.
    void releaseMirrorsHowItWasKeyed() {
        State streamed;
        engage(streamed, Owner::PttButton, Route::StreamedFromHere);
        const Effects a = release(streamed, Owner::PttButton);
        QVERIFY(a.setGate);
        QVERIFY(!a.gateActive);
        QVERIFY(!a.sendRx); // never keyed by CAT, so nothing to unkey by CAT
        QVERIFY(!a.transmitting);

        State keyed;
        engage(keyed, Owner::Xmit, Route::RadioLocal);
        const Effects b = release(keyed, Owner::Xmit);
        QVERIFY(b.sendRx); // keyed by TX;, so unkeyed by RX;
        QVERIFY(!b.setGate);
        QVERIFY(!b.transmitting);
    }

    // Releasing a streamed transmission restores the microphone, so the next key cannot inherit a
    // departed client's source and transmit silence.
    void releasingTciRestoresTheMicrophone() {
        State s;
        engage(s, Owner::TciClient, Route::StreamedFromHere);
        const Effects e = release(s, Owner::TciClient);
        QVERIFY(e.setSource);
        QCOMPARE(e.source, AudioSource::Microphone);
        QVERIFY(!e.gateActive);
    }

    // ---- Contention --------------------------------------------------------------------------

    // A client cannot take the transmitter from another client, and the refusal must not disturb
    // the transmission in progress.
    void remoteCannotTakeItFromRemote() {
        State s;
        engage(s, Owner::TciClient, Route::StreamedFromHere);
        const Effects e = engage(s, Owner::CatClient, Route::StreamedFromHere);
        QVERIFY(e.refused);
        QVERIFY(!e.setGate);
        QVERIFY(!e.setSource);
        QVERIFY(!e.sendTx);
        QVERIFY(e.transmitting); // still transmitting — for the other owner
        QCOMPARE(s.owner, Owner::TciClient);
    }

    // The operator is at the radio and the client is not.
    void localTakesItFromRemote() {
        State s;
        engage(s, Owner::TciClient, Route::StreamedFromHere);
        const Effects e = engage(s, Owner::PttButton, Route::StreamedFromHere);
        QVERIFY(!e.refused);
        QCOMPARE(s.owner, Owner::PttButton);
        // Both routes stream, so the gate does not flap — but the source must move off the client.
        QVERIFY(!e.setGate);
        QVERIFY(e.setSource);
        QCOMPARE(e.source, AudioSource::Microphone);
    }

    // Two local producers still queue: whoever pressed first keeps it. Otherwise XMIT would unkey
    // a transmission the PTT button is holding, which is how the old code behaved by accident.
    void localDoesNotTakeItFromLocal() {
        State s;
        engage(s, Owner::PttButton, Route::StreamedFromHere);
        const Effects e = engage(s, Owner::Xmit, Route::RadioLocal);
        QVERIFY(e.refused);
        QVERIFY(!e.sendTx);
        QCOMPARE(s.owner, Owner::PttButton);
    }

    // Preempting a streamed transmission with a CAT-keyed one has to do both halves.
    void preemptionAcrossRoutesClosesTheGateAndKeys() {
        State s;
        engage(s, Owner::TciClient, Route::StreamedFromHere);
        const Effects e = engage(s, Owner::Xmit, Route::RadioLocal);
        QVERIFY(!e.refused);
        QVERIFY(e.setGate);
        QVERIFY(!e.gateActive);
        QVERIFY(e.sendTx);
        QVERIFY(e.setSource);
        QCOMPARE(e.source, AudioSource::Microphone);
    }

    // ---- Releases that must do nothing --------------------------------------------------------

    // A client's unkey while somebody else holds the transmitter is a status report, not a
    // command. Acting on it would let any client unkey the operator.
    void anUnownedReleaseDoesNotUnkey() {
        State s;
        engage(s, Owner::PttButton, Route::StreamedFromHere);
        const Effects e = release(s, Owner::CatClient);
        QVERIFY(e.ignored);
        QVERIFY(!e.setGate);
        QVERIFY(!e.sendRx);
        QVERIFY(e.transmitting);
        QCOMPARE(s.owner, Owner::PttButton);
    }

    // A release with nobody holding it is the base case that stops a release cascading.
    void releasingWhenIdleDoesNothing() {
        State s;
        const Effects e = release(s, Owner::PttButton);
        QVERIFY(e.ignored);
        QVERIFY(!e.setGate);
        QVERIFY(!e.sendRx);
        QVERIFY(!e.transmitting);

        State t;
        const Effects f = releaseAll(t);
        QVERIFY(f.ignored);
        QVERIFY(!f.setGate);
        QVERIFY(!f.sendRx);
    }

    // ---- Fail closed --------------------------------------------------------------------------

    // Esc, losing the radio, and shutting down all release whoever holds it. AUD-001 was exactly
    // this not happening: the gate survived a disconnect and the next mic-device change transmitted.
    void releaseAllReleasesAnyOwner() {
        for (auto who : {Owner::PttButton, Owner::Xmit, Owner::CatClient, Owner::TciClient}) {
            const Route how = (who == Owner::Xmit) ? Route::RadioLocal : Route::StreamedFromHere;
            State s;
            engage(s, who, how);
            const Effects e = releaseAll(s);
            QVERIFY(!e.ignored);
            QVERIFY(!e.transmitting);
            QCOMPARE(s.owner, Owner::None);
            QCOMPARE(s.route, Route::None);
            if (how == Route::StreamedFromHere) {
                QVERIFY2(e.setGate && !e.gateActive, "a streamed transmission must have its gate closed");
            } else {
                QVERIFY2(e.sendRx, "a CAT-keyed transmission must be unkeyed by CAT");
            }
        }
    }

    // ---- The radio's own transmit state (INT-002) ---------------------------------------------

    // The radio keyed itself — front panel, a footswitch plugged into the radio, TUNE, VOX.
    // Record it so the indicator is honest, and touch nothing: we did not cause it.
    void radioKeyingItselfIsObservedNotDriven() {
        State s;
        const Effects e = radioReports(s, true);
        QVERIFY(e.transmitting);
        QVERIFY(!e.setGate);
        QVERIFY(!e.sendTx);
        QVERIFY(!e.setSource);
        QCOMPARE(s.owner, Owner::Radio);
        QCOMPARE(s.route, Route::Observed);
    }

    // The radio dropped out from under us: a fault, a tune timeout, RX pressed at the radio. The
    // gate must close, or QK4 keeps sending mic frames that re-key the radio. That is INT-002.
    void radioDroppingTxClosesOurGate() {
        State s;
        engage(s, Owner::PttButton, Route::StreamedFromHere);
        const Effects e = radioReports(s, false);
        QVERIFY(!e.ignored);
        QVERIFY(e.setGate);
        QVERIFY(!e.gateActive);
        QVERIFY(!e.transmitting);
        QCOMPARE(s.owner, Owner::None);
    }

    // Our own TX; coming back must not be mistaken for the radio keying itself, or every
    // CAT-keyed transmission would immediately reassign ownership away from whoever asked.
    void ourOwnEchoChangesNothing() {
        State s;
        engage(s, Owner::Xmit, Route::RadioLocal);
        const Effects e = radioReports(s, true);
        QVERIFY(e.ignored);
        QVERIFY(e.transmitting);
        QVERIFY(!e.setGate);
        QVERIFY(!e.sendTx);
        QCOMPARE(s.owner, Owner::Xmit);
        QCOMPARE(s.route, Route::RadioLocal);
    }

    // An RX echo with nobody holding it is the normal end of a transmission, already accounted for.
    void radioReportingRxWhenIdleDoesNothing() {
        State s;
        const Effects e = radioReports(s, false);
        QVERIFY(e.ignored);
        QVERIFY(!e.setGate);
        QVERIFY(!e.transmitting);
    }

    // The operator can take the transmitter from a transmission the radio started itself; a client
    // cannot. Someone standing at the radio outranks a program.
    void observedTransmitYieldsToTheOperatorButNotToAClient() {
        State local;
        radioReports(local, true);
        QVERIFY(!engage(local, Owner::PttButton, Route::StreamedFromHere).refused);
        QCOMPARE(local.owner, Owner::PttButton);

        State remote;
        radioReports(remote, true);
        QVERIFY(engage(remote, Owner::CatClient, Route::StreamedFromHere).refused);
        QCOMPARE(remote.owner, Owner::Radio);
    }

    // ---- Invariants over every transition -----------------------------------------------------

    // TX; and RX; are mutually exclusive in a single decision, the gate is only written when it
    // actually changes, and `transmitting` always agrees with the state the call left behind.
    void everyTransitionIsSelfConsistent() {
        const Owner owners[] = {Owner::None,      Owner::PttButton, Owner::Xmit,
                                Owner::CatClient, Owner::TciClient, Owner::Radio};
        const Route routes[] = {Route::None, Route::StreamedFromHere, Route::RadioLocal, Route::Observed};

        for (Owner startOwner : owners) {
            for (Route startRoute : routes) {
                if ((startOwner == Owner::None) != (startRoute == Route::None))
                    continue; // not a state the arbiter can be in
                for (Owner who : owners) {
                    if (who == Owner::None)
                        continue;
                    for (Route how : {Route::StreamedFromHere, Route::RadioLocal, Route::Observed}) {
                        State s{startOwner, startRoute};
                        const Effects e = engage(s, who, how);
                        QVERIFY2(!(e.sendTx && e.sendRx), "a single decision cannot both key and unkey");
                        QVERIFY2(!(e.refused && (e.sendTx || e.sendRx || e.setGate || e.setSource)),
                                 "a refused request must change nothing");
                        QCOMPARE(e.transmitting, s.transmitting());

                        State t = s;
                        const Effects r = releaseAll(t);
                        QVERIFY2(!r.transmitting, "releaseAll must always end idle");
                        QCOMPARE(t.owner, Owner::None);
                        QVERIFY2(!(r.sendTx), "releasing must never key the radio");
                    }
                }
            }
        }
    }
};

QTEST_APPLESS_MAIN(TestTransmitOwner)
#include "test_transmitowner.moc"
