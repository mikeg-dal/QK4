#include "audio/rxplayout.h"
#include "utils/radioutils.h"

#include <QtTest>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <random>
#include <vector>

// RX playout sizing and the jitter buffer (audio/rxplayout.h), plus a millisecond simulation of the
// whole RX path - network arrivals, the app queue, the feed timer, the sink and an OS device that
// drains it a period at a time - run with the jitter buffer and with the two policies before it.
// There is no K4 or audio device in CI, so the simulation is the evidence for the sizes chosen.
namespace {

// Uniform integers in [0, bound], identical on every platform.
//
// WHY not std::uniform_int_distribution: the standard fixes std::mt19937's output sequence but not
// how a distribution maps it onto a range, and libstdc++ (Linux) and libc++ (macOS) map it
// differently. The same seed therefore gave each platform its own jitter sequence, and a row that
// passed on macOS failed on Linux CI. The mapping below is Lemire's unbiased multiply-and-reject,
// which is also what libstdc++ uses, so Linux sees the sequence it always saw.
class PortableJitter {
public:
    explicit PortableJitter(std::uint32_t seed) : m_engine(seed) {}

    int next(int bound) {
        if (bound <= 0)
            return 0;
        const std::uint32_t range = static_cast<std::uint32_t>(bound) + 1;
        std::uint64_t product = std::uint64_t(static_cast<std::uint32_t>(m_engine())) * range;
        std::uint32_t low = static_cast<std::uint32_t>(product);
        if (low < range) {
            const std::uint32_t threshold = static_cast<std::uint32_t>(-range) % range;
            while (low < threshold) {
                product = std::uint64_t(static_cast<std::uint32_t>(m_engine())) * range;
                low = static_cast<std::uint32_t>(product);
            }
        }
        return static_cast<int>(product >> 32);
    }

private:
    std::mt19937 m_engine; // its sequence is fixed by the standard; only the mapping above is ours
};

// Which policy the simulated engine runs.
enum class Policy {
    JitterBuffer,    // RxPlayout::JitterBuffer, driven in AudioEngine::feedAudioDevice's order
    AdaptByUnderrun, // its predecessor: play after one packet, grow only by underrunning, trim at
                     // highWaterBytes() back to two packets. Kept here as the "before" reference.
    Legacy500,       // the engine before that: 500 ms sink, queue-only trimming at enqueue
};

struct SimConfig {
    Policy policy = Policy::JitterBuffer;
    int packetMs = 20;
    int devicePeriodMs = 10;
    int jitterMs = 0; // each packet delayed by a uniform 0..jitterMs, order kept (TCP)
    int phaseMs = 0;  // every packet this much after its slot: off the feed's 5 ms grid
    std::uint32_t seed = 12345;
    int stallAtMs = -1; // packets due in [stallAtMs, stallAtMs + stallMs) all arrive at the end
    int stallMs = 0;
    int pauseAtMs = -1; // packets due in [pauseAtMs, pauseAtMs + pauseMs) are never sent (we transmit)
    int pauseMs = 0;
    int pauseLastLateMs = 0; // the last packet before the pause arrives this much late (the radio's final one)
    int pauseBurstMs = 0;    // packets due in the first pauseBurstMs after it arrive together at its end
    bool pauseKnown = false; // QK4 knows it is transmitting: the engine tells the jitter buffer
    int spikeFromMs = -1;    // packets due in [spikeFromMs, spikeToMs) get spikeJitterMs instead
    int spikeToMs = -1;
    int spikeJitterMs = 0;
    int devicePpm = 0; // device clock fast (+) or slow (-) against the radio's
    // A real LAN: mostly jitterMs, but one packet in rareEvery arrives up to rareJitterMs late
    // instead (Mike's wired-LAN log, 2026-11-02: 10-15 ms with peaks to 30 ms at SL3).
    int rareEvery = 0;
    int rareJitterMs = 0;
    int durationMs = 20000;
    int steadyFromMs = 10000; // start of the window the mean latency is taken over
};

struct SimResult {
    int underruns = 0;            // over the whole run
    int underrunsAfterWarmup = 0; // after the first ten seconds
    // Underruns while the feed had a packet waiting that it did not write: the sink, not the
    // network, ran dry. This is what DEVICE_MARGIN_MS exists to prevent. Every other underrun is
    // the network delivering a packet later than everything buffered could cover.
    int sinkStarvations = 0;
    int dropped = 0;
    int maxBufferedLastSecondMs = 0; // queue + sink, the latency a listener hears
    int maxBufferedAfterStallMs = 0; // max over the second that starts 1 s after the stall ends
    double meanBufferedMs = 0;       // queue + sink, averaged from steadyFromMs to the end
    // Jitter buffer only. The first moment its estimate reached the jitter the network applies
    // (within 1 ms), and the underruns after it: the estimator has seen the jitter level by then.
    int settledAtMs = -1;
    int underrunsAfterSettled = 0;
    int underrunsAfterMs = 0; // after SimConfig::spikeToMs + 1 s, or the pause's end + 1 s
    int silenceInserts = 0;
    int shrinkSkips = 0;
    int backlogTrims = 0;
    int finalJitterMs = 0;
    int finalTargetMs = 0;
    // Around SimConfig's pause: the target and jitter just before it, how far the target strays from
    // that from one packet after the first packet back, and the most audio buffered from then on.
    int targetBeforePauseMs = -1;
    int jitterBeforePauseMs = -1;
    int maxTargetDriftAfterPauseMs = 0;
    int maxBufferedAfterPauseMs = 0;
    // From a second after the first packet back; three when the pause ends in a burst under the
    // high-water mark, which the shrink eases out at up to SHRINK_STEP_MAX_MS a second.
    int maxBufferedSettledAfterPauseMs = 0;
    int maxBufferedOneSecondAfterPauseMs = 0;
    int stalls = 0;
};

SimResult simulate(const SimConfig &c) {
    using namespace RxPlayout;
    const int pkt = c.packetMs * BYTES_PER_MS;
    const bool legacy = c.policy == Policy::Legacy500;
    const int cap = legacy ? 500 * BYTES_PER_MS : sinkBufferBytes(c.packetMs);
    constexpr int kMaxQueue = 1000 * BYTES_PER_MS;
    constexpr int kOldTargetPackets = 2; // what both earlier policies trimmed back to

    PortableJitter jitter(c.seed);
    std::vector<int> arrivals;
    int last = 0;
    for (int due = c.packetMs; due < c.durationMs; due += c.packetMs) {
        if (c.pauseAtMs >= 0 && due >= c.pauseAtMs && due < c.pauseAtMs + c.pauseMs)
            continue;
        const bool spike = due >= c.spikeFromMs && due < c.spikeToMs;
        const bool rare = c.rareEvery > 0 && (due / c.packetMs) % c.rareEvery == 0;
        int at = due + c.phaseMs + jitter.next(spike ? c.spikeJitterMs : (rare ? c.rareJitterMs : c.jitterMs));
        if (c.stallAtMs >= 0 && due >= c.stallAtMs && due < c.stallAtMs + c.stallMs)
            at = c.stallAtMs + c.stallMs + (due - c.stallAtMs) / c.packetMs; // the backlog, at link speed
        if (c.pauseAtMs >= 0 && due < c.pauseAtMs && due + c.packetMs >= c.pauseAtMs)
            at += c.pauseLastLateMs;
        const int resumeMs = c.pauseAtMs + c.pauseMs;
        if (c.pauseAtMs >= 0 && due >= resumeMs && due < resumeMs + c.pauseBurstMs)
            at = resumeMs + c.pauseBurstMs + (due - resumeMs) / c.packetMs;
        at = std::max(at, last);
        last = at;
        arrivals.push_back(at);
    }
    const int afterMs =
        c.spikeToMs >= 0 ? c.spikeToMs + 1000 : (c.pauseAtMs >= 0 ? c.pauseAtMs + c.pauseMs + 1000 : -1);

    SimResult r;
    JitterBuffer jb;
    std::deque<int> queue; // bytes left in each queued packet
    std::size_t seen = 0;  // queued packets, from the front, the jitter buffer has observed
    int queued = 0;
    int sink = 0;
    bool playing = false;
    bool packetWaitingAfterFeed = false;
    size_t next = 0;
    double owed = 0; // device drift: audio the device has consumed beyond whole frames
    double sumBuffered = 0;
    int samples = 0;
    const int stallEnd = c.stallAtMs + c.stallMs;
    int firstBackMs = -1; // the first packet after the pause
    bool wasPaused = false;

    auto popFront = [&] {
        queue.pop_front();
        if (seen > 0)
            --seen;
    };

    for (int t = 0; t < c.durationMs; ++t) {
        while (next < arrivals.size() && arrivals[next] <= t) {
            if (c.pauseAtMs >= 0 && firstBackMs < 0 && arrivals[next] >= c.pauseAtMs + c.pauseMs)
                firstBackMs = t;
            ++next;
            if (legacy && queued + pkt > HIGH_WATER_PACKETS * pkt) { // the old marks, queue only
                while (queued > kOldTargetPackets * pkt && !queue.empty()) {
                    queued -= queue.front();
                    popFront();
                    ++r.dropped;
                }
            }
            while (queued + pkt > kMaxQueue && !queue.empty()) {
                queued -= queue.front();
                popFront();
                ++r.dropped;
            }
            queue.push_back(pkt);
            queued += pkt;
        }

        if (t % FEED_INTERVAL_MS == 0) {
            const std::int64_t nowUs = std::int64_t(t) * 1000;
            if (c.policy == Policy::JitterBuffer) {
                // AudioEngine::feedAudioDevice, step for step.
                const bool paused = c.pauseKnown && t >= c.pauseAtMs && t < c.pauseAtMs + c.pauseMs;
                if (paused != wasPaused) {
                    if (paused)
                        jb.receivePaused();
                    else
                        jb.receiveResumed();
                    wasPaused = paused;
                }
                jb.observeSink(sink, nowUs);
                if (!queue.empty()) {
                    int through = sink;
                    for (std::size_t i = 0; i < queue.size(); ++i) {
                        through += queue[i];
                        if (i >= seen)
                            jb.observePacket(pkt, through, nowUs);
                    }
                    seen = queue.size();
                    jb.decide(queued + sink, pkt, nowUs);
                    int skip = jb.takeSkip(queued);
                    while (skip > 0) {
                        if (skip >= queue.front()) {
                            skip -= queue.front();
                            queued -= queue.front();
                            popFront();
                            ++r.dropped;
                            continue;
                        }
                        queue.front() -= skip;
                        queued -= skip;
                        skip = 0;
                    }
                }
                while (jb.pendingSilenceBytes() > 0 && cap - sink >= FRAME_BYTES) {
                    int n = std::min(jb.pendingSilenceBytes(), cap - sink);
                    n -= n % FRAME_BYTES;
                    sink += n;
                    jb.silenceWritten(n);
                }
            } else if (c.policy == Policy::AdaptByUnderrun && !queue.empty()) {
                if (queued + sink > highWaterBytes(pkt)) {
                    while (queued + sink > kOldTargetPackets * pkt && queue.size() > 1) {
                        queued -= queue.front();
                        popFront();
                        ++r.dropped;
                    }
                }
            }
            while (!queue.empty() && jb.pendingSilenceBytes() == 0) {
                const int n = legacy ? (queue.front() <= cap - sink ? queue.front() : 0)
                                     : bytesToWrite(queue.front(), cap - sink, sink);
                if (n <= 0)
                    break;
                sink += n;
                queued -= n;
                queue.front() -= n;
                if (queue.front() > 0)
                    break;
                popFront();
            }
            if (c.policy == Policy::JitterBuffer) {
                jb.noteWritten(sink);
                if (c.pauseAtMs >= 0 && r.targetBeforePauseMs < 0 && t >= c.pauseAtMs - c.packetMs) {
                    r.targetBeforePauseMs = jb.targetBytes() / BYTES_PER_MS;
                    r.jitterBeforePauseMs = jb.jitterMs();
                }
                if (firstBackMs >= 0 && t >= firstBackMs + c.packetMs)
                    r.maxTargetDriftAfterPauseMs =
                        std::max(r.maxTargetDriftAfterPauseMs,
                                 std::abs(jb.targetBytes() / BYTES_PER_MS - r.targetBeforePauseMs));
                if (r.settledAtMs < 0 && next > 0 && jb.jitterMs() >= c.jitterMs - 1)
                    r.settledAtMs = t;
            }
            playing = playing || sink > 0;
            packetWaitingAfterFeed = !queue.empty();
        }

        if (t % c.devicePeriodMs == 0 && playing) {
            owed += c.devicePeriodMs * BYTES_PER_MS * (1.0 + c.devicePpm * 1e-6);
            const int need = int(owed) - int(owed) % FRAME_BYTES;
            owed -= need;
            if (sink < need) {
                ++r.underruns;
                if (t >= 10000)
                    ++r.underrunsAfterWarmup;
                if (r.settledAtMs >= 0 && t > r.settledAtMs)
                    ++r.underrunsAfterSettled;
                if (afterMs >= 0 && t >= afterMs)
                    ++r.underrunsAfterMs;
                if (packetWaitingAfterFeed)
                    ++r.sinkStarvations;
            }
            sink -= std::min(sink, need);
        }

        const int bufferedMs = (queued + sink) / BYTES_PER_MS;
        if (t >= c.steadyFromMs) {
            sumBuffered += (queued + sink) / double(BYTES_PER_MS);
            ++samples;
        }
        if (firstBackMs >= 0)
            r.maxBufferedAfterPauseMs = std::max(r.maxBufferedAfterPauseMs, bufferedMs);
        if (firstBackMs >= 0 && t >= firstBackMs + 1000)
            r.maxBufferedOneSecondAfterPauseMs = std::max(r.maxBufferedOneSecondAfterPauseMs, bufferedMs);
        if (firstBackMs >= 0 && t >= firstBackMs + (c.pauseBurstMs > 0 ? 3000 : 1000))
            r.maxBufferedSettledAfterPauseMs = std::max(r.maxBufferedSettledAfterPauseMs, bufferedMs);
        if (t >= c.durationMs - 1000)
            r.maxBufferedLastSecondMs = std::max(r.maxBufferedLastSecondMs, bufferedMs);
        if (c.stallAtMs >= 0 && t >= stallEnd + 1000 && t < stallEnd + 2000)
            r.maxBufferedAfterStallMs = std::max(r.maxBufferedAfterStallMs, bufferedMs);
    }
    r.meanBufferedMs = samples ? sumBuffered / samples : 0;
    r.silenceInserts = jb.silenceInserts();
    r.shrinkSkips = jb.shrinkSkips();
    r.backlogTrims = jb.backlogTrims();
    r.finalJitterMs = jb.jitterMs();
    r.finalTargetMs = jb.targetBytes() / BYTES_PER_MS;
    r.stalls = jb.stalls();
    return r;
}

// What a settled buffer may hold beyond packet + jitter + device period: the target slack, the grow
// headroom, the shrink hysteresis and the feed's 5 ms look.
constexpr int kSteadyExtraMs = RxPlayout::JitterBuffer::TARGET_SLACK_MS + RxPlayout::JitterBuffer::GROW_HEADROOM_MS +
                               RxPlayout::JitterBuffer::SHRINK_MIN_MS + RxPlayout::FEED_INTERVAL_MS;
// How long a jitter peak is remembered.
constexpr int kWindowMs = RxPlayout::JitterBuffer::WINDOW_BUCKETS * 1000;

// Feeds the estimator packets at given lateness, without a sink, to test the measurement alone.
struct EstimatorDriver {
    RxPlayout::JitterBuffer jb;
    int packetMs = 20;
    std::int64_t dueUs = 0;
    void packet(int latenessMs) {
        dueUs += packetMs * 1000;
        jb.observePacket(packetMs * RxPlayout::BYTES_PER_MS, 0, dueUs + latenessMs * 1000);
    }
    void packets(int n, int latenessMs) {
        for (int i = 0; i < n; ++i)
            packet(latenessMs);
    }
    void pause(int ms) { dueUs += ms * 1000; } // the radio sends nothing
};

} // namespace

