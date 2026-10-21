// SPDX-License-Identifier: GPL-3.0-or-later
//
// PaddleKeyer as the app runs it: its own thread, the real steady clock and its sleeps, paddle
// events from another thread, releases and close from a third. The virtual-clock suites pin what
// is keyed; this one pins that the threads agree. That covers the locking, the queue wake-up and
// the shutdown join. Run it under ThreadSanitizer (scheduled-sanitizers.yml) as well as plainly.
// Timing assertions are loose on purpose: a loaded CI runner is not a quiet bench.

#include "audio/sidetonescheduler.h"
#include "hardware/keyerruntime.h"
#include "hardware/paddlekeyer.h"
#include "utils/rtsan.h"

#include <QtTest/QtTest>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <random>
#include <thread>

namespace {

class CaptureLink : public CwKeying::Link {
public:
    void send(std::string_view c) override {
        // WHY allowed: a test double recording what the keyer sends. The real link, KzEgress::post,
        // is checked on its own.
        RtSan::Allow allow;
        std::lock_guard lock(m_mutex);
        m_sent.emplace_back(c);
    }
    std::vector<std::string> sent() {
        std::lock_guard lock(m_mutex);
        return m_sent;
    }

private:
    std::mutex m_mutex;
    std::vector<std::string> m_sent;
};

class NullTimer : public CwKeying::OneShotTimer {
public:
    void start(double, std::function<void()>) override {}
    void cancel() override {}
};

struct Rig {
    SteadyKeyerClock clock;
    CaptureLink link;
    NullTimer timer;
    // No audio stream: sidetone requests are refused and the keyer works from its own estimates,
    // as it does while no output device is open.
    SidetoneScheduler sidetone{clock, []() -> std::optional<double> { return std::nullopt; }};
    PaddleKeyer keyer{clock, link, sidetone, timer};
    std::thread thread;

    explicit Rig(int wpm = 40) {
        keyer.setSpeed(wpm);
        thread = std::thread([this] { keyer.run(); });
    }
    ~Rig() {
        if (thread.joinable()) {
            keyer.close();
            thread.join();
        }
    }
    void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
};

int openKzd(const std::vector<std::string> &sent) {
    int open = 0;
    for (const auto &c : sent)
        open += c.rfind("KZD", 0) == 0 ? 1 : c.rfind("KZU", 0) == 0 ? -1 : 0;
    return open;
}

// The KZ frames as the radio would act on them: a KZU before its KZD, a second KZD while keyed,
// or a run that ends keyed is a fault. Returns an empty string when the sequence is sound.
std::string kzFault(const std::vector<std::string> &sent) {
    bool keyed = false;
    std::string seq;
    for (const auto &c : sent) {
        if (c.rfind("KZ", 0) != 0)
            continue;
        seq += c.substr(0, 3) + ' ';
        const bool down = c.rfind("KZD", 0) == 0;
        if (down == keyed)
            return (down ? "KZD while keyed: " : "KZU before its KZD: ") + seq;
        keyed = down;
    }
    if (keyed)
        return "left keyed: " + seq;
    return {};
}

// A link whose ML0000 takes a while to hand over, as a busy I/O queue can. The straight key sends
// ML0000 just before its KZD, so this holds the keyer between deciding to key and sending the KZD.
class SlowMuteLink : public CaptureLink {
public:
    void send(std::string_view c) override {
        RtSan::Allow allow; // WHY allowed: a test double that stalls on purpose
        if (c == "ML0000;")
            std::this_thread::sleep_for(std::chrono::microseconds(1500));
        CaptureLink::send(c);
    }
};

struct StraightRaceRig {
    SteadyKeyerClock clock;
    SlowMuteLink link;
    NullTimer timer;
    SidetoneScheduler sidetone{clock, []() -> std::optional<double> { return std::nullopt; }};
    PaddleKeyer keyer{clock, link, sidetone, timer};
    std::thread thread;

    StraightRaceRig() {
        keyer.setSpeed(30);
        keyer.setStraightKeying(true);
        keyer.setMuteRadioMonitor(true);
        thread = std::thread([this] { keyer.run(); });
    }
    std::vector<std::string> closeAndCollect() {
        keyer.close();
        thread.join();
        return link.sent();
    }
};

// A link that holds the keyer in its first send until opened, so the test can fill the keyer's
// event queue past capacity while nothing drains it.
class GateLink : public CaptureLink {
public:
    void send(std::string_view c) override {
        RtSan::Allow allow; // WHY allowed: a test double that blocks the keyer on purpose
        {
            std::unique_lock lock(m_gateMutex);
            m_entered = true;
            m_changed.notify_all();
            m_changed.wait(lock, [this] { return m_open; });
        }
        CaptureLink::send(c);
    }
    void waitUntilHeld() {
        std::unique_lock lock(m_gateMutex);
        m_changed.wait(lock, [this] { return m_entered; });
    }
    void open() {
        std::lock_guard lock(m_gateMutex);
        m_open = true;
        m_changed.notify_all();
    }

private:
    std::mutex m_gateMutex;
    std::condition_variable m_changed;
    bool m_entered = false;
    bool m_open = false;
};

enum class StopBy { Release, Inhibit };

// One press of a straight key on a device thread, stopped from this thread at `offsetUs` after the
// press: by a release (the key comes up later and its DitUp arrives) or by an inhibit (the DitUp is
// dropped at the door, so nothing but the keyer's own release can unkey the radio).
std::vector<std::string> straightKeyStoppedAt(StopBy how, int offsetUs) {
    StraightRaceRig rig;
    std::atomic<bool> pressed{false};
    std::thread device([&] {
        rig.keyer.paddleEvent(PaddleKeyer::Event::DitDown, 0.050);
        pressed = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        rig.keyer.paddleEvent(PaddleKeyer::Event::DitUp, 0.005);
    });
    while (!pressed)
        std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::microseconds(offsetUs));
    if (how == StopBy::Release)
        rig.keyer.releasePaddles();
    else
        rig.keyer.setInhibited(true);
    device.join();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    return rig.closeAndCollect();
}

} // namespace

