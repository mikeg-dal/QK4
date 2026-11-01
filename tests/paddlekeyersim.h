// SPDX-License-Identifier: GPL-3.0-or-later
//
// Virtual-clock harness for PaddleKeyer + SidetoneScheduler: a C++ port of sim() in
// K4-Companion's keying_simulations/paddle_sweep.py (Dale Farnsworth, GPL-3.0). Nothing about the
// keyer is reimplemented. Paddle events go in on the "device thread" and the keyer pass runs a hop
// later. Every sleep the keyer takes advances the virtual clock, running the audio blocks and
// device deliveries that fall inside it. Sidetone edges are the frames nextKeyEdge actually placed.
//
// The monitor restore is left alone, as the Python harness's wall-clock timer never fires inside
// a virtual run: the keyer is woken only for paddle events and releases, never for the restore's
// deadline. fireUnmute() runs the clock to it afterwards.
//
// Every expression keeps the Python's order of operations, so the doubles come out bit-identical
// and the golden commands replay exactly.

#ifndef PADDLEKEYERSIM_H
#define PADDLEKEYERSIM_H

#include "audio/sidetonescheduler.h"
#include "hardware/paddlekeyer.h"
#include "utils/rtsan.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace PaddleSim {

enum class Scenario { Tap, DitTap, Swap, Squeeze, DahFirst, Stall };

inline const char *scenarioName(Scenario s) {
    switch (s) {
    case Scenario::Tap:
        return "tap";
    case Scenario::DitTap:
        return "dit-tap";
    case Scenario::Swap:
        return "swap";
    case Scenario::Squeeze:
        return "squeeze";
    case Scenario::DahFirst:
        return "dah-first";
    case Scenario::Stall:
        return "stall";
    }
    return "?";
}

// What happened at the paddles, named as the harness names them so equal-time events sort the
// way Python's sorted() sorts the tuples: by time, then by name.
enum class Paddle { DahDown, DahUp, DitDown, DitUp };

inline const char *paddleName(Paddle p) {
    switch (p) {
    case Paddle::DahDown:
        return "dah_down";
    case Paddle::DahUp:
        return "dah_up";
    case Paddle::DitDown:
        return "dit_down";
    case Paddle::DitUp:
        return "dit_up";
    }
    return "?";
}

inline PaddleKeyer::Event toEvent(Paddle p) {
    switch (p) {
    case Paddle::DitDown:
        return PaddleKeyer::Event::DitDown;
    case Paddle::DitUp:
        return PaddleKeyer::Event::DitUp;
    case Paddle::DahDown:
        return PaddleKeyer::Event::DahDown;
    case Paddle::DahUp:
        return PaddleKeyer::Event::DahUp;
    }
    return PaddleKeyer::Event::Close;
}

struct Config {
    Scenario scenario = Scenario::Squeeze;
    int wpm = 30;
    double ditMemPercent = 0.0;
    PaddleKeyer::Mode mode = PaddleKeyer::Mode::IambicB;
    bool muteRadioMonitor = true; // the golden runs were recorded with it on
    double weighting = 1.0;
    int monitorLevel = 0;
    int keyDownInitialDelayMs = 0;
    // When set, releasePaddles() is called at this time after T0 (an Esc or a disconnect).
    std::optional<double> releaseAt;
};

// The sweep's fixed geometry, scaled to the speed exactly as paddle_sweep.py scales it.
struct Geometry {
    double hold = 0;
    int tapFirstMs = 0;
    int tapLastMs = 0;
    double window = 0;
    double scale = 1;
    double stallFrom = 0;
    std::vector<std::pair<double, Paddle>> stallPaddles;
    long long stallGapMs = 0;

