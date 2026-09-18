#ifndef TRANSMITOWNER_H
#define TRANSMITOWNER_H

// Decides who owns the transmitter and what has to happen when that changes.
//
// Pure logic with no Qt, no audio and no network dependency, so it can be unit-tested without
// standing up AudioController or a socket — mirroring hardware/halikey_edge.h and
// network/connect_failure.h.
//
// WHY this exists at all: QK4 had no owner of "are we transmitting". Several producers each wrote
// an audio gate and a button independently, only one of them put TX;/RX; on the wire, the radio's
// own TX state was never fed back, and no disconnect path released anything. That is the G1
// violation in AUDIT.md, and AUD-001 / INT-001 / INT-002 are three symptoms of it rather than three
// bugs.
//
// The design separates three things that were tangled into one:
//
//   WHO wants to transmit          -> Owner   (was implied by which code path ran)
//   HOW the radio gets keyed       -> Route   (was implied by the owner)
//   WHERE the TX audio comes from  -> follows from the Route and the Owner
//
// Route is the load-bearing idea. `TX;` and "stream audio packets" are NOT two ways to do the same
// thing: `TX;` keys the remote K4 expecting audio from ITS OWN mic input, while QK4's audio packets
// carry a tunnel header telling the K4 the audio arrives over ethernet. Recording which mechanism
// was used is what lets the release be the exact inverse of the engage, instead of a fixed guess.
namespace TransmitOwner {

enum class Owner {
    None,
    PttButton, // the bottom-row PTT button (and its right-click latch)
    Xmit,      // the side-panel XMIT button
    CatClient, // WSJT-X and friends over the native CAT server
    TciClient, // a TCI client
    Radio      // the K4 keyed itself: front panel, footswitch at the radio, TUNE, a fault
};

enum class Route {
    None,
    StreamedFromHere, // open the audio gate and stream; NO TX; — the tunnel header keys the K4
    RadioLocal,       // TX; only; never open the gate — the K4 transmits from its own input
    Observed          // the radio is already transmitting; reflect it and touch nothing
};

// Mirrors AudioController::TxSource. Duplicated rather than included so this header — and the test
// that links it alone — stays clear of the controller layer. transmitcontroller.cpp static_asserts
// that the two agree, so drift is a build error rather than a silently wrong source.
enum class AudioSource { Microphone = 0, Tci = 1 };

struct State {
    Owner owner = Owner::None;
    Route route = Route::None;

    bool transmitting() const { return owner != Owner::None; }
};

// What the caller must do to reach the new state. Every field defaults to "do nothing", so a
// no-op decision is the zero value and cannot accidentally key anything.
struct Effects {
    bool refused = false; // the request was rejected; State is unchanged
    bool ignored = false; // not an error: a release from someone who did not hold it

    bool sendTx = false; // put TX; on the wire
    bool sendRx = false; // put RX; on the wire

    bool setGate = false;    // write the audio gate...
    bool gateActive = false; // ...to this

    bool setSource = false; // write the TX audio source...
    AudioSource source = AudioSource::Microphone;

    bool transmitting = false; // resulting public state, for the indicator

