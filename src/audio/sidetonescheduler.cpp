// SPDX-License-Identifier: GPL-3.0-or-later
//
// Port of K4-Companion's sidetone edge scheduling (Dale Farnsworth, GPL-3.0). Function by function
// against k4companion: edge_clock_offset 9122, schedule_sidetone_down/up 9165/9207,
// sidetone_down_heard_at 9222, sidetone_down_deadline 9239, cancel_sidetone_down 9260,
// sidetone_edge_deadline 9293, sidetone_delay 9339, sidetone_delay_floor 9358,
// sidetone_down_shift 9375, placed_sidetone_down 9387, key_sidetone 9402, note_block_timing 10473,
// wanted_sidetone_lag 10515, first_edge_lag 10530, ease_sidetone_lag 10538, next_key_edge 10568,
// clear_sidetone_state 11001. Expressions keep the Python's order so the doubles match.

#include "audio/sidetonescheduler.h"

#include <algorithm>
#include <cmath>

using CwKeying::pyRound;

SidetoneScheduler::SidetoneScheduler(CwKeying::Clock &clock, StreamTimeFn streamTime)
    : m_clock(clock), m_streamTime(std::move(streamTime)) {}

void SidetoneScheduler::setStreamTimeSource(StreamTimeFn streamTime) {
    std::lock_guard lock(m_mutex);
    m_streamTime = std::move(streamTime);
}

void SidetoneScheduler::setMinimumSidetoneMs(int ms) {
    std::lock_guard lock(m_mutex);
    m_minimumSidetoneMs = ms;
}

int SidetoneScheduler::minimumSidetoneMs() const {
    std::lock_guard lock(m_mutex);
    return m_minimumSidetoneMs;
}

// ---- keyer side ----

std::optional<std::pair<double, double>> SidetoneScheduler::edgeClockOffsetLocked() const {
    const std::optional<double> streamNow = m_streamTime ? m_streamTime() : std::nullopt;
    if (!streamNow)
        return std::nullopt;
    const double streamOffset = *streamNow - m_clock.now();
    return std::make_pair(streamOffset, streamOffset);
}

bool SidetoneScheduler::scheduleDown(double downAt, bool late) noexcept {
    std::lock_guard lock(m_mutex);
    const auto offsets = edgeClockOffsetLocked();
    if (!offsets) {
        m_scheduleOffset.reset();
        return false;
    }
    m_scheduleOffset = offsets->first;
    m_downAsked = downAt + *m_scheduleOffset;
    m_downLate = late ? m_downAsked : std::nullopt;
    m_keyEvents.push_back({*m_downAsked, true});
    return true;
}

void SidetoneScheduler::scheduleUp(double upAt) noexcept {
    std::lock_guard lock(m_mutex);
    if (!m_scheduleOffset)
        return;
    const double offset = *m_scheduleOffset;
    m_scheduleOffset.reset();
    m_keyEvents.push_back({upAt + offset, false});
}

void SidetoneScheduler::keyNow(bool down) noexcept {
    std::lock_guard lock(m_mutex);
    const auto offsets = edgeClockOffsetLocked();
    if (!offsets)
        return;
    double eventTime = m_clock.now() + offsets->first;
    if (m_blockTiming)
        eventTime += m_blockTiming->lead + m_blockTiming->block;
    m_keyEvents.push_back({eventTime, down});
}

std::optional<std::pair<double, double>> SidetoneScheduler::placedDownLocked() const {
    if (!m_downPlaced || m_downPlaced->first != m_downAsked)
        return std::nullopt;
    return m_downPlaced;
}

std::optional<double> SidetoneScheduler::downHeardAt() const noexcept {
    std::lock_guard lock(m_mutex);
    const auto placed = placedDownLocked();
    if (!m_scheduleOffset || !placed)
        return std::nullopt;
    return placed->second - *m_scheduleOffset;
}

