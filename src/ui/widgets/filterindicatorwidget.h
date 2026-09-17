#ifndef FILTERINDICATORWIDGET_H
#define FILTERINDICATORWIDGET_H

#include <QColor>
#include <QWidget>
#include "ui/styling/k4constants.h"

class RadioState;

// Compact filter indicator widget showing filter position,
// bandwidth shape, and shift position above a horizontal line
class FilterIndicatorWidget : public QWidget {
    Q_OBJECT

public:
    enum class Vfo { A, B };

    explicit FilterIndicatorWidget(QWidget *parent = nullptr);

    // Direct Observation (PATTERNS.md): wire this widget's setters to the
    // RadioState signals for the named VFO. Replaces the former
    // FilterIndicatorController thin-shell. Call once after construction.
    void observe(RadioState *state, Vfo vfo);

    // Filter position (1, 2, or 3) - displayed as FIL1/FIL2/FIL3
    void setFilterPosition(int position);
    int filterPosition() const { return m_filterPosition; }

    // Bandwidth in Hz - controls shape morphing (triangle → trapezoid)
    void setBandwidth(int bandwidthHz);
    int bandwidth() const { return m_bandwidthHz; }

    // Shift position in decahertz (10 Hz units) - controls horizontal position
    // SSB: ~135 = 1350 Hz center, CW: ~50 = 500 Hz (pitch)
    void setShift(int shift);
    int shift() const { return m_shift; }

    // Mode affects shift center calculation (SSB vs CW)
    void setMode(const QString &mode);
    QString mode() const { return m_mode; }

    // DATA submode (0=DATA-A, 1=AFSK-A, 2=FSK-D, 3=PSK-D)
    void setDataSubMode(int subMode);

    // Bandwidth range for normalization
    void setBandwidthRange(int minHz, int maxHz);

    // Shape color (for VFO A/B color coding)
    void setShapeColor(const QColor &fill, const QColor &outline);

    // Reset to idle state on K4 disconnect: zero bandwidth, 50 decahertz
    // shift, centered filter position, empty mode, no data sub-mode.
    void resetToDefaults();

signals:
    // Left-click: MainWindow routes this to RightSideController::cycleFilterPreset for this VFO.
    void clicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void drawBandwidthShape(QPainter &painter, int lineY, int lineWidth);
    void drawRttyToneTriangles(QPainter &painter, int lineY, int lineWidth);
    void drawPskToneTriangle(QPainter &painter, int lineY, int lineWidth);

    int m_filterPosition = 2;
    int m_bandwidthHz = 2400;    // Current bandwidth in Hz
    int m_shift = 135;           // Shift in decahertz
    QString m_mode = "USB";      // Mode for shift center calculation
    int m_dataSubMode = 0;       // DATA submode (0=DATA-A, 1=AFSK-A, 2=FSK-D, 3=PSK-D)
    int m_minBandwidthHz = 50;   // Minimum bandwidth (triangle)
    int m_maxBandwidthHz = 5000; // Maximum bandwidth (full trapezoid)

    // Gold palette — pulled from K4Styles::Colors::FilterIndicatorGold so a future palette tweak
    // propagates here. Shape fill uses 50% alpha so the passband shape shows through.
    QColor m_lineColor{QColor(K4Styles::Colors::FilterIndicatorGold)};
    QColor m_textColor{QColor(K4Styles::Colors::FilterIndicatorGold)};
    QColor m_shapeColor{[] {
        QColor c(K4Styles::Colors::FilterIndicatorGold);
        c.setAlpha(128);
        return c;
    }()};
    QColor m_shapeOutline{QColor(K4Styles::Colors::FilterIndicatorGold)};
};

#endif // FILTERINDICATORWIDGET_H