class TestPaddleKeyerThreaded : public QObject {
    Q_OBJECT

private slots:
    // A dit held for 200 ms at 40 WPM: a run of dits, each KZU exactly 30 ms, the gaps 30 ms,
    // and nothing left open after the release.
    void heldDitOnRealThreads() {
        Rig rig(40);
        std::thread device([&] {
            rig.keyer.paddleEvent(PaddleKeyer::Event::DitDown, 0);
            rig.sleepMs(200);
            rig.keyer.paddleEvent(PaddleKeyer::Event::DitUp, 0);
        });
        device.join();
        rig.sleepMs(150);
        const auto sent = rig.link.sent();
        QVERIFY(sent.size() >= 6);
        QCOMPARE(sent.front(), std::string("KZD0000;"));
        for (size_t i = 1; i < sent.size(); ++i) {
            if (sent[i].rfind("KZU", 0) == 0)
                QCOMPARE(sent[i], std::string("KZU0030;"));
            else
                QCOMPARE(sent[i], std::string("KZD0030;"));
        }
        QCOMPARE(openKzd(sent), 0);
    }

    // A squeeze from the device thread while the main thread releases mid-burst: the radio is
    // left unkeyed and the keyer thread still exits on close.
    void releaseFromAnotherThreadMidBurst() {
        Rig rig(30);
        std::thread device([&] {
            rig.keyer.paddleEvent(PaddleKeyer::Event::DitDown, 0);
            rig.keyer.paddleEvent(PaddleKeyer::Event::DahDown, 0);
        });
        device.join();
        rig.sleepMs(250);
        rig.keyer.releasePaddles();
        rig.sleepMs(250);
        const auto sent = rig.link.sent();
        QVERIFY(!sent.empty());
        QCOMPARE(openKzd(sent), 0);
        rig.keyer.close();
        rig.thread.join();
    }

    // Inhibit mid-burst (KPOD+ took over, mode left CW): keying stops, nothing open, and events
    // while inhibited key nothing.
    void inhibitStopsKeying() {
        Rig rig(30);
        rig.keyer.paddleEvent(PaddleKeyer::Event::DitDown, 0);
        rig.sleepMs(150);
        rig.keyer.setInhibited(true);
        rig.sleepMs(150);
        const auto before = rig.link.sent();
        const int finishingElement = openKzd(before);
        QVERIFY(finishingElement == 0 || finishingElement == 1);
        rig.keyer.paddleEvent(PaddleKeyer::Event::DahDown, 0);
        rig.sleepMs(150);
        // Inhibit releases the paddles; an element already started still sends its matching
        // KZU. A loaded runner can delay that cleanup beyond the first settling interval.
        QTRY_COMPARE_WITH_TIMEOUT(openKzd(rig.link.sent()), 0, 2000);
        const auto sent = rig.link.sent();
        QCOMPARE(sent.size(), before.size() + static_cast<size_t>(finishingElement));
        for (size_t i = before.size(); i < sent.size(); ++i)
            QVERIFY2(sent[i].rfind("KZU", 0) == 0, sent[i].c_str());
        QCOMPARE(openKzd(sent), 0);
    }

    // A straight key held at quit gets its KZU from close(), before the thread is joined.
    void closeEndsAStraightKeyHeldDown() {
        Rig rig(30);
        rig.keyer.setStraightKeying(true);
        rig.keyer.paddleEvent(PaddleKeyer::Event::DitDown, 0);
        rig.sleepMs(100);
        rig.keyer.close();
        rig.thread.join();
        const auto sent = rig.link.sent();
        QCOMPARE(sent.size(), size_t(2));
        QCOMPARE(sent[0].substr(0, 3), std::string("KZD"));
        QCOMPARE(sent[1].substr(0, 3), std::string("KZU"));
        const int ms = std::stoi(sent[1].substr(3, 4));
        QVERIFY2(ms >= 90 && ms < 1000, sent[1].c_str());
    }

