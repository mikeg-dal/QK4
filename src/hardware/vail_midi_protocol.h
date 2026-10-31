#ifndef VAIL_MIDI_PROTOCOL_H
#define VAIL_MIDI_PROTOCOL_H

#include <QString>
#include <QStringList>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// Vail's adapter owns the keyer and its local buzzer. These are already-timed key edges,
// never paddle input for QK4's software keyer. No RtMidi/USB device is needed to test this layer.
namespace VailMidiProtocol {

using Message = std::vector<unsigned char>;

struct Configuration {
    int wpm = 20;
    int pitchHz = 600;
    int mode = 0; // 0 = Iambic A, 1 = Iambic B, 2 = straight/cootie

    bool valid() const { return wpm >= 5 && wpm <= 100 && pitchHz >= 100 && pitchHz <= 2000 && mode >= 0 && mode <= 2; }

    int ditController() const { return std::clamp(static_cast<int>(std::lround(600.0 / wpm)), 1, 127); }
    int pitchController() const {
        return std::clamp(static_cast<int>(std::lround(69.0 + 12.0 * std::log2(pitchHz / 440.0))), 0, 127);
    }
    int program() const { return mode == 2 ? 1 : (mode == 1 ? 8 : 7); }
};

inline std::vector<Message> configurationMessages(const Configuration &configuration,
                                                  const Configuration *previous = nullptr) {
    if (!configuration.valid()) {
        return {};
    }
    std::vector<Message> messages;
    if (!previous) {
        messages.push_back({0xB0, 0, 0}); // MIDI mode; 127 would select keyboard mode.
    }
    // Firmware stores these in EEPROM. Only write changed values, and only while all keys are up.
    if (!previous || configuration.program() != previous->program()) {
        messages.push_back({0xC0, static_cast<unsigned char>(configuration.program())});
    }
    if (!previous || configuration.ditController() != previous->ditController()) {
        messages.push_back({0xB0, 1, static_cast<unsigned char>(configuration.ditController())});
    }
    if (!previous || configuration.pitchController() != previous->pitchController()) {
        messages.push_back({0xB0, 2, static_cast<unsigned char>(configuration.pitchController())});
    }
    return messages;
}

enum class PortNaming { Exact, WinmmIndexed };

inline QString portIdentity(const QString &name, PortNaming naming) {
    if (naming == PortNaming::Exact) {
        return name;
    }
    const auto separator = name.lastIndexOf(QLatin1Char(' '));
    if (separator <= 0 || separator + 1 == name.size()) {
        return name;
    }
    for (auto i = separator + 1; i < name.size(); ++i) {
        if (name.at(i) < QLatin1Char('0') || name.at(i) > QLatin1Char('9')) {
            return name;
        }
    }
    return name.left(separator);
}

inline int uniquePort(const QStringList &names, const QString &selected, PortNaming naming = PortNaming::Exact) {
    if (selected.isEmpty()) {
        return -1;
    }
    // WHY: WinMM appends independent input/output indices, which change when other devices
    // disappear. Strip only that suffix; two identical device names remain ambiguous even
    // when their current numeric indices happen to match across both directions.
    const auto identity = portIdentity(selected, naming);
    int match = -1;
    for (int index = 0; index < names.size(); ++index) {
        if (portIdentity(names.at(index), naming) == identity) {
            if (match >= 0) {
                return -1;
            }
            match = index;
        }
    }
    return match;
}

enum class Edge { Ignored, Unchanged, Down, Up };

class Notes {
public:
    Edge accept(const Message &message) {
        // Adapter firmware uses MIDI channel one. Reject malformed data, other channels,
        // and arbitrary MIDI controls rather than allowing them to affect radio keying.
        if (message.size() != 3 || (message[0] != 0x90 && message[0] != 0x80) || message[1] > 2 || message[2] > 127) {
            return Edge::Ignored;
        }
        const bool before = down();
        m_notes[message[1]] = message[0] == 0x90 && message[2] != 0;
        const bool after = down();
        return before == after ? Edge::Unchanged : (after ? Edge::Down : Edge::Up);
    }