    explicit Geometry(const Config &c) {
        scale = 30.0 / c.wpm;
        switch (c.scenario) {
        case Scenario::Tap:
            hold = .400, tapFirstMs = 84, tapLastMs = 164;
            break;
        case Scenario::DitTap:
            hold = .760, tapFirstMs = 80, tapLastMs = 300;
            break;
        case Scenario::Swap:
            hold = .520, tapFirstMs = 84, tapLastMs = 164;
            break;
        case Scenario::Squeeze:
            hold = .500, tapFirstMs = 4, tapLastMs = 500;
            break;
        case Scenario::DahFirst:
            hold = .300, tapFirstMs = 4, tapLastMs = 40;
            break;
        case Scenario::Stall:
            hold = .680, tapFirstMs = 200, tapLastMs = 600;
            break;
        }
        if (scale != 1) {
            hold *= scale;
            tapFirstMs = static_cast<int>(std::max<long long>(4, CwKeying::pyRound(tapFirstMs * scale / 4) * 4));
            tapLastMs = static_cast<int>(CwKeying::pyRound(tapLastMs * scale / 4) * 4);
        }
        window = hold + .12 * scale;
        stallFrom = .190 * scale;
        stallPaddles = {{0, Paddle::DitDown},
                        {.200 * scale, Paddle::DitUp},
                        {.400 * scale, Paddle::DahDown},
                        {.460 * scale, Paddle::DahUp}};
        stallGapMs = CwKeying::pyRound(200 * scale);
    }

    std::vector<int> taps() const {
        std::vector<int> out;
        for (int ms = tapFirstMs; ms <= tapLastMs; ms += 4)
            out.push_back(ms);
        return out;
    }
};

struct Result {
    std::vector<std::pair<double, Paddle>> script; // relative to T0
    std::vector<std::pair<double, bool>> edges;    // sidetone edges placed, relative to T0
    std::vector<std::string> sent;
    double ditSeconds = 0;
    double dahSeconds = 0;
    double ditMemSeconds = 0;
};

class Run {
public:
    static constexpr int kRate = 48000;
    static constexpr int kBlockFrames = 384;
    static constexpr double kT0 = 100.1;
    static constexpr double kHop = .0001;
    static constexpr double kLead = .016;

    Run(const Config &config, double tapAt, double tapLength = .005)
        : m_config(config), m_geo(config), m_tapAt(tapAt), m_tapLength(tapLength), m_clock(*this), m_link(*this),
          m_sidetone(m_clock, [this]() -> std::optional<double> { return m_now + 1000.0; }),
          m_keyer(m_clock, m_link, m_sidetone) {
        m_sidetone.setMinimumSidetoneMs(0);
        m_keyer.setStraightKeying(false);
        m_keyer.setDitNotes({20});
        m_keyer.setDahNotes({21});
        m_keyer.setMode(config.mode);
        m_keyer.setDitMemoryDelayPercent(config.ditMemPercent);
        m_keyer.setMuteRadioMonitor(config.muteRadioMonitor);
        m_keyer.setMonitorLevel(config.monitorLevel);
        m_keyer.setKeyDownInitialDelayMs(config.keyDownInitialDelayMs);
        m_keyer.setSpeed(config.wpm, config.weighting);
    }

    Result go() {
        buildScript();
        const double end = kT0 + m_geo.window + .4;
        while (m_now < end) {
            double next = m_wake ? *m_wake : (m_pending.empty() ? end : m_pending.front().at);
            advance(std::min(next, end));
            if (m_wake && m_now >= *m_wake) {
                m_wake.reset();
                if (m_keyer.hasPendingEvents()) {
                    m_keyer.processNextEvent();
                    if (m_keyer.hasPendingEvents())
                        m_wake = m_now;
                }
            }
        }
        Result r;
        for (const auto &[t, what] : m_script)
            r.script.emplace_back(t - kT0, what);
        for (const auto &[t, down] : m_placed)
            r.edges.emplace_back(t - kT0, down);
        r.sent = m_sent;
        r.ditSeconds = m_keyer.ditSeconds();
        r.dahSeconds = m_keyer.dahSeconds();
        r.ditMemSeconds = m_config.ditMemPercent / 100 * (m_keyer.dahSeconds() + m_keyer.interElementSeconds());
        return r;
    }