    // Ordering, which the caller must honour and which is why this is not a set of independent
    // flags: on a key the source must be in place BEFORE the gate opens, on an unkey it must change
    // AFTER the gate closes. Writing it directly on an unkey opens a one-audio-block window in
    // which the gate is still asserted and the source has already moved — see the WHY on
    // AudioController::setTxSourceAfterPtt. Callers read this instead of rederiving it.
    bool sourceBeforeGate() const { return gateActive; }
};

// ---------------------------------------------------------------------------------------------
// Internals — what a given route requires to be true while it is held.
// ---------------------------------------------------------------------------------------------
namespace detail {

inline bool needsGate(Route r) {
    return r == Route::StreamedFromHere;
}

inline bool keyedByCat(Route r) {
    return r == Route::RadioLocal;
}

// A local producer is the operator, physically at this computer. Local preempts remote, because
// the operator is here and the client is not — the rule PR #133 established for TCI and which this
// generalises. Remote never preempts anything, including a transmission the radio started itself.
inline bool isLocal(Owner o) {
    return o == Owner::PttButton || o == Owner::Xmit;
}

inline AudioSource sourceFor(Owner o) {
    return o == Owner::TciClient ? AudioSource::Tci : AudioSource::Microphone;
}

// The whole decision, expressed once: diff what the old state required against what the new one
// requires. Every transition — engage, release, preempt, radio-initiated — goes through here, so
// none of them can disagree about what "stop transmitting" means.
inline Effects transition(State &s, Owner newOwner, Route newRoute) {
    const Owner oldOwner = s.owner;
    const Route oldRoute = s.route;

    Effects e;
    s.owner = newOwner;
    s.route = newRoute;
    e.transmitting = s.transmitting();

    const bool gateWas = needsGate(oldRoute);
    const bool gateNow = needsGate(newRoute);
    if (gateWas != gateNow) {
        e.setGate = true;
        e.gateActive = gateNow;
    }

    // The exact inverse of however the engage keyed the radio — never a fixed RX;.
    if (keyedByCat(newRoute) && !keyedByCat(oldRoute))
        e.sendTx = true;
    if (keyedByCat(oldRoute) && !keyedByCat(newRoute))
        e.sendRx = true;

    // The source matters only while something is streaming. On the way down, restore the
    // microphone so the next key does not inherit a departed client's source.
    if (gateNow) {
        const AudioSource want = sourceFor(newOwner);
        if (!gateWas || sourceFor(oldOwner) != want) {
            e.setSource = true;
            e.source = want;
        }
    } else if (gateWas) {
        e.setSource = true;
        e.source = AudioSource::Microphone;
    }

    return e;
}

} // namespace detail

// ---------------------------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------------------------

/// `who` asks to transmit by mechanism `how`.
/// Free if nobody holds it. Re-asserting your own hold is accepted and may change the route.
/// A local producer takes the transmitter from a remote one; anything else is refused, and a
/// refusal leaves the state untouched so the current transmission continues undisturbed.
inline Effects engage(State &s, Owner who, Route how) {
    if (s.owner != Owner::None && s.owner != who && !(detail::isLocal(who) && !detail::isLocal(s.owner))) {
        Effects e;
        e.refused = true;
        e.transmitting = s.transmitting();
        return e;
    }
    return detail::transition(s, who, how);
}

/// `who` is finished transmitting.
/// A release from someone who does not hold the transmitter is a status report, not a command —
/// acting on it would let any client unkey the operator. It is reported as `ignored`, not refused,
/// because the caller did nothing wrong.
inline Effects release(State &s, Owner who) {
    if (s.owner != who || s.owner == Owner::None) {
        Effects e;
        e.ignored = true;
        e.transmitting = s.transmitting();
        return e;
    }
    return detail::transition(s, Owner::None, Route::None);
}

/// Unconditional release, whoever holds it: the Esc key, losing the radio, shutting down.
/// Fail closed — these paths must never be able to leave the transmitter keyed.
inline Effects releaseAll(State &s) {
    if (s.owner == Owner::None) {
        Effects e;
        e.ignored = true;
        return e;
    }
    return detail::transition(s, Owner::None, Route::None);
}

/// The K4 told us its own transmit state changed (a TX;/RX; echo).
///
/// Transmitting with nobody holding it means the radio keyed itself — front panel, a footswitch
/// plugged into the radio, TUNE, VOX. Record it as Observed so the indicator is honest and so a
/// client cannot key on top of it.
///
/// NOT transmitting while somebody holds it means the radio dropped out from under us: a fault, a
/// tune timeout, the operator pressing RX at the radio. The gate has to close, or QK4 keeps
/// encoding and sending mic frames that re-key the radio. That is INT-002.
///
/// CALLER OBLIGATION: do not deliver a report that predates your own most recent transition. On a
/// StreamedFromHere release the K4 emits its own RX; a few milliseconds later, and if the operator
/// (or a fast client) has re-keyed in the meantime, that stale echo arrives while somebody legitimately
/// holds the transmitter and this function will dutifully unkey them. The arbiter cannot see that —
/// CAT echoes carry no sequence — so the freshness guard belongs in the caller, where the clock is.
inline Effects radioReports(State &s, bool transmitting) {
    if (transmitting) {
        if (s.owner != Owner::None) {
            Effects e; // our own echo coming back; already accounted for
            e.ignored = true;
            e.transmitting = true;
            return e;
        }
        return detail::transition(s, Owner::Radio, Route::Observed);
    }

    if (s.owner == Owner::None) {
        Effects e;
        e.ignored = true;
        return e;
    }
    return detail::transition(s, Owner::None, Route::None);
}

} // namespace TransmitOwner

#endif // TRANSMITOWNER_H