    bool down() const { return m_notes[0] || m_notes[1] || m_notes[2]; }

private:
    // Note zero is the documented straight/keyed output. Released firmware also emits its
    // keyed dits and dahs on notes one and two. OR them so an overlapping alias never unkeys.
    std::array<bool, 3> m_notes{{false, false, false}};
};

struct Result {
    QString command;
    bool stateChanged = false;
    bool down = false;
    bool aborted = false;
};

class Stream {
public:
    static constexpr int kMaximumDurationMs = 2500;
    static constexpr int kHoldTimeoutMs = 2400;

    Result setEnabled(bool enabled, std::int64_t nowNs) {
        if (m_enabled == enabled) {
            return {};
        }
        m_enabled = enabled;
        m_blocked = m_notes.down();
        m_hasRelease = false;
        if (!enabled && m_active) {
            return abort(nowNs);
        }
        return {};
    }

    Result accept(const Message &message, std::int64_t timestampNs) {
        const Edge edge = m_notes.accept(message);
        if (edge == Edge::Up && m_blocked) {
            m_blocked = false;
            return {};
        }
        if (!m_enabled || m_blocked || (edge != Edge::Down && edge != Edge::Up)) {
            return {};
        }
        if (edge == Edge::Down) {
            const int gap = m_hasRelease ? durationMs(timestampNs, m_lastEdgeNs) : 0;
            m_active = true;
            m_lastEdgeNs = timestampNs;
            // A fresh session (or an idle gap longer than K4 can represent) starts at D0000.
            return {command(true, gap >= 0 && gap <= kMaximumDurationMs ? gap : 0), true, true, false};
        }
        if (!m_active) {
            return {};
        }
        const int held = durationMs(timestampNs, m_lastEdgeNs);
        if (held < 0 || held > kMaximumDurationMs) {
            return abort(timestampNs);
        }
        m_active = false;
        m_hasRelease = true;
        m_lastEdgeNs = timestampNs;
        return {command(false, held), true, false, false};
    }

    Result checkTimeout(std::int64_t nowNs) {
        return m_active && durationMs(nowNs, m_lastEdgeNs) >= kHoldTimeoutMs ? abort(nowNs) : Result{};
    }

    Result abort(std::int64_t nowNs) {
        const bool wasActive = m_active;
        const int elapsed = wasActive ? durationMs(nowNs, m_lastEdgeNs) : 0;
        m_active = false;
        m_hasRelease = false;
        m_blocked = m_notes.down();
        return {wasActive ? command(false, std::clamp(elapsed, 0, kMaximumDurationMs)) : QString(), wasActive, false,
                wasActive};
    }

    bool notesDown() const { return m_notes.down(); }
    bool active() const { return m_active; }

private:
    static int durationMs(std::int64_t end, std::int64_t start) {
        if (end < start) {
            return -1;
        }
        // Avoid overflow on corrupt timestamps, and bound before narrowing to int.
        const auto elapsed = static_cast<std::uint64_t>(end) - static_cast<std::uint64_t>(start);
        const auto milliseconds = elapsed / 1'000'000 + (elapsed % 1'000'000 >= 500'000 ? 1 : 0);
        return static_cast<int>(std::min<std::uint64_t>(milliseconds, 1'000'000));
    }
    static QString command(bool down, int duration) {
        return QStringLiteral("KZ%1%2;").arg(down ? QChar('D') : QChar('U')).arg(duration, 4, 10, QChar('0'));
    }

    Notes m_notes;
    bool m_enabled = false;
    bool m_blocked = false;
    bool m_active = false;
    bool m_hasRelease = false;
    std::int64_t m_lastEdgeNs = 0;
};

} // namespace VailMidiProtocol

#endif // VAIL_MIDI_PROTOCOL_H