class TestRxPlayout : public QObject {
    Q_OBJECT
private slots:
    void sinkSizePerSlTier_data();
    void sinkSizePerSlTier();
    void backlogMarkScalesWithPacket();
    void bytesToWriteWholePacketWhenItFits();
    void bytesToWritePartialOnlyWhenStarving();
    void targetFollowsMeasuredJitter();
    void stallIsNotPlannedFor();
    void radioPauseKeepsLearnedJitter();
    void targetIsBounded();
    void burstBacklogIsTrimmed_data();
    void burstBacklogIsTrimmed();
    void steadyJitterSettlesWithoutUnderruns_data();
    void steadyJitterSettlesWithoutUnderruns();
    void jitterSweepHasNoUnderrunsOnceMeasured_data();
    void jitterSweepHasNoUnderrunsOnceMeasured();
    void bufferShrinksBackAfterJitterSpike_data();
    void bufferShrinksBackAfterJitterSpike();
    void transmitPauseKeepsLatency();
    void transmitPauseNeverEntersTheEstimate_data();
    void transmitPauseNeverEntersTheEstimate();
    void deviceClockDriftIsAbsorbed_data();
    void deviceClockDriftIsAbsorbed();
    void steadyLanSettlesWithoutCorrections_data();
    void steadyLanSettlesWithoutCorrections();
    void declickerJoinsASkipWithoutAStep();
    void declickerGlidesIntoAndOutOfSilence();
    void declickerLeavesContiguousAudioUntouched();
    void declickerResumesFromSilenceAfterTheDeviceRanDry();
    void declickerNeverPassesFullScale();
};

