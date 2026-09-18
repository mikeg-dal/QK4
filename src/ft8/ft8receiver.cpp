#include "ft8receiver.h"
#include "ft8snr.h"
#include <QHash>
#include <QSet>
#include <QThread>
#include <QTimeZone>
#include <algorithm>
#include <cmath>
#include <cstring>
extern "C" {
#include <common/monitor.h>
#include <ft8/message.h>
}

namespace {
thread_local QHash<quint32, QByteArray> hashes;
bool lookupHash(ftx_callsign_hash_type_t type, uint32_t value, char *out) {
    const int shift = type == FTX_CALLSIGN_HASH_10_BITS ? 12 : type == FTX_CALLSIGN_HASH_12_BITS ? 10 : 0;
    QByteArray match;
    for (auto it = hashes.begin(); it != hashes.end(); ++it)
        if ((it.key() >> shift) == value) {
            if (!match.isEmpty() && match != it.value())
                return false;
            match = it.value();
        }
    if (match.isEmpty())
        return false;
    std::memcpy(out, match.constData(), size_t(match.size() + 1));
    return true;
}
void saveHash(const char *call, uint32_t value) {
    if (hashes.size() >= 2048 && !hashes.contains(value))
        hashes.erase(hashes.begin());
    hashes[value] = QByteArray(call).left(11);
}
QVector<Ft8::Decode> decodeMonitor(const monitor_t &mon, Ft8::Mode mode, QDateTime utc, const QVector<float> &samples) {
    ftx_candidate_t candidates[140];
    int count = ftx_find_candidates(&mon.wf, 140, candidates, 10);
    std::sort(candidates, candidates + count, [](const auto &a, const auto &b) { return a.score > b.score; });
    ftx_callsign_hash_interface_t interface{lookupHash, saveHash};
    QVector<Ft8::Decode> result;
    QSet<QString> seen;
    std::unique_ptr<Ft8Snr> reports;
    for (int i = 0; i < count; ++i) {
        if (QThread::currentThread()->isInterruptionRequested())
            break;
        ftx_message_t message{};
        ftx_decode_status_t status{};
        if (!ftx_decode_candidate(&mon.wf, &candidates[i], 25, &message, &status))
            continue;
        char text[FTX_MAX_MESSAGE_LENGTH]{};
        ftx_message_offsets_t offsets{};
        if (ftx_message_decode(&message, &interface, text, &offsets) != FTX_MESSAGE_RC_OK)
            continue;
        QString decoded = QString::fromLatin1(text).trimmed();
        if (seen.contains(decoded))
            continue;
        seen.insert(decoded);
        Ft8::Decode d;
        d.utc = utc;
        d.mode = mode;
        d.message = decoded;
        d.audioHz = qRound((mon.min_bin + candidates[i].freq_offset + float(candidates[i].freq_sub) / mon.wf.freq_osr) /
                           mon.symbol_period);
        d.dt = (candidates[i].time_offset + float(candidates[i].time_sub) / mon.wf.time_osr) * mon.symbol_period;
        d.syncScore = candidates[i].score;
        if (!reports)
            reports = std::make_unique<Ft8Snr>(samples, mode);
        // The monitor's two-symbol analysis window centers one symbol after
        // the candidate index's physical start. WSJT-X refinement uses start.
        const double frequency =
            (mon.min_bin + candidates[i].freq_offset + double(candidates[i].freq_sub) / mon.wf.freq_osr) /
            mon.symbol_period;
        d.snr = reports->estimate(message.payload, frequency, d.dt - mon.symbol_period);
        result.append(d);
    }
    return result;
}
} // namespace
struct Ft8Receiver::Dsp {
    monitor_t monitor{};
    QVector<float> samples;
    explicit Dsp(Ft8::Mode mode) {
        monitor_config_t cfg{100, 3300, 12000, 2, 2, mode == Ft8::Mode::FT4 ? FTX_PROTOCOL_FT4 : FTX_PROTOCOL_FT8};
        monitor_init(&monitor, &cfg);
    }
    ~Dsp() { monitor_free(&monitor); }
    void reset() {
        samples.clear();
        monitor_reset(&monitor);
        std::fill(monitor.last_frame, monitor.last_frame + monitor.nfft, 0.0f);
    }
};
Ft8Receiver::Ft8Receiver(QObject *parent) : QObject(parent) {}
Ft8Receiver::~Ft8Receiver() = default;
void Ft8Receiver::setCapture(bool enabled, Ft8::Mode mode) {
    if (m_capture.load() == enabled && m_mode.load() == int(mode))
        return;
    m_capture.store(false);
    m_mode.store(int(mode));
    ++m_generation;
    m_capture.store(enabled);
}
void Ft8Receiver::enqueue(const QByteArray &pcm, qint64 utc) {
    if (!m_capture.load() || pcm.isEmpty() || pcm.size() % (2 * sizeof(float)) != 0)
        return;
    constexpr int maxQueued = 12000 * 2 * sizeof(float) * 2;
    int count = int(pcm.size());
    if (m_pendingBytes.fetch_add(count) + count > maxQueued) {
        m_pendingBytes.fetch_sub(count);
        ++m_generation;
        return;
    }
    const auto generation = m_generation.load();
    const auto mode = Ft8::Mode(m_mode.load());
    QMetaObject::invokeMethod(
        this,
        [this, pcm, utc, generation, mode, count] {
            m_pendingBytes.fetch_sub(count);
            if (m_capture.load() && generation == m_generation.load())
                consume(pcm, utc, generation, mode);
        },
        Qt::QueuedConnection);
}
void Ft8Receiver::consume(const QByteArray &pcm, qint64 utc, quint64 generation, Ft8::Mode mode) {
    const int frames = int(pcm.size() / (2 * sizeof(float)));
    const qint64 firstMs = utc - frames * 1000 / 12000;
    if (generation != m_workerGeneration || !m_dsp) {
        m_dsp = std::make_unique<Dsp>(mode);
        m_workerGeneration = generation;
        m_slotStart = -1;
        m_block.clear();
    }
    const int lengthMs = Ft8::periodMs(mode);
    const int slotSamples = lengthMs * 12;
    if (m_slotStart >= 0 && qAbs(firstMs - (m_slotStart + m_position / 12)) > 1000) {
        m_slotStart = -1;
        m_block.clear();
        m_dsp->reset();
        emit streamStatus("Audio gap — waiting for a complete receive period", generation);
    }
    if (m_slotStart < 0) {
        m_earlyDecoded = false;
        m_slotStart = Ft8::slot(firstMs, mode) * lengthMs;
        m_position = int(firstMs - m_slotStart) * 12;
        m_dsp->samples.fill(0, m_position);
        QVector<float> zero(m_dsp->monitor.block_size, 0);
        for (int i = 0; i < m_position / zero.size(); ++i)
            monitor_process(&m_dsp->monitor, zero.constData());
        m_block.fill(0, m_position % zero.size());
    }
    for (int i = 0; i < frames; ++i) {
        float sample;
        std::memcpy(&sample, pcm.constData() + i * 2 * sizeof(float), sizeof(float));
        m_block.append(std::isfinite(sample) ? sample : 0.0f);
        m_dsp->samples.append(std::isfinite(sample) ? sample : 0.0f);
        ++m_position;
        auto &mon = m_dsp->monitor;
        if (m_block.size() == mon.block_size) {
            monitor_process(&mon, m_block.constData());
            m_block.clear();
            if (mon.wf.num_blocks > 0 && (mode == Ft8::Mode::FT8 || mon.wf.num_blocks % 3 == 0)) {
                QVector<float> row(mon.wf.num_bins);
                const int base = (mon.wf.num_blocks - 1) * mon.wf.block_stride;
                for (int b = 0; b < row.size(); ++b)
                    row[b] = (float(mon.wf.mag[base + b]) - 240.0f) / 2.0f;
                emit spectrum(row, mon.min_bin / double(mon.symbol_period), 1.0 / mon.symbol_period, generation);
            }
        }
        // Complete normal on-time signals before the next TX slot is frozen.
        // Keep the final pass for late stations; message IDs suppress duplicates.
        if (!m_earlyDecoded && m_position >= (lengthMs - 1500) * 12) {
            m_earlyDecoded = true;
            emit decoded(
                decodeMonitor(mon, mode, QDateTime::fromMSecsSinceEpoch(m_slotStart, QTimeZone::UTC), m_dsp->samples),
                generation);
        }
        if (m_position >= slotSamples) {
            auto decodes =
                decodeMonitor(mon, mode, QDateTime::fromMSecsSinceEpoch(m_slotStart, QTimeZone::UTC), m_dsp->samples);
            emit decoded(decodes, generation);
            emit streamStatus(QString("Listening · %1 decoded last period").arg(decodes.size()), generation);
            m_slotStart += lengthMs;
            m_position = 0;
            m_earlyDecoded = false;
            m_block.clear();
            m_dsp->reset();
        }
    }
}
QVector<Ft8::Decode> Ft8Receiver::decodeSamples(const QVector<float> &mono, Ft8::Mode mode, const QDateTime &utc) {
    Dsp dsp(mode);
    for (int pos = 0; pos + dsp.monitor.block_size <= mono.size(); pos += dsp.monitor.block_size)
        monitor_process(&dsp.monitor, mono.constData() + pos);
    return decodeMonitor(dsp.monitor, mode, utc, mono);
}
