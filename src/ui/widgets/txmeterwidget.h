#ifndef TXMETERWIDGET_H
#define TXMETERWIDGET_H

#include <QWidget>
#include <QTimer>

/**
 * TxMeterWidget - Multi-function TX meter display (IC-7760 style)
 *
 * Displays 5 horizontal bar meters stacked vertically:
 * - Po (Forward Power): 0-100W (QRO) or 0-10W (QRP)
 * - ALC: 0-10 bars
 * - COMP: 0-30 dB compression
 * - SWR: 1.0 to 3.0+ ratio
 * - Id: 0-15A current draw
 *
 * Appears below S-meter on the TX VFO side (follows split state).
 * Uses S-meter gradient (green→red) for Po, ALC, COMP, SWR.
 * Id uses a subtle green theme.
 */
class TxMeterWidget : public QWidget {
    Q_OBJECT

public:
    explicit TxMeterWidget(QWidget *parent = nullptr);

    // Set meter values (from TM command and supply current)
    void setPower(double watts, bool isQrp = false);
    void setAlc(int bars);        // 0-10
    void setCompression(int dB);  // 0-30
    void setSwr(double ratio);    // 1.0-3.0+
    void setCurrent(double amps); // 0-25
    void setQrp(bool isQrp);

    // A scale mark: the value it denotes, where along the bar that value falls (0.0-1.0), and
    // the text drawn beneath it.
    //
    // WHY the position is explicit, and why `value` is carried alongside it. Labels used to be
    // spread evenly across the bar, which is only correct when the label VALUES are themselves
    // evenly spaced over the full range. Po, COMP and Id happen to satisfy that; ALC ("1,3,5,7"),
    // SWR (an infinity label consuming a tick slot of its own) and the S-meter (whose S1 belongs
    // at 1/15 of the bar, not at 0) did not, so their marks sat away from the values actually
    // drawn there - the S9 mark at 57% of a bar where S9 fills to 60%, and the ALC "3" mark half
    // a division from where an ALC of 3 reaches. Real meters are not uniformly marked; forcing
    // uniform spacing was the wrong constraint. Keeping `value` next to `pos` is what lets
    // test_meterscale assert that the two agree.
    struct ScaleMark {
        double value;
        double pos; // 0.0 = left end of the bar, 1.0 = right end
        QString text;
    };

    // Scale maxima. Public so the marks can be checked against the same numbers the setters use -
    // a mark and a fill disagreeing about full scale is exactly the defect this replaced.
    static constexpr double MaxPowerQRO = 110.0;     // K4 max power in QRO mode (watts)
    static constexpr double MaxPowerQRP = 10.0;      // K4 max power in QRP mode (watts)
    static constexpr double MaxPowerDefault = 100.0; // Default max for setPower() (watts)
    static constexpr int MaxAlcBars = 7;             // K4 ALC meter scale (marked 1/3/5/7)
    static constexpr int MaxCompressionDb = 25;      // Compression display max (dB)
    static constexpr double MaxCurrentAmps = 25.0;   // K4 PA max drain current (amps)
    static constexpr double MaxSwrScale = 2.0;       // SWR display range: 1.0 to 3.0 (mapped to 0-1)
    static constexpr double MaxSMeter = 15.0;        // S-meter max (S9+60dB)
    static constexpr double MinSwr = 1.0;            // SWR bar starts at 1.0, not 0

    // The scale marks for each meter. Static and public so they can be asserted directly.
    static QList<ScaleMark> sMeterMarks();
    static QList<ScaleMark> powerMarks(bool qrp);
    static QList<ScaleMark> alcMarks();
    static QList<ScaleMark> compMarks();
    static QList<ScaleMark> swrMarks();
    static QList<ScaleMark> currentMarks();

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

    // Decay timer
    QTimer *m_decayTimer;
    static constexpr int DecayIntervalMs = 50;    // Timer fires every 50ms
    static constexpr double DecayRate = 0.1;      // Ratio units per interval (~500ms full decay)
    static constexpr double PeakDecayRate = 0.05; // Peak decays slower
    static constexpr int PeakHoldTicks = 10;      // 500ms hold time (10 × 50ms)

    // K4 hardware meter scaling constants

    // Meter types for color selection
    enum class MeterType { Gradient, Red };

    // Drawing helpers
    void drawMeterRow(QPainter &painter, int y, int rowHeight, const QString &label, double fillRatio, double peakRatio,
                      const QList<ScaleMark> &marks, const QFont &scaleFont, int barStartX, int barWidth, int barHeight,
                      MeterType type);
};

#endif // TXMETERWIDGET_H
