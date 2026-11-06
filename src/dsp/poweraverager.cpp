#include "poweraverager.h"

#include <algorithm>
#include <cmath>

namespace {
// Running sums are corrected from the ring this often, so floating-point drift from subtracting
// the oldest frame can never build up over a long session.
constexpr int kResumEvery = 1024;
} // namespace

void PowerAverager::setLength(int frames) {
    const int length = std::clamp(frames, 1, MaxLength);
    if (length == m_length)
        return;
    m_length = length;
    restart(m_bins);
}

void PowerAverager::reset() {
    restart(m_bins);
}

void PowerAverager::restart(int bins) {
    m_bins = bins;
    m_count = 0;
    m_head = 0;
    m_sinceSum = 0;
    m_ring.assign(static_cast<qsizetype>(m_length) * bins, 0.0);
    m_sum.assign(bins, 0.0);
}

void PowerAverager::add(const QVector<float> &frameDbm, QVector<float> &outDbm) {
    const int bins = static_cast<int>(frameDbm.size());
    if (m_length == 1) { // #AVG 1: every frame as it arrives
        outDbm = frameDbm;
        return;
    }
    if (bins != m_bins)
        restart(bins);

    double *slot = m_ring.data() + static_cast<qsizetype>(m_head) * bins;
    const bool full = m_count == m_length;
    for (int i = 0; i < bins; ++i) {
        const double power = std::pow(10.0, static_cast<double>(frameDbm[i]) / 10.0);
        if (full)
            m_sum[i] -= slot[i];
        slot[i] = power;
        m_sum[i] += power;
    }
    m_head = (m_head + 1) % m_length;
    m_count = (std::min)(m_count + 1, m_length);

    if (++m_sinceSum >= kResumEvery) {
        m_sinceSum = 0;
        std::fill(m_sum.begin(), m_sum.end(), 0.0);
        for (int f = 0; f < m_count; ++f) {
            const double *row = m_ring.constData() + static_cast<qsizetype>(f) * bins;
            for (int i = 0; i < bins; ++i)
                m_sum[i] += row[i];
        }
    }

    outDbm.resize(bins);
    for (int i = 0; i < bins; ++i) {
        const double mean = (std::max)(m_sum[i] / m_count, 1e-300);
        outDbm[i] = static_cast<float>(10.0 * std::log10(mean));
    }
}

void PowerAverager::addEased(const QVector<float> &frameDbm, QVector<float> &shownDbm) {
    if (m_length == 1) {
        shownDbm = frameDbm;
        return;
    }
    add(frameDbm, m_average);
    if (shownDbm.size() != m_average.size()) {
        shownDbm = m_average;
        return;
    }
    for (qsizetype i = 0; i < m_average.size(); ++i)
        shownDbm[i] += TraceEase * (m_average[i] - shownDbm[i]);
}
