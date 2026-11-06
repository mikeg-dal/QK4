// SPDX-License-Identifier: GPL-3.0-or-later
//
// Paddle and straight-key keyer for HaliKey (serial and MIDI). A port of the Keyer class of Dale
// Farnsworth's K4-Companion (GPL-3.0, https://github.com/DaleFarnsworth/K4-Companion).
//
// What goes on the air is carried in timed key commands: KZD#### (key down after a gap of ####
// ms) and KZU#### (key up after #### ms down). The K4 replays those intervals behind its KZL
// key-down delay, so thread wake-ups and network jitter cannot change an element's length. Paddle
// edges are timestamped at capture (MoMIDI's own clock, or the 1 ms serial poll), and every
// decision reads the paddles as they stood when it was due. Iambic memories are measured from when
// the previous element was heard.
//
// Threads: paddleEvent/midiMessage/serialSignalChanged on the device thread; processNextEvent/run on
// the keyer thread; configuration, releasePaddles and close from any thread. Every KZ and ML frame
// is sent from the keyer thread, in the order the keyer decides: other threads post a request to
// its queue and never send.
//
// The main thread takes no lock the keyer or a device thread can wait on: configuration is held in
// atomics; a release or close is a request in a ring only requesters lock, and the keyer does the
// work of a release itself, at its next decision. It wakes on a semaphore, or when the monitor
// restore it keeps the deadline for falls due.
//
// KPOD+ keying does not pass through here. See docs/cw-keyer-port-plan.md.

#ifndef PADDLEKEYER_H
#define PADDLEKEYER_H

#include "hardware/keyertiming.h"
#include "utils/functionref.h"
#include "utils/ringdeque.h"
#include "utils/rtsan.h"
#include "utils/rtsemaphore.h"
#include "utils/spscring.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

class SidetoneScheduler;

class PaddleKeyer {
public:
    enum class Mode { PlainIambic, IambicA, IambicB, Ultimatic };
    // Release, Close and RestoreMonitor are the keyer's own: a pass that applied a release, ended
    // the keyer, or restored the monitor. paddleEvent() ignores them.
    enum class Event { DitDown, DitUp, DahDown, DahUp, Close, Release, RestoreMonitor };
    enum class Signal { CTS, DCD, DSR, RI };

    PaddleKeyer(CwKeying::Clock &clock, CwKeying::Link &link, SidetoneScheduler &sidetone);

    PaddleKeyer(const PaddleKeyer &) = delete;
    PaddleKeyer &operator=(const PaddleKeyer &) = delete;

    // ---- configuration (any thread) ----
    void setMode(Mode mode);
    Mode mode() const;
    void setStraightKeying(bool straight);         // straight key or external keyer passthrough
    void setDitMemoryDelayPercent(double percent); // Iambic B only; 0 disables
    void setSpeed(int wpm, double weighting = 1.0);
    void setDitNotes(std::vector<int> notes);
    void setDahNotes(std::vector<int> notes);
    void setDitSignal(Signal signal);
    void setDahSignal(Signal signal);
    // Mute the K4's CW monitor for the length of each burst (ML0000, restored afterwards). Off by
    // default: whether the K4 already suppresses its monitor for KZD/KZU is a bench question.
    void setMuteRadioMonitor(bool mute);
    void setMonitorLevel(int level);       // the ML0 value restored after a burst
    void setKeyDownInitialDelayMs(int ms); // the K4's KZL value
    // While inhibited (KPOD+ owns CW, not in CW mode, link down) nothing is keyed: whatever is keyed
    // is released and paddle events are dropped at the door.
    void setInhibited(bool inhibited);

    double ditSeconds() const;
    double dahSeconds() const;
    double interElementSeconds() const;

    // ---- device threads ----
    void paddleEvent(Event event, double deltaTime,
                     std::optional<double> timestamp = std::nullopt) noexcept QK4_NONBLOCKING;
    void midiMessage(int status, int note, int velocity, double rtmidiDelta) noexcept QK4_NONBLOCKING;
    void serialSignalChanged(Signal signal, bool asserted, double deltaTime) noexcept QK4_NONBLOCKING;

    // Ends whatever is keyed: the running burst stops at its next element boundary with the
    // current element sent whole; a straight key held down gets its KZU from the keyer thread,
    // after any KZD already queued ahead of it, timed to this call. closing = true also refuses
    // every later event.
    void releasePaddles(bool closing = false);