void TestRxPlayout::sinkSizePerSlTier_data() {
    QTest::addColumn<int>("sl");
    QTest::addColumn<int>("sinkMs");
    QTest::newRow("SL0") << 0 << 50;
    QTest::newRow("SL1") << 1 << 70;
    QTest::newRow("SL2") << 2 << 70;
    QTest::newRow("SL3") << 3 << 90;
    QTest::newRow("SL5") << 5 << 90;
    QTest::newRow("SL6") << 6 << 150;
    QTest::newRow("SL7") << 7 << 150;
}

void TestRxPlayout::sinkSizePerSlTier() {
    QFETCH(int, sl);
    QFETCH(int, sinkMs);
    const int packetMs = RxPlayout::packetMsForFrameSamples(RadioUtils::slTierToFrameSamples(sl));
    QCOMPARE(RxPlayout::sinkBufferMs(packetMs), sinkMs);
    // One whole packet must always fit with the margin left over, or the feed could never write.
    QVERIFY(RxPlayout::sinkBufferBytes(packetMs) >=
            packetMs * RxPlayout::BYTES_PER_MS + RxPlayout::DEVICE_MARGIN_MS * RxPlayout::BYTES_PER_MS);
    // Far below the 500 ms it replaced, for every tier.
    QVERIFY(RxPlayout::sinkBufferMs(packetMs) <= 150);
}

void TestRxPlayout::backlogMarkScalesWithPacket() {
    // In packets, never fixed milliseconds: five packets, capped at 300 ms but never under three.
    QCOMPARE(RxPlayout::highWaterBytes(1920), 5 * 1920);   // SL0: 100 ms
    QCOMPARE(RxPlayout::highWaterBytes(3840), 5 * 3840);   // SL1-2: 200 ms
    QCOMPARE(RxPlayout::highWaterBytes(5760), 5 * 5760);   // SL3-5: 300 ms
    QCOMPARE(RxPlayout::highWaterBytes(11520), 3 * 11520); // SL6-7: 360 ms, not 600
    QCOMPARE(RxPlayout::highWaterBytes(0), 0);
    QCOMPARE(RxPlayout::highWaterBytes(-5), 0);
}

void TestRxPlayout::bytesToWriteWholePacketWhenItFits() {
    QCOMPARE(RxPlayout::bytesToWrite(1920, 4800, 0), 1920);
    QCOMPARE(RxPlayout::bytesToWrite(1920, 1920, 2880), 1920);
    QCOMPARE(RxPlayout::bytesToWrite(0, 4800, 0), 0);
    QCOMPARE(RxPlayout::bytesToWrite(1920, 0, 4800), 0);
}

