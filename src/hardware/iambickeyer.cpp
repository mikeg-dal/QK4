#include "hardware/iambickeyer.h"

#include <QLoggingCategory>
#include <QTime>

// Trace category for the CW keyer state machine. Disabled by default; enable with
// QT_LOGGING_RULES="cw.keyer.debug=true" to capture paddle edges, element-start decisions,
// timer-expiry decisions, and idle transitions. Pair with cat.tx category to correlate
// keyer state with on-wire KZ commands when diagnosing extra-dit reports.
Q_LOGGING_CATEGORY(cwKeyer, "cw.keyer")

namespace {
QString nowMs() {
    return QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz"));
}
} // namespace

IambicKeyer::IambicKeyer(QObject *parent) : QObject(parent) {
    m_elementTimer = new QTimer(this);
    m_elementTimer->setSingleShot(true);
    m_elementTimer->setTimerType(Qt::PreciseTimer);
    connect(m_elementTimer, &QTimer::timeout, this, &IambicKeyer::onTimerFired);
    m_pressClock.start();
}

void IambicKeyer::setEnabled(bool enabled) {
    m_enabled = enabled;
    if (!enabled)
        stop();
}

void IambicKeyer::setMode(Mode mode) {
    m_mode = mode;
}

void IambicKeyer::setReversed(bool reversed) {
    m_reversed = reversed;
}

void IambicKeyer::setSpeed(int wpm) {
    if (wpm > 0)
        m_ditMs = 1200 / wpm;
}

void IambicKeyer::setHoldGateEnabled(bool enabled) {
    m_holdGateEnabled.store(enabled, std::memory_order_release);
}

namespace {
// Latch bookkeeping for one lever. Shared by every paddle entry point so the hold gate and the
// press-timestamp rules cannot drift between them.
//
// Returns the hold duration on a release (-1 on a press) and reports whether the hold gate
// rejected it, both purely so the caller can trace the decision.
qint64 updateLatch(std::atomic<bool> &latch, std::atomic<qint64> &pressNs, bool pressed, qint64 now, bool holdGate,
                   qint64 minHoldNs, bool &bounceFiltered) {
    bounceFiltered = false;
    if (pressed) {
        // Only stamp the press timestamp on a fresh transition unset→set. A bounce-press
        // arriving while the latch is already true must NOT overwrite the legitimate press time
        // (that would let a quick subsequent release wrongly clear a real latch).
        if (!latch.exchange(true, std::memory_order_acq_rel))
            pressNs.store(now, std::memory_order_release);
        return -1;
    }

    // Release: hold-duration gate (V1.4 serial only — disabled for MIDI, see setHoldGateEnabled).
    // Anything shorter than minHoldNs is treated as bounce or accidental graze and the latch is
    // cleared so the next element timer doesn't fire a phantom element. See
    // docs/halikey-cw-trace.md for the captured signatures. The hold duration is computed
    // unconditionally so the trace logs it for both transports.
    const qint64 holdNs = now - pressNs.load(std::memory_order_acquire);
    if (holdGate && holdNs < minHoldNs) {
        latch.store(false, std::memory_order_release);
        bounceFiltered = true;
    }
    return holdNs;
}
} // namespace

void IambicKeyer::traceLine(const char *line, bool pressed, bool bounceFiltered, qint64 holdNs) const {
    if (!cwKeyer().isDebugEnabled())
        return;
    const quint8 phys = m_phys.load(std::memory_order_acquire);
    qCDebug(cwKeyer).noquote().nospace() << "KEYER@" << nowMs() << " [" << line << (pressed ? " down" : " up")
                                         << (bounceFiltered ? " bounce-filtered" : "") << "] state=" << m_state
                                         << " physD=" << ((phys & kDitBit) != 0) << " physA=" << ((phys & kDahBit) != 0)
                                         << " latchD=" << m_ditLatch.load(std::memory_order_acquire)
                                         << " latchA=" << m_dahLatch.load(std::memory_order_acquire)
                                         << (holdNs >= 0 ? QString(" hold=%1ms").arg(holdNs / 1'000'000.0, 0, 'f', 1)
                                                         : QString());
}

