#ifndef LIVESIDETONE_H
#define LIVESIDETONE_H

#include "utils/ringdeque.h"
#include "utils/rtsan.h"
#include <QtGlobal>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>

// Timestamped hardware edges, independent of QAudioSink and of the software paddle keyer.
// The paced output converts mono float samples to the negotiated device format.
class LiveSidetone {
public:
    static constexpr int kSampleRate = 48000;
    static constexpr int kRampSamples = kSampleRate * 3 / 1000;
    static constexpr int kMaximumQueuedEdges = 256;

    void setFrequency(int frequency) { m_frequency.store(std::clamp(frequency, 100, 2000)); }
    void setVolume(float volume) { m_volume.store(std::isfinite(volume) ? std::clamp(volume, 0.0f, 1.0f) : 0.0f); }

    bool setKeyDown(bool down, qint64 timestampNs) noexcept QK4_NONBLOCKING {
        std::lock_guard lock(m_mutex);
        if (m_requestedDown == down) {
            return true;
        }
        if (m_edges.size() >= kMaximumQueuedEdges) {
            resetLocked(); // Fail silent if a stalled audio backend cannot consume its edge queue.
            return false;
        }
        m_requestedDown = down;
        const bool reanchor = m_edges.empty() && (!m_timelineValid || (down && !m_renderDown && m_level == 0.0));
        m_edges.push_back({down, timestampNs, reanchor});
        return true;
    }

    void reset() {
        std::lock_guard lock(m_mutex);
        resetLocked();
    }

    qint64 render(char *data, qint64 maximumBytes) noexcept QK4_NONBLOCKING {
        if (!data || maximumBytes <= 0) {
            return 0;
        }
        const qint64 frames = maximumBytes / sizeof(qint16);
        return renderSamples(frames, kSampleRate,
                             [data](qint64 frame, float value) {
                                 const qint16 sample = static_cast<qint16>(value * 32767.0f);
                                 std::memcpy(data + frame * sizeof(qint16), &sample, sizeof(sample));
                             }) *
               sizeof(qint16);
    }

    qint64 renderFloat(float *data, qint64 frames, int sampleRate) noexcept QK4_NONBLOCKING {
        if (!data || frames <= 0 || sampleRate <= 0)
            return 0;
        return renderSamples(frames, sampleRate, [data](qint64 frame, float value) { data[frame] = value; });
    }

private:
    template <typename WriteSample>
    qint64 renderSamples(qint64 frames, int sampleRate, WriteSample writeSample) noexcept QK4_NONBLOCKING {
        std::lock_guard lock(m_mutex);
        const double increment = 2.0 * kPi * m_frequency.load() / sampleRate;
        const int rampSamples = std::max(1, sampleRate * 3 / 1000);
        const float volume = m_volume.load();
        for (qint64 frame = 0; frame < frames; ++frame) {
            while (!m_edges.empty()) {
                const auto &edge = m_edges.front();
                if (edge.reanchor) {
                    // Re-anchor after silence without changing the spacing of already-queued
                    // edges, even when DOWN and UP reached the audio thread in one burst.
                    m_timelineValid = true;
                    m_originNs = edge.timestampNs;
                    m_originFrame = m_renderedFrames;
                }
                const auto offsetNs = edge.timestampNs >= m_originNs ? edge.timestampNs - m_originNs : 0;
                const auto offsetFrames =
                    static_cast<quint64>((static_cast<long double>(offsetNs) * sampleRate) / 1'000'000'000.0L);
                if (m_originFrame + offsetFrames > m_renderedFrames) {
                    break;
                }
                m_renderDown = edge.down;
                m_edges.pop_front();
            }
            if (m_renderDown) {
                m_level = std::min(1.0, m_level + 1.0 / rampSamples);
            } else {
                m_level = std::max(0.0, m_level - 1.0 / rampSamples);
            }
            const double envelope = 0.5 - 0.5 * std::cos(kPi * m_level);
            writeSample(frame, static_cast<float>(std::sin(m_phase) * volume * envelope));
            m_phase += increment;
            if (m_phase >= 2.0 * kPi) {
                m_phase -= 2.0 * kPi;
            }
            ++m_renderedFrames;
        }
        return frames;
    }

    struct Edge {
        bool down;
        qint64 timestampNs;
        bool reanchor;
    };
    void resetLocked() noexcept {
        m_edges.clear();
        m_requestedDown = false;
        m_renderDown = false;
        m_timelineValid = false;
        m_originNs = 0;
        m_originFrame = 0;
        m_renderedFrames = 0;
        m_level = 0.0;
        m_phase = 0.0;
    }

    static constexpr double kPi = 3.14159265358979323846;
    std::atomic<int> m_frequency{600};
    std::atomic<float> m_volume{0.3f};
    // Every holder does bounded DSP/queue work only; the explicit edge limit prevents ring growth.
    RtSan::BoundedMutex m_mutex;
    RingDeque<Edge> m_edges{kMaximumQueuedEdges};
    bool m_requestedDown = false;
    bool m_renderDown = false;
    bool m_timelineValid = false;
    qint64 m_originNs = 0;
    quint64 m_originFrame = 0;
    quint64 m_renderedFrames = 0;
    double m_phase = 0.0;
    double m_level = 0.0;
};

#endif // LIVESIDETONE_H
