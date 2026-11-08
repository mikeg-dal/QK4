#ifndef VAIL_CAT_ROUTING_H
#define VAIL_CAT_ROUTING_H

#include <QString>
#include <atomic>

namespace VailCatRouting {

// Invalidate immediately, before posting a worker enable/disable. Queued DOWNs
// must still belong to this generation when the I/O thread receives them.
struct Control {
    std::atomic<quint64> epoch{0};
    std::atomic<bool> enabled{false};

    quint64 invalidate() {
        enabled.store(false, std::memory_order_release);
        return epoch.fetch_add(1, std::memory_order_acq_rel) + 1;
    }
};

// Used only by the I/O thread. Cleanup belongs to a DOWN actually forwarded;
// dropping a stale DOWN must not let its UP/RX interrupt another device/radio.
class Route {
public:
    bool accept(const QString &command, quint64 epoch, const Control &control, bool eligible) {
        if (command.startsWith(QLatin1String("KZD"))) {
            m_allowAbortRx = false;
            if (m_down || m_softwareDown || !eligible || !control.enabled.load(std::memory_order_acquire) ||
                control.epoch.load(std::memory_order_acquire) != epoch)
                return false;
            m_epoch = epoch;
            m_down = true;
            return true;
        }
        if (command.startsWith(QLatin1String("KZU"))) {
            m_allowAbortRx = m_down && m_epoch == epoch;
            if (!m_allowAbortRx)
                return false;
            m_down = false;
            return true;
        }
        if (command == QLatin1String("RX;")) {
            const bool allowed = m_allowAbortRx && !m_down && m_epoch == epoch;
            m_allowAbortRx = false;
            return allowed;
        }
        return false;
    }

    bool acceptSoftware(const QString &command, quint64 epoch, bool eligible) {
        if (command.startsWith(QLatin1String("KZD"))) {
            if (!eligible || m_down || m_softwareDown)
                return false;
            m_allowAbortRx = false;
            m_softwareEpoch = epoch;
            m_softwareDown = true;
            return true;
        }
        if (command.startsWith(QLatin1String("KZU"))) {
            if (!m_softwareDown || epoch != m_softwareEpoch)
                return false;
            m_softwareDown = false;
            return true;
        }
        // Monitor restoration must survive a device change even when new key-downs cannot.
        return true;
    }

    void reset() {
        m_down = false;
        m_softwareDown = false;
        m_allowAbortRx = false;
    }

private:
    quint64 m_epoch = 0;
    quint64 m_softwareEpoch = 0;
    bool m_down = false;
    bool m_softwareDown = false;
    bool m_allowAbortRx = false;
};

} // namespace VailCatRouting

#endif // VAIL_CAT_ROUTING_H