    // A straight key worked on the device thread while another thread releases: the release reads
    // the key-down time the device thread writes, so ThreadSanitizer flags any read outside the
    // writer's lock. Plainly, every KZU stays within the 5 s cap and nothing is left open.
    void straightKeyReleaseRacesKeyDown() {
        Rig rig(30);
        rig.keyer.setStraightKeying(true);
        std::atomic<bool> done{false};
        std::thread device([&] {
            for (int i = 0; i < 5000; ++i) {
                rig.keyer.paddleEvent(PaddleKeyer::Event::DitDown, 0.001);
                rig.keyer.paddleEvent(PaddleKeyer::Event::DitUp, 0.001);
            }
            done = true;
        });
        while (!done)
            rig.keyer.releasePaddles();
        device.join();
        rig.keyer.close();
        rig.thread.join();
        const auto sent = rig.link.sent();
        QCOMPARE(openKzd(sent), 0);
        const std::string fault = kzFault(sent);
        QVERIFY2(fault.empty(), fault.c_str());
        for (const auto &c : sent)
            QVERIFY2(std::stoi(c.substr(3, 4)) <= 5000, c.c_str());
    }

    // More straight-key edges than the keyer's queue holds, while the keyer is stuck in a send: the
    // queue keeps an even run ending on a key-down, and the key-up after it is dropped. Once the
    // keyer drains, it must find the key up and unkey the radio by itself, not only at close().
    void straightKeyUpLostToAFullQueueStillUnkeys() {
        SteadyKeyerClock clock;
        GateLink link;
        NullTimer timer;
        SidetoneScheduler sidetone{clock, []() -> std::optional<double> { return std::nullopt; }};
        PaddleKeyer keyer{clock, link, sidetone, timer};
        keyer.setSpeed(30);
        keyer.setStraightKeying(true);
        std::thread thread([&] { keyer.run(); });

        keyer.paddleEvent(PaddleKeyer::Event::DitDown, 0.010);
        link.waitUntilHeld(); // the keyer is sending this KZD and drains nothing
        keyer.paddleEvent(PaddleKeyer::Event::DitUp, 0.010);
        for (int i = 0; i < 1100; ++i) {
            keyer.paddleEvent(PaddleKeyer::Event::DitDown, 0.010);
            keyer.paddleEvent(PaddleKeyer::Event::DitUp, 0.010);
        }
        link.open();

        std::string fault = "not drained";
        for (int i = 0; i < 200 && !fault.empty(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            fault = kzFault(link.sent());
        }
        QVERIFY2(fault.empty(), fault.c_str());

        keyer.close();
        thread.join();
        fault = kzFault(link.sent());
        QVERIFY2(fault.empty(), fault.c_str());
    }

    // A straight key stopped from another thread at random offsets around the moment the keyer
    // commits its KZD. The KZ frames must come out in the keyer's order, KZD then KZU, and the
    // radio must never be left keyed, whether the stop is a release or an inhibit. Each run is
    // checked after close() has joined the keyer, so its sequence is complete.
    void straightKeyStopNeverOutrunsItsKzd() {
        std::mt19937 rng(20261029);
        std::uniform_int_distribution<int> offsetUs(0, 2500);
        for (StopBy how : {StopBy::Release, StopBy::Inhibit}) {
            for (int i = 0; i < 150; ++i) {
                const int offset = offsetUs(rng);
                const auto sent = straightKeyStoppedAt(how, offset);
                const std::string fault = kzFault(sent);
                const std::string where = std::string(how == StopBy::Release ? "release" : "inhibit") + " at " +
                                          std::to_string(offset) + " us: " + fault;
                QVERIFY2(fault.empty(), where.c_str());
            }
        }
    }

    // The main thread releases while a first element's sidetone edge is scheduled but not yet
    // placed (no audio callback runs here). Cancelling that edge waits up to 100 ms for the audio
    // side; the wait belongs to the keyer, never to the thread that asked for the release.
    void releaseNeverWaitsOnTheCaller() {
        SteadyKeyerClock clock;
        CaptureLink link;
        NullTimer timer;
        SidetoneScheduler sidetone{clock, [&clock]() -> std::optional<double> { return clock.now(); }};
        PaddleKeyer keyer{clock, link, sidetone, timer};
        keyer.setSpeed(30);
        keyer.paddleEvent(PaddleKeyer::Event::DitDown, 0); // schedules the first element's tone
        QElapsedTimer t;
        t.start();
        keyer.releasePaddles();
        QVERIFY2(t.elapsed() < 20, qPrintable(QString::number(t.elapsed()) + " ms"));
        std::thread keyerThread([&keyer] { keyer.run(); });
        keyer.close();
        keyerThread.join();
        QCOMPARE(openKzd(link.sent()), 0);
    }

    // Close with nothing keyed returns promptly: the loop wakes on the Close event.
    void closeWhenIdleJoinsPromptly() {
        Rig rig;
        QElapsedTimer t;
        t.start();
        rig.keyer.close();
        rig.thread.join();
        QVERIFY(t.elapsed() < 500);
        QVERIFY(rig.link.sent().empty());
    }
};

QTEST_GUILESS_MAIN(TestPaddleKeyerThreaded)
#include "test_paddlekeyerthreaded.moc"
