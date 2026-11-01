// SPDX-License-Identifier: GPL-3.0-or-later
//
// Port of K4-Companion's Keyer (Dale Farnsworth, GPL-3.0). Function by function against
// k4companion: release_paddles 19536, end_straight_key_on_air 19588, midi_message_callback 19924,
// midi_event_timestamp 19975, serial_message_callback 20010, send_paddle_event 20026,
// start_burst_sidetone 20122, send_straight_key 20189, paddle_timestamps 20238, send_iambic 20281,
// send_ultimatic 20407, send_burst 20442, missed_first_element 20604, placed_first_element 20634,
// start_element 20671, plan_sidetone_catchup 20853, confirm_element 21008, element_sidetone 21088,
// wait_until_sidetone_heard 21133, end_element 21190, measure_inherent_sidetone_lag 21246,
// on_speed_change 21312, the monitor mute 21331-21352.
//
// Left out on purpose: auto character/word spacing. With it off, auto_space_until() returns None
// everywhere, so removing it changes nothing else.
//
// Expressions keep the Python's order so the doubles match the recorded runs bit for bit.

#include "hardware/paddlekeyer.h"
#include "audio/sidetonescheduler.h"

#include <algorithm>
#include <cmath>
#include <utility>

using CwKeying::pyRound;

