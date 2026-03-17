#ifndef IAMBICKEYER_H
#define IAMBICKEYER_H

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <atomic>

// Shared high-resolution timer for CW chain diagnostics.
// Returns ms since first call — all CW log entries share this epoch.
inline double cwChainMs() {
    static QElapsedTimer t = []() {
        QElapsedTimer timer;
        timer.start();
        return timer;
    }();
    return t.nsecsElapsed() / 1e6;
}

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

    // Accept PHYSICAL paddle state — reversal applied internally.
    // These write atomic bools directly (safe from any thread via DirectConnection),
    // then post handlePaddleChange() to the keyer thread to wake from idle.
    void setDitPaddle(bool pressed);
    void setDahPaddle(bool pressed);

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

    // Physical paddle state — written from HaliKey thread via DirectConnection,
    // read from keyer thread's timer. Atomics eliminate cross-thread queue delay
    // so onTimerFired() always sees real-time paddle state.
    std::atomic<bool> m_physDit{false};
    std::atomic<bool> m_physDah{false};

    // Paddle latches — capture any press during an active element so that
    // onTimerFired() doesn't miss a paddle-down that was released before
    // the element timer fired.  Set on key-down, cleared at element start.
    std::atomic<bool> m_ditLatch{false};
    std::atomic<bool> m_dahLatch{false};
};

#endif // IAMBICKEYER_H
