#include "ft8transmitter.h"
#include <cmath>
#include <algorithm>
#include <QDebug>
extern "C" {
#include <ft8/encode.h>
#include <ft8/message.h>
#include <ft8/constants.h>
}

QVector<qint16> ft8TransmitWaveform(const QString &text, Ft8::Mode mode, int hz, QString *error) {
    const auto reject = [error](const QString &why) {
        if (error)
            *error = why;
        return QVector<qint16>();
    };
    if (hz < 100 || hz > 3200)
        return reject("TX audio frequency must be 100–3200 Hz.");
    const auto normalized = text.simplified().toUpper();
    ftx_message_t message{};
    if (ftx_message_encode(&message, nullptr, normalized.toLatin1().constData()) != FTX_MESSAGE_RC_OK ||
        ftx_message_get_type(&message) != FTX_MESSAGE_TYPE_STANDARD)
        return reject("This message cannot be sent as a supported standard FT8/FT4 exchange.");
    char decoded[FTX_MAX_MESSAGE_LENGTH]{};
    ftx_message_offsets_t offsets{};
    if (ftx_message_decode(&message, nullptr, decoded, &offsets) != FTX_MESSAGE_RC_OK ||
        QString::fromLatin1(decoded).simplified() != normalized)
        return reject("The encoded message does not exactly match the selected message.");
    const bool ft4 = mode == Ft8::Mode::FT4;
    QVector<uint8_t> tones(ft4 ? FT4_NN : FT8_NN);
    if (ft4)
        ft4_encode(message.payload, tones.data());
    else
        ft8_encode(message.payload, tones.data());
    // WSJT-X gen_ft8wave/gen_ft4wave: BT=2/1; FT4's first and last
    // encoded symbols are envelope ramps, not additional data symbols.
    const int sps = ft4 ? 576 : 1920;
    const int count = ft4 ? tones.size() - 2 : tones.size();
    const int length = tones.size() * sps;
    QVector<double> pulse(3 * sps), phaseStep((count + 2) * sps, 2 * M_PI * hz / 12000.0);
    const double bt = ft4 ? 1.0 : 2.0;
    for (int i = 0; i < pulse.size(); ++i) {
        const double t = double(i) / sps - 1.5;
        pulse[i] = (std::erf(5.336446 * bt * (t + .5)) - std::erf(5.336446 * bt * (t - .5))) / 2;
    }
    for (int symbol = 0; symbol < count; ++symbol)
        for (int j = 0; j < pulse.size(); ++j)
            phaseStep[symbol * sps + j] += 2 * M_PI / sps * tones[symbol + (ft4 ? 1 : 0)] * pulse[j];
    if (!ft4)
        for (int j = 0; j < 2 * sps; ++j) {
            phaseStep[j] += 2 * M_PI / sps * tones.first() * pulse[j + sps];
            phaseStep[count * sps + j] += 2 * M_PI / sps * tones.last() * pulse[j];
        }
    QVector<qint16> wave(length);
    double phase = 0;
    const int ramp = ft4 ? sps : sps / 8;
    for (int i = 0; i < length; ++i) {
        const int edge = qMin(i, length - 1 - i);
        const double envelope = edge < ramp ? .5 * (1 - std::cos(M_PI * edge / ramp)) : 1;
        wave[i] = qRound(26213 * envelope * std::sin(phase));
        phase = std::fmod(phase + phaseStep[i + (ft4 ? 0 : sps)], 2 * M_PI);
    }
    return wave;
}

