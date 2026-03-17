#include "networkmetrics.h"
#include "../hardware/iambickeyer.h" // for cwChainMs()
#include <QDebug>
#include <cmath>

NetworkMetrics::NetworkMetrics(QObject *parent) : QObject(parent) {
    m_summaryTimer = new QTimer(this);
    m_summaryTimer->setInterval(2000);
    connect(m_summaryTimer, &QTimer::timeout, this, &NetworkMetrics::onSummaryTimer);
}

void NetworkMetrics::onLatencyChanged(int ms) {
    m_rttCurrent = ms;
    m_stalePingCount = 0;
    m_rttSamples.push_back(ms);
    if (static_cast<int>(m_rttSamples.size()) > RTT_WINDOW)
        m_rttSamples.pop_front();
    updateRttStats();

    // RTT spike event: must exceed both 3x average AND 20ms absolute floor
    if (m_rttSamples.size() > 3 && ms > 20 && (ms > m_rttAvg * 3.0 || ms > 200)) {
        qDebug("[NET %10.3f] RTT SPIKE: %dms (avg:%.0fms)", cwChainMs(), ms, m_rttAvg);
    }
}

void NetworkMetrics::updateRttStats() {
    if (m_rttSamples.empty())
        return;

    int minVal = m_rttSamples[0];
    int maxVal = m_rttSamples[0];
    double sum = 0.0;
    for (int s : m_rttSamples) {
        sum += s;
        if (s < minVal)
            minVal = s;
        if (s > maxVal)
            maxVal = s;
    }
    m_rttMin = minVal;
    m_rttMax = maxVal;
    m_rttAvg = sum / m_rttSamples.size();

    // Jitter = stddev
    double variance = 0.0;
    for (int s : m_rttSamples) {
        double diff = s - m_rttAvg;
        variance += diff * diff;
    }
    m_rttJitter = std::sqrt(variance / m_rttSamples.size());
}

void NetworkMetrics::onAudioSequence(quint8 seq) {
    m_totalPacketsInterval++;
    m_totalPacketsTotal++;
    m_audioActive = true;

    if (m_lastAudioSeq >= 0) {
        int expected = (m_lastAudioSeq + 1) & 0xFF;
        if (seq != expected) {
            int gap = (seq - expected) & 0xFF;
            if (gap > 0 && gap < 128) {
                m_lostPacketsInterval += gap;
                m_lostPacketsTotal += gap;
                qDebug("[NET %10.3f] LOSS: %d audio packet(s) lost (seq %d->%d, expected %d)", cwChainMs(), gap,
                       m_lastAudioSeq, seq, expected);
            }
        }
    }
    m_lastAudioSeq = seq;
}

void NetworkMetrics::onBufferStatus(int queueBytes, int maxBytes, bool prebuffering) {
    m_bufferBytes = queueBytes;
    m_bufferMaxBytes = maxBytes;
    m_prebuffering = prebuffering;
}

void NetworkMetrics::onConnectionStateChanged(bool connected) {
    m_connected = connected;
    if (connected) {
        qDebug("[NET %10.3f] CONNECTED", cwChainMs());
        m_summaryTimer->start();
        // Reset state for new connection
        m_rttSamples.clear();
        m_rttCurrent = -1;
        m_rttMin = -1;
        m_rttMax = -1;
        m_rttAvg = 0.0;
        m_rttJitter = 0.0;
        m_lastAudioSeq = -1;
        m_lostPacketsInterval = 0;
        m_totalPacketsInterval = 0;
        m_lostPacketsTotal = 0;
        m_totalPacketsTotal = 0;
        m_underrunsInterval = 0;
        m_underrunsTotal = 0;
        m_prebuffering = true;
        m_audioActive = false;
        m_audioWasActive = false;
        m_stalePingCount = 0;
        m_tier = Green;
        emit healthTierChanged(m_tier);
    } else {
        qDebug("[NET %10.3f] DISCONNECTED", cwChainMs());
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

    // Buffer depth: high buffer = playing stale audio (latency accumulation)
    // Normal steady-state is 1-3 packets (20-60ms depending on SL).
    // Over 500ms means packets queued during a network event and never flushed.
    double bufferMs = m_bufferBytes / 96.0;
    if (bufferMs > 500.0)
        tier = std::max(tier, Orange);
    else if (bufferMs > 200.0)
        tier = std::max(tier, Yellow);

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

    double bufferMs = m_bufferBytes / 96.0; // BYTES_PER_MS = 96

    static const char *tierNames[] = {"GREEN", "YELLOW", "ORANGE", "RED"};

    qDebug("[NET %10.3f] RTT %dms (avg:%.0f min:%d max:%d jit:%.1f) | "
           "BUF %.0fms | PKT %d/2s | %s",
           cwChainMs(), m_rttCurrent, m_rttAvg, m_rttMin, m_rttMax, m_rttJitter, bufferMs, m_totalPacketsInterval,
           tierNames[m_tier]);

    // Track stale ping (reset in onLatencyChanged when PONG arrives)
    m_stalePingCount++;

    // Reset per-interval counters
    m_lostPacketsInterval = 0;
    m_totalPacketsInterval = 0;
    m_underrunsInterval = 0;
    if (m_audioActive)
        m_audioWasActive = true;
    m_audioActive = false; // Will be set true again when next audio packet arrives
}