namespace {

// One KZ or ML frame, formatted on the stack by hand: the keyer thread never allocates, and never
// enters printf, whose locale handling is not something a real-time thread should depend on.
struct Frame {
    char text[32];
    size_t length = 0;
    operator std::string_view() const { return {text, length}; }
};

// prefix, then value zero-padded to at least `width` digits (printf's %0*lld), then ';'.
Frame formatFrame(const char *prefix, long long value, int width) {
    Frame f;
    for (const char *p = prefix; *p; ++p)
        f.text[f.length++] = *p;
    const bool negative = value < 0;
    unsigned long long magnitude = negative ? 0ULL - static_cast<unsigned long long>(value) : value;
    if (negative)
        f.text[f.length++] = '-';
    char digits[20];
    int n = 0;
    do {
        digits[n++] = static_cast<char>('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude > 0);
    for (int pad = width - n - (negative ? 1 : 0); pad > 0; --pad)
        f.text[f.length++] = '0';
    while (n > 0)
        f.text[f.length++] = digits[--n];
    f.text[f.length++] = ';';
    return f;
}

Frame fourDigits(const char *prefix, long long ms) {
    return formatFrame(prefix, ms, 4);
}

} // namespace

PaddleKeyer::PaddleKeyer(CwKeying::Clock &clock, CwKeying::Link &link, SidetoneScheduler &sidetone)
    : m_clock(clock), m_link(link), m_sidetone(sidetone), m_notes(packNotes(packNotes(~0ULL, 0, {20}), 1, {21})) {
    setSpeed(20);
}

// ---- configuration ----

void PaddleKeyer::setMode(Mode mode) {
    m_mode.store(mode);
}

PaddleKeyer::Mode PaddleKeyer::mode() const {
    return m_mode.load();
}

void PaddleKeyer::setStraightKeying(bool straight) {
    if (m_straightKeying.exchange(straight) != straight)
        releasePaddles();
}

void PaddleKeyer::setDitMemoryDelayPercent(double percent) {
    m_ditMemoryDelayPercent.store(std::clamp(percent, 0.0, 100.0));
}

void PaddleKeyer::setSpeed(int wpm, double weighting) {
    if (wpm <= 0)
        return;
    const double delta = (1 - weighting) / (1 + weighting);
    const double eltMult = 1 - delta;
    const double spaceMult = 1 + delta;
    const double unweightedDitSeconds = 1.2 / wpm;
    const double unweightedDahSeconds = unweightedDitSeconds * 3;
    m_ditSeconds.store(unweightedDitSeconds * eltMult);
    m_dahSeconds.store(unweightedDahSeconds * eltMult);
    m_interElementSeconds.store(unweightedDitSeconds * spaceMult);
}

// Lever 0 (dit) in bits 0-31, lever 1 (dah) in bits 32-63; one note per byte, 0xFF for none.
std::uint64_t PaddleKeyer::packNotes(std::uint64_t packed, int lever, const std::vector<int> &notes) {
    std::uint64_t half = 0xFFFFFFFFULL;
    for (size_t i = 0; i < notes.size() && i < kMaxNotes; ++i) {
        half &= ~(0xFFULL << (8 * i));
        half |= static_cast<std::uint64_t>(notes[i] & 0x7F) << (8 * i);
    }
    const int shift = lever * 32;
    return (packed & ~(0xFFFFFFFFULL << shift)) | (half << shift);
}

bool PaddleKeyer::leverHasNote(std::uint64_t packed, int lever, int note) {
    const std::uint64_t half = packed >> (lever * 32);
    for (int i = 0; i < kMaxNotes; ++i)
        if (static_cast<int>((half >> (8 * i)) & 0xFF) == note)
            return true;
    return false;
}

void PaddleKeyer::setDitNotes(std::vector<int> notes) {
    std::lock_guard lock(m_notesWriteMutex);
    m_notes.store(packNotes(m_notes.load(), 0, notes));
}

void PaddleKeyer::setDahNotes(std::vector<int> notes) {
    std::lock_guard lock(m_notesWriteMutex);
    m_notes.store(packNotes(m_notes.load(), 1, notes));
}

void PaddleKeyer::setDitSignal(Signal signal) {
    m_ditSignal.store(signal);
}

void PaddleKeyer::setDahSignal(Signal signal) {
    m_dahSignal.store(signal);
}

void PaddleKeyer::setMuteRadioMonitor(bool mute) {
    m_muteRadioMonitor = mute;
}

void PaddleKeyer::setMonitorLevel(int level) {
    m_monitorLevel = level;
}

void PaddleKeyer::setKeyDownInitialDelayMs(int ms) {
    m_keyDownInitialDelayMs = ms;
}

void PaddleKeyer::setInhibited(bool inhibited) {
    if (m_inhibited.exchange(inhibited) != inhibited && inhibited)
        releasePaddles();
}

double PaddleKeyer::ditSeconds() const {
    return m_ditSeconds.load();
}

double PaddleKeyer::dahSeconds() const {
    return m_dahSeconds.load();
}

double PaddleKeyer::interElementSeconds() const {
    return m_interElementSeconds.load();
}

// ---- queues ----

void PaddleKeyer::pushEvent(PaddleEventItem item) {
    if (m_events.push(item))
        m_wake.post();
    else
        m_eventsLost.store(true, std::memory_order_relaxed);
}

void PaddleKeyer::pushRequest(PaddleEventItem item) {
    bool queued;
    {
        std::lock_guard lock(m_requestMutex);
        queued = m_requests.push(item);
    }
    if (queued)
        m_wake.post();
}

bool PaddleKeyer::hasPendingEvents() const {
    return !m_releases.empty() || !m_events.empty() || !m_requests.empty();
}

std::optional<double> PaddleKeyer::monitorRestoreDue() const {
    return m_monitorRestoreDue;
}

// ---- release / close ----

// WHY a request the keyer carries out, rather than the work done here: this is called from the
// main thread (Esc, inhibit, a lost device, a reversal), and the work takes the locks the keyer and
// the paddle thread share, and the sidetone scheduler's. A normal-priority thread holding them
// could stall a real-time one, and cancelling a scheduled sidetone edge waits up to 100 ms. The
// keyer applies the release at its next decision, with the time it was asked for, so the paddles
// read as released from that instant, exactly as before.
void PaddleKeyer::releasePaddles(bool closing) {
    if (closing)
        m_closed.store(true);
    bool queued;
    {
        std::lock_guard lock(m_requestMutex);
        queued = m_releases.push(m_clock.now());
    }
    if (queued)
        m_wake.post();
}

bool PaddleKeyer::applyPendingReleases() {
    bool applied = false;
    double at;
    while (m_releases.pop(at)) {
        applyRelease(at);
        applied = true;
    }
    return applied;
}

void PaddleKeyer::applyRelease(double at) {
    bool straightKeyDown = false;
    std::optional<FirstElement> scheduled;
    {
        std::lock_guard lock(m_firstElementMutex);
        // A key pressed after the release was asked for is a new press, and stays.
        if (m_straightKeyDown && m_straightKeyDownAt <= at) {
            straightKeyDown = true;
            m_straightKeyDown = false;
        }
        if (m_firstElementScheduled && m_firstElementScheduled->keyedStart <= at) {
            scheduled = m_firstElementScheduled;
            m_firstElementScheduled.reset();
        }
        {
            std::lock_guard paddles(m_paddleMutex);
            m_stamps.ditUp = std::max(m_stamps.ditUp, at);
            m_stamps.dahUp = std::max(m_stamps.dahUp, at);
            // In time order, as if appended when it was asked for: edges that arrived since are
            // later and stay after it.
            std::size_t pos = m_history.size();
            while (pos > 0 && m_history[pos - 1].first > at)
                --pos;
            m_history.insert(pos, {at, Event::DahUp});
            m_history.insert(pos, {at, Event::DitUp});
            while (m_history.size() > kHistoryLength)
                m_history.pop_front();
        }
        // WHY queued, and under this lock: the KZD this release ends may still be in the queue.
        // paddleEvent queues under the same lock, so the DitDown this release saw is ahead of it.
        if (straightKeyDown)
            pushEvent({Event::Release, at, m_straightKeyDownAt});
    }
    if (straightKeyDown)
        m_sidetone.keyNow(false);
    else if (scheduled && !m_sidetone.cancelDown())
        m_sidetone.keyNow(false);
}

void PaddleKeyer::endStraightKeyOnAir(double keyDownAt, double releasedAt) {
    if (!std::exchange(m_k4Keyed, false))
        return;
    long long ms;
    {
        std::lock_guard lock(m_straightMutex);
        const double seconds = releasedAt - keyDownAt;
        m_previousKeyUpTimestamp = m_previousKeyDownTimestamp + seconds;
        m_paddleTimestamp = std::max(m_paddleTimestamp, m_previousKeyUpTimestamp);
        ms = straightKeyMs(seconds);
    }
    send(fourDigits("KZU", ms));
}

void PaddleKeyer::close() {
    releasePaddles(true);
    pushRequest({Event::Close, m_clock.now()});
}

// ---- input ----

void PaddleKeyer::midiMessage(int status, int note, int velocity, double rtmidiDelta) noexcept {
    if (status == 0xb0 || status == 0xa0) {
        m_midiDelta = (velocity * 126) / 1000.0;
        return;
    }
    if (velocity == 0 || velocity == 127)
        m_midiDelta = rtmidiDelta; // no time in this message: use RtMidi's
    else
        m_midiDelta += velocity / 1000.0;

    if (status == 0x90 || status == 0x80) {
        // WHY every note, mapped or not: the device counts its deltas from its previous note.
        const double timestamp = midiEventTimestamp(m_midiDelta);
        const std::uint64_t notes = m_notes.load();
        const bool ditNote = leverHasNote(notes, 0, note);
        const bool dahNote = leverHasNote(notes, 1, note);
        // WHY 0x90 is key-down whatever its velocity: these devices never send note-on velocity 0
        // as a release; only 0x80 releases.
        if (status == 0x90) {
            if (ditNote) {
                m_ditWasDown = true;
                paddleEvent(Event::DitDown, m_midiDelta, timestamp);
            } else if (dahNote) {
                m_dahWasDown = true;
                paddleEvent(Event::DahDown, m_midiDelta, timestamp);
            }
        } else {
            if (ditNote) {
                if (m_ditWasDown) {
                    m_ditWasDown = false;
                    paddleEvent(Event::DitUp, m_midiDelta, timestamp);
                }
            } else if (dahNote) {
                if (m_dahWasDown) {
                    m_dahWasDown = false;
                    paddleEvent(Event::DahUp, m_midiDelta, timestamp);
                }
            }
        }
    }
    m_midiDelta = 0;
}

double PaddleKeyer::midiEventTimestamp(double delta) {
    const double arrived = m_clock.now();
    m_midiDeviceClock += delta;
    const double offset = arrived - m_midiDeviceClock;
    if (!m_midiClockOffset || delta >= kMidiReanchorSeconds || offset - *m_midiClockOffset >= kMidiReanchorSeconds)
        m_midiClockOffset = offset;
    else
        m_midiClockOffset = std::min(*m_midiClockOffset, offset);
    return m_midiDeviceClock + *m_midiClockOffset;
}

void PaddleKeyer::serialSignalChanged(Signal signal, bool asserted, double deltaTime) noexcept {
    const Signal dit = m_ditSignal.load();
    const Signal dah = m_dahSignal.load();
    if (signal == dit)
        paddleEvent(asserted ? Event::DitDown : Event::DitUp, deltaTime);
    if (signal == dah && !m_straightKeying)
        paddleEvent(asserted ? Event::DahDown : Event::DahUp, deltaTime);
}

void PaddleKeyer::paddleEvent(Event event, double deltaTime, std::optional<double> timestamp) noexcept {
    if (event != Event::DitDown && event != Event::DitUp && event != Event::DahDown && event != Event::DahUp)
        return;
    // WHY one lock from the inhibit check to the queue: releasePaddles takes it too, so an event
    // is either queued ahead of a release (which then ends it) or sees the inhibit and is dropped.
    // Checked outside it, a key-down could slip in after the release and key with nothing to end it.
    std::lock_guard lock(m_firstElementMutex);
    // Deliberate deviation: after close() an event is dropped rather than queued for a keyer
    // thread that has gone.
    if (m_inhibited || m_closed)
        return;
    double stamp;
    if (m_straightKeying) {
        if (event == Event::DitDown) {
            m_sidetone.keyNow(true);
            m_straightKeyDown = true;
            m_straightKeyDownAt = m_clock.now();
        } else if (event == Event::DitUp) {
            m_sidetone.keyNow(false);
            m_straightKeyDown = false;
        }
        std::lock_guard straight(m_straightMutex);
        m_paddleTimestamp += deltaTime;
        stamp = m_paddleTimestamp;
    } else {
        const double arrived = m_clock.now();
        stamp = timestamp ? *timestamp : arrived;
        startBurstSidetoneLocked(event, arrived, stamp);
    }
    {
        std::lock_guard lock(m_paddleMutex);
        switch (event) {
        case Event::DitDown:
            m_stamps.ditDown = stamp;
            break;
        case Event::DitUp:
            m_stamps.ditUp = stamp;
            break;
        case Event::DahDown:
            m_stamps.dahDown = stamp;
            break;
        case Event::DahUp:
            m_stamps.dahUp = stamp;
            break;
        case Event::Close:
        case Event::Release:
        case Event::RestoreMonitor:
            break;
        }
        m_history.push_back({stamp, event});
        while (m_history.size() > kHistoryLength)
            m_history.pop_front();
    }
    pushEvent({event, stamp});
}

// Caller holds m_firstElementMutex.
void PaddleKeyer::startBurstSidetoneLocked(Event event, double timestamp, double pressed) {
    char element;
    if (event == Event::DitDown)
        element = '.';
    else if (event == Event::DahDown)
        element = '-';
    else
        return;
    if (m_closed || m_inBurst || m_firstElementScheduled)
        return;
    if (m_sidetone.scheduleDown(timestamp))
        m_firstElementScheduled = FirstElement{timestamp, element, pressed};
}

std::optional<PaddleKeyer::FirstElement> PaddleKeyer::takeFirstElement() {
    std::lock_guard lock(m_firstElementMutex);
    auto scheduled = m_firstElementScheduled;
    m_firstElementScheduled.reset();
    return scheduled;
}

// ---- keyer loop ----

void PaddleKeyer::run() {
    while (processNextEvent() != Event::Close) {
    }
}

// WHY wait first and dispatch after: the mode in force is the one when the event arrives, not the
// one when the thread started waiting. K4-Companion gets the same by closing and reopening its
// keyer whenever straight keying is switched (set_device_type).
PaddleKeyer::Event PaddleKeyer::processNextEvent() noexcept {
    PaddleEventItem item;
    bool restoreWaitTimedOut = false;
    while (true) {
        const bool released = applyPendingReleases();
        if (m_events.pop(item))
            break;
        if (m_requests.pop(item)) {
            if (item.event == Event::Close) {
                // Whatever the release before it queued (a straight key's KZU) goes out first.
                applyPendingReleases();
                PaddleEventItem last;
                while (m_events.pop(last))
                    if (last.event == Event::Release)
                        endStraightKeyOnAir(last.keyDownAt, last.timestamp);
                // WHY: the Release above can itself have been lost to a full queue. Whatever
                // happened, the keyer does not exit with the radio keyed.
                if (m_k4Keyed) {
                    double at;
                    {
                        std::lock_guard lock(m_straightMutex);
                        at = m_paddleTimestamp;
                    }
                    straightKeyUpOnAir(at, false);
                }
                return Event::Close;
            }
            break;
        }
        // WHY after the queues: an edge already queued may start a burst, which voids the restore.
        // A timed-out wait counts as due even if the clocks disagree by a hair, so it never spins.
        if (m_monitorRestoreDue && (restoreWaitTimedOut || m_clock.now() >= *m_monitorRestoreDue)) {
            m_monitorRestoreDue.reset();
            unmuteSidetoneMonitor();
            return Event::RestoreMonitor;
        }
        if (released)
            return Event::Release;
        reconcileLostEdges();
        if (!m_events.empty())
            continue;
        {
            // WHY allowed: this is where the keyer blocks between wake-ups, as a real-time thread
            // must (CONVENTIONS Rule 16). Both waits are a semaphore wait; the timed one ends at
            // the monitor restore.
            RtSan::Allow allow;
            if (m_monitorRestoreDue)
                restoreWaitTimedOut = !m_wake.waitFor(*m_monitorRestoreDue - m_clock.now());
            else
                m_wake.wait();
        }
    }
    switch (item.event) {
    case Event::Close:
        return item.event;
    case Event::Release:
        endStraightKeyOnAir(item.keyDownAt, item.timestamp);
        return item.event;
    default:
        break;
    }
    if (m_straightKeying)
        sendStraightKey(item);
    else if (mode() == Mode::Ultimatic)
        sendUltimatic();
    else
        sendIambic();
    return item.event;
}

long long PaddleKeyer::straightKeyMs(double seconds) const {
    const long long ms = pyRound(seconds * 1000);
    if (ms > 5000)
        return 0;
    return std::max<long long>(ms, m_sidetone.minimumSidetoneMs());
}

void PaddleKeyer::sendStraightKey(const PaddleEventItem &item) {
    const double timestamp = item.timestamp;
    if (item.event == Event::DitDown) {
        // WHY: a key-down while the radio is still keyed means the key-up between them was lost
        // to a full queue. End the old element first, so the radio never sees two KZDs in a row.
        if (m_k4Keyed)
            straightKeyUpOnAir(timestamp, false);
        long long ms;
        {
            std::lock_guard lock(m_straightMutex);
            ms = straightKeyMs(timestamp - m_previousKeyUpTimestamp);
            m_previousKeyDownTimestamp = timestamp;
        }
        m_k4Keyed = true;
        muteSidetoneMonitor();
        send(fourDigits("KZD", ms));
    } else if (item.event == Event::DitUp) {
        straightKeyUpOnAir(timestamp, true);
    }
}

// Keyer thread. The KZU for a straight key that went up at timestamp (paddle time), if the radio
// is keyed. restoreMonitor schedules the monitor unmute, as a normal key-up does.
void PaddleKeyer::straightKeyUpOnAir(double timestamp, bool restoreMonitor) {
    long long ms;
    {
        std::lock_guard lock(m_straightMutex);
        ms = straightKeyMs(timestamp - m_previousKeyDownTimestamp);
        m_previousKeyUpTimestamp = timestamp;
    }
    if (!std::exchange(m_k4Keyed, false))
        return;
    send(fourDigits("KZU", ms));
    if (!restoreMonitor)
        return;
    double delay = m_keyDownInitialDelayMs / 1000.0;
    delay += m_sidetone.delay();
    delay += ms / 1000.0;
    delay += kUnmuteExtraDelay;
    delayedUnmuteSidetoneMonitor(delay);
}

// Keyer thread, with nothing left to process. After an edge was lost to a full queue the queued
// edges no longer say whether the key is down, so the key itself is read: a radio still keyed for a
// key that is up gets its KZU now. A lost key-down is left lost: not keying is the safe side.
void PaddleKeyer::reconcileLostEdges() {
    if (!m_eventsLost.load(std::memory_order_relaxed))
        return;
    bool keyUp;
    {
        // Every push happens under this lock, so an empty queue here has no edge in flight.
        std::lock_guard lock(m_firstElementMutex);
        if (!m_events.empty())
            return;
        m_eventsLost.store(false, std::memory_order_relaxed);
        keyUp = !m_straightKeyDown;
    }
    if (!m_k4Keyed || !keyUp)
        return;
    double at;
    {
        std::lock_guard lock(m_straightMutex);
        at = m_paddleTimestamp;
    }
    straightKeyUpOnAir(at, true);
}

PaddleKeyer::Stamps PaddleKeyer::paddleTimestamps(std::optional<double> asOf) {
    // Every decision reads the paddles here: a release asked for since takes effect now.
    applyPendingReleases();
    std::lock_guard lock(m_paddleMutex);
    const Stamps stamps = m_stamps;
    if (!asOf)
        return stamps;
    // Walk the history newest first for each paddle's latest edge at or before asOf: the
    // paddles as they stood when the decision was due, not when the keyer got to it.
    std::optional<double> found[4];
    for (std::size_t n = m_history.size(); n-- > 0;) {
        const auto &[timestamp, event] = m_history[n];
        if (timestamp > *asOf)
            continue;
        int index;
        switch (event) {
        case Event::DitUp:
            index = 0;
            break;
        case Event::DahUp:
            index = 1;
            break;
        case Event::DitDown:
            index = 2;
            break;
        case Event::DahDown:
            index = 3;
            break;
        default:
            continue;
        }
        if (!found[index]) {
            found[index] = timestamp;
            if (found[0] && found[1] && found[2] && found[3])
                break;
        }
    }
    const double s[4] = {stamps.ditUp, stamps.dahUp, stamps.ditDown, stamps.dahDown};
    double r[4];
    for (int i = 0; i < 4; ++i)
        r[i] = found[i] ? *found[i] : (s[i] <= *asOf ? s[i] : 0);
    return {r[0], r[1], r[2], r[3]};
}

void PaddleKeyer::sendIambic() {
    const Mode mode = this->mode();
    const double ditUpMemoryDelay =
        m_ditMemoryDelayPercent.load() / 100 * (m_dahSeconds.load() + m_interElementSeconds.load());

    auto choose = [this, mode, ditUpMemoryDelay](Element prev, std::optional<double> look, std::optional<double> heard,
                                                 std::optional<double> asOf) -> Element {
        const Stamps t = paddleTimestamps(asOf);
        const bool ditHeld = t.ditDown > t.ditUp;
        const bool dahHeld = t.dahDown > t.dahUp;
        bool dit = ditHeld;
        bool dah = dahHeld;
        if (prev && mode != Mode::PlainIambic) {
            const double heardTs = *heard;
            const double lookTs = *look;
            dit = dit || (*prev == '-' && t.ditDown > heardTs);
            if (mode == Mode::IambicB) {
                const bool ditTapped = t.ditDown > lookTs && t.ditUp - lookTs > ditUpMemoryDelay;
                const bool ditSqueezeReleased = !dahHeld && t.ditUp - heardTs > ditUpMemoryDelay;
                dit = dit || (*prev == '-' && (ditTapped || ditSqueezeReleased));
                const bool dahTapped = t.dahDown > lookTs && t.dahUp > lookTs;
                const bool dahSqueezeReleased = !ditHeld && t.dahUp > heardTs;
                dah = dah || (*prev == '.' && (dahTapped || dahSqueezeReleased));
            }
        }
        if (dit && dah) {
            if (!prev)
                return t.dahDown < t.ditDown ? '-' : '.';
            return *prev == '.' ? '-' : '.';
        }
        if (dit)
            return '.';
        if (dah)
            return '-';
        return std::nullopt;
    };
    sendBurst(choose);
}

void PaddleKeyer::sendUltimatic() {
    auto choose = [this](Element, std::optional<double>, std::optional<double>, std::optional<double> asOf) -> Element {
        const Stamps t = paddleTimestamps(asOf);
        const bool dit = t.ditDown > t.ditUp;
        const bool dah = t.dahDown > t.dahUp;
        if (dit && dah)
            return t.dahDown > t.ditDown ? '-' : '.';
        if (dit)
            return '.';
        if (dah)
            return '-';
        return std::nullopt;
    };
    sendBurst(choose);
}

void PaddleKeyer::sendBurst(Chooser choose) {
    m_inBurst = true;
    Element prev;
    std::optional<double> prevLook;
    std::optional<double> prevHeard;
    bool anyElements = false;

    auto finish = [&] {
        if (anyElements) {
            double delay = m_keyDownInitialDelayMs / 1000.0;
            delay += m_sidetone.delay();
            delay += *m_endElementTimestamp - m_elementKeyedStart;
            delay += kUnmuteExtraDelay;
            delayedUnmuteSidetoneMonitor(delay);
        }
        m_inBurst = false;
    };

    while (true) {
        const bool firstElement = !prev;
        Element elt;
        if (firstElement) {
            elt = choose(prev, prevLook, prevHeard, std::nullopt);
            if (elt) {
                const Stamps t = paddleTimestamps(std::nullopt);
                m_firstElementClosedAt = *elt == '-' ? t.dahDown : t.ditDown;
            }
            if (!elt)
                elt = placedFirstElement();
            if (!elt)
                elt = missedFirstElement(m_burstEndDue);
            if (!elt) {
                finish();
                return;
            }
            m_burstEndDue.reset();
        }
        auto wanted = [&](double asOf) {
            elt = choose(prev, prevLook, prevHeard, asOf);
            return elt.has_value();
        };
        if (!startElement(firstElement, wanted)) {
            finish();
            return;
        }
        if (firstElement)
            muteSidetoneMonitor();
        const double eltTs = m_elementKeyedStart;
        waitUntilSidetoneHeard();
        const double eltHeardTs = firstElement ? eltTs : m_elementHeardStart;
        const double readDue = m_elementReadDue ? *m_elementReadDue : m_clock.now();
        const double eltLookTs = firstElement ? eltTs : readDue;
        if (const Element again = choose(prev, prevLook, prevHeard, readDue))
            elt = again;
        anyElements = true;
        endElement(*elt);
        prev = elt;
        prevLook = eltLookTs;
        prevHeard = eltHeardTs;
    }
}

PaddleKeyer::Element PaddleKeyer::missedFirstElement(std::optional<double> endedDue) {
    if (!endedDue)
        return std::nullopt;
    applyPendingReleases();
    std::lock_guard lock(m_paddleMutex);
    for (std::size_t n = 0; n < m_history.size(); ++n) {
        const auto &[timestamp, event] = m_history[n];
        if (timestamp <= *endedDue)
            continue;
        char elt;
        if (event == Event::DitDown)
            elt = '.';
        else if (event == Event::DahDown)
            elt = '-';
        else
            continue;
        m_firstElementClosedAt = timestamp;
        return elt;
    }
    return std::nullopt;
}

PaddleKeyer::Element PaddleKeyer::placedFirstElement() {
    applyPendingReleases();
    std::optional<FirstElement> scheduled;
    {
        std::lock_guard lock(m_firstElementMutex);
        scheduled = m_firstElementScheduled;
    }
    if (!scheduled)
        return std::nullopt;
    if (!m_sidetone.cancelDown())
        return scheduled->element;
    takeFirstElement();
    return std::nullopt;
}

double PaddleKeyer::wholeMs(double seconds) {
    return std::ceil(seconds * 1000) / 1000;
}

std::optional<double> PaddleKeyer::sleepUntil(double when) {
    const double wait = when - m_clock.now();
    if (wait > 0) {
        {
            // WHY allowed: the keyer's deadline wait. Elements are timed by sleeping until they are
            // due, which is blocking by design, not work done late.
            RtSan::Allow allow;
            m_clock.sleepUntil(when);
        }
        return m_clock.now() - when;
    }
    return std::nullopt;
}

bool PaddleKeyer::startElement(bool firstElement, Wanted wanted) {
    planSidetoneCatchup(firstElement);
    double keyedStart;
    double sidetoneStart;
    long long ms;
    if (firstElement) {
        double closedAt;
        if (const auto scheduled = takeFirstElement()) {
            keyedStart = scheduled->keyedStart;
            closedAt = scheduled->closedAt;
            m_elementSidetoneQueued = true;
        } else {
            keyedStart = m_clock.now();
            closedAt = m_firstElementClosedAt ? std::min(*m_firstElementClosedAt, keyedStart) : keyedStart;
            m_elementSidetoneQueued = m_sidetone.scheduleDown(keyedStart);
        }
        m_firstElementClosedAt.reset();
        m_elementTimestamp = keyedStart;
        sidetoneStart = keyedStart;
        ms = 0;
        if (m_endElementTimestamp && *m_endElementTimestamp != 0) {
            ms = pyRound((closedAt - *m_endElementTimestamp) * 1000);
            if (!(0 <= ms && ms <= 5000))
                ms = 0;
        }
    } else {
        const double interElementSeconds = wholeMs(this->interElementSeconds());
        const auto unshifted = std::make_pair(m_elementTimestamp, m_endElementTimestamp);
        double gridShift = 0.0;
        const double timeSinceEndElement = m_clock.now() - *m_endElementTimestamp;
        if (timeSinceEndElement > interElementSeconds) {
            gridShift = timeSinceEndElement - interElementSeconds + m_sidetoneDelayUsed;
            *m_elementTimestamp += gridShift;
            *m_endElementTimestamp += gridShift;
        }
        keyedStart = *m_elementTimestamp;
        sidetoneStart = keyedStart - m_sidetoneDelayUsed;
        if (!confirmElement(sidetoneStart, wanted, gridShift)) {
            m_elementTimestamp = unshifted.first;
            m_endElementTimestamp = unshifted.second;
            return false;
        }
        sleepUntil(*m_endElementTimestamp + interElementSeconds);
        ms = pyRound(this->interElementSeconds() * 1000);
    }
    m_lastKzdMs = ms;
    send(fourDigits("KZD", ms));
    m_elementKzdTimestamp = m_clock.now();
    m_elementKeyedStart = keyedStart;
    m_elementSidetoneStart = sidetoneStart;
    return true;
}

void PaddleKeyer::planSidetoneCatchup(bool firstElement) {
    const double sidetoneDelay = m_sidetone.delay();
    if (firstElement) {
        m_sidetoneDelayUsed = 0.0;
    } else {
        if (m_elementFirst && m_elementSidetoneQueued) {
            if (const auto shift = m_sidetone.downShift())
                m_sidetoneDelaySeen = *shift;
        }
        m_sidetoneDelayUsed += m_sidetonePendingStep + sidetoneDelay - m_sidetoneDelaySeen;
    }
    m_sidetoneDelaySeen = sidetoneDelay;
    m_elementFirst = firstElement;
    const double tolerableSidetoneLag = m_inherentSidetoneLag + kTolerableSidetoneLagMargin;
    double behind = 0.0;
    if (m_inherentSidetoneLag >= .001)
        behind = std::max(0.0, sidetoneDelay - tolerableSidetoneLag - m_sidetoneDelayUsed);
    m_sidetoneBehind = behind;
}

bool PaddleKeyer::confirmElement(double sidetoneStart, Wanted wanted, double gridShift) {
    const std::optional<double> checkAt = m_sidetone.downDeadline(sidetoneStart);
    const double wakeAt = checkAt ? *checkAt - kSidetoneEdgeMarginSeconds - m_wakeOverrun : *m_elementTimestamp;
    const std::optional<double> overrun = sleepUntil(wakeAt);
    const double readAt = m_clock.now();
    if (checkAt && overrun)
        m_wakeOverrun = std::min(kMaxWakeOverrun, std::max(*overrun, m_wakeOverrun * kWakeOverrunDecay));
    const double due = std::min(readAt, wakeAt - gridShift);
    if (!wanted(due)) {
        m_burstEndDue = due;
        m_elementSidetoneQueued = false;
        return false;
    }
    const bool late = checkAt && readAt > *checkAt;
    m_elementSidetoneQueued = m_sidetone.scheduleDown(sidetoneStart, late);
    return true;
}

std::pair<double, double> PaddleKeyer::elementSidetone(double elementSeconds) const {
    const double gapSeconds = wholeMs(interElementSeconds());
    const double fraction = std::min(kMaxSidetoneCatchupFraction, m_sidetoneBehind / (elementSeconds + gapSeconds));
    const double elementTrim = elementSeconds * fraction;
    return {elementSeconds - elementTrim, elementTrim + gapSeconds * fraction};
}

double PaddleKeyer::upEdgeMargin(double ditSidetoneSeconds) const {
    return std::max(0.0, ditSidetoneSeconds - m_sidetone.delay());
}

void PaddleKeyer::waitUntilSidetoneHeard() {
    m_elementHeardStart = m_elementKeyedStart + m_sidetone.delay();
    m_elementReadDue.reset();
    if (!m_elementSidetoneQueued)
        return;
    const double giveUpAt = m_elementKzdTimestamp + ditSeconds();
    double heardAt;
    while (true) {
        if (const auto h = m_sidetone.downHeardAt()) {
            heardAt = *h;
            m_elementHeardStart = heardAt;
            break;
        }
        const double now = m_clock.now();
        if (now >= giveUpAt)
            return;
        // WHY allowed: a deadline wait, as in sleepUntil.
        RtSan::Allow allow;
        m_clock.sleep(std::min(kSidetoneHeardPollSeconds, giveUpAt - now));
    }
    const double ditSidetoneSeconds = elementSidetone(wholeMs(ditSeconds())).first;
    double waitUntil = std::min(heardAt, giveUpAt);
    if (const auto queueBy = m_sidetone.edgeDeadline(heardAt + ditSidetoneSeconds))
        waitUntil = std::min(waitUntil, *queueBy - upEdgeMargin(ditSidetoneSeconds));
    m_elementReadDue = waitUntil;
    sleepUntil(waitUntil);
}

void PaddleKeyer::endElement(char element) {
    const double elementSeconds = wholeMs(element == '-' ? dahSeconds() : ditSeconds());
    const double sidetoneStart = m_elementSidetoneStart;
    const auto [sidetoneSeconds, pendingStep] = elementSidetone(elementSeconds);
    m_sidetonePendingStep = pendingStep;
    m_sidetone.scheduleUp(sidetoneStart + sidetoneSeconds);
    sleepUntil(m_elementKzdTimestamp + elementSeconds);
    send(fourDigits("KZU", pyRound(elementSeconds * 1000)));
    if ((m_lastKzdMs && *m_lastKzdMs == 0) || m_inherentSidetoneLag < .001)
        measureInherentSidetoneLag();
    m_endElementTimestamp = m_elementKeyedStart + elementSeconds;
    *m_elementTimestamp += elementSeconds + interElementSeconds();
}

void PaddleKeyer::measureInherentSidetoneLag() {
    double lag = m_sidetone.delay();
    if (m_inherentSidetoneLag >= .001)
        lag = std::min(lag, m_inherentSidetoneLag);
    if (const auto floor = m_sidetone.delayFloor())
        lag = std::max(lag, *floor);
    m_inherentSidetoneLag = lag;
}

// ---- K4 link and monitor ----

void PaddleKeyer::send(std::string_view command) {
    m_link.send(command);
}

void PaddleKeyer::muteSidetoneMonitor() {
    if (!m_muteRadioMonitor)
        return;
    m_monitorRestoreDue.reset();
    if (!std::exchange(m_monitorMuted, true))
        send("ML0000;");
}

void PaddleKeyer::unmuteSidetoneMonitor() {
    send(formatFrame("ML0", m_monitorLevel.load(), 3));
    m_monitorMuted = false;
}

void PaddleKeyer::delayedUnmuteSidetoneMonitor(double delay) {
    if (!m_muteRadioMonitor)
        return;
    // WHY a deadline the keyer waits for, not a timer: ML is sent only from the keyer thread, and
    // handing a callback to a timer on another thread allocates on this one.
    m_monitorRestoreDue = m_clock.now() + delay;
}
