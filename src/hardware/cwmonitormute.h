#ifndef CWMONITORMUTE_H
#define CWMONITORMUTE_H

#include <QString>
#include <algorithm>
#include <limits>

namespace CwMonitorMute {

struct Action {
    QString command;
    qint64 restoreAtMs = -1;
    quint64 generation = 0;
};

// I/O-thread only. Feed keying commands after the source/session route accepts them.
class State {
public:
    Action setEnabled(bool enabled, qint64 nowMs) {
        nowMs = std::max<qint64>(0, nowMs);
        if (m_enabled == enabled)
            return action();
        m_enabled = enabled;
        if (!enabled) {
            const QString command = m_muted ? levelCommand(m_level) : QString();
            m_muted = false;
            cancelRestore();
            return action(command);
        }
        return action(muteIfNeeded(nowMs));
    }

    Action observeLevel(int level, qint64 nowMs) {
        nowMs = std::max<qint64>(0, nowMs);
        if (level < 0 || level > 100 || (m_muted && level == 0))
            return action();
        m_level = level;
        if (m_muted) {
            if (m_down || pendingTail(nowMs))
                return action(QStringLiteral("ML0000;"));
            m_muted = false;
            cancelRestore();
            return action();
        }
        return action(muteIfNeeded(nowMs));
    }

    Action setKeyDownDelay(int delayMs, qint64 nowMs) {
        nowMs = std::max<qint64>(0, nowMs);
        if (delayMs < 0 || delayMs > 1000 || delayMs == m_keyDownDelayMs)
            return action();
        const int increase = std::max(0, delayMs - m_keyDownDelayMs);
        m_keyDownDelayMs = delayMs;
        // WHY: a lower current setting cannot retract keying already queued with the old delay.
        if (increase > 0 && (m_down || pendingTail(nowMs))) {
            if (m_downDueMs >= 0)
                m_downDueMs = add(m_downDueMs, increase);
            if (m_upDueMs >= 0)
                m_upDueMs = add(m_upDueMs, increase);
            if (m_muted && !m_down)
                scheduleRestore();
        }
        return action();
    }

    Action observeResponse(const QString &response, qint64 nowMs) {
        QString commands;
        qsizetype start = 0;
        qsizetype end;
        while ((end = response.indexOf(QLatin1Char(';'), start)) >= 0) {
            const auto command = response.mid(start, end - start);
            int value = 0;
            if (command.size() >= 4 && command.size() <= 6 && command.startsWith(QLatin1String("ML0")) &&
                decimal(command, 3, static_cast<int>(command.size()) - 3, 100, value)) {
                commands += observeLevel(value, nowMs).command;
            } else if (command.size() >= 4 && command.size() <= 7 && command.startsWith(QLatin1String("KZL")) &&
                       decimal(command, 3, static_cast<int>(command.size()) - 3, 1000, value)) {
                commands += setKeyDownDelay(value, nowMs).command;
            }
            start = end + 1;
        }
        return action(commands);
    }

    Action accept(const QString &acceptedCommand, qint64 nowMs) {
        nowMs = std::max<qint64>(0, nowMs);
        int durationMs = 0;
        if (!duration(acceptedCommand, durationMs))
            return action();
        if (acceptedCommand.at(2) == QLatin1Char('D')) {
            if (m_down)
                return action();
            cancelRestore();
            m_down = true;
            m_downDueMs = add(nowMs, m_keyDownDelayMs);
            if (m_upDueMs >= 0)
                m_downDueMs = std::max(m_downDueMs, add(m_upDueMs, durationMs));
            return action(muteIfNeeded(nowMs));
        }
        if (!m_down)
            return action();
        m_down = false;
        m_upDueMs = std::max(add(m_downDueMs, durationMs), nowMs);
        if (m_muted)
            scheduleRestore();
        return action();
    }

    Action pollRestore(quint64 generation, qint64 nowMs) {
        if (generation != m_generation || m_down || !m_muted || m_restoreAtMs < 0)
            return {};
        if (nowMs < m_restoreAtMs)
            return action();
        const auto command = levelCommand(m_level);
        m_muted = false;
        cancelRestore();
        return action(command);
    }

    QString restoreBeforeDisconnect() {
        const auto command = m_muted ? levelCommand(m_level) : QString();
        reset();
        return command;
    }

    void reset() {
        cancelRestore();
        m_level = -1;
        m_keyDownDelayMs = 1000;
        m_muted = false;
        m_down = false;
        m_downDueMs = -1;
        m_upDueMs = -1;
    }

private:
    static constexpr int RESTORE_MARGIN_MS = 500;

    static qint64 add(qint64 time, int milliseconds) {
        const auto maximum = std::numeric_limits<qint64>::max();
        return time > maximum - milliseconds ? maximum : time + milliseconds;
    }

    static QString levelCommand(int level) { return QStringLiteral("ML0%1;").arg(level, 3, 10, QLatin1Char('0')); }

    static bool duration(const QString &command, int &value) {
        if (command.size() != 8 || !command.startsWith(QLatin1String("KZ")) ||
            (command.at(2) != QLatin1Char('D') && command.at(2) != QLatin1Char('U')) ||
            command.at(7) != QLatin1Char(';'))
            return false;
        return decimal(command, 3, 4, 9999, value);
    }

    static bool decimal(const QString &text, int start, int count, int maximum, int &value) {
        value = 0;
        for (int index = start; index < start + count; ++index) {
            const auto digit = text.at(index).unicode();
            if (digit < '0' || digit > '9')
                return false;
            value = value * 10 + digit - '0';
        }
        return value <= maximum;
    }

    bool pendingTail(qint64 nowMs) const { return m_upDueMs >= 0 && add(m_upDueMs, RESTORE_MARGIN_MS) > nowMs; }

    QString muteIfNeeded(qint64 nowMs) {
        if (!m_enabled || m_muted || m_level <= 0 || (!m_down && !pendingTail(nowMs)))
            return {};
        m_muted = true;
        if (!m_down)
            scheduleRestore();
        return QStringLiteral("ML0000;");
    }

    void cancelRestore() {
        ++m_generation;
        m_restoreAtMs = -1;
    }

    void scheduleRestore() {
        ++m_generation;
        m_restoreAtMs = add(m_upDueMs, RESTORE_MARGIN_MS);
    }

    Action action(const QString &command = {}) const { return {command, m_restoreAtMs, m_generation}; }

    bool m_enabled = false;
    bool m_muted = false;
    bool m_down = false;
    int m_level = -1;
    // WHY: until KZL is observed, its maximum is the conservative replay bound.
    int m_keyDownDelayMs = 1000;
    qint64 m_downDueMs = -1;
    qint64 m_upDueMs = -1;
    qint64 m_restoreAtMs = -1;
    quint64 m_generation = 0;
};

} // namespace CwMonitorMute

#endif // CWMONITORMUTE_H
