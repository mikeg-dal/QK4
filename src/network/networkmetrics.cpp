#include "networkmetrics.h"
#include <algorithm>
#include <cmath>

namespace {
// Summary interval: how often we re-derive `HealthTier` and fire `healthTierChanged`. 2 s keeps
// the NetHealth LED responsive without thrashing consumers; must be ≥ ping interval so at least
// one RTT sample can land per interval (otherwise `m_stalePingCount` falsely trips Red).
constexpr int kSummaryIntervalMs = 2000;

// Buffer-fill thresholds for HealthTier, in milliseconds of decoded audio. Over 500 ms means packets
// queued during a network event and never flushed.
//
// WHY they move with the packet above SL5: a healthy buffer holds one packet plus the jitter target,
// device period and margins - about 120 ms at SL3's 60 ms packets, about 180 ms at SL6-7's 120 ms. A
// flat 200 ms left SL6-7 at the yellow line on a good link. Packet + 140 ms keeps SL3's ~80 ms of
// headroom at every tier, and never goes below the flat values, so SL0-5 are unchanged.
constexpr double kBufferMsWarnYellow = 200.0;
constexpr double kBufferMsWarnOrange = 500.0;
constexpr double kBufferMsYellowOverPacket = 140.0;
constexpr double kBufferMsOrangeOverPacket = 440.0;
} // namespace

NetworkMetrics::HealthTier NetworkMetrics::bufferTier(double bufferedMs, int packetMs) {
    const double yellow = (std::max)(kBufferMsWarnYellow, packetMs + kBufferMsYellowOverPacket);
    const double orange = (std::max)(kBufferMsWarnOrange, packetMs + kBufferMsOrangeOverPacket);
    if (bufferedMs > orange)
        return Orange;
    if (bufferedMs > yellow)
        return Yellow;
    return Green;
}

NetworkMetrics::NetworkMetrics(QObject *parent) : QObject(parent) {
    m_summaryTimer = new QTimer(this);
    m_summaryTimer->setInterval(kSummaryIntervalMs);
    connect(m_summaryTimer, &QTimer::timeout, this, &NetworkMetrics::onSummaryTimer);
    m_clock.start();
}

void NetworkMetrics::pushHistory(std::deque<TimedSample> &history, float value) {
    const qint64 now = m_clock.elapsed();
    history.push_back({now, value});
    while (!history.empty() &&
           (now - history.front().tMs > HISTORY_WINDOW_MS || static_cast<int>(history.size()) > HISTORY_MAX_SAMPLES))
        history.pop_front();
}

void NetworkMetrics::onLatencyChanged(int ms) {
    m_rttCurrent = ms;
    m_stalePingCount = 0;
    m_rttSamples.push_back(ms);
    if (static_cast<int>(m_rttSamples.size()) > RTT_WINDOW)
        m_rttSamples.pop_front();
    updateRttStats();
    // Trail RTT and the freshly recomputed jitter for the sparkline popup.
    pushHistory(m_rttHistory, static_cast<float>(ms));
    pushHistory(m_jitterHistory, static_cast<float>(m_rttJitter));
}

void NetworkMetrics::updateRttStats() {
    if (m_rttSamples.empty())
        return;

    double sum = 0.0;
    for (int s : m_rttSamples) {
        sum += s;
    }
    m_rttAvg = sum / m_rttSamples.size();

    // Jitter = stddev
    double variance = 0.0;
    for (int s : m_rttSamples) {
        double diff = s - m_rttAvg;
        variance += diff * diff;
    }
    m_rttJitter = std::sqrt(variance / m_rttSamples.size());
}

void NetworkMetrics::onAudioSequence() {
    m_audioActive = true;
}

void NetworkMetrics::onBufferStatus(int queueBytes) {
    m_bufferBytes = queueBytes;
    pushHistory(m_bufferHistory, static_cast<float>(bufferMs()));
}

void NetworkMetrics::onRxPlayoutCounters(quint32 underruns, quint32 droppedPackets) {
    m_rxUnderruns = underruns;
    m_rxDropped = droppedPackets;
    if (!m_rxCountersBaselined) {
        m_rxUnderrunsBase = underruns;
        m_rxDroppedBase = droppedPackets;
        m_rxCountersBaselined = true;
    }
}

void NetworkMetrics::onConnectionStateChanged(bool connected) {
    m_connected = connected;
    if (connected) {
        m_summaryTimer->start();
        // Reset state for new connection
        m_rttSamples.clear();
        m_rttHistory.clear();
        m_jitterHistory.clear();
        m_bufferHistory.clear();
        m_rttCurrent = -1;
        m_rttAvg = 0.0;
        m_rttJitter = 0.0;
        m_audioActive = false;
        m_audioWasActive = false;
        m_stalePingCount = 0;
        m_rxCountersBaselined = false;
        m_rxUnderrunsBase = m_rxUnderruns;
        m_rxDroppedBase = m_rxDropped;
        m_tier = Green;
        emit healthTierChanged(m_tier);
    } else {
        m_summaryTimer->stop();
        m_audioActive = false;
        m_rttCurrent = -1;
        m_tier = Red;
        emit healthTierChanged(m_tier);
    }
}

void NetworkMetrics::computeHealthTier() {
    if (!m_connected) {
        if (m_tier != Red) {
            m_tier = Red;
            emit healthTierChanged(m_tier);
        }
        return;
    }

    HealthTier tier = Green;

    // RTT thresholds
    if (m_rttCurrent > 200)
        tier = std::max(tier, Red);
    else if (m_rttCurrent > 100)
        tier = std::max(tier, Orange);
    else if (m_rttCurrent > 50)
        tier = std::max(tier, Yellow);

    // RTT jitter
    if (m_rttJitter > 50.0)
        tier = std::max(tier, Orange);
    else if (m_rttJitter > 20.0)
        tier = std::max(tier, Yellow);

    // Buffer depth: high buffer = playing stale audio (latency accumulation).
    // Thresholds live in the anonymous namespace at file top.
    tier = (std::max)(tier, bufferTier(bufferMs(), m_packetMs));

    // Audio dropout: was receiving packets but stopped
    if (m_audioWasActive && !m_audioActive)
        tier = std::max(tier, Orange);

    // Stale RTT: no PONG replies (ping sent every 1s, summary every 2s → expect ≥1)
    if (m_stalePingCount >= 2)
        tier = std::max(tier, Red);
    else if (m_stalePingCount >= 1)
        tier = std::max(tier, Orange);

    if (m_tier != tier) {
        m_tier = tier;
        emit healthTierChanged(m_tier);
    }
}

void NetworkMetrics::onSummaryTimer() {
    if (!m_connected)
        return;

    computeHealthTier();

    // Track stale ping (reset in onLatencyChanged when PONG arrives)
    m_stalePingCount++;

    if (m_audioActive)
        m_audioWasActive = true;
    m_audioActive = false; // Will be set true again when next audio packet arrives
}
