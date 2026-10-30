#include "pingtracker.h"

#include <QString>
#include <QStringTokenizer>
#include <algorithm>

void PingTracker::pingSent(qint64 key, qint64 nowMs) {
    if (m_count == kMaxOutstanding)
        dropOldest(1);
    m_sent[m_count++] = Sent{key, nowMs};
}

void PingTracker::dropOldest(std::size_t n) {
    n = (std::min)(n, m_count);
    std::move(m_sent.begin() + n, m_sent.begin() + m_count, m_sent.begin());
    m_count -= n;
}

qint64 PingTracker::answer(QStringView pong, qint64 nowMs) {
    if (m_count == 0)
        return -1;
    const QStringView keyText = pong.sliced(4).trimmed();
    std::size_t match = 0; // a bare PONG answers the oldest ping
    if (!keyText.isEmpty()) {
        bool ok = false;
        const qint64 key = keyText.toLongLong(&ok);
        if (!ok)
            return -1;
        const auto end = m_sent.begin() + m_count;
        const auto it = std::find_if(m_sent.begin(), end, [key](const Sent &s) { return s.key == key; });
        // A key we never sent, or one already answered: a reply from a session that has ended.
        if (it == end)
            return -1;
        match = static_cast<std::size_t>(it - m_sent.begin());
    }
    const qint64 rtt = nowMs - m_sent[match].atMs;
    dropOldest(match + 1);
    return rtt;
}

qint64 PingTracker::onCatPayload(QStringView payload, qint64 nowMs) {
    qint64 rtt = -1;
    for (QStringView command : payload.tokenize(QChar(u';'))) {
        command = command.trimmed();
        if (command.startsWith(u"PONG")) {
            const qint64 r = answer(command, nowMs);
            if (r >= 0)
                rtt = r;
        }
    }
    return rtt;
}