    // ---- keyer thread ----
    bool hasPendingEvents() const;
    // When the monitor restore is due (clock time), if one is armed.
    std::optional<double> monitorRestoreDue() const;
    // One pass of the keyer loop; blocks until an event arrives or the monitor restore falls due.
    // Real-time throughout, except its two waits: for the next event, and for an element's deadline
    // (sleepUntil).
    Event processNextEvent() noexcept QK4_NONBLOCKING;
    void run(); // the keyer loop, until Close
    // Releases, posts Close and refuses everything after. A straight key held down gets its KZU
    // before the keyer sees Close. The owner joins the keyer thread.
    void close();

private:
    using Element = std::optional<char>; // '.', '-', or none
    // WHY FunctionRef, not std::function: a std::function can allocate, per burst, on the keyer thread.
    using Chooser = FunctionRef<Element(Element prev, std::optional<double> look, std::optional<double> heard,
                                        std::optional<double> asOf)>;
    using Wanted = FunctionRef<bool(double asOf)>;

    struct PaddleEventItem {
        Event event;
        double timestamp;
        double keyDownAt = 0.0; // Release: when the straight key went down
    };
    // Up to four MIDI notes per lever, packed so both levers are read in one atomic load.
    static constexpr int kMaxNotes = 4;
    static std::uint64_t packNotes(std::uint64_t packed, int lever, const std::vector<int> &notes);
    static bool leverHasNote(std::uint64_t packed, int lever, int note);
    struct Stamps {
        double ditUp, dahUp, ditDown, dahDown;
    };
    struct FirstElement {
        double keyedStart;
        char element;
        double closedAt;
    };

    void pushEvent(PaddleEventItem item);   // caller holds m_firstElementMutex
    void pushRequest(PaddleEventItem item); // Close, any thread
    bool applyPendingReleases();            // keyer thread; true if one was applied
    void applyRelease(double at);           // keyer thread

    Stamps paddleTimestamps(std::optional<double> asOf);
    void sendStraightKey(const PaddleEventItem &item);
    void sendIambic();
    void sendUltimatic();
    void sendBurst(Chooser choose);
    Element missedFirstElement(std::optional<double> endedDue);
    Element placedFirstElement();
    std::optional<FirstElement> takeFirstElement();
    void startBurstSidetoneLocked(Event event, double timestamp, double pressed);
    bool startElement(bool firstElement, Wanted wanted);
    void planSidetoneCatchup(bool firstElement);
    bool confirmElement(double sidetoneStart, Wanted wanted, double gridShift);
    std::pair<double, double> elementSidetone(double elementSeconds) const;
    double upEdgeMargin(double ditSidetoneSeconds) const;
    void waitUntilSidetoneHeard();
    void endElement(char element);
    void measureInherentSidetoneLag();
    void endStraightKeyOnAir(double keyDownAt, double releasedAt);
    void straightKeyUpOnAir(double timestamp, bool restoreMonitor);
    void reconcileLostEdges();
    long long straightKeyMs(double seconds) const;
    double midiEventTimestamp(double delta);
    std::optional<double> sleepUntil(double when);
    void send(std::string_view command);
    void muteSidetoneMonitor();
    void delayedUnmuteSidetoneMonitor(double delay);
    void unmuteSidetoneMonitor();
    static double wholeMs(double seconds);

    CwKeying::Clock &m_clock;
    CwKeying::Link &m_link;
    SidetoneScheduler &m_sidetone;

    // ---- configuration (written by any thread, read lock-free by the keyer and device threads) ----
    std::atomic<Mode> m_mode{Mode::IambicB};
    std::atomic<bool> m_straightKeying{false};
    std::atomic<double> m_ditMemoryDelayPercent{33.0};
    std::atomic<std::uint64_t> m_notes; // dit notes in the low 32 bits, dah in the high; see packNotes
    std::atomic<Signal> m_ditSignal{Signal::DSR};
    std::atomic<Signal> m_dahSignal{Signal::DCD};
    std::mutex m_notesWriteMutex; // writers of m_notes only, so two setters cannot lose an update
    std::atomic<bool> m_muteRadioMonitor{false};
    std::atomic<int> m_monitorLevel{0};
    std::atomic<int> m_keyDownInitialDelayMs{0};
    std::atomic<bool> m_inhibited{false};

    // Element timing from on_speed_change. Each value is read on its own, as it always was.
    std::atomic<double> m_ditSeconds{0};
    std::atomic<double> m_dahSeconds{0};
    std::atomic<double> m_interElementSeconds{0};

