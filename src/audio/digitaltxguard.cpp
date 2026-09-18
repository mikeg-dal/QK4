#include "digitaltxguard.h"

#include <QDateTime>
#include <cmath>

namespace {
QString calibrationKey(int mode) {
    return mode == int(DigitalTxGuard::Mode::Sstv) ? QStringLiteral("digitalTx/calibration/sstv")
                                                   : QStringLiteral("digitalTx/calibration/ft8-ft4");
}
} // namespace

std::optional<float> DigitalTxCalibration::load(QSettings &settings, int mode) {
    const QVariantMap record = settings.value(calibrationKey(mode)).toMap();
    bool ok = false;
    const float gain = record.value(QStringLiteral("gain")).toFloat(&ok);
    if (!ok || record.value(QStringLiteral("version")).toInt() != 1 || !std::isfinite(gain) ||
        gain < DigitalTxGuard::MinimumGain || gain > DigitalTxGuard::MaximumGain)
        return {};
    return gain;
}

bool DigitalTxCalibration::save(QSettings &settings, int mode, float gain) {
    if (!std::isfinite(gain) || gain < DigitalTxGuard::MinimumGain || gain > DigitalTxGuard::MaximumGain)
        return false;
    settings.setValue(calibrationKey(mode),
                      QVariantMap{{QStringLiteral("version"), 1},
                                  {QStringLiteral("gain"), gain},
                                  {QStringLiteral("utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}});
    settings.sync();
    return settings.status() == QSettings::NoError;
}

bool DigitalTxGuard::begin(Mode mode, quint64 generation, qint64 now, bool calibration) {
    if (m_active || m_latched || generation == 0)
        return false;
    m_mode = mode;
    m_generation = generation;
    m_started = m_adjusted = m_lastMeter = now;
    m_firstAudio = m_lastAudio = m_stableSince = m_highSince = m_lastReduction = -1;
    m_highSamples = 0;
    m_calibrating = calibration;
    m_reduced = false;
    m_reason.clear();
    m_meterDiagnostic.clear();
    m_control->calibrating.store(calibration, std::memory_order_release);
    if (calibration)
        m_control->gain.store(CalibrationStartGain, std::memory_order_release);
    m_control->generation.store(generation, std::memory_order_release);
    m_active = true;
    return true;
}

void DigitalTxGuard::stop() {
    m_control->close(m_generation);
    m_control->calibrating.store(false, std::memory_order_release);
    m_active = false;
}

void DigitalTxGuard::acknowledge() {
    if (!m_active) {
        m_latched = false;
        m_reason.clear();
    }
}

void DigitalTxGuard::audioAccepted(qint64 now) {
    if (!m_active)
        return;
    if (m_firstAudio < 0)
        m_firstAudio = now;
    m_lastAudio = now;
}

DigitalTxGuard::Action DigitalTxGuard::trip(const QString &reason) {
    if (!m_active)
        return Action::None;
    stop();
    m_latched = true;
    m_reason = reason + (m_meterDiagnostic.isEmpty() ? QString() : QLatin1Char('\n') + m_meterDiagnostic);
    return Action::Tripped;
}

DigitalTxGuard::Action DigitalTxGuard::tick(qint64 now) {
    if (!m_active)
        return Action::None;
    if (now - m_lastMeter >= 1500)
        return trip(QStringLiteral("TX stopped: fresh K4 ALC readings are unavailable."));
    if (m_calibrating && now - m_started >= 15000)
        return trip(QStringLiteral("Calibration stopped: a stable ALC level could not be established."));
    if (m_firstAudio >= 0 && now - m_lastAudio >= 1500)
        return trip(QStringLiteral("TX stopped: digital audio is no longer streaming."));
    if (m_highSamples >= 3 && m_highSince >= 0 && now - m_highSince >= 1500)
        return trip(QStringLiteral("TX stopped: ALC stayed high after automatic reduction."));
    return Action::None;
}

DigitalTxGuard::Action DigitalTxGuard::meter(const QString &command, qint64 now) {
    if (!m_active || !command.startsWith(QStringLiteral("TM")) || command == QStringLiteral("TM1") ||
        command == QStringLiteral("TM0"))
        return Action::None;
    if (command.size() != 14)
        return trip(QStringLiteral("TX stopped: invalid K4 meter response."));
    for (int i = 2; i < command.size(); ++i)
        if (!command[i].isDigit())
            return trip(QStringLiteral("TX stopped: invalid K4 meter response."));
    m_lastMeter = now;
    const int alc = command.mid(2, 3).toInt();
    const int compression = command.mid(5, 3).toInt();
    const int forward = command.mid(8, 3).toInt();
    m_meterDiagnostic = QStringLiteral("raw ALC %1 · drive %2 dB")
                            .arg(alc)
                            .arg(20.0 * std::log10(qMax(gain(), MinimumGain)), 0, 'f', 1);
    if (m_calibrating && forward > 0)
        return trip(QStringLiteral("Calibration stopped: RF was reported while K4 TEST was enabled."));
    if (compression > 0)
        return trip(QStringLiteral("TX stopped: speech compression is active; use DATA-A with compression off."));
    if (alc >= StopAlc)
        return trip(QStringLiteral("TX stopped: K4 ALC is excessively high."));
    if (m_firstAudio < 0 || now - m_firstAudio < 600 || now - m_adjusted < 600)
        return Action::None;

    if (alc < ReduceAlc) {
        m_highSince = -1;
        m_highSamples = 0;
        if (!m_calibrating)
            return Action::None;
        if (alc >= 3) {
            if (m_stableSince < 0)
                m_stableSince = now;
            if (now - m_stableSince >= 1200)
                return Action::Calibrated;
        } else if (now - m_adjusted >= 600) {
            m_stableSince = -1;
            const float current = gain();
            if (current >= MaximumGain)
                return trip(QStringLiteral("Calibration stopped: safe audio limit reached before ALC 3."));
            m_control->gain.store(qMin(MaximumGain, current * 1.41421356f), std::memory_order_release);
            m_adjusted = now;
        }
        return Action::None;
    }

    m_stableSince = -1;
    if (m_highSince < 0)
        m_highSince = now;
    ++m_highSamples;
    if (m_lastReduction >= 0 && now - m_lastReduction < 400)
        return Action::None;
    m_control->gain.store(qMax(MinimumGain, gain() * .70710678f), std::memory_order_release);
    m_adjusted = m_lastReduction = now;
    m_reduced = true;
    if (m_calibrating) {
        m_highSince = -1;
        m_highSamples = 0;
    }
    return Action::Reduced;
}