std::optional<double> SidetoneScheduler::downDeadline(double downAt) const noexcept {
    std::lock_guard lock(m_mutex);
    const auto offsets = edgeClockOffsetLocked();
    if (!offsets)
        return std::nullopt;
    return edgeDeadlineLocked(downAt + m_lag, offsets->first);
}

bool SidetoneScheduler::cancelDown() noexcept {
    double giveUpAt;
    std::optional<double> asked;
    {
        std::lock_guard lock(m_mutex);
        asked = m_downAsked;
        if (!asked)
            return true;
        m_downCancelled = asked;
    }
    giveUpAt = m_clock.now() + kCancelWaitSeconds;
    while (true) {
        {
            std::lock_guard lock(m_mutex);
            if (placedDownLocked())
                return false;
            if (m_downDropped == asked)
                return true;
        }
        if (m_clock.now() >= giveUpAt)
            return true;
        // WHY allowed: the keyer waits here, for at most kCancelWaitSeconds, for the audio thread
        // to place or drop the edge. A deadline wait on the keyer thread, as in its sleepUntil.
        RtSan::Allow allow;
        m_clock.sleep(kCancelPollSeconds);
    }
}

std::optional<double> SidetoneScheduler::edgeDeadlineLocked(double heardAt, std::optional<double> offset) const {
    if (!offset)
        offset = m_scheduleOffset;
    if (!m_blockTiming || !offset)
        return std::nullopt;
    const auto &t = *m_blockTiming;
    const double blocks = std::floor((heardAt + *offset - t.dac) / t.block);
    return t.dac + blocks * t.block - t.lead - *offset;
}

std::optional<double> SidetoneScheduler::edgeDeadline(double heardAt, std::optional<double> offset) const noexcept {
    std::lock_guard lock(m_mutex);
    return edgeDeadlineLocked(heardAt, offset);
}

double SidetoneScheduler::delay() const noexcept {
    std::lock_guard lock(m_mutex);
    return m_edgeShift ? *m_edgeShift : m_lag;
}

std::optional<double> SidetoneScheduler::delayFloor() const noexcept {
    std::lock_guard lock(m_mutex);
    return wantedLagLocked();
}

std::optional<double> SidetoneScheduler::downShift() const noexcept {
    std::lock_guard lock(m_mutex);
    const auto placed = placedDownLocked();
    if (!placed)
        return std::nullopt;
    return placed->second - placed->first;
}

// ---- callback side ----

void SidetoneScheduler::noteBlockTiming(double dacTime, double currentTime, double blockSeconds) noexcept {
    std::lock_guard lock(m_mutex);
    const double lead = dacTime - currentTime;
    if (!m_blockLeadWindowEnd || dacTime >= *m_blockLeadWindowEnd) {
        m_blockLeadPrevious = m_blockLeadCurrent;
        m_blockLeadCurrent = lead;
        m_blockLeadWindowEnd = dacTime + kBlockLeadWindowSeconds;
    } else if (lead > m_blockLeadCurrent) {
        m_blockLeadCurrent = lead;
    }
    m_blockTiming = BlockTiming{dacTime, blockSeconds, std::max(m_blockLeadPrevious, m_blockLeadCurrent)};
    m_prevBlockDacForSkip = m_blockDacForSkip;
    m_blockDacForSkip = dacTime;
}

std::optional<double> SidetoneScheduler::wantedLagLocked() const {
    if (!m_blockTiming)
        return std::nullopt;
    return m_blockTiming->lead + m_blockTiming->block;
}

std::optional<double> SidetoneScheduler::firstEdgeLagLocked() const {
    if (m_lag != 0.0)
        return std::nullopt;
    return wantedLagLocked();
}

void SidetoneScheduler::easeLag(double seconds) noexcept {
    std::lock_guard lock(m_mutex);
    const auto wanted = wantedLagLocked();
    if (!wanted || m_lag <= *wanted)
        return;
    m_lag = std::max(*wanted, m_lag - kLagEasePerSecond * seconds);
}