void TestRxPlayout::bytesToWritePartialOnlyWhenStarving() {
    const int margin = RxPlayout::DEVICE_MARGIN_MS * RxPlayout::BYTES_PER_MS;
    // Plenty still in the sink: wait for room for the whole packet.
    QCOMPARE(RxPlayout::bytesToWrite(11520, 5000, margin + 8), 0);
    // About to starve: write what fits, in whole stereo frames.
    QCOMPARE(RxPlayout::bytesToWrite(11520, 5003, margin), 5000);
    QCOMPARE(RxPlayout::bytesToWrite(11520, 5003, 0) % RxPlayout::FRAME_BYTES, 0);
}

// The estimate is the spread of arrival times over the window (kWindowMs): up at once on the packet
// that shows it, held while it stays in the window, gone once it has left.
void TestRxPlayout::targetFollowsMeasuredJitter() {
    using RxPlayout::BYTES_PER_MS;
    using RxPlayout::JitterBuffer;
    EstimatorDriver e;
    e.packets(100, 0);
    QCOMPARE(e.jb.jitterMs(), 0);
    QCOMPARE(e.jb.targetBytes(), JitterBuffer::TARGET_SLACK_MS * BYTES_PER_MS); // the floor
    // A constant network delay is not jitter.
    e.packets(100, 40);
    QCOMPARE(e.jb.jitterMs(), 40);          // the step itself is a 40 ms spread while both are in the window
    e.packets((kWindowMs + 2000) / 20, 40); // two seconds past the window, the 0 ms packets have left it
    QCOMPARE(e.jb.jitterMs(), 0);
    // One packet 30 ms late raises the target on that packet.
    e.packet(70);
    QCOMPARE(e.jb.jitterMs(), 30);
    QCOMPARE(e.jb.targetBytes(), (30 + JitterBuffer::TARGET_SLACK_MS) * BYTES_PER_MS);
    e.packets((kWindowMs - 2000) / 20, 40); // two seconds short of the window: still held
    QCOMPARE(e.jb.jitterMs(), 30);
    e.packets(150, 40); // a second past it: dropped
    QCOMPARE(e.jb.jitterMs(), 0);
}

// A packet more than JITTER_MAX_MS late is a stall: it and the backlog behind it are kept out of
// the estimate, so a one-off hiccup does not hold its length in latency for the whole window.
void TestRxPlayout::stallIsNotPlannedFor() {
    EstimatorDriver e;
    for (int i = 0; i < 200; ++i)
        e.packet(i % 2 ? 10 : 0);
    QCOMPARE(e.jb.jitterMs(), 10);
    // 400 ms of packets held back, then delivered together.
    for (int i = 0; i < 20; ++i)
        e.packet(400 - i * 20);
    QVERIFY(e.jb.stalls() == 1);
    e.packets(5, 0);
    QVERIFY(!e.jb.inStall());
    QCOMPARE(e.jb.jitterMs(), 10);
}

// When the radio stops sending (we transmit) and resumes, every later packet looks late by the
// pause. The timeline is re-anchored instead, and the jitter learned before it is kept.
void TestRxPlayout::radioPauseKeepsLearnedJitter() {
    EstimatorDriver e;
    for (int i = 0; i < 200; ++i)
        e.packet(i % 2 ? 12 : 0);
    QCOMPARE(e.jb.jitterMs(), 12);
    e.pause(3000);
    for (int i = 0; i < 200; ++i)
        e.packet(i % 2 ? 12 : 0);
    QVERIFY(!e.jb.inStall());
    QCOMPARE(e.jb.jitterMs(), 12);
    e.packets((kWindowMs + 2000) / 20, 0); // and it still decays normally afterwards
    QCOMPARE(e.jb.jitterMs(), 0);
}

void TestRxPlayout::targetIsBounded() {
    using RxPlayout::BYTES_PER_MS;
    using RxPlayout::JitterBuffer;
    EstimatorDriver e;
    e.packets(50, 0);
    e.packet(JitterBuffer::JITTER_MAX_MS - 1); // the most still taken as jitter
    QCOMPARE(e.jb.jitterMs(), JitterBuffer::JITTER_MAX_MS - 1);
    QVERIFY(e.jb.targetBytes() <= (JitterBuffer::JITTER_MAX_MS + JitterBuffer::TARGET_SLACK_MS) * BYTES_PER_MS);
    e.packets(50, 0);
    e.packet(JitterBuffer::JITTER_MAX_MS + 50); // a stall: not planned for
    QCOMPARE(e.jb.jitterMs(), JitterBuffer::JITTER_MAX_MS - 1);
    QVERIFY(e.jb.targetBytes() % RxPlayout::FRAME_BYTES == 0);
}

void TestRxPlayout::burstBacklogIsTrimmed_data() {
    QTest::addColumn<int>("packetMs");
    QTest::addColumn<int>("devicePeriodMs");
    for (int p : {20, 40, 60, 120}) {
        for (int d : {10, 21}) {
            QTest::newRow(qPrintable(QStringLiteral("%1ms packets, %2ms device").arg(p).arg(d))) << p << d;
        }
    }
}

// A 400 ms stall (an I/O thread or Wi-Fi hiccup) then everything at once. The old engine moved the
// burst into its 500 ms sink, out of the policy's reach, and played it late for the rest of the
// session. The jitter buffer must be back to its target within a second of the burst.
void TestRxPlayout::burstBacklogIsTrimmed() {
    QFETCH(int, packetMs);
    QFETCH(int, devicePeriodMs);
    SimConfig c;
    c.packetMs = packetMs;
    c.devicePeriodMs = devicePeriodMs;
    c.jitterMs = 5;
    c.stallAtMs = 5000;
    c.stallMs = 400;
    const SimResult now = simulate(c);
    c.policy = Policy::Legacy500;
    const SimResult before = simulate(c);

    qInfo("%3d ms packets, %2d ms device: after a 400 ms burst, buffered %3d ms (was %3d ms); "
          "end of run %3d ms (was %3d ms); dropped %d (was %d); underruns %d (was %d); trims %d",
          packetMs, devicePeriodMs, now.maxBufferedAfterStallMs, before.maxBufferedAfterStallMs,
          now.maxBufferedLastSecondMs, before.maxBufferedLastSecondMs, now.dropped, before.dropped, now.underruns,
          before.underruns, now.backlogTrims);

    // Bounded by the high-water mark plus one packet in flight - never the 500 ms sink.
    const int bound =
        RxPlayout::highWaterBytes(packetMs * RxPlayout::BYTES_PER_MS) / RxPlayout::BYTES_PER_MS + packetMs;
    QVERIFY2(now.maxBufferedAfterStallMs <= bound, qPrintable(QString::number(now.maxBufferedAfterStallMs)));
    QVERIFY(now.maxBufferedLastSecondMs <= bound);
    // And strictly less than the backlog the old engine kept.
    QVERIFY(now.maxBufferedAfterStallMs < before.maxBufferedAfterStallMs);
    // The backlog went at once, by the trim - not by a second-by-second shrink.
    QVERIFY(now.backlogTrims >= 1);
}

