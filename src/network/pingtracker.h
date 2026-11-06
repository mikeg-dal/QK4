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

    void reset() {
        m_count = 0;
        m_lastRttMs = -1;
    }

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

    // The K4's PONG is bare ("PONG;", bench 2026-10-30), so replies pair with pings only by order and
    // one lost reply would leave every later round trip measured from the ping before its own: a
    // permanent ~1000 ms reading on a 1 ms link (seen after a long transmit). With a newer ping out,
    // the oldest is a full ping interval old; on a link whose last round trip was under
    // kFastRttMs that ping's reply is not coming, so it is dropped as lost before pairing. A link
    // that slows gradually raises m_lastRttMs past the bound first, so real round trips longer than
    // the interval are still reported in full.
    static constexpr qint64 kFastRttMs = 500;
    qint64 m_lastRttMs = -1;
};

#endif // PINGTRACKER_H
