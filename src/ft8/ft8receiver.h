#pragma once
#include "ft8types.h"
#include <QObject>
#include <QByteArray>
#include <atomic>
#include <memory>

class Ft8Receiver : public QObject {
    Q_OBJECT
public:
    explicit Ft8Receiver(QObject *parent = nullptr);
    ~Ft8Receiver() override;
    // Called on the I/O thread. Bounded queue; never blocks radio playback.
    void enqueue(const QByteArray &stereoFloat, qint64 receivedUtcMs);
    void setCapture(bool enabled, Ft8::Mode mode);
    static QVector<Ft8::Decode> decodeSamples(const QVector<float> &mono, Ft8::Mode mode, const QDateTime &slotStart);
signals:
    void decoded(const QVector<Ft8::Decode> &messages, quint64 generation);
    void spectrum(const QVector<float> &db, double firstHz, double binHz, quint64 generation);
    void streamStatus(const QString &message, quint64 generation);

public:
    quint64 generation() const { return m_generation.load(); }

private:
    void consume(const QByteArray &pcm, qint64 receivedUtcMs, quint64 generation, Ft8::Mode mode);
    struct Dsp;
    std::unique_ptr<Dsp> m_dsp;
    std::atomic<bool> m_capture{false};
    std::atomic<int> m_mode{0};
    std::atomic<quint64> m_generation{0};
    std::atomic<int> m_pendingBytes{0};
    quint64 m_workerGeneration = 0;
    qint64 m_slotStart = -1;
    int m_position = 0;
    bool m_earlyDecoded = false;
    QVector<float> m_block;
};
