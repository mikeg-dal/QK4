#ifndef SSTVDECODER_H
#define SSTVDECODER_H

#include "sstvmoderegistry.h"

#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QTimer>
#include <QVector>

#include <array>
#include <deque>
#include <vector>

// Streaming decoder for the K4's already-decoded 12 kHz stereo Float32 RX
// stream. Instances are single-thread owned; MainWindow places the production
// instance on its own worker thread.
class SstvDecoder : public QObject {
    Q_OBJECT
public:
    static constexpr int SampleRate = 12000;

    explicit SstvDecoder(QObject *parent = nullptr);

    Q_INVOKABLE void resetAuto();
    Q_INVOKABLE void consumeStereoFloat(const QByteArray &stereoFloat);

    // Fixture/test entry point. Samples are mono Float32 at SampleRate.
    void consumeMono(const QVector<float> &samples);
    qsizetype bufferedFrequencySampleCount() const { return m_frequency.size(); }

signals:
    void statusChanged(const QString &status);
    void modeDetected(int modeId, const QString &displayName);
    void inputLevelChanged(int percent);
    void inputStreamChanged(bool active);
    void imageUpdated(const QImage &image, int completedRows, int totalRows, const QString &slantStatus);
    void imageCompleted(const QImage &image, int modeId, const QString &slantStatus);
    void callsignDetected(const QString &callsign, const QString &source, int confidence);

private:
    enum class State { SearchingVis, Receiving };
    enum class AcquisitionKind { FullPreamble, SecondLeaderRecovery };
    struct SyncObservation {
        int line;
        qint64 sample;
    };
    struct AfcObservation {
        int line;
        double offsetHz;
    };

    void resetDsp();
    bool boundIdleSearchHistory(qint64 position);
    void initializeBandpass();
    void initializeIqLowpass();
    double filterBandpass(double sample);
    void demodulate(float sample);
    int classifyTone(double frequency) const;
    void completeToneRun(int tone, qint64 start, qint64 end, int nextTone);
    void tryCorrelatedVisRecovery(qint64 position);
    void tryDecodeVis();
    void announceModeIfNeeded();
    double meanFrequency(qint64 start, qint64 end) const;
    double percentileFrequency(qint64 start, qint64 end, double percentile) const;
    double tonePresenceFraction(qint64 start, qint64 end, double targetHz, double offsetHz, double toleranceHz) const;
    qint64 longestToneRun(qint64 start, qint64 end, double targetHz, double offsetHz, double toleranceHz,
                          qint64 mergeGapSamples) const;
    void processSyncSample(double frequency, qint64 position);
    void acceptSync(qint64 start, qint64 end);
    void updateImageAfc(int line, qint64 start, qint64 end);
    double offsetForLine(int line) const;
    void updateSlantEstimate();
    void renderAvailableLines(bool forceAll = false);
    void decodeScanLine(int scanLine, double startSample);
    int sampleLevel(double position, double pixelSamples) const;
    double medianFrequency(qint64 start, qint64 end) const;
    QRgb yuvPixel(int y, int ry, int by) const;
    double nominalLineSamples() const;
    int scanLineCount() const;
    void beginPostImageIds();
    void consumePostImageId(float sample, double correctedFrequency);
    void resetFskIdSearch();
    void consumeFskId(double correctedFrequency);
    void processFskSymbol(quint8 symbol);
    void tryDecodeCwId();

    State m_state = State::SearchingVis;
    const SstvModeSpec *m_mode = nullptr;
    QVector<float> m_frequency;

    // A narrow linear-phase FIR removes audio outside the SSTV signalling
    // band before the FM discriminator. Its fixed delay is shared by VIS,
    // sync and pixel timing, so packet boundaries cannot move relative to one
    // another. Raw audio remains available to the post-image CW detector.
    static constexpr int BandpassTapCount = 65;
    std::array<double, BandpassTapCount> m_bandpassTaps{};
    std::array<double, BandpassTapCount> m_bandpassHistory{};
    int m_bandpassIndex = 0;

    // Complex-baseband FM discriminator. A flat-passband low-pass preserves
    // the complete AFC-shifted SSTV deviation while rejecting the image around
    // twice the 1900 Hz mixer frequency. This replaces the former eight-sample
    // boxcar, whose droop disproportionately weakened 1100/1200 Hz VIS tones.
    static constexpr int IqTapCount = 49;
    std::array<double, IqTapCount> m_iqTaps{};
    double m_ncoPhase = 0.0;
    std::array<double, IqTapCount> m_iHistory{};
    std::array<double, IqTapCount> m_qHistory{};
    int m_iqIndex = 0;
    bool m_haveBasebandSample = false;
    double m_previousI = 0.0;
    double m_previousQ = 0.0;
    double m_smoothedFrequency = 1900.0;
    double m_levelEnergy = 0.0;
    int m_levelSamples = 0;
    QTimer *m_streamWatchdog = nullptr;
    bool m_streamActive = false;

    int m_toneRun = 0;
    qint64 m_toneRunStart = 0;
    int m_pendingTone = -1;
    qint64 m_pendingToneStart = 0;
    bool m_haveTuningOffset = false;
    double m_tuningOffsetHz = 0.0;
    bool m_seenFirstLeader = false;
    bool m_seenBreak = false;
    qint64 m_firstLeaderEnd = -1;
    qint64 m_visSequenceDeadline = -1;
    bool m_ignoreLeaderUntilTransition = false;
    qint64 m_visStart = -1;
    qint64 m_lastCorrelatedVisScan = -1;
    AcquisitionKind m_acquisitionKind = AcquisitionKind::FullPreamble;
    bool m_modeAnnounced = false;
    bool m_acquisitionConfirmed = false;
    qint64 m_acquisitionDeadline = -1;
    qint64 m_confirmationSyncSample = -1;
    int m_confirmationSyncCount = 0;

    bool m_inSync = false;
    qint64 m_syncStart = 0;
    std::deque<std::pair<double, qint64>> m_syncMedianWindow;
    std::vector<std::pair<double, qint64>> m_syncMedianSorted;
    std::deque<std::pair<qint64, double>> m_syncMinimums;
    std::deque<std::pair<qint64, double>> m_syncMaximums;
    QVector<SyncObservation> m_syncs;
    QVector<AfcObservation> m_imageAfc;
    double m_renderTuningOffsetHz = 0.0;
    qint64 m_firstLineStart = -1;
    qint64 m_imageStart = -1;
    double m_linePeriod = 0.0;
    bool m_slantLocked = false;
    QString m_slantStatus = QStringLiteral("SLANT: AUTO • WAITING");
    int m_renderedScanLines = 0;
    QImage m_image;
    QVector<int> m_robotRy;

    enum class FskIdState { Leader1500, Mark2100, Start1900, Bits };
    bool m_postIdActive = false;
    bool m_postFskEmitted = false;
    bool m_postCwEmitted = false;
    qint64 m_postIdSamples = 0;
    double m_postIdOffsetHz = 0.0;
    QVector<float> m_postIdAudio;
    FskIdState m_fskIdState = FskIdState::Leader1500;
    int m_fskToneSamples = 0;
    qint64 m_fskNextBitSample = 0;
    quint8 m_fskSymbol = 0;
    int m_fskBit = 0;
    bool m_fskHaveHeader = false;
    bool m_fskExpectChecksum = false;
    QByteArray m_fskText;
    quint8 m_fskChecksum = 0;
};

#endif // SSTVDECODER_H