void IambicKeyer::setPaddleState(bool dit, bool dah) {
    // WHY one store for both levers: this is the entry point the transports use when a single
    // hardware sample yielded both. Writing the pair in one store means the keyer thread can never
    // observe a half-applied release — the case that made a released squeeze look like one lever
    // still held and appended an element nobody keyed.
    const qint64 now = m_pressClock.nsecsElapsed();
    const quint8 packed = static_cast<quint8>((dit ? kDitBit : 0) | (dah ? kDahBit : 0));
    m_phys.store(packed, std::memory_order_release);

    const bool holdGate = m_holdGateEnabled.load(std::memory_order_acquire);
    bool ditFiltered = false;
    bool dahFiltered = false;
    const qint64 ditHoldNs = updateLatch(m_ditLatch, m_ditPressNs, dit, now, holdGate, kMinHoldNs, ditFiltered);
    const qint64 dahHoldNs = updateLatch(m_dahLatch, m_dahPressNs, dah, now, holdGate, kMinHoldNs, dahFiltered);

    traceLine("DIT", dit, ditFiltered, ditHoldNs);
    traceLine("DAH", dah, dahFiltered, dahHoldNs);

    // One wake-up for the pair. handlePaddleChange re-reads the atomic, so a single post covers
    // both levers.
    QMetaObject::invokeMethod(this, &IambicKeyer::handlePaddleChange, Qt::QueuedConnection);
}

void IambicKeyer::setDitPaddle(bool pressed) {
    // Single-lever entry: only for callers that genuinely learn one lever at a time (the V1.4
    // PTT demux, and mode-change cleanup). Read-modify-write keeps the other lever's bit intact.
    const qint64 now = m_pressClock.nsecsElapsed();
    if (pressed)
        m_phys.fetch_or(kDitBit, std::memory_order_acq_rel);
    else
        m_phys.fetch_and(static_cast<quint8>(~kDitBit), std::memory_order_acq_rel);

    bool bounceFiltered = false;
    const qint64 holdNs = updateLatch(m_ditLatch, m_ditPressNs, pressed, now,
                                      m_holdGateEnabled.load(std::memory_order_acquire), kMinHoldNs, bounceFiltered);
    traceLine("DIT", pressed, bounceFiltered, holdNs);

    QMetaObject::invokeMethod(this, &IambicKeyer::handlePaddleChange, Qt::QueuedConnection);
}

void IambicKeyer::setDahPaddle(bool pressed) {
    const qint64 now = m_pressClock.nsecsElapsed();
    if (pressed)
        m_phys.fetch_or(kDahBit, std::memory_order_acq_rel);
    else
        m_phys.fetch_and(static_cast<quint8>(~kDahBit), std::memory_order_acq_rel);

    bool bounceFiltered = false;
    const qint64 holdNs = updateLatch(m_dahLatch, m_dahPressNs, pressed, now,
                                      m_holdGateEnabled.load(std::memory_order_acquire), kMinHoldNs, bounceFiltered);
    traceLine("DAH", pressed, bounceFiltered, holdNs);

    QMetaObject::invokeMethod(this, &IambicKeyer::handlePaddleChange, Qt::QueuedConnection);
}

bool IambicKeyer::ditDown() const {
    const quint8 phys = m_phys.load(std::memory_order_acquire);
    return (phys & (m_reversed ? kDahBit : kDitBit)) != 0;
}

bool IambicKeyer::dahDown() const {
    const quint8 phys = m_phys.load(std::memory_order_acquire);
    return (phys & (m_reversed ? kDitBit : kDahBit)) != 0;
}