void TestRxPlayout::steadyJitterSettlesWithoutUnderruns_data() {
    QTest::addColumn<int>("packetMs");
    QTest::addColumn<int>("devicePeriodMs");
    QTest::addColumn<int>("jitterMs");
    // LAN (a few ms) and Wi-Fi (tens of ms) arrival jitter, against both device periods.
    for (int p : {20, 40, 60, 120}) {
        for (int d : {10, 21}) {
            for (int j : {3, 15, 30}) {
                QTest::newRow(qPrintable(QStringLiteral("%1ms/%2ms/jitter %3").arg(p).arg(d).arg(j))) << p << d << j;
            }
        }
    }
}

// One minute at a steady jitter, compared with the adapt-by-underrun policy the jitter buffer
// replaced. That policy started after one packet and moved playout later only by underrunning, so a
// new worst lateness could underrun at any point in the session: with this sequence, at 17 s on
// 40ms/21ms/jitter 30. The jitter buffer holds the measured spread ahead of time instead.
//
// Two different things can underrun, and only one of them is the sink's size:
//   - the sink running dry while the feed had a packet it could not write. That is DEVICE_MARGIN_MS
//     being too small for the device period, and it must never happen, warm-up included;
//   - the network delivering a packet later than everything buffered could cover. That is what the
//     jitter buffer's target is for.
void TestRxPlayout::steadyJitterSettlesWithoutUnderruns() {
    QFETCH(int, packetMs);
    QFETCH(int, devicePeriodMs);
    QFETCH(int, jitterMs);
    SimConfig c;
    c.packetMs = packetMs;
    c.devicePeriodMs = devicePeriodMs;
    c.jitterMs = jitterMs;
    c.durationMs = 60000;
    c.steadyFromMs = 30000;
    const SimResult r = simulate(c);
    c.policy = Policy::AdaptByUnderrun;
    const SimResult before = simulate(c);
    qInfo("%3d ms packets, %2d ms device, jitter %2d ms: underruns %d (%d after warm-up), sink starvations %d, "
          "mean buffered %.0f ms, at end %d ms | adapt-by-underrun: underruns %d (%d after warm-up), "
          "mean buffered %.0f ms",
          packetMs, devicePeriodMs, jitterMs, r.underruns, r.underrunsAfterWarmup, r.sinkStarvations, r.meanBufferedMs,
          r.maxBufferedLastSecondMs, before.underruns, before.underrunsAfterWarmup, before.meanBufferedMs);

    QVERIFY2(r.sinkStarvations == 0,
             qPrintable(QStringLiteral("the sink ran dry %1 times with a packet waiting").arg(r.sinkStarvations)));
    QVERIFY(r.maxBufferedLastSecondMs <=
            RxPlayout::highWaterBytes(packetMs * RxPlayout::BYTES_PER_MS) / RxPlayout::BYTES_PER_MS + packetMs);
    QVERIFY2(r.underrunsAfterWarmup == 0,
             qPrintable(
                 QStringLiteral("%1 underruns after warm-up, %2 total").arg(r.underrunsAfterWarmup).arg(r.underruns)));
}

void TestRxPlayout::jitterSweepHasNoUnderrunsOnceMeasured_data() {
    QTest::addColumn<int>("packetMs");
    QTest::addColumn<int>("devicePeriodMs");
    QTest::addColumn<int>("jitterMs");
    // 11 ms: a device period that is not a multiple of the 5 ms feed, like CoreAudio's 10.7 ms.
    for (int p : {20, 40, 60, 120}) {
        for (int d : {10, 11, 21}) {
            for (int j : {0, 15, 30}) {
                QTest::newRow(qPrintable(QStringLiteral("%1ms/%2ms/jitter %3").arg(p).arg(d).arg(j))) << p << d << j;
            }
        }
    }
}

// The guarantee the jitter buffer makes, across 200 jitter sequences per row, each with its packets
// at a different phase to the feed: once its estimate has reached the jitter the network applies,
// no packet underruns - not one, in any sequence - and the sink never starves at all. One sequence
// passing is luck; the adapt-by-underrun policy passed most of them too, and failed up to half of
// them after warm-up at 15-30 ms of jitter on an 11 or 21 ms device.
//
// And the latency is the smallest that does it: at most one packet + the jitter + the device period
// + kSteadyExtraMs, in every sequence.
void TestRxPlayout::jitterSweepHasNoUnderrunsOnceMeasured() {
    QFETCH(int, packetMs);
    QFETCH(int, devicePeriodMs);
    QFETCH(int, jitterMs);
    constexpr int kSeeds = 200;
    constexpr int kSettleByMs = 40000;
    int failingSeeds = 0;
    int underrunsAfterSettled = 0;
    int starvations = 0;
    int latestSettle = 0;
    int worstEndMs = 0;
    double meanMs = 0;
    int beforeFailingSeeds = 0;
    double beforeMeanMs = 0;
    for (int s = 1; s <= kSeeds; ++s) {
        SimConfig c;
        c.packetMs = packetMs;
        c.devicePeriodMs = devicePeriodMs;
        c.jitterMs = jitterMs;
        c.seed = std::uint32_t(s);
        c.phaseMs = s % RxPlayout::FEED_INTERVAL_MS;
        c.durationMs = 60000;
        c.steadyFromMs = 30000;
        const SimResult r = simulate(c);
        failingSeeds += r.underrunsAfterSettled > 0;
        underrunsAfterSettled += r.underrunsAfterSettled;
        starvations += r.sinkStarvations;
        latestSettle = std::max(latestSettle, r.settledAtMs < 0 ? c.durationMs : r.settledAtMs);
        worstEndMs = std::max(worstEndMs, r.maxBufferedLastSecondMs);
        meanMs += r.meanBufferedMs / kSeeds;
        c.policy = Policy::AdaptByUnderrun;
        const SimResult b = simulate(c);
        beforeFailingSeeds += b.underrunsAfterWarmup > 0;
        beforeMeanMs += b.meanBufferedMs / kSeeds;
    }
    qInfo("%3d ms packets, %2d ms device, jitter %2d ms: mean buffered %5.1f ms (adapt-by-underrun %5.1f ms), "
          "worst at end %3d ms; sequences underrunning after settle %d/%d (adapt-by-underrun, after 10 s: %d/%d); "
          "settled by %d ms",
          packetMs, devicePeriodMs, jitterMs, meanMs, beforeMeanMs, worstEndMs, failingSeeds, kSeeds,
          beforeFailingSeeds, kSeeds, latestSettle);

    QVERIFY2(latestSettle <= kSettleByMs, qPrintable(QStringLiteral("estimate settled at %1 ms").arg(latestSettle)));
    QVERIFY2(starvations == 0, qPrintable(QStringLiteral("%1 sink starvations").arg(starvations)));
    QVERIFY2(underrunsAfterSettled == 0,
             qPrintable(QStringLiteral("%1 underruns after the estimate settled, in %2 of %3 sequences")
                            .arg(underrunsAfterSettled)
                            .arg(failingSeeds)
                            .arg(kSeeds)));
    const int bound = packetMs + jitterMs + devicePeriodMs + kSteadyExtraMs;
    QVERIFY2(worstEndMs <= bound, qPrintable(QStringLiteral("%1 ms buffered, bound %2").arg(worstEndMs).arg(bound)));
}

