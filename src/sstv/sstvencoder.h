#ifndef SSTVENCODER_H
#define SSTVENCODER_H

#include "sstvmoderegistry.h"

#include <QImage>
#include <QVector>

class SstvEncoder {
public:
    static constexpr int SampleRate = 12000;

    bool begin(const QImage &frame, SstvModeId mode, QString *error = nullptr, const QString &morseId = QString(),
               int morseWpm = 20, const QString &fskId = QString(), int preRollMs = 0, int postRollMs = 0);
    QVector<qint16> nextSamples(int maximumSamples);
    bool isActive() const;
    bool isComplete() const;
    int totalSamples() const;
    int imageSamples() const { return m_imageSamples; }
    int fskIdEndSamples() const { return m_fskIdEndSamples; }
    int emittedSamples() const;
    int progressPercent() const;
    const SstvModeSpec *mode() const { return m_mode; }

private:
    struct ToneSegment {
        double frequencyHz;
        double durationMs;
        bool shaped = false;
    };

    void appendTone(double frequencyHz, double durationMs, bool shaped = false);
    void appendVis(int visCode);
    void appendFskId(const QString &text);
    void appendFskSymbol(quint8 symbol);
    void appendMorseId(const QString &text, int wpm);
    void appendSequentialImage();
    void appendRobot36Image();
    void appendPdImage();
    int roundedSegmentSamples(double durationMs);
    quint8 componentValue(QRgb pixel, int component) const;
    quint8 luminance(QRgb pixel) const;
    quint8 redDifference(QRgb pixel) const;
    quint8 blueDifference(QRgb pixel) const;

    const SstvModeSpec *m_mode = nullptr;
    QImage m_image;
    QVector<ToneSegment> m_segments;
    int m_segmentIndex = 0;
    int m_remainingSegmentSamples = 0;
    int m_currentSegmentSamples = 0;
    double m_phase = 0.0;
    double m_roundingError = 0.0;
    int m_totalSamples = 0;
    int m_imageSamples = 0;
    int m_fskIdEndSamples = 0;
    int m_emittedSamples = 0;
};

#endif // SSTVENCODER_H
