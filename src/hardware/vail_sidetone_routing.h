#ifndef VAIL_SIDETONE_ROUTING_H
#define VAIL_SIDETONE_ROUTING_H

#include "vail_cat_routing.h"

namespace VailSidetoneRouting {

// Owned by the sidetone thread. Audio has its own source generation: changing
// feedback must never invalidate the radio's timed-keying session.
class Route {
public:
    bool accept(bool down, quint64 epoch, quint64 sourceGeneration, quint64 currentSourceGeneration,
                const VailCatRouting::Control &control, bool eligible) {
        if (down) {
            if (!eligible || sourceGeneration != currentSourceGeneration ||
                !control.enabled.load(std::memory_order_acquire) ||
                epoch != control.epoch.load(std::memory_order_acquire))
                return false;
            m_epoch = epoch;
            m_sourceGeneration = sourceGeneration;
            m_down = true;
            return true;
        }
        // Cleanup from the session that produced the tone still releases it
        // after a gate closes. An old source's queued UP cannot stop a new tone.
        if (!m_down || epoch != m_epoch ||
            (sourceGeneration != m_sourceGeneration && m_sourceGeneration == currentSourceGeneration))
            return false;
        m_down = false;
        return true;
    }

    void reset() { m_down = false; }

    bool resetIfCurrent(quint64 generation, quint64 currentGeneration) {
        if (generation != currentGeneration)
            return false;
        reset();
        return true;
    }

private:
    quint64 m_epoch = 0;
    quint64 m_sourceGeneration = 0;
    bool m_down = false;
};

} // namespace VailSidetoneRouting

#endif // VAIL_SIDETONE_ROUTING_H
