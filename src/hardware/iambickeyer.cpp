#include "hardware/iambickeyer.h"
#include <QDebug>
#include <QThread>

IambicKeyer::IambicKeyer(QObject *parent) : QObject(parent) {
    m_elementTimer = new QTimer(this);
    m_elementTimer->setSingleShot(true);
    m_elementTimer->setTimerType(Qt::PreciseTimer);
    connect(m_elementTimer, &QTimer::timeout, this, &IambicKeyer::onTimerFired);
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

void IambicKeyer::setDitPaddle(bool pressed) {
    // Write atomic immediately (called from HaliKey thread via DirectConnection).
    // onTimerFired() reads this with zero delay — no cross-thread queue latency.
    m_physDit.store(pressed, std::memory_order_relaxed);
    if (pressed)
        m_ditLatch.store(true, std::memory_order_relaxed);
    qDebug("[CW %10.3f] ATOMIC dit=%s (thread=%s)", cwChainMs(), pressed ? "DOWN" : "UP",
           QThread::currentThread()->objectName().toLatin1().constData());

    // Post handlePaddleChange to keyer thread to wake from idle.
    // If keyer is already running, the timer will read the atomic directly.
    QMetaObject::invokeMethod(this, &IambicKeyer::handlePaddleChange, Qt::QueuedConnection);
}

void IambicKeyer::setDahPaddle(bool pressed) {
    m_physDah.store(pressed, std::memory_order_relaxed);
    if (pressed)
        m_dahLatch.store(true, std::memory_order_relaxed);
    qDebug("[CW %10.3f] ATOMIC dah=%s (thread=%s)", cwChainMs(), pressed ? "DOWN" : "UP",
           QThread::currentThread()->objectName().toLatin1().constData());
    QMetaObject::invokeMethod(this, &IambicKeyer::handlePaddleChange, Qt::QueuedConnection);
}

bool IambicKeyer::ditDown() const {
    bool dit = m_physDit.load(std::memory_order_relaxed);
    bool dah = m_physDah.load(std::memory_order_relaxed);
    return m_reversed ? dah : dit;
}

bool IambicKeyer::dahDown() const {
    bool dit = m_physDit.load(std::memory_order_relaxed);
    bool dah = m_physDah.load(std::memory_order_relaxed);
    return m_reversed ? dit : dah;
}

void IambicKeyer::handlePaddleChange() {
    if (!m_enabled) {
        qDebug("[CW %10.3f] KEYER handlePaddleChange IGNORED (disabled)", cwChainMs());
        return;
    }

    bool dit = ditDown() ||
               (m_reversed ? m_dahLatch.load(std::memory_order_relaxed) : m_ditLatch.load(std::memory_order_relaxed));
    bool dah = dahDown() ||
               (m_reversed ? m_ditLatch.load(std::memory_order_relaxed) : m_dahLatch.load(std::memory_order_relaxed));

    const char *stateStr = m_state == Idle ? "Idle" : m_state == PlayingDit ? "Dit" : "Dah";
    if (m_reversed)
        qDebug("[CW %10.3f] KEYER handlePaddleChange dit=%d dah=%d state=%s (reversed, phys: dit=%d dah=%d)",
               cwChainMs(), dit, dah, stateStr, m_physDit.load(std::memory_order_relaxed),
               m_physDah.load(std::memory_order_relaxed));
    else
        qDebug("[CW %10.3f] KEYER handlePaddleChange dit=%d dah=%d state=%s", cwChainMs(), dit, dah, stateStr);

    // Track squeeze state during active element
    if (m_state != Idle && dit && dah)
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
    // Transitioning from idle — emit pause duration before the element
    if (m_state == Idle && m_idleSince.isValid()) {
        int elapsed = static_cast<int>(m_idleSince.elapsed());
        if (elapsed <= 2000)
            emit restartAfterPause(elapsed);
        // else: skip KZP entirely — pause too long to be meaningful CW spacing
    }

    m_state = isDit ? PlayingDit : PlayingDah;
    m_squeezed = false;

    // Clear only the same-element latch (consumed by this transition).
    // The opposite latch preserves any cross-paddle press that occurred during
    // the previous element — without this, brief taps are lost.
    if (isDit != m_reversed)
        m_ditLatch.store(false, std::memory_order_relaxed);
    else
        m_dahLatch.store(false, std::memory_order_relaxed);

    // Re-check current paddles for squeeze detection within this element
    if (ditDown() && dahDown())
        m_squeezed = true;

    // Dit = 1 unit on + 1 unit off = 2 ditMs; Dah = 3 units on + 1 unit off = 4 ditMs
    int interval = isDit ? m_ditMs * 2 : m_ditMs * 4;
    m_elementTimer->start(interval);
    qDebug("[CW %10.3f] KEYER ELEMENT %s started (interval=%dms, dit=%d dah=%d squeeze=%d)", cwChainMs(),
           isDit ? "DIT" : "DAH", interval, ditDown(), dahDown(), m_squeezed);
    emit elementStarted(isDit);
}

void IambicKeyer::onTimerFired() {
    bool liveDit = ditDown();
    bool liveDah = dahDown();

    // Check both live paddle state AND latch (paddle was pressed during this element)
    bool dit = liveDit ||
               (m_reversed ? m_dahLatch.load(std::memory_order_relaxed) : m_ditLatch.load(std::memory_order_relaxed));
    bool dah = liveDah ||
               (m_reversed ? m_ditLatch.load(std::memory_order_relaxed) : m_dahLatch.load(std::memory_order_relaxed));
    bool wasDit = (m_state == PlayingDit);
    qDebug("[CW %10.3f] KEYER TIMER fired (was=%s, dit=%d dah=%d squeeze=%d)", cwChainMs(), wasDit ? "DIT" : "DAH", dit,
           dah, m_squeezed);

    // Squeeze release: both paddles physically released while squeeze was active.
    // Bypass latches — use Iambic A/B mode rules instead.  Without this guard,
    // a stale opposite-paddle latch would produce an unwanted extra element in
    // Iambic A mode.
    if (m_squeezed && !liveDit && !liveDah) {
        if (m_mode == IambicB)
            enterElement(!wasDit);
        else
            goIdle();
        return;
    }

    if (dit && dah) {
        // Both held — squeeze alternation
        enterElement(!wasDit);
    } else if (wasDit && dah) {
        // Opposite paddle held — cross-paddle
        enterElement(false);
    } else if (!wasDit && dit) {
        // Opposite paddle held — cross-paddle
        enterElement(true);
    } else if (wasDit && dit) {
        // Same paddle held — repeat
        enterElement(true);
    } else if (!wasDit && dah) {
        // Same paddle held — repeat
        enterElement(false);
    } else if (m_squeezed && m_mode == IambicB) {
        // Iambic B: one extra opposite element after squeeze release
        enterElement(!wasDit);
    } else {
        // Nothing held, no squeeze memory — done
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
    qDebug("[CW %10.3f] KEYER IDLE", cwChainMs());
    emit characterSpace();
    emit keyingFinished();
}

void IambicKeyer::stop() {
    if (m_state != Idle) {
        m_state = Idle;
        m_elementTimer->stop();
        m_squeezed = false;
        m_physDit.store(false, std::memory_order_relaxed);
        m_physDah.store(false, std::memory_order_relaxed);
        m_ditLatch.store(false, std::memory_order_relaxed);
        m_dahLatch.store(false, std::memory_order_relaxed);
        emit keyingFinished();
    }
}

bool IambicKeyer::isKeying() const {
    return m_state != Idle;
}
