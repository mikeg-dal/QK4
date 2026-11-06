#ifndef DXSPOTOVERLAY_H
#define DXSPOTOVERLAY_H

#include <QVector>
#include <QWidget>

#include "network/dxclusterclient.h"

/**
 * @brief Transparent overlay drawn on top of the panadapter that plots DX-cluster spots by
 *        frequency. Lays out callsign labels in up to MAX_DISPLAY_ROWS rows (stacking same-freq
 *        clusters), caps per-frequency at MAX_PER_FREQUENCY. Left-clicking a label emits
 *        `spotClicked(frequencyHz)`, right-clicking emits `spotRightClicked(frequencyHz)` —
 *        both for click-to-tune (left → VFO A, right → VFO B).
 */
class DxSpotOverlay : public QWidget {
    Q_OBJECT

public:
    explicit DxSpotOverlay(QWidget *parent = nullptr);

    void setSpots(const QVector<DxSpot> &spots);
    void setFrequencyRange(qint64 centerFreq, int spanHz);
    // Pixels at the left edge that labels keep out of (the panadapter's amplitude-axis column).
    void setLeftReservedPx(int px);

public slots:
    // Pixel size for spot label text (clamped to FontSizeSpotMin..FontSizeSpotMax).
    void setFontPixelSize(int px);

signals:
    void spotClicked(qint64 frequencyHz);
    void spotRightClicked(qint64 frequencyHz);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    struct SpotLabel {
        QString callsign;
        qint64 frequencyHz;
        int xPixel;
        int row;
        QRect rect;
    };

    void layoutLabels();
    float freqToX(qint64 freq) const;
    // Left edge for a label of textWidth centred on xCenter: inside the widget and clear of the
    // reserved column, unless the widget is too narrow to honour both.
    int labelLeftFor(int xCenter, int textWidth) const;
    int rowHeight() const { return m_fontPixelSize + 4; }

    QVector<DxSpot> m_spots;
    QVector<SpotLabel> m_labels;
    qint64 m_centerFreq = 0;
    int m_spanHz = 10000;
    int m_leftReservedPx = 0;
    int m_fontPixelSize = 11; // K4Styles::Dimensions::FontSizeSpot default

    static constexpr int LABEL_GAP = 4;
    static constexpr int TOP_MARGIN = 4;
    static constexpr int MAX_DISPLAY_ROWS = 8;  // Cap rows even if space allows more
    static constexpr int MAX_PER_FREQUENCY = 5; // Max spots shown at same frequency (FT8 etc.)
};

#endif // DXSPOTOVERLAY_H