    // ---- queues to the keyer ----
    // Paddle events (device threads) and the keyer's own straight-key Release: pushed under
    // m_firstElementMutex, which makes the producers one at a time.
    SpscRing<PaddleEventItem, 1024> m_events;
    // Set when m_events was full and an edge was dropped (under m_firstElementMutex, like every
    // push). Paddles lose nothing by it, since they are read from m_stamps, but a straight key is
    // keyed from the queued edges alone; see reconcileLostEdges.
    std::atomic<bool> m_eventsLost{false};
    // Releases (the time each was asked for) and Close, from any thread: pushed under
    // m_requestMutex, which the keyer never takes. Separate, so a pending Close never holds up a
    // release the keyer applies mid-burst.
    SpscRing<double, 64> m_releases;
    SpscRing<PaddleEventItem, 64> m_requests;
    std::mutex m_requestMutex;
    RtSemaphore m_wake; // one post per push

    // ---- paddle state (device thread writes, keyer reads) ----
    // Locks the keyer and the device threads share (RtSan::BoundedMutex): held for a few
    // assignments, a queue push or a sidetone scheduler call, never across a sleep, a send or an
    // allocation. Only the requesters take m_requestMutex, never the keyer.
    mutable RtSan::BoundedMutex m_paddleMutex;
    Stamps m_stamps{0, 0, 0, 0};
    // Capped at kHistoryLength; preallocated with room for the two a release inserts past it.
    RingDeque<std::pair<double, Event>> m_history{kHistoryLength * 2};

    // ---- MIDI decoding (device thread only) ----
    double m_midiDelta = 0;
    bool m_ditWasDown = false;
    bool m_dahWasDown = false;
    double m_midiDeviceClock = 0.0;
    std::optional<double> m_midiClockOffset;

    // ---- first element / straight key: K4-Companion's first_element_lock ----
    RtSan::BoundedMutex m_firstElementMutex;
    std::optional<FirstElement> m_firstElementScheduled;
    std::atomic<bool> m_inBurst{false};
    std::atomic<bool> m_closed{false};
    bool m_straightKeyDown = false;
    double m_straightKeyDownAt = 0.0; // read only under m_firstElementMutex, like its writer

    // ---- straight-key on-air state (keyer thread; m_paddleTimestamp also the device thread) ----
    RtSan::BoundedMutex m_straightMutex;
    double m_paddleTimestamp = 0;
    double m_previousKeyDownTimestamp = 0;
    double m_previousKeyUpTimestamp = 0;
    bool m_k4Keyed = false; // keyer thread only

    // ---- burst state (keyer thread only) ----
    std::optional<double> m_burstEndDue;
    std::optional<double> m_firstElementClosedAt;
    std::optional<double> m_elementTimestamp;
    std::optional<double> m_endElementTimestamp;
    double m_sidetoneDelayUsed = 0.0;
    double m_sidetonePendingStep = 0.0;
    double m_sidetoneDelaySeen = 0.0;
    double m_sidetoneBehind = 0.0;
    double m_elementKeyedStart = 0.0;
    double m_elementSidetoneStart = 0.0;
    double m_elementKzdTimestamp = 0.0;
    double m_elementHeardStart = 0.0;
    std::optional<double> m_elementReadDue;
    bool m_elementSidetoneQueued = false;
    bool m_elementFirst = false;
    double m_inherentSidetoneLag = 0.0;
    double m_wakeOverrun = 0.0;
    std::optional<long long> m_lastKzdMs;

    // ---- monitor mute (keyer thread only) ----
    bool m_monitorMuted = false;
    // A burst or a straight key-down clears it, so a restore never lands inside keying.
    std::optional<double> m_monitorRestoreDue;

    static constexpr double kUnmuteExtraDelay = 0.5;
    static constexpr double kTolerableSidetoneLagMargin = .001;
    static constexpr double kMaxSidetoneCatchupFraction = .03;
    static constexpr double kSidetoneHeardPollSeconds = .001;
    static constexpr double kSidetoneEdgeMarginSeconds = .002;
    static constexpr double kWakeOverrunDecay = .98;
    static constexpr double kMaxWakeOverrun = .010;
    static constexpr double kMidiReanchorSeconds = 1.0;
    static constexpr size_t kHistoryLength = 64;
};

#endif // PADDLEKEYER_H