Ft8Transmitter::Ft8Transmitter(std::shared_ptr<DigitalTxControl> control, QObject *parent)
    : QObject(parent), m_control(std::move(control)) {
    m_timer.setParent(this);
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(5);
    connect(&m_timer, &QTimer::timeout, this, &Ft8Transmitter::tick);
}
qint64 Ft8Transmitter::nextSlot(qint64 utc, Ft8::Mode mode, bool even) {
    const int period = Ft8::periodMs(mode);
    qint64 slot = utc / period;
    if (utc % period >= latestStartMs(mode))
        ++slot;
    if ((slot % 2 == 0) != even)
        ++slot;
    return slot * period;
}
int Ft8Transmitter::latestStartMs(Ft8::Mode mode) {
    // WSJT-X guiUpdate permits a start in the matching period before 75%.
    // Also leave time for the existing K4 500 ms key-up and some actual audio.
    const int waveMs = mode == Ft8::Mode::FT4 ? 5040 : 12640;
    return qMin(Ft8::periodMs(mode) * 3 / 4, audioOffsetMs(mode) + waveMs - 600);
}
void Ft8Transmitter::schedule(const QString &text, int mode, int hz, qint64 slotUtc, int frames, quint64 gen) {
    if (!gen)
        return;
    if (m_control->scheduledGeneration.load() != gen) {
        emit finished(false, "TX halted before preparation.", gen);
        return;
    }
    if (m_phase != Phase::Idle) {
        emit finished(false, "Another FT transmission is active.", gen);
        return;
    }
    m_generation = gen;
    m_anchorUtc = QDateTime::currentMSecsSinceEpoch();
    m_clock.start();
    m_mode = mode;
    m_phase = Phase::Waiting;
    if ((mode != 0 && mode != 1) || (frames != 240 && frames != 480 && frames != 720 && frames != 1440) ||
        slotUtc % Ft8::periodMs(Ft8::Mode(mode)) != 0 || m_anchorUtc - slotUtc >= latestStartMs(Ft8::Mode(mode))) {
        fail("TX stopped: invalid or late transmit slot.");
        return;
    }
    QString error;
    m_wave = ft8TransmitWaveform(text, Ft8::Mode(mode), hz, &error);
    if (m_wave.isEmpty()) {
        fail(error);
        return;
    }
    m_frameSamples = frames;
    m_emitted = 0;
    m_keyedMs = -1;
    m_startMs = slotUtc + audioOffsetMs(Ft8::Mode(mode)) - m_anchorUtc;
    m_timer.start();
    tick(); // An operator's late Call can key now, without another UI cycle.
}
void Ft8Transmitter::cancel(quint64 gen) {
    if (m_phase != Phase::Idle && gen == m_generation)
        fail("TX halted · listening");
}
void Ft8Transmitter::fail(const QString &reason) {
    if (m_phase == Phase::Idle)
        return;
    const auto gen = m_generation;
    m_control->close(gen);
    auto expected = gen;
    m_control->scheduledGeneration.compare_exchange_strong(expected, 0);
    m_timer.stop();
    m_phase = Phase::Idle;
    m_wave.clear();
    emit unkeyRequested(gen);
    emit finished(false, reason, gen);
}
void Ft8Transmitter::keyed(int, quint64 gen) {
    if (gen != m_generation || m_phase != Phase::Keying)
        return;
    m_keyedMs = m_clock.elapsed();
}
void Ft8Transmitter::accepted(int emitted, int total, int, quint64 gen) {
    if (gen != m_generation || m_phase != Phase::Drain || total <= 0 || emitted < total)
        return;
    m_drainMs = m_clock.elapsed() + m_frameSamples / 12 + 150;
}
void Ft8Transmitter::unkeyed(quint64 gen) {
    if (gen != m_generation || m_phase != Phase::Unkeying)
        return;
    m_timer.stop();
    m_phase = Phase::Idle;
    m_wave.clear();
    emit finished(true, "TX complete · listening", gen);
}
void Ft8Transmitter::tick() {
    const qint64 now = m_clock.elapsed();
    if (m_phase == Phase::Idle)
        return;
    if (m_phase == Phase::Unkeying) {
        if (now > m_drainMs + 2000)
            fail("TX stopped: receive command was not acknowledged.");
        return;
    }
    if (m_control->scheduledGeneration.load() != m_generation) {
        fail("TX halted · listening");
        return;
    }
    if (qAbs(QDateTime::currentMSecsSinceEpoch() - m_anchorUtc - now) > 250) {
        fail("TX stopped: the device clock changed. Check time synchronization.");
        return;
    }
    if (m_phase == Phase::Waiting && now >= m_startMs - 600) {
        if (now - m_startMs + audioOffsetMs(Ft8::Mode(m_mode)) >= latestStartMs(Ft8::Mode(m_mode))) {
            fail("TX stopped: transmit start window has elapsed.");
            return;
        }
        m_phase = Phase::Keying;
        m_keyRequestMs = now;
        emit keyRequested(m_mode, m_generation);
    }
    if (m_phase == Phase::Keying && now >= m_startMs) {
        if (now > m_startMs + m_wave.size() / 12 - 20 || now - m_keyRequestMs > 1500) {
            fail("TX stopped: K4 key-up was not ready for the slot.");
            return;
        }
        if (m_keyedMs < 0 || now - m_keyedMs < 500)
            return;
        if (!m_control->allows(m_generation)) {
            fail("TX stopped by digital audio protection.");
            return;
        }
        // WSJT-X Modulator::start skips elapsed samples, preserving UTC symbol
        // positions and the original end time rather than shifting a full frame.
        m_emitted = int(qMax<qint64>(0, now - m_startMs) * 12);
        if (m_emitted > 0) {
            const int ramp = qMin(60, int(m_wave.size()) - m_emitted);
            for (int i = 0; i < ramp; ++i)
                m_wave[m_emitted + i] = qRound(m_wave[m_emitted + i] * .5 * (1 - std::cos(M_PI * i / ramp)));
        }
        m_phase = Phase::Audio;
        emit encodingStarted(m_generation);
        emit transmitting(m_generation);
        qInfo() << "FT TX audio starts" << m_generation << "UTC" << QDateTime::currentMSecsSinceEpoch()
                << "skipped samples" << m_emitted;
    }
    if (m_phase == Phase::Audio || m_phase == Phase::Drain) {
        if (!m_control->allows(m_generation)) {
            fail("TX stopped by digital audio protection.");
            return;
        }
    }
    if (m_phase == Phase::Audio) {
        const qint64 due = m_startMs + m_emitted / 12;
        if (now < due)
            return;
        if (now - due > qMax(80, m_frameSamples / 12)) {
            fail("TX stopped: audio pacing missed its deadline.");
            return;
        }
        const int count = qMin(m_frameSamples, int(m_wave.size()) - m_emitted);
        QVector<qint16> frame(m_frameSamples, 0);
        std::copy_n(m_wave.constData() + m_emitted, count, frame.data());
        m_emitted += count;
        if (m_emitted == m_wave.size()) {
            m_phase = Phase::Drain;
            m_drainMs = 0;
        }
        emit frameReady(frame, m_emitted, m_wave.size(), m_generation);
    }
    if (m_phase == Phase::Drain) {
        if (now > m_startMs + m_wave.size() / 12 + 1500) {
            fail("TX stopped: final audio was not acknowledged.");
            return;
        }
        if (m_drainMs > 0 && now >= m_drainMs) {
            m_phase = Phase::Unkeying;
            emit unkeyRequested(m_generation);
        }
    }
}
