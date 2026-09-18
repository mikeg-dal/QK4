#pragma once
#include "ft8types.h"
#include "audio/digitaltxguard.h"
#include <QObject>
#include <QElapsedTimer>
#include <QTimer>

// Mono 12 kHz GFSK. Returns an error instead of silently packing free text or
// substituting a hashed callsign that the standard-QSO UI did not display.
QVector<qint16> ft8TransmitWaveform(const QString &text, Ft8::Mode mode, int hz, QString *error);

// Lives on the audio thread. UTC chooses a slot; monotonic sample deadlines
// pace it. Neither the UI event loop nor receive decoding clocks transmission.
class Ft8Transmitter : public QObject {
    Q_OBJECT
public:
    explicit Ft8Transmitter(std::shared_ptr<DigitalTxControl> control, QObject *parent = nullptr);
    static qint64 nextSlot(qint64 utc, Ft8::Mode mode, bool even);
    static int audioOffsetMs(Ft8::Mode mode) { return mode == Ft8::Mode::FT4 ? 300 : 500; }
    static int latestStartMs(Ft8::Mode mode);
    void schedule(const QString &message, int mode, int hz, qint64 slotUtc, int frameSamples, quint64 generation);
    void cancel(quint64 generation);
    void keyed(int mode, quint64 generation);
    void accepted(int emitted, int total, int image, quint64 generation);
    void unkeyed(quint64 generation);
signals:
    void keyRequested(int mode, quint64 generation);
    void unkeyRequested(quint64 generation);
    void encodingStarted(quint64 generation);
    void frameReady(const QVector<qint16> &samples, int emitted, int total, quint64 generation);
    void transmitting(quint64 generation);
    void finished(bool success, const QString &reason, quint64 generation);

private:
    void tick();
    void fail(const QString &reason);
    enum class Phase { Idle, Waiting, Keying, Audio, Drain, Unkeying };
    Phase m_phase = Phase::Idle;
    std::shared_ptr<DigitalTxControl> m_control;
    QTimer m_timer;
    QElapsedTimer m_clock;
    qint64 m_anchorUtc = 0, m_startMs = 0, m_keyedMs = -1, m_keyRequestMs = 0, m_drainMs = 0;
    quint64 m_generation = 0;
    int m_mode = 0, m_frameSamples = 240, m_emitted = 0;
    QVector<qint16> m_wave;
};