    double now() const { return m_now; }
    PaddleKeyer &keyer() { return m_keyer; }
    std::optional<double> monitorRestoreDue() const { return m_keyer.monitorRestoreDue(); }
    // Runs the clock to the monitor restore's deadline and lets the keyer act on it; returns what
    // it sent. Nothing, if no restore is armed.
    std::vector<std::string> fireUnmute() {
        const size_t before = m_sent.size();
        if (const auto due = m_keyer.monitorRestoreDue()) {
            advance(std::max(m_now, *due));
            m_keyer.processNextEvent();
        }
        return {m_sent.begin() + static_cast<long>(before), m_sent.end()};
    }
    SidetoneScheduler &sidetone() { return m_sidetone; }

private:
    struct Pending {
        double at;
        Paddle what;
        double happened;
        bool release = false;
    };

    class SimClock : public CwKeying::Clock {
    public:
        explicit SimClock(Run &r) : m_run(r) {}
        double now() override { return m_run.m_now; }
        void sleep(double s) override {
            double to = m_run.m_now + s;
            if (m_run.m_freeze && m_run.m_freeze->first <= to && to < m_run.m_freeze->second)
                to = m_run.m_freeze->second;
            m_run.advance(to);
        }

    private:
        Run &m_run;
    };

    class SimLink : public CwKeying::Link {
    public:
        explicit SimLink(Run &r) : m_run(r) {}
        void send(std::string_view command) override {
            {
                // WHY allowed: the simulation's record of what was sent. The real link,
                // KzEgress::post, is checked on its own; the simulated audio below still is.
                RtSan::Allow allow;
                m_run.m_sent.emplace_back(command);
            }
            m_run.advance(m_run.m_now + .0002);
        }

    private:
        Run &m_run;
    };

    void buildScript() {
        const double t0 = kT0;
        const double tap = m_tapAt;
        const double hold = m_geo.hold;
        switch (m_config.scenario) {
        case Scenario::Squeeze:
            m_script = {
                {t0, Paddle::DitDown}, {t0, Paddle::DahDown}, {t0 + tap, Paddle::DitUp}, {t0 + tap, Paddle::DahUp}};
            break;
        case Scenario::Tap:
        case Scenario::DitTap: {
            const bool ditHeld = m_config.scenario == Scenario::Tap;
            m_script = {{t0, ditHeld ? Paddle::DitDown : Paddle::DahDown},
                        {t0 + tap, ditHeld ? Paddle::DahDown : Paddle::DitDown},
                        {t0 + tap + m_tapLength, ditHeld ? Paddle::DahUp : Paddle::DitUp},
                        {t0 + hold, ditHeld ? Paddle::DitUp : Paddle::DahUp}};
            std::sort(m_script.begin(), m_script.end(), [](const auto &a, const auto &b) {
                if (a.first != b.first)
                    return a.first < b.first;
                return std::string(paddleName(a.second)) < std::string(paddleName(b.second));
            });
            break;
        }
        case Scenario::DahFirst:
            m_script = {{t0, Paddle::DahDown},
                        {t0 + tap, Paddle::DitDown},
                        {t0 + hold, Paddle::DitUp},
                        {t0 + hold, Paddle::DahUp}};
            break;
        case Scenario::Stall:
            for (const auto &[when, what] : m_geo.stallPaddles)
                m_script.emplace_back(t0 + when, what);
            break;
        case Scenario::Swap:
            m_script = {{t0, Paddle::DitDown},
                        {t0 + tap, Paddle::DitUp},
                        {t0 + tap, Paddle::DahDown},
                        {t0 + hold, Paddle::DahUp}};
            break;
        }

        if (m_config.scenario == Scenario::Stall) {
            const double stallFrom = t0 + m_geo.stallFrom;
            const double stallTo = t0 + tap;
            std::optional<double> arrived;
            std::optional<double> heldUntil;
            for (const auto &[when, what] : m_script) {
                double at = (stallFrom <= when && when < stallTo) ? stallTo : when;
                if (arrived)
                    at = std::max(at, *arrived + .0001);
                if (stallFrom <= when && when < stallTo)
                    heldUntil = at;
                m_pending.push_back({at, what, when});
                arrived = at;
            }
            if (!heldUntil)
                heldUntil = stallTo;
            m_freeze = std::make_pair(stallFrom, *heldUntil + .001);
        } else {
            for (const auto &[when, what] : m_script)
                m_pending.push_back({when, what, when});
        }
        if (m_config.releaseAt) {
            const double at = t0 + *m_config.releaseAt;
            auto pos = std::find_if(m_pending.begin(), m_pending.end(), [at](const Pending &p) { return p.at > at; });
            m_pending.insert(pos, Pending{at, Paddle::DitUp, at, true});
        }
    }