void TestRxPlayout::bufferShrinksBackAfterJitterSpike_data() {
    QTest::addColumn<int>("packetMs");
    QTest::addColumn<int>("devicePeriodMs");
    for (int p : {20, 40, 60, 120}) {
        for (int d : {10, 21}) {
            QTest::newRow(qPrintable(QStringLiteral("%1ms packets, %2ms device").arg(p).arg(d))) << p << d;
        }
    }
}

// Ten seconds of 60 ms Wi-Fi jitter on a 3 ms LAN. The buffer must grow to cover it, and after it
// ends, once it has left the window, ease back to the latency of a run that never had it.
void TestRxPlayout::bufferShrinksBackAfterJitterSpike() {
    QFETCH(int, packetMs);
    QFETCH(int, devicePeriodMs);
    SimConfig base;
    base.packetMs = packetMs;
    base.devicePeriodMs = devicePeriodMs;
    base.jitterMs = 3;
    base.durationMs = 20000 + kWindowMs + 15000; // the spike ends at 20 s
    base.steadyFromMs = base.durationMs - 5000;
    const SimResult calm = simulate(base);

    SimConfig spike = base;
    spike.spikeFromMs = 10000;
    spike.spikeToMs = 20000;
    spike.spikeJitterMs = 60;
    const SimResult after = simulate(spike);
    SimConfig during = spike;
    during.durationMs = 20000;
    during.steadyFromMs = 15000;
    const SimResult inSpike = simulate(during);

    qInfo("%3d ms packets, %2d ms device: target %d ms calm, %d ms in the spike, %d ms at the end; mean "
          "buffered %.0f / %.0f / %.0f ms; underruns after it %d; %d shrink skips",
          packetMs, devicePeriodMs, calm.finalTargetMs, inSpike.finalTargetMs, after.finalTargetMs, calm.meanBufferedMs,
          inSpike.meanBufferedMs, after.meanBufferedMs, after.underrunsAfterMs, after.shrinkSkips);

    // It grew for the spike. (Mean buffered audio rises by less than the target: under jitter the
    // late packets spend less time in the buffer, which is the point.)
    QVERIFY(inSpike.finalTargetMs >= 55);
    QVERIFY(inSpike.meanBufferedMs >= calm.meanBufferedMs + 15);
    // And came back down, to the latency of a run that never had it.
    QCOMPARE(after.finalTargetMs, calm.finalTargetMs);
    QVERIFY(after.meanBufferedMs <= calm.meanBufferedMs + 5);
    QVERIFY(after.shrinkSkips >= 1); // by the shrink, a step at a time
    QCOMPARE(after.underrunsAfterMs, 0);
    QCOMPARE(after.sinkStarvations, 0);
}

// Transmitting: the radio sends nothing for a while, then resumes. The pause is not jitter - the
// estimate must come back to the network's, and the latency to what it was.
void TestRxPlayout::transmitPauseKeepsLatency() {
    for (int p : {20, 120}) {
        SimConfig c;
        c.packetMs = p;
        c.devicePeriodMs = 21;
        c.jitterMs = 5;
        c.durationMs = 30000;
        c.steadyFromMs = 20000;
        const SimResult calm = simulate(c);
        c.pauseAtMs = 10000;
        c.pauseMs = 3000;
        const SimResult r = simulate(c);
        qInfo("%3d ms packets: after a 3 s pause jitter %d ms, mean buffered %.0f ms (no pause %.0f ms), "
              "underruns after it %d",
              p, r.finalJitterMs, r.meanBufferedMs, calm.meanBufferedMs, r.underrunsAfterMs);
        QVERIFY(r.finalJitterMs <= 5);
        QVERIFY(r.meanBufferedMs <= calm.meanBufferedMs + 5);
        QCOMPARE(r.underrunsAfterMs, 0);
    }
}

void TestRxPlayout::transmitPauseNeverEntersTheEstimate_data() {
    QTest::addColumn<int>("pauseMs");
    QTest::addColumn<int>("lastLateMs");
    QTest::addColumn<int>("burstMs");
    QTest::addColumn<bool>("known");
    for (bool known : {false, true}) {
        for (int pause : {500, 1000, 1500, 2100, 5000, 12000}) {
            for (int late : {0, 250}) {
                for (int burst : {0, 100}) {
                    QTest::newRow(qPrintable(QStringLiteral("%1 %2 ms pause, last packet +%3 ms, burst %4 ms")
                                                 .arg(known ? "known" : "unannounced")
                                                 .arg(pause)
                                                 .arg(late)
                                                 .arg(burst)))
                        << pause << late << burst << known;
                }
            }
        }
    }
}

