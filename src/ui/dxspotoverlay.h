#ifndef DXSPOTOVERLAY_H
#define DXSPOTOVERLAY_H

#include <QVector>
#include <QWidget>

#include "network/dxclusterclient.h"

class DxSpotOverlay : public QWidget {
    Q_OBJECT

public:
    explicit DxSpotOverlay(QWidget *parent = nullptr);

    void setSpots(const QVector<DxSpot> &spots);
    void setFrequencyRange(qint64 centerFreq, int spanHz);

signals:
    void spotClicked(qint64 frequencyHz);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

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

    QVector<DxSpot> m_spots;
    QVector<SpotLabel> m_labels;
    qint64 m_centerFreq = 0;
    int m_spanHz = 10000;

    static constexpr int ROW_HEIGHT = 12;
    static constexpr int LABEL_GAP = 4;
    static constexpr int BOTTOM_MARGIN = 28; // Clear frequency labels + spectrum trace
    static constexpr int TOP_MARGIN = 4;
    static constexpr int MAX_DISPLAY_ROWS = 8;  // Cap rows even if space allows more
    static constexpr int MAX_PER_FREQUENCY = 5; // Max spots shown at same frequency (FT8 etc.)
};

#endif // DXSPOTOVERLAY_H
