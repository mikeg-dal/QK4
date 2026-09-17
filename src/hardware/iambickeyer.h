#ifndef IAMBICKEYER_H
#define IAMBICKEYER_H

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <atomic>

/**
 * @brief Iambic A/B CW keyer state machine. Lives on `HardwareController::m_keyerThread`
 *        (HighPriority). Accepts physical paddle state from HaliKey via atomics
 *        (DirectConnection-safe from any thread), emits `elementStarted(isDit)` /
 *        `characterSpace()` / `restartAfterPause(ms)` / `keyingFinished()` for MainWindow to
 *        dispatch K4 KZ commands (see `memory/kz-protocol.md`).
 *
 * Latch semantics (see `iambickeyer.cpp::enterElement`): opposite-paddle latch preserves
 * brief taps that ended before the element timer fired — required for Iambic-A correctness.
 */
class IambicKeyer : public QObject {
    Q_OBJECT

public:
    enum Mode { IambicA, IambicB };
    Q_ENUM(Mode)

    explicit IambicKeyer(QObject *parent = nullptr);

    // All public methods are Q_INVOKABLE so they can be called cross-thread
    // via QMetaObject::invokeMethod or auto-queued signal connections.

    Q_INVOKABLE void setEnabled(bool enabled);
    Q_INVOKABLE void setMode(Mode mode);
    Q_INVOKABLE void setReversed(bool reversed);
    Q_INVOKABLE void setSpeed(int wpm);

    // Accept PHYSICAL paddle state — reversal applied internally. These write the packed
    // paddle atomic directly (safe from any thread via DirectConnection), then post
    // handlePaddleChange() to the keyer thread to wake from idle.
    //
    // PREFER setPaddleState() wherever both levers are known from one sample. A squeeze
    // release delivered as two single-line calls leaves a window in which the element timer
    // can observe one lever still down; the guard for "both released" then misses and the
    // keyer emits a cross-paddle element the operator never asked for. In Iambic A that is
    // always wrong — A must stop at the element boundary. Passing the pair closes the window
    // because both bits land in one store.
    void setPaddleState(bool dit, bool dah);
    void setDitPaddle(bool pressed);
    void setDahPaddle(bool pressed);

    // Enable/disable the kMinHoldNs release gate. Plain atomic write — callable from
    // any thread, like the paddle setters above (NOT a queued slot: the flag is read
    // on the HaliKey worker thread inside setDit/DahPaddle, so a queued hop to the
    // keyer thread would add delay without adding safety). V1.4 serial: enabled
    // (contact-bounce signatures ≤4 ms, see docs/halikey-cw-trace.md). MIDI: disabled
    // — events are firmware-debounced, and WinMM delivers press+release bursts
    // back-to-back, so an arrival-time hold gate would drop real elements.
    void setHoldGateEnabled(bool enabled);

    // Emergency stop (disconnect, etc.)
    Q_INVOKABLE void stop();

    bool isKeying() const;

signals:
    void elementStarted(bool isDit);
    void characterSpace();
    void restartAfterPause(int ms);
    void keyingFinished();

private:
    enum State { Idle, PlayingDit, PlayingDah };

    void handlePaddleChange();
    void enterElement(bool isDit);
    void onTimerFired();
    void goIdle();

    // Read the current logical paddle state (applies reversal to atomics)
    bool ditDown() const;
    bool dahDown() const;

    QTimer *m_elementTimer;
    State m_state = Idle;
    Mode m_mode = IambicA;
    bool m_reversed = false;
    bool m_squeezed = false; // both paddles held during current element
    bool m_enabled = false;  // gated by connection state
    int m_ditMs = 60;        // 1200 / WPM
    QElapsedTimer m_idleSince;

    // Free-running monotonic clock used to measure paddle-press hold durations and as the
    // timebase for the element deadline grid. Started in the constructor; never reset. Read
    // concurrently from the HaliKey worker thread (in setDit/DahPaddle) — QElapsedTimer's
    // methods are reentrant per Qt docs.
    QElapsedTimer m_pressClock;

    // Absolute next-element deadline (ns on m_pressClock's timebase). Keyer-thread-only:
    // written/read exclusively in enterElement(). Arming each element against this grid
    // keeps per-element timer overshoot from accumulating into tempo drift; the Idle→element
    // transition re-anchors it, so goIdle()/stop() need no reset.
    qint64 m_nextDeadlineNs = 0;

    // Physical paddle state — written from the HaliKey thread via DirectConnection, read from
    // the keyer thread's timer. The atomic eliminates cross-thread queue delay so onTimerFired()
    // always sees real-time paddle state.
    //
    // WHY both levers share ONE atomic rather than two bools: iambic decisions are a function of
    // the PAIR, so a reader must never see one lever's new value beside the other's old one. With
    // separate atomics every read of both (ditDown() + dahDown(), or either accessor on its own,
    // which loads both) can tear across a concurrent update, and a squeeze release then looks like
    // one lever still held — producing an element that was never keyed. One store, one load, no
    // torn pair.
    // Emits one [DIT]/[DAH] trace line. Split out so every paddle entry point logs identically.
    void traceLine(const char *line, bool pressed, bool bounceFiltered, qint64 holdNs) const;

    static constexpr quint8 kDitBit = 0x1;
    static constexpr quint8 kDahBit = 0x2;
    std::atomic<quint8> m_phys{0};

    // Paddle latches — capture any press during an active element so that
    // onTimerFired() doesn't miss a paddle-down that was released before
    // the element timer fired.  Set on key-down, cleared at element start.
    std::atomic<bool> m_ditLatch{false};
    std::atomic<bool> m_dahLatch{false};

    // Press timestamps (nsecsElapsed from m_pressClock). Written in setDitPaddle/setDahPaddle
    // ONLY when the corresponding latch transitions unset → set, so a bounce-press during an
    // already-latched window cannot overwrite the original press timestamp. Read on the matching
    // release path to compute hold duration vs kMinHoldNs.
    std::atomic<qint64> m_ditPressNs{0};
    std::atomic<qint64> m_dahPressNs{0};

    // Minimum-hold threshold for a press to count as "real" and keep its latch on release.
    // Anything shorter is treated as paddle bounce or accidental graze (the bounce signatures
    // captured in docs/halikey-cw-trace.md were all ≤ 4ms hold; comfortably below any deliberate
    // tap at practical WPM rates). V1.4-serial-only: the gate measures ARRIVAL-time spacing,
    // which equals contact timing for the locally-polled serial worker but not for MIDI, where
    // WinMM burst delivery can compress a real tap below the threshold. CwController disables
    // it for the MIDI transport via setHoldGateEnabled(false).
    static constexpr qint64 kMinHoldNs = 8'000'000; // 8 ms
    std::atomic<bool> m_holdGateEnabled{true};
};

#endif // IAMBICKEYER_H