// Mike's bench log, 2026-10-29 (SL0, 11 ms device, 10-26 ms of jitter): after a 2 s transmit the
// estimate read 2100 ms, the target sat at its 202 ms cap with ~230 ms buffered until the backlog
// trim, and one pause counted as 99 stalls. The last packet before the transmit had arrived over
// 200 ms late and opened a stall; its settle timer had long expired when the radio resumed, so the
// first packet back was re-anchored against that stale packet and entered the window as a 1.9 s
// delay, after which every packet looked like a new stall. A pause of any length, announced or not,
// however packets straddle it, must never reach the estimate: the target is back to its value before
// the pause one packet after the first packet back, the buffer is back within the packet, the
// learned jitter, the device period and kSteadyExtraMs once the shrink has run, and one pause is
// one stall.
void TestRxPlayout::transmitPauseNeverEntersTheEstimate() {
    QFETCH(int, pauseMs);
    QFETCH(int, lastLateMs);
    QFETCH(int, burstMs);
    QFETCH(bool, known);
    SimConfig c;
    c.packetMs = 20;
    c.devicePeriodMs = 11;
    c.jitterMs = 20;
    c.phaseMs = 2;
    c.durationMs = 20000 + pauseMs + 10000;
    c.pauseAtMs = 20000;
    c.pauseMs = pauseMs;
    c.pauseLastLateMs = lastLateMs;
    c.pauseBurstMs = burstMs;
    c.pauseKnown = known;
    const SimResult r = simulate(c);
    qInfo("target %d ms before the pause, drifts %d ms after it; jitter %d ms before, %d ms at end; most buffered "
          "after it %d ms, %d ms from a second after; %d stalls; underruns after it %d",
          r.targetBeforePauseMs, r.maxTargetDriftAfterPauseMs, r.jitterBeforePauseMs, r.finalJitterMs,
          r.maxBufferedAfterPauseMs, r.maxBufferedSettledAfterPauseMs, r.stalls, r.underrunsAfterMs);

    QVERIFY(r.targetBeforePauseMs > RxPlayout::JitterBuffer::TARGET_SLACK_MS); // it had learned the jitter
    QVERIFY2(r.maxTargetDriftAfterPauseMs <= 3, qPrintable(QStringLiteral("target moved %1 ms from %2 ms")
                                                               .arg(r.maxTargetDriftAfterPauseMs)
                                                               .arg(r.targetBeforePauseMs)));
    // Once the shrink has run - a second after the first packet back, three when a burst under the
    // high-water mark has to be eased out at SHRINK_STEP_MAX_MS a second - the buffer is held to the
    // steady-state bound.
    const int bound = c.packetMs + r.jitterBeforePauseMs + c.devicePeriodMs + kSteadyExtraMs;
    QVERIFY2(r.maxBufferedSettledAfterPauseMs <= bound,
             qPrintable(QStringLiteral("%1 ms buffered, bound %2").arg(r.maxBufferedSettledAfterPauseMs).arg(bound)));
    if (burstMs > 0) {
        // A burst is real audio arriving at once: held to the burst contract (burstBacklogIsTrimmed).
        const int burstBound =
            RxPlayout::highWaterBytes(c.packetMs * RxPlayout::BYTES_PER_MS) / RxPlayout::BYTES_PER_MS + c.packetMs;
        QVERIFY2(r.maxBufferedOneSecondAfterPauseMs <= burstBound,
                 qPrintable(QStringLiteral("%1 ms buffered, burst bound %2")
                                .arg(r.maxBufferedOneSecondAfterPauseMs)
                                .arg(burstBound)));
    } else {
        // The first packet back is taken as on time: how late it really was cannot be known when it
        // arrives, and assuming less risks an underrun. So until the shrink the resume can overshoot
        // by at most that packet's lateness - at most the learned jitter.
        QVERIFY2(r.maxBufferedAfterPauseMs <= bound + r.jitterBeforePauseMs,
                 qPrintable(QStringLiteral("%1 ms buffered at the resume, bound %2")
                                .arg(r.maxBufferedAfterPauseMs)
                                .arg(bound + r.jitterBeforePauseMs)));
    }
    QVERIFY2(r.stalls == 1, qPrintable(QStringLiteral("%1 stalls for one pause").arg(r.stalls)));
    QCOMPARE(r.underrunsAfterMs, 0);
}

void TestRxPlayout::deviceClockDriftIsAbsorbed_data() {
    QTest::addColumn<int>("ppm");
    QTest::newRow("device 100 ppm fast") << 100;
    QTest::newRow("device 100 ppm slow") << -100;
}

// The radio's sample clock and the sound card's are never the same. A device running fast drains
// the buffer slowly, a slow one fills it; both must be corrected without underruns, and rarely
// enough not to be heard as a stream of clicks (the grow headroom and shrink hysteresis).
void TestRxPlayout::deviceClockDriftIsAbsorbed() {
    QFETCH(int, ppm);
    for (int p : {20, 120}) {
        SimConfig c;
        c.packetMs = p;
        c.devicePeriodMs = 10;
        c.jitterMs = 5;
        c.devicePpm = ppm;
        c.durationMs = 180000;
        c.steadyFromMs = 160000;
        const SimResult r = simulate(c);
        qInfo("%3d ms packets, device %+d ppm: underruns %d after warm-up, %d silence inserts, %d shrink skips, "
              "mean buffered %.0f ms",
              p, ppm, r.underrunsAfterWarmup, r.silenceInserts, r.shrinkSkips, r.meanBufferedMs);
        QCOMPARE(r.underrunsAfterWarmup, 0);
        // 100 ppm is 18 ms over three minutes; a correction at most every ~20 s.
        QVERIFY(r.silenceInserts + r.shrinkSkips <= 12);
        QVERIFY(r.meanBufferedMs <= p + 5 + 10 + 15);
    }
}

void TestRxPlayout::steadyLanSettlesWithoutCorrections_data() {
    QTest::addColumn<int>("packetMs");
    QTest::addColumn<int>("rareEvery");
    for (int p : {20, 40, 60, 120})
        for (int every : {10, 30, 100})
            QTest::addRow("%d ms packets, a late one every %d", p, every) << p << every;
}

// Mike's wired-LAN log, 2026-11-02 (SL3, 60 ms packets): jitter 10-15 ms with a packet now and then up
// to 30 ms late, and a correction - each one a splice he heard as a click - about every 7 s, because a
// late packet that recurs every few seconds kept leaving a 10 s window: the buffer shrank, and the
// next one found it short (here, underruns in 4-29 of 20 runs per row). With the peak remembered for
// a minute the buffer settles: at most about one correction a minute, and underruns only from a
// packet later than any seen before, which no estimate can cover.
void TestRxPlayout::steadyLanSettlesWithoutCorrections() {
    QFETCH(int, packetMs);
    QFETCH(int, rareEvery);
    int total = 0;
    int underruns = 0;
    for (std::uint32_t seed = 1; seed <= 20; ++seed) {
        SimConfig c;
        c.packetMs = packetMs;
        c.devicePeriodMs = 11;
        c.jitterMs = 12;
        c.rareEvery = rareEvery;
        c.rareJitterMs = 30;
        c.seed = seed;
        c.durationMs = 120000;
        c.steadyFromMs = 60000;
        SimResult warm;
        {
            SimConfig w = c;
            w.durationMs = 30000;
            warm = simulate(w);
        }
        const SimResult r = simulate(c);
        const int afterWarmup = (r.silenceInserts + r.shrinkSkips + r.backlogTrims) -
                                (warm.silenceInserts + warm.shrinkSkips + warm.backlogTrims);
        underruns += r.underrunsAfterWarmup;
        total += afterWarmup;
    }
    const double perMinute = total / 20.0 / 1.5;
    qInfo("%d ms packets, late one every %d: %.1f corrections per minute after warm-up, %d underruns (20 runs)",
          packetMs, rareEvery, perMinute, underruns);
    QVERIFY2(perMinute <= 1.5, qPrintable(QStringLiteral("%1 corrections a minute").arg(perMinute)));
    QVERIFY2(underruns <= 2, qPrintable(QStringLiteral("%1 underruns in 20 runs").arg(underruns)));
}

