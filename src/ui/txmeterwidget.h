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

    // Meter types for color selection
    enum class MeterType { Gradient, Red };

    // Drawing helpers
    void drawMeterRow(QPainter &painter, int y, int rowHeight, const QString &label, double fillRatio, double peakRatio,
                      const QStringList &scaleLabels, const QFont &scaleFont, int barStartX, int barWidth,
                      int barHeight, MeterType type);
};

#endif // TXMETERWIDGET_H
