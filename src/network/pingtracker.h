#ifndef PINGTRACKER_H
#define PINGTRACKER_H

#include <QStringView>
#include <QtGlobal>
#include <array>
#include <cstddef>

// Round-trip time of the K4 link from its PING/PONG keepalive. Pure bookkeeping: the caller supplies
// times from one monotonic clock, so it is tested without a socket.
//
// WHY a queue of send times and not one timer restarted per ping: a ping goes out every second, so a
// round trip longer than that was timed from the NEXT ping and read modulo one second - a slow link
// showed as fast. Each PING carries a key (the epoch second); its send time is kept until the PONG
// for it arrives. WHY scan the whole payload: the K4 can bundle a PONG after other replies in one
// CAT packet ("FA…;PONG…;"), and a PONG matched only at the start of a payload was lost.
class PingTracker {
public:
    // Unanswered pings kept. The K4 drops a session whose PING goes unanswered, so more than one or
    // two outstanding means the link is already gone; the bound only stops unbounded growth.
    static constexpr std::size_t kMaxOutstanding = 16;

    void reset() { m_count = 0; }

    // A PING<key>; was written at nowMs.
    void pingSent(qint64 key, qint64 nowMs);

    // Scans one CAT payload (one or more ';'-terminated commands) for PONGs. A PONG carrying a key
    // answers the oldest outstanding ping with that key; a bare PONG answers the oldest ping. Pings
    // older than the one answered are dropped as lost (TCP keeps the replies in order). Returns the
    // round trip of the last PONG answered, in ms, or -1 if the payload answered none.
    qint64 onCatPayload(QStringView payload, qint64 nowMs);

    std::size_t outstanding() const { return m_count; }

private:
    struct Sent {
        qint64 key = 0;
        qint64 atMs = 0;
    };
    qint64 answer(QStringView pong, qint64 nowMs);
    void dropOldest(std::size_t n);

    std::array<Sent, kMaxOutstanding> m_sent{}; // oldest first
    std::size_t m_count = 0;
};

#endif // PINGTRACKER_H
