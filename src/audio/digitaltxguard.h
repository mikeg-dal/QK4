#pragma once

#include <QSettings>
#include <QString>
#include <atomic>
#include <memory>
#include <optional>

class DigitalTxCalibration {
public:
    static std::optional<float> load(QSettings &settings, int mode);
    static bool save(QSettings &settings, int mode, float gain);
};

struct DigitalTxControl {
    std::atomic<quint64> generation{0};
    std::atomic<quint64> scheduledGeneration{0};
    std::atomic<float> gain{0.5f};
    std::atomic<quint64> audioFault{0};
    std::atomic<bool> calibrating{false};
    bool allows(quint64 id) const { return id != 0 && generation.load(std::memory_order_acquire) == id; }
    void close(quint64 id) { generation.compare_exchange_strong(id, 0, std::memory_order_acq_rel); }
};

class DigitalTxGuard {
public:
    enum class Mode { Ft8, Ft4, Sstv };
    enum class Action { None, Reduced, Tripped, Calibrated };
    static constexpr float MinimumGain = 1.0f / 32768.0f;
    static constexpr float CalibrationStartGain = 1.0f / 32.0f;
    static constexpr float MaximumGain = 0.5f;
    static constexpr int ReduceAlc = 6;
    static constexpr int StopAlc = 10;

    explicit DigitalTxGuard(std::shared_ptr<DigitalTxControl> control) : m_control(std::move(control)) {}
    bool begin(Mode mode, quint64 generation, qint64 now, bool calibration = false);
    void stop();
    void acknowledge();
    void audioAccepted(qint64 now);
    Action meter(const QString &command, qint64 now);
    Action tick(qint64 now);
    Action trip(const QString &reason);
    bool active() const { return m_active; }
    bool latched() const { return m_latched; }
    bool calibrating() const { return m_calibrating; }
    bool reduced() const { return m_reduced; }
    float gain() const { return m_control->gain.load(std::memory_order_acquire); }
    quint64 generation() const { return m_generation; }
    QString reason() const { return m_reason; }
    QString meterDiagnostic() const { return m_meterDiagnostic; }

private:
    std::shared_ptr<DigitalTxControl> m_control;
    Mode m_mode = Mode::Ft8;
    quint64 m_generation = 0;
    bool m_active = false;
    bool m_latched = false;
    bool m_calibrating = false;
    bool m_reduced = false;
    qint64 m_started = 0;
    qint64 m_firstAudio = -1;
    qint64 m_lastAudio = -1;
    qint64 m_lastMeter = 0;
    qint64 m_adjusted = 0;
    qint64 m_stableSince = -1;
    qint64 m_highSince = -1;
    qint64 m_lastReduction = -1;
    int m_highSamples = 0;
    QString m_reason;
    QString m_meterDiagnostic;
};