    void advance(double to) {
        while (true) {
            const double dac = m_grid0 + static_cast<double>(kBlockFrames * m_block) / kRate;
            double run = dac - kLead;
            run -= 1000.0;
            const double next = std::min(run, m_pending.empty() ? 1e18 : m_pending.front().at);
            if (next > to)
                break;
            m_now = std::max(m_now, next);
            if (!m_pending.empty() && m_pending.front().at == next) {
                const Pending p = m_pending.front();
                m_pending.pop_front();
                if (p.release) {
                    m_keyer.releasePaddles();
                    continue;
                }
                if (m_config.scenario == Scenario::Stall)
                    sendMidi(p.what, p.happened);
                else
                    m_keyer.paddleEvent(toEvent(p.what), 0);
                if (!m_wake)
                    m_wake = m_now + kHop;
            } else {
                callback(dac, run + 1000.0);
                ++m_block;
            }
        }
        m_now = std::max(m_now, to);
    }

    void callback(double dac, double current) {
        const double blockSeconds = static_cast<double>(kBlockFrames) / kRate;
        m_sidetone.noteBlockTiming(dac, current, blockSeconds);
        if (!m_keyed && !m_sidetone.pendingEdgeHeld() && dac - m_lastEdge > .5)
            m_sidetone.setLag(0.0);
        int start = 0;
        while (true) {
            const auto e = m_sidetone.nextKeyEdge(start, kBlockFrames, dac, kRate);
            if (!e)
                break;
            m_keyed = e->down;
            m_lastEdge = dac + static_cast<double>(e->offset) / kRate;
            m_placed.emplace_back(dac + static_cast<double>(e->offset) / kRate - 1000.0, e->down);
            start = e->offset;
        }
    }

    // As a MoMIDI device sends it: the time since its last event in 126 ms steps by polyphonic
    // aftertouch, where there are any, and the leftover milliseconds as the note's velocity --
    // never 0 or 127, which mean "no time in this message".
    void sendMidi(Paddle what, double happened) {
        const double delta = happened - m_deviceClock;
        m_deviceClock = happened;
        const long long steps = static_cast<long long>(delta / .126);
        const long long ms =
            std::max<long long>(1, std::min<long long>(126, CwKeying::pyRound((delta - steps * .126) * 1000)));
        const bool dit = what == Paddle::DitDown || what == Paddle::DitUp;
        const bool down = what == Paddle::DitDown || what == Paddle::DahDown;
        const int note = dit ? 20 : 21;
        if (steps)
            m_keyer.midiMessage(0xa0, note, static_cast<int>(steps), 0);
        m_keyer.midiMessage(down ? 0x90 : 0x80, note, static_cast<int>(ms), 0);
    }

    Config m_config;
    Geometry m_geo;
    double m_tapAt;
    double m_tapLength;

    double m_now = 100.0;
    const double m_grid0 = 1000.0 + 99.9;
    long long m_block = 0;
    bool m_keyed = false;
    double m_lastEdge = -1e9;
    std::vector<std::pair<double, bool>> m_placed;
    std::vector<std::pair<double, Paddle>> m_script;
    std::deque<Pending> m_pending;
    std::optional<double> m_wake;
    std::optional<std::pair<double, double>> m_freeze;
    std::vector<std::string> m_sent;
    double m_deviceClock = 0.0;

    SimClock m_clock;
    SimLink m_link;
    SidetoneScheduler m_sidetone;
    PaddleKeyer m_keyer;
};

// ---- verdicts, ported from paddle_sweep.py ----

inline std::vector<std::string> elementsFrom(const std::vector<std::string> &sent, int wpm) {
    const double dit = 1.2 / wpm;
    std::vector<std::string> out;
    for (const auto &cmd : sent) {
        if (cmd.rfind("KZU", 0) == 0) {
            const int ms = std::stoi(cmd.substr(3, 4));
            out.push_back(ms > CwKeying::pyRound(dit * 1000 * 2) ? "dah" : "dit");
        }
    }
    return out;
}

