#include "network/networkmetrics.h"
#include <QtTest>
#include <cmath>

// Covers the 10-second sparkline history added to NetworkMetrics: append on RTT/buffer updates,
// the size-cap trim, reset on (re)connect, and that the existing jitter std-dev is unchanged.
class TestNetworkMetrics : public QObject {
    Q_OBJECT
private slots:
    void historyAppendsOnLatency();
    void historyAppendsOnBuffer();
    void historySizeCapped();
    void historyClearedOnReconnect();
    void jitterStdDevUnchanged();
    void rxPlayoutCountersCountFromConnect();
    void bufferThresholdsFollowThePacket();
};

void TestNetworkMetrics::historyAppendsOnLatency() {
    NetworkMetrics m;
    QCOMPARE(static_cast<int>(m.rttHistory().size()), 0);
    m.onLatencyChanged(42);
    QCOMPARE(static_cast<int>(m.rttHistory().size()), 1);
    QCOMPARE(static_cast<int>(m.jitterHistory().size()), 1); // jitter trails alongside RTT
    QCOMPARE(m.rttHistory().back().value, 42.0f);
}

void TestNetworkMetrics::historyAppendsOnBuffer() {
    NetworkMetrics m;
    m.onBufferStatus(9600); // 9600 bytes / 96 = 100 ms
    QCOMPARE(static_cast<int>(m.bufferHistory().size()), 1);
    QCOMPARE(m.bufferHistory().back().value, 100.0f);
}

void TestNetworkMetrics::historySizeCapped() {
    NetworkMetrics m;
    // Rapid pushes (all within the 30 s window) must still be bounded by the hard size cap
    // (HISTORY_MAX_SAMPLES = 4000) so a fast producer can't grow the deque without bound.
    for (int i = 0; i < 5000; ++i)
        m.onBufferStatus(96 * (i % 50));
    QVERIFY(static_cast<int>(m.bufferHistory().size()) <= 4000);
}

void TestNetworkMetrics::historyClearedOnReconnect() {
    NetworkMetrics m;
    m.onLatencyChanged(10);
    m.onBufferStatus(960);
    QVERIFY(!m.rttHistory().empty());
    m.onConnectionStateChanged(true); // a fresh connection resets all trails
    QCOMPARE(static_cast<int>(m.rttHistory().size()), 0);
    QCOMPARE(static_cast<int>(m.jitterHistory().size()), 0);
    QCOMPARE(static_cast<int>(m.bufferHistory().size()), 0);
}

void TestNetworkMetrics::jitterStdDevUnchanged() {
    NetworkMetrics m;
    m.onLatencyChanged(10);
    m.onLatencyChanged(20);
    m.onLatencyChanged(30);
    QCOMPARE(m.rttAvg(), 20.0);
    const double expected = std::sqrt(200.0 / 3.0); // population stddev of {10,20,30}
    QVERIFY(std::abs(m.rttJitter() - expected) < 1e-6);
}

void TestNetworkMetrics::rxPlayoutCountersCountFromConnect() {
    NetworkMetrics m;
    m.onRxPlayoutCounters(7, 3); // left over from an earlier connection: the engine's are cumulative
    m.onConnectionStateChanged(true);
    QCOMPARE(m.rxUnderruns(), 0u);
    QCOMPARE(m.rxDroppedPackets(), 0u);
    m.onRxPlayoutCounters(7, 3); // first sample after connect is the baseline
    m.onRxPlayoutCounters(9, 4);
    QCOMPARE(m.rxUnderruns(), 2u);
    QCOMPARE(m.rxDroppedPackets(), 1u);
}

// A healthy SL6-7 buffer (one 120 ms packet + jitter target + device period, ~180 ms) sat at the flat
// 200 ms yellow line. The thresholds keep the same headroom over a normal steady state at every tier,
// and SL0-5 keep the flat 200 / 500 ms.
void TestNetworkMetrics::bufferThresholdsFollowThePacket() {
    using T = NetworkMetrics;
    for (int packetMs : {20, 40, 60}) {
        QCOMPARE(T::bufferTier(200, packetMs), T::Green);
        QCOMPARE(T::bufferTier(201, packetMs), T::Yellow);
        QCOMPARE(T::bufferTier(500, packetMs), T::Yellow);
        QCOMPARE(T::bufferTier(501, packetMs), T::Orange);
    }
    QCOMPARE(T::bufferTier(200, 120), T::Green); // SL6-7 steady state: green, not yellow
    QCOMPARE(T::bufferTier(260, 120), T::Green);
    QCOMPARE(T::bufferTier(261, 120), T::Yellow);
    QCOMPARE(T::bufferTier(561, 120), T::Orange);
}

QTEST_MAIN(TestNetworkMetrics)
#include "test_networkmetrics.moc"
