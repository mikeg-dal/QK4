#ifndef LOGGING_LOGQUEUE_H
#define LOGGING_LOGQUEUE_H

#include <QString>
#include <QtGlobal>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <vector>

namespace Logging {

// One log message as the handler hands it over. The pointers are static strings (category name,
// __FILE__, Q_FUNC_INFO). The QStrings are implicitly shared copies: storing one is a reference
// count increment, never an allocation, and the writer is the one that releases the last reference.
struct LogRecord {
    qint64 wallMs = 0;
    QtMsgType type = QtDebugMsg;
    const char *category = nullptr;
    const char *file = nullptr;
    const char *function = nullptr;
    int line = 0;
    QString thread;
    QString message;
};

// Bounded multi-producer queue drained by one writer. Both rings are allocated up front; a push is
// one slot assignment under a mutex held for O(1), and the writer takes the whole ring by swapping
// it with its spare. A full ring drops the record and counts it, it never blocks or grows.
class LogQueue {
public:
    explicit LogQueue(std::size_t capacity) : m_ring(capacity), m_capacity(capacity) {}

    std::size_t capacity() const { return m_capacity; }

    // `realtime` producers only try the lock: if the writer or another producer holds it, the
    // record is dropped and counted rather than waited for.
    bool push(LogRecord &&record, bool realtime) {
        std::unique_lock<std::mutex> lock(m_mutex, std::defer_lock);
        if (realtime) {
            if (!lock.try_lock()) {
                m_dropped.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
        } else {
            lock.lock();
        }
        if (m_count == m_capacity) {
            m_dropped.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        m_ring[m_count++] = std::move(record);
        return true;
    }

    // Swaps the queued records into `spare` and returns how many there are. `spare` must hold
    // capacity() cleared records; clear the first n with release() once they are written.
    std::size_t take(std::vector<LogRecord> &spare) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_ring.swap(spare);
        const std::size_t n = m_count;
        m_count = 0;
        return n;
    }

    static void release(std::vector<LogRecord> &records, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i)
            records[i] = LogRecord();
    }

    quint64 takeDropped() { return m_dropped.exchange(0, std::memory_order_relaxed); }

private:
    std::mutex m_mutex;
    std::vector<LogRecord> m_ring;
    std::size_t m_count = 0;
    const std::size_t m_capacity;
    std::atomic<quint64> m_dropped{0};
};

} // namespace Logging

#endif // LOGGING_LOGQUEUE_H