struct SqueezeVerdict {
    std::string during;
    std::string owed;
    std::vector<std::string> after;
    bool met = false;
};

// What Iambic B owes a released squeeze: exactly one more element, the opposite of the one it was
// released during (or nothing, inside the Dit Mem window of a dah) and nothing after it. Windows
// are cut from the sidetone as it was placed, since the rule is about what the operator heard. A
// release within 1 us of a cut is allowed either answer.
inline SqueezeVerdict squeezeVerdict(const Result &r, double releaseAt, int wpm) {
    constexpr double kTie = .000001;
    SqueezeVerdict v;
    const auto elements = elementsFrom(r.sent, wpm);
    std::vector<double> heard;
    for (const auto &[when, down] : r.edges)
        if (down)
            heard.push_back(when);
    if (elements.empty() || heard.empty())
        return v;
    const size_t count = std::min(heard.size(), elements.size());
    size_t window = 0;
    for (size_t i = 0; i < count; ++i)
        if (releaseAt > heard[i] + kTie)
            window = i;
    std::vector<size_t> windows{window};
    if (window + 1 < count && releaseAt > heard[window + 1] - kTie)
        windows.push_back(window + 1);

    auto owedBy = [&](size_t i) -> std::vector<std::string> {
        if (elements[i] == "dit")
            return {"dah"};
        const double into = releaseAt - heard[i];
        const double delay = r.ditMemSeconds;
        if (into > delay + kTie)
            return {"dit"};
        if (into < delay - kTie)
            return {"nothing"};
        return {"dit", "nothing"};
    };

    for (size_t i : windows) {
        for (const auto &owed : owedBy(i)) {
            std::vector<std::string> wanted;
            if (owed != "nothing")
                wanted.push_back(owed);
            std::vector<std::string> rest(elements.begin() + static_cast<long>(i) + 1, elements.end());
            if (rest == wanted)
                return {elements[i], owed, rest, true};
        }
    }
    v.during = elements[window];
    v.owed = owedBy(window).front();
    v.after.assign(elements.begin() + static_cast<long>(window) + 1, elements.end());
    return v;
}

// A squeeze is close enough to a tone's start that either answer is right.
inline bool squeezeReleaseIsTie(const Result &r, double releaseAt) {
    for (const auto &[when, down] : r.edges)
        if (down && std::fabs(releaseAt - when) <= .000001)
            return true;
    return false;
}

// Begun with the paddle closed first, alternating from there.
inline bool dahFirstVerdict(const Result &r, int wpm) {
    const auto e = elementsFrom(r.sent, wpm);
    if (e.empty() || e.front() != "dah")
        return false;
    for (size_t i = 1; i < e.size(); ++i)
        if (e[i] == e[i - 1])
            return false;
    return true;
}

// The S, the gap the operator left, and the T, whatever the stall. The gap is measured from the
// S's keyed end, and elements are keyed in whole milliseconds rounded up. So where a dit isn't a
// whole number of ms (35, 45 WPM), the gap comes out a few ms under the operator's. toleranceMs
// allows for that; 0 is exact.
inline bool stallVerdict(const Result &r, int wpm, long long gapMs, std::string *gapOut = nullptr,
                         long long toleranceMs = 0) {
    const auto e = elementsFrom(r.sent, wpm);
    std::vector<std::string> kzds;
    for (const auto &c : r.sent)
        if (c.rfind("KZD", 0) == 0)
            kzds.push_back(c);
    const std::string gap = kzds.size() > 3 ? kzds[3] : std::string();
    if (gapOut)
        *gapOut = gap;
    if (e != std::vector<std::string>{"dit", "dit", "dit", "dah"} || gap.size() != 8)
        return false;
    const long long got = std::stoll(gap.substr(3, 4));
    return got <= gapMs && gapMs - got <= toleranceMs;
}

} // namespace PaddleSim

#endif // PADDLEKEYERSIM_H