std::optional<std::pair<double, bool>> SidetoneScheduler::peekKeyEventLocked() {
    if (!m_pendingKeyEvent && !m_keyEvents.empty()) {
        m_pendingKeyEvent = m_keyEvents.front();
        m_keyEvents.pop_front();
    }
    return m_pendingKeyEvent;
}

std::optional<SidetoneScheduler::Edge> SidetoneScheduler::nextKeyEdge(int start, int frames, double dacTime,
                                                                      int sampleRate) noexcept {
    std::lock_guard lock(m_mutex);
    bool dragged = false;
    double eventTime = 0;
    bool keyDown = false;
    long long offset = 0;
    while (true) {
        const auto event = peekKeyEventLocked();
        if (!event)
            return std::nullopt;
        eventTime = event->first;
        keyDown = event->second;
        offset = pyRound((eventTime + m_lag - dacTime) * sampleRate);
        if (offset >= frames)
            return std::nullopt; // left pending, for a later block
        m_pendingKeyEvent.reset();
        if (!keyDown || eventTime != m_downCancelled)
            break;
        m_downDropped = eventTime;
    }
    if (offset < start) {
        const auto fixed = keyDown ? firstEdgeLagLocked() : std::nullopt;
        const double soonest = dacTime + static_cast<double>(start) / sampleRate - eventTime;
        if (fixed && *fixed >= soonest) {
            m_lag = *fixed;
            offset = pyRound((eventTime + *fixed - dacTime) * sampleRate);
            if (offset >= frames) {
                m_pendingKeyEvent = std::make_pair(eventTime, keyDown);
                return std::nullopt;
            }
        } else if (keyDown && m_downLate && eventTime == *m_downLate) {
            dragged = true;
            offset = start;
        } else if (keyDown) {
            const double blockSeconds = m_blockTiming ? m_blockTiming->block : 0.004;
            const bool skipped = m_prevBlockDacForSkip && m_blockDacForSkip &&
                                 *m_blockDacForSkip - *m_prevBlockDacForSkip > blockSeconds * 1.5;
            if (skipped) {
                dragged = true;
                offset = start;
            } else {
                m_lag += static_cast<double>(start - offset) / sampleRate;
                offset = start;
            }
        } else {
            dragged = true;
            offset = start;
        }
    }
    const double placedAt = dacTime + static_cast<double>(offset) / sampleRate;
    if (!dragged)
        m_edgeShift = placedAt - eventTime;
    if (keyDown)
        m_downPlaced = std::make_pair(eventTime, placedAt);
    return Edge{static_cast<int>(offset), keyDown};
}

// ---- state ----

bool SidetoneScheduler::pendingEdgeHeld() const noexcept {
    std::lock_guard lock(m_mutex);
    return m_pendingKeyEvent.has_value();
}

bool SidetoneScheduler::queueEmpty() const noexcept {
    std::lock_guard lock(m_mutex);
    return m_keyEvents.empty();
}

double SidetoneScheduler::lag() const {
    std::lock_guard lock(m_mutex);
    return m_lag;
}

void SidetoneScheduler::setLag(double seconds) {
    std::lock_guard lock(m_mutex);
    m_lag = seconds;
}

void SidetoneScheduler::clear() {
    std::lock_guard lock(m_mutex);
    clearLocked();
}

void SidetoneScheduler::clearLocked() {
    m_keyEvents.clear();
    m_pendingKeyEvent.reset();
    m_downCancelled.reset();
    m_downDropped.reset();
    m_downLate.reset();
    m_lag = 0.0;
    m_edgeShift.reset();
    m_blockTiming.reset();
    m_blockDacForSkip.reset();
    m_prevBlockDacForSkip.reset();
    m_blockLeadCurrent = 0.0;
    m_blockLeadPrevious = 0.0;
    m_blockLeadWindowEnd.reset();
}
