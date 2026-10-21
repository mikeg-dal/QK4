#ifndef LIVESIDETONE_H
#define LIVESIDETONE_H

#include <QMutex>
#include <QtGlobal>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>

// The pull-device DSP is independent of QAudioSink so timing, envelopes and stop behavior can
// be tested without opening a sound device. Its only input is already-keyed DOWN/UP edges.
class LiveSidetone {
public:
    static constexpr int kSampleRate = 48000;
    static constexpr int kRampSamples = kSampleRate * 3 / 1000;
    static constexpr int kMaximumQueuedEdges = 256;

    void setFrequency(int frequency) { m_frequency.store(std::clamp(frequency, 100, 2000)); }
    void setVolume(float volume) { m_volume.store(std::isfinite(volume) ? std::clamp(volume, 0.0f, 1.0f) : 0.0f); }

    bool setKeyDown(bool down, qint64 timestampNs) {
        QMutexLocker lock(&m_mutex);
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
        QMutexLocker lock(&m_mutex);
        resetLocked();
    }

    qint64 render(char *data, qint64 maximumBytes) {
        if (!data || maximumBytes <= 0) {
            return 0;
        }
        QMutexLocker lock(&m_mutex);
        const qint64 frames = maximumBytes / sizeof(qint16);
        const double increment = 2.0 * kPi * m_frequency.load() / kSampleRate;
        const float volume = m_volume.load();
        for (qint64 frame = 0; frame < frames; ++frame) {
            while (!m_edges.empty()) {
                const auto &edge = m_edges.front();
                if (edge.reanchor) {
                    // Re-anchor after silence. An idle audio device may have rendered silence
                    // ahead of wall time; that must not add a stale gap before the next press.
                    m_timelineValid = true;
                    m_originNs = edge.timestampNs;
                    m_originFrame = m_renderedFrames;
                }
                const auto offsetNs = edge.timestampNs >= m_originNs ? edge.timestampNs - m_originNs : 0;
                const auto offsetFrames =
                    static_cast<quint64>((static_cast<long double>(offsetNs) * kSampleRate) / 1'000'000'000.0L);
                if (m_originFrame + offsetFrames > m_renderedFrames) {
                    break;
                }
                m_renderDown = edge.down;
                m_edges.pop_front();
            }
            if (m_renderDown) {
                m_level = std::min(1.0, m_level + 1.0 / kRampSamples);
            } else {
                m_level = std::max(0.0, m_level - 1.0 / kRampSamples);
            }
            const double envelope = 0.5 - 0.5 * std::cos(kPi * m_level);
            const qint16 sample = static_cast<qint16>(std::sin(m_phase) * volume * envelope * 32767.0);
            std::memcpy(data + frame * sizeof(qint16), &sample, sizeof(sample));
            m_phase += increment;
            if (m_phase >= 2.0 * kPi) {
                m_phase -= 2.0 * kPi;
            }
            ++m_renderedFrames;
        }
        return frames * sizeof(qint16);
    }

private:
    struct Edge {
        bool down;
        qint64 timestampNs;
        bool reanchor;
    };
    void resetLocked() {
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
    QMutex m_mutex;
    std::deque<Edge> m_edges;
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
