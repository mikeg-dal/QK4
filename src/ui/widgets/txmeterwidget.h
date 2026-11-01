#ifndef TXMETERWIDGET_H
#define TXMETERWIDGET_H

#include <QWidget>
#include <QTimer>

/**
 * TxMeterWidget - Multi-function TX meter display (IC-7760 style)
 *
 * Displays 5 horizontal bar meters stacked vertically:
 * - S/Po: S-meter on receive, forward power 0-110 W (QRO) or 0-10 W (QRP) on transmit
 * - ALC: 0-7, marked 1/3/5/7
 * - COMP: 0-25 dB
 * - SWR: 1.0 to 3.0+
 * - Id: 0-25 A drain current
 *
 * One per VFO, in the VFO widget's meter row.
 * Uses S-meter gradient (green→red) for Po, ALC, COMP, SWR.
 * Id uses a subtle green theme.
 */
class TxMeterWidget : public QWidget {
    Q_OBJECT

public:
    explicit TxMeterWidget(QWidget *parent = nullptr);

    // PA drain current, amps
    void setCurrent(double amps); // 0-25
    void setQrp(bool isQrp);

    // A scale mark: the value it denotes, where along the bar that value falls (0.0-1.0), and
    // the text drawn beneath it.
    //
    // WHY the position is explicit: real meter scales are not uniform (ALC 1/3/5/7, S1 at 1/15
    // of the bar). Keeping `value` next to `pos` lets test_meterscale assert that the two agree.
    struct ScaleMark {
        double value;
        double pos; // 0.0 = left end of the bar, 1.0 = right end
        QString text;
    };

    // Scale maxima. Public so test_meterscale checks the marks against the same numbers the
    // setters use.
    static constexpr double MaxPowerQRO = 110.0;   // K4 max power in QRO mode (watts)
    static constexpr double MaxPowerQRP = 10.0;    // K4 max power in QRP mode (watts)
    static constexpr int MaxAlcBars = 7;           // K4 ALC meter scale (marked 1/3/5/7)
    static constexpr int MaxCompressionDb = 25;    // Compression display max (dB)
    static constexpr double MaxCurrentAmps = 25.0; // K4 PA max drain current (amps)
    static constexpr double MaxSwrScale = 2.0;     // SWR display range: 1.0 to 3.0 (mapped to 0-1)
    static constexpr double MaxSMeter = 15.0;      // S-meter max (S9+60dB)
    static constexpr double MinSwr = 1.0;          // SWR bar starts at 1.0, not 0

    // The scale marks for each meter. Static and public so they can be asserted directly.
    static QList<ScaleMark> sMeterMarks();
    static QList<ScaleMark> powerMarks(bool qrp);
    static QList<ScaleMark> alcMarks();
    static QList<ScaleMark> compMarks();
    static QList<ScaleMark> swrMarks();
    static QList<ScaleMark> currentMarks();

    // Left edge of a scale label of textWidth for a mark at markX: centred on the mark, but kept
    // within [barLeft, barRight] so the end labels (0, +60, 25dB) never overhang the bar.
    static qreal scaleLabelLeft(qreal markX, qreal textWidth, qreal barLeft, qreal barRight);

    // Set all TX meters at once (from txMeterChanged signal)
    void setTxMeters(int alc, int compDb, double fwdPower, double swr);

    // S-meter mode (for dual S/Po meter)
    void setSMeter(double sValue);   // S-units (0-9 for S1-S9, 9+ for +dB over S9)
    void setTransmitting(bool isTx); // Switch between RX (S-meter) and TX (Po) mode

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void decayValues();

private:
    // Target values (what we're decaying toward)
    double m_powerTarget = 0.0;
    double m_alcTarget = 0.0;
    double m_compTarget = 0.0;
    double m_swrTarget = 0.0;
    double m_currentTarget = 0.0;

    // Displayed values (with decay/smoothing)
    double m_powerDisplay = 0.0;
    double m_alcDisplay = 0.0;
    double m_compDisplay = 0.0;
    double m_swrDisplay = 0.0;
    double m_currentDisplay = 0.0;

    // Peak hold values
    double m_powerPeak = 0.0;
    double m_alcPeak = 0.0;
    double m_compPeak = 0.0;
    double m_swrPeak = 0.0;
    double m_currentPeak = 0.0;

    // Peak hold timers (counts down from PeakHoldTicks)
    int m_powerPeakHold = 0;
    int m_alcPeakHold = 0;
    int m_compPeakHold = 0;
    int m_swrPeakHold = 0;
    int m_currentPeakHold = 0;

    // S-meter mode values (for dual S/Po meter)
    double m_sMeterTarget = 0.0;
    double m_sMeterDisplay = 0.0;
    double m_sMeterPeak = 0.0;
    int m_sMeterPeakHold = 0;

    bool m_isQrp = false;
    bool m_isTransmitting = false; // RX mode shows S-meter, TX mode shows Po

    // Decay timer: runs only while a display or peak value is still above its target.
    QTimer *m_decayTimer;
    void startDecay();
    static constexpr int DecayIntervalMs = 50;    // Timer fires every 50ms
    static constexpr double DecayRate = 0.1;      // Ratio units per interval (~500ms full decay)
    static constexpr double PeakDecayRate = 0.05; // Peak decays slower
    static constexpr int PeakHoldTicks = 10;      // 500ms hold time (10 × 50ms)

    // Meter types for color selection
    enum class MeterType { Gradient, Red };

    // Drawing helpers
    void drawMeterRow(QPainter &painter, int y, int rowHeight, const QString &label, double fillRatio, double peakRatio,
                      const QList<ScaleMark> &marks, const QFont &scaleFont, int barStartX, int barWidth, int barHeight,
                      MeterType type);
};

#endif // TXMETERWIDGET_H