namespace {
// Interleaved stereo tone: left and right at different phases, as two receivers would be.
std::vector<float> tone(int frames, double hz, float amplitude, double phase = 0) {
    std::vector<float> v(std::size_t(frames) * 2);
    for (int i = 0; i < frames; ++i) {
        constexpr double kPi = 3.14159265358979323846;
        const double t = 2 * kPi * hz * i / 12000.0 + phase;
        v[2 * i] = amplitude * float(std::sin(t));
        v[2 * i + 1] = amplitude * float(std::cos(t * 0.7));
    }
    return v;
}

// The largest sample-to-sample change on either channel, across all frames.
float largestStep(const std::vector<float> &v) {
    float worst = 0;
    for (std::size_t i = 2; i < v.size(); ++i)
        worst = (std::max)(worst, std::abs(v[i] - v[i - 2]));
    return worst;
}
} // namespace

// A shrink skip cuts from one point of the stream to a later one. Whatever is cut, at whatever phase,
// the output across the cut must be no steeper than the audio itself - no step, so no click.
void TestRxPlayout::declickerJoinsASkipWithoutAStep() {
    constexpr int kFrames = 4000;
    const std::vector<float> in = tone(kFrames, 1000, 0.5f);
    const float natural = largestStep(in);
    float worstRaw = 0;
    for (int at = 300; at < 420; at += 7) {
        for (int skip = 1; skip < 600; skip += 13) {
            RxPlayout::Declicker d;
            std::vector<float> first(in.begin(), in.begin() + 2 * at);
            d.process(first.data(), at); // the stream's start fades in from silence; that is not the join
            std::vector<float> rest(in.begin() + 2 * (at + skip), in.end());
            d.splice();
            d.process(rest.data(), kFrames - at - skip);
            std::vector<float> out(first.end() - 2 * (RxPlayout::Declicker::RAMP_FRAMES + 1), first.end());
            out.insert(out.end(), rest.begin(), rest.end());
            QVERIFY2(largestStep(out) <= natural * 1.6f,
                     qPrintable(QStringLiteral("cut at %1, skip %2: step %3, the tone's own %4")
                                    .arg(at)
                                    .arg(skip)
                                    .arg(largestStep(out))
                                    .arg(natural)));
            worstRaw = (std::max)(worstRaw, std::abs(in[2 * (at + skip)] - in[2 * (at - 1)]));
        }
    }
    QVERIFY(worstRaw > natural * 3); // guard: the raw splices really were steps
}

// Inserted silence starts and ends mid-waveform: the output glides down to zero and back up, and
// after RAMP_FRAMES it is exactly silence, then exactly the audio.
void TestRxPlayout::declickerGlidesIntoAndOutOfSilence() {
    constexpr int kRamp = RxPlayout::Declicker::RAMP_FRAMES;
    const std::vector<float> in = tone(2000, 700, 0.6f, 0.4);
    const float natural = largestStep(in);
    RxPlayout::Declicker d;
    std::vector<float> a(in.begin(), in.begin() + 1000);
    d.process(a.data(), 500);
    d.splice();
    std::vector<float> silence(2 * 300, 0.0f);
    d.process(silence.data(), 300);
    QVERIFY(std::abs(silence[0] - a[a.size() - 2]) <= natural); // continues from the last frame
    for (int i = kRamp; i < 300; ++i)
        QCOMPARE(silence[2 * i], 0.0f);
    d.splice();
    std::vector<float> b(in.begin() + 1000, in.end());
    d.process(b.data(), 1000);
    QVERIFY(std::abs(b[0]) <= natural); // continues from silence
    for (int i = kRamp; i < 1000; ++i)
        QCOMPARE(b[2 * i], in[1000 + 2 * i]); // then exactly the audio
    std::vector<float> all = silence;
    all.insert(all.end(), b.begin(), b.end());
    QVERIFY(largestStep(all) <= natural * 1.6f);
    QVERIFY(largestStep(silence) <= natural * 1.6f);
}

// No splice, no change: contiguous audio passes bit for bit once the stream's opening fade is over,
// however it is cut into writes.
void TestRxPlayout::declickerLeavesContiguousAudioUntouched() {
    const std::vector<float> in = tone(3000, 1300, 0.8f);
    RxPlayout::Declicker d;
    std::vector<float> out = in;
    int done = 0;
    for (int n : {17, 1, 240, 720, 3, 1000, 1019})
        d.process(out.data() + 2 * done, n), done += n;
    QCOMPARE(done, 3000);
    QVERIFY(std::abs(out[0]) < 1e-6f); // a new stream fades in from silence
    for (int i = RxPlayout::Declicker::RAMP_FRAMES; i < 3000; ++i) {
        QCOMPARE(out[2 * i], in[2 * i]);
        QCOMPARE(out[2 * i + 1], in[2 * i + 1]);
    }
    QVERIFY(!d.ramping());
}

// After an underrun the device has played its own silence: the next audio rises from zero rather than
// jumping from the last frame we wrote, which the listener never heard continue.
void TestRxPlayout::declickerResumesFromSilenceAfterTheDeviceRanDry() {
    const std::vector<float> in = tone(1000, 900, 0.7f, 1.0);
    RxPlayout::Declicker d;
    std::vector<float> a(in.begin(), in.begin() + 800);
    d.process(a.data(), 400);
    d.deviceRanDry();
    std::vector<float> b(in.begin() + 800, in.end());
    d.process(b.data(), 600);
    QVERIFY(std::abs(b[0]) < 1e-6f);
    QVERIFY(std::abs(b[1]) < 1e-6f);
    QVERIFY(largestStep(b) <= largestStep(in) * 1.6f);
}

// The audio reaching the declicker is already limited to full scale; a glide between opposite peaks
// must not push it past, or the sink's hard clip is the click the glide was meant to remove.
void TestRxPlayout::declickerNeverPassesFullScale() {
    RxPlayout::Declicker d;
    std::vector<float> before(2 * 200, 0.9f); // ends at +0.9 on both channels
    d.process(before.data(), 200);
    d.splice();
    std::vector<float> after(2 * 200);
    for (int i = 0; i < 200; ++i)
        after[2 * i] = after[2 * i + 1] = i == 0 ? -0.9f : 0.95f; // the cut lands on -0.9, then a +0.95 peak
    d.process(after.data(), 200);
    for (float v : after)
        QVERIFY2(std::abs(v) <= 1.0f, qPrintable(QStringLiteral("%1 past full scale").arg(v)));
}

QTEST_GUILESS_MAIN(TestRxPlayout)
#include "test_rxplayout.moc"
