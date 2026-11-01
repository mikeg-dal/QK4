// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sample-accurate sidetone edge placement, ported from the Audio class of Dale Farnsworth's
// K4-Companion (GPL-3.0, https://github.com/DaleFarnsworth/K4-Companion). The keyer asks for "key
// down at T" on its own clock; the audio output callback places that edge on an exact frame of
// the block it is filling, against the block's play-out (DAC) time. The delay from keying to
// hearing is fixed at the first edge of a stream (lead + one block). It never ratchets up on edges
// that were late for reasons other than the audio, and eases back 1 ms per second of silence.
//
// Two sides, two threads: the keyer side (scheduleDown/Up, heard/deadline queries, from the keyer
// and paddle-device threads) and the callback side (noteBlockTiming, nextKeyEdge, from the audio
// thread). See docs/cw-keyer-port-plan.md, "Sidetone".

#ifndef SIDETONESCHEDULER_H
#define SIDETONESCHEDULER_H

#include "hardware/keyertiming.h"
#include "utils/ringdeque.h"
#include "utils/rtsan.h"

#include <functional>
#include <mutex>
#include <optional>
#include <utility>

class SidetoneScheduler {
public:
    // Current time on the audio stream's clock, in seconds; nullopt while no stream is open.
    using StreamTimeFn = std::function<std::optional<double>()>;

    struct Edge {
        int offset; // frame within the block
        bool down;
    };

    SidetoneScheduler(CwKeying::Clock &clock, StreamTimeFn streamTime);

    // The stream clock, once the output that owns it exists. Replaces the constructor's.
    void setStreamTimeSource(StreamTimeFn streamTime);

    // Shortest tone a straight key is allowed to produce, in ms: half the rise plus half the fall
    // (4 ms with the 4 ms ramps). K4-Companion's minimum_sidetone_ms.
    void setMinimumSidetoneMs(int ms);
    int minimumSidetoneMs() const;

    // ---- keyer side (keyer thread, device thread) ----
    bool scheduleDown(double downAt, bool late = false) noexcept QK4_NONBLOCKING;
    void scheduleUp(double upAt) noexcept QK4_NONBLOCKING;
    void keyNow(bool down) noexcept QK4_NONBLOCKING; // straight key: key as soon as the next block allows
    std::optional<double> downHeardAt() const noexcept QK4_NONBLOCKING;
    std::optional<double> downDeadline(double downAt) const noexcept QK4_NONBLOCKING;
    std::optional<double> edgeDeadline(double heardAt,
                                       std::optional<double> offset = std::nullopt) const noexcept QK4_NONBLOCKING;
    // True when the edge was dropped (or never asked for) rather than placed.
    bool cancelDown() noexcept QK4_NONBLOCKING;
    double delay() const noexcept QK4_NONBLOCKING;
    std::optional<double> delayFloor() const noexcept QK4_NONBLOCKING;
    std::optional<double> downShift() const noexcept QK4_NONBLOCKING;

    // ---- callback side (audio thread) ----
    void noteBlockTiming(double dacTime, double currentTime, double blockSeconds) noexcept QK4_NONBLOCKING;
    std::optional<Edge> nextKeyEdge(int start, int frames, double dacTime, int sampleRate) noexcept QK4_NONBLOCKING;
    void easeLag(double seconds) noexcept QK4_NONBLOCKING;

    // ---- state the harness and the mixer inspect (the mixer per block, on the audio thread) ----
    bool pendingEdgeHeld() const noexcept QK4_NONBLOCKING;
    bool queueEmpty() const noexcept QK4_NONBLOCKING;
    double lag() const;
    void setLag(double seconds);
    void clear(); // stream opened or replaced: drop queued edges and every timing estimate

private:
    struct BlockTiming {
        double dac;
        double block;
        double lead;
    };

    // All private helpers expect m_mutex held.
    std::optional<std::pair<double, double>> edgeClockOffsetLocked() const;
    std::optional<std::pair<double, double>> placedDownLocked() const;
    std::optional<double> wantedLagLocked() const;
    std::optional<double> firstEdgeLagLocked() const;
    std::optional<double> edgeDeadlineLocked(double heardAt, std::optional<double> offset) const;
    std::optional<std::pair<double, bool>> peekKeyEventLocked();
    void clearLocked();

    CwKeying::Clock &m_clock;
    StreamTimeFn m_streamTime;

    // WHY one mutex rather than lock-free snapshots: K4-Companion leans on the GIL to make each
    // field read and tuple assignment atomic. A short mutex gives the same guarantee with the
    // same code shape. It is never held across a sleep, a send or a call out, so the audio
    // callback waits at most a few hundred nanoseconds.
    mutable RtSan::BoundedMutex m_mutex;

    int m_minimumSidetoneMs = 0;

    // (stream time, down). WHY 256: a handful are queued per element, and the audio thread drains
    // them every block, so this never grows unless the sidetone stream has stopped.
    RingDeque<std::pair<double, bool>> m_keyEvents{256};
    std::optional<std::pair<double, bool>> m_pendingKeyEvent;

    double m_lag = 0.0;
    std::optional<double> m_edgeShift;
    std::optional<double> m_scheduleOffset;
    std::optional<double> m_downAsked;
    std::optional<std::pair<double, double>> m_downPlaced; // (asked, placed at)
    std::optional<double> m_downCancelled;
    std::optional<double> m_downDropped;
    std::optional<double> m_downLate;

    std::optional<BlockTiming> m_blockTiming;
    double m_blockLeadCurrent = 0.0;
    double m_blockLeadPrevious = 0.0;
    std::optional<double> m_blockLeadWindowEnd;
    std::optional<double> m_blockDacForSkip;
    std::optional<double> m_prevBlockDacForSkip;

    static constexpr double kBlockLeadWindowSeconds = .5;
    static constexpr double kCancelPollSeconds = .001;
    static constexpr double kCancelWaitSeconds = .1;
    static constexpr double kLagEasePerSecond = .001;
};

#endif // SIDETONESCHEDULER_H