void IambicKeyer::handlePaddleChange() {
    if (!m_enabled)
        return;

    bool dit = ditDown() ||
               (m_reversed ? m_dahLatch.load(std::memory_order_acquire) : m_ditLatch.load(std::memory_order_acquire));
    bool dah = dahDown() ||
               (m_reversed ? m_ditLatch.load(std::memory_order_acquire) : m_dahLatch.load(std::memory_order_acquire));

    // Track squeeze state during active element.
    //
    // WHY live levers and not the latch-inclusive `dit`/`dah` above: a squeeze is a physical fact —
    // both levers down at once. This slot is queued, so it evaluates a snapshot taken when it runs,
    // by which time a latch set by a later press can sit beside a stale live read and assert a
    // squeeze that never happened. In Iambic B that fabricates the trailing element directly. This
    // now matches enterElement(), which has always used the live pair.
    if (m_state != Idle && ditDown() && dahDown())
        m_squeezed = true;

    // Start keying if idle and any paddle is down
    if (m_state == Idle) {
        if (dit && !dah)
            enterElement(true);
        else if (dah && !dit)
            enterElement(false);
        else if (dit && dah)
            enterElement(true); // squeeze from idle starts with dit
    }
}

void IambicKeyer::enterElement(bool isDit) {
    // Captured BEFORE m_state is assigned below — anchors the deadline grid and gates the
    // pause emit. Anchoring on every element instead would silently reintroduce the drift.
    const bool fromIdle = (m_state == Idle);

    // Transitioning from idle — emit pause duration before the element
    if (fromIdle && m_idleSince.isValid()) {
        int elapsed = static_cast<int>(m_idleSince.elapsed());
        if (elapsed <= 2000) {
            qCDebug(cwKeyer).noquote().nospace() << "KEYER@" << nowMs() << " [PAUSE emit] elapsed=" << elapsed << "ms";
            emit restartAfterPause(elapsed);
        } else {
            qCDebug(cwKeyer).noquote().nospace()
                << "KEYER@" << nowMs() << " [PAUSE skip] elapsed=" << elapsed << "ms (>2000)";
        }
    }

    m_state = isDit ? PlayingDit : PlayingDah;
    m_squeezed = false;

    // WHY only the same-element latch clears here: the latch we consume represents the
    // just-played element's paddle; leaving the opposite latch alone preserves any
    // cross-paddle press that happened during the previous element. Without this, brief
    // Iambic-A taps would be dropped — the paddle released before the element timer fired
    // but the opposite-paddle latch is what lets the keyer still emit the character.
    if (isDit != m_reversed)
        m_ditLatch.store(false, std::memory_order_relaxed);
    else
        m_dahLatch.store(false, std::memory_order_relaxed);

    // Re-check current paddles for squeeze detection within this element
    if (ditDown() && dahDown())
        m_squeezed = true;

    // Dit = 1 unit on + 1 unit off = 2 ditMs; Dah = 3 units on + 1 unit off = 4 ditMs
    const int intervalMs = isDit ? m_ditMs * 2 : m_ditMs * 4;

    // WHY a deadline grid instead of m_elementTimer->start(intervalMs): a restarted
    // single-shot timer accumulates overshoot + processing time on every element, so long
    // element strings drift slow (~1 ms/element on Windows) and the KZ stream + sidetone
    // inherit the jitter. Chaining on an absolute ns deadline lets element N's overshoot
    // shorten element N+1's arm time instead — rounding errors never accumulate. A WPM
    // change mid-chain extends the grid by the new interval from the current boundary,
    // matching the K4's KZL semantics.
    const qint64 intervalNs = qint64(intervalMs) * 1'000'000;
    const qint64 now = m_pressClock.nsecsElapsed();
    if (fromIdle)
        m_nextDeadlineNs = now + intervalNs; // anchor a fresh grid
    else
        m_nextDeadlineNs += intervalNs; // chain on the absolute grid
    qint64 remainNs = m_nextDeadlineNs - now;
    if (remainNs <= 0) {
        // WHY re-anchor: a stall >= one full element can't be caught up without
        // machine-gunning catch-up elements; slip the grid to now instead.
        m_nextDeadlineNs = now;
        remainNs = 0;
    }
    const int armMs = static_cast<int>((remainNs + 500'000) / 1'000'000); // round to nearest ms
    m_elementTimer->start(armMs);

    qCDebug(cwKeyer).noquote().nospace() << "KEYER@" << nowMs() << " [ELEM start " << (isDit ? "DIT" : "DAH")
                                         << "] interval=" << intervalMs << "ms armed=" << armMs
                                         << "ms physD=" << ditDown() << " physA=" << dahDown()
                                         << " latchD=" << m_ditLatch.load(std::memory_order_acquire)
                                         << " latchA=" << m_dahLatch.load(std::memory_order_acquire)
                                         << " squeezed=" << m_squeezed;

    emit elementStarted(isDit);
}

void IambicKeyer::onTimerFired() {
    bool liveDit = ditDown();
    bool liveDah = dahDown();

    // Check both live paddle state AND latch (paddle was pressed during this element).
    // Acquire ordering pairs with setDitPaddle/setDahPaddle's release stores from the
    // HaliKey worker thread. Reads of latches stored on this same (keyer) thread don't
    // need a barrier — but the keyer can't distinguish at the load site, so the stricter
    // acquire is used uniformly. Trivially compiles to the same code as relaxed on x86
    // and a one-instruction barrier on ARM.
    bool latchDit =
        m_reversed ? m_dahLatch.load(std::memory_order_acquire) : m_ditLatch.load(std::memory_order_acquire);
    bool latchDah =
        m_reversed ? m_ditLatch.load(std::memory_order_acquire) : m_dahLatch.load(std::memory_order_acquire);
    bool dit = liveDit || latchDit;
    bool dah = liveDah || latchDah;
    bool wasDit = (m_state == PlayingDit);

    auto traceDecision = [&](const char *branch) {
        qCDebug(cwKeyer).noquote().nospace()
            << "KEYER@" << nowMs() << " [TIMER fired wasDit=" << wasDit << "] liveD=" << liveDit << " liveA=" << liveDah
            << " latchD=" << latchDit << " latchA=" << latchDah << " squeezed=" << m_squeezed
            << " mode=" << (m_mode == IambicB ? "B" : "A") << " → " << branch;
    };

    // Squeeze release: both paddles physically released while squeeze was active.
    // Bypass latches — use Iambic A/B mode rules instead.  Without this guard,
    // a stale opposite-paddle latch would produce an unwanted extra element in
    // Iambic A mode.
    if (m_squeezed && !liveDit && !liveDah) {
        if (m_mode == IambicB) {
            traceDecision("squeeze-release IambicB → opposite element");
            enterElement(!wasDit);
        } else {
            traceDecision("squeeze-release IambicA → idle");
            goIdle();
        }
        return;
    }

    if (dit && dah) {
        traceDecision("both held → alternate");
        enterElement(!wasDit);
    } else if (wasDit && dah) {
        traceDecision("cross-paddle dit→dah");
        enterElement(false);
    } else if (!wasDit && dit) {
        traceDecision("cross-paddle dah→dit");
        enterElement(true);
    } else if (wasDit && dit) {
        traceDecision("same-paddle repeat dit");
        enterElement(true);
    } else if (!wasDit && dah) {
        traceDecision("same-paddle repeat dah");
        enterElement(false);
    } else {
        traceDecision("nothing held → idle");
        goIdle();
    }
}

void IambicKeyer::goIdle() {
    m_state = Idle;
    m_elementTimer->stop();
    m_squeezed = false;
    m_ditLatch.store(false, std::memory_order_relaxed);
    m_dahLatch.store(false, std::memory_order_relaxed);
    m_idleSince.start();

    qCDebug(cwKeyer).noquote().nospace() << "KEYER@" << nowMs() << " [IDLE]";

    emit characterSpace();
    emit keyingFinished();
}

void IambicKeyer::stop() {
    if (m_state != Idle) {
        m_state = Idle;
        m_elementTimer->stop();
        m_squeezed = false;
        m_phys.store(0, std::memory_order_relaxed);
        m_ditLatch.store(false, std::memory_order_relaxed);
        m_dahLatch.store(false, std::memory_order_relaxed);
        emit keyingFinished();
    }
}

bool IambicKeyer::isKeying() const {
    return m_state != Idle;
}
