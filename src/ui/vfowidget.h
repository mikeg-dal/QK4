#ifndef VFOWIDGET_H
#define VFOWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QColor>

class MiniPanRhiWidget;
class TxMeterWidget;
class FrequencyDisplayWidget;

class VFOWidget : public QWidget {
    Q_OBJECT

public:
    enum VFOType { VFO_A, VFO_B };

    explicit VFOWidget(VFOType type, QWidget *parent = nullptr);

    // Setters for radio state
    void setFrequency(const QString &formatted);
    void setTuningRate(int rate); // 0-5: 1Hz, 10Hz, 100Hz, 1kHz, 10kHz, 100kHz
    void setSMeterValue(double value);
    void setAGC(const QString &mode);
    void setPreamp(bool on, int level);
    void setAtt(bool on, int level);
    void setNB(bool on);
    void setNR(bool on);
    void setNotch(bool autoEnabled, bool manualEnabled);
    void setApf(bool enabled, int bandwidth); // APF: 0=30Hz, 1=50Hz, 2=150Hz

    // TX Meter support (multifunction meter - S/Po, ALC, COMP, SWR, Id)
    void setTransmitting(bool isTx); // Switch meter between RX (S-meter) and TX (Po) mode
    void setTxMeters(int alc, int compDb, double fwdPower, double swr);
    void setTxMeterCurrent(double amps);
    void setTxMeterQrp(bool isQrp);

    // Mini-pan support
    void updateMiniPan(const QByteArray &data);
    void showMiniPan();
    void showNormal();
    bool isMiniPanVisible() const;

    // Mini-pan configuration (applied when miniPan is created, or immediately if it exists)
    void setMiniPanMode(const QString &mode);
    void setMiniPanDataSubMode(int subMode);
    void setMiniPanFilterBandwidth(int bw);
    void setMiniPanIfShift(int shift);
    void setMiniPanCwPitch(int pitch);
    void setMiniPanNotchFilter(bool enabled, int pitchHz);
    void setMiniPanPassbandColor(const QColor &color);
    void setMiniPanWaterfallHeight(int percent);
    void setMiniPanAveraging(int level);

    // Access to mini-pan (may return nullptr if not yet created)
    MiniPanRhiWidget *miniPan() const { return m_miniPan; }

    // Access to frequency display for styling (e.g., dimming when SUB RX off)
    FrequencyDisplayWidget *frequencyDisplay() const { return m_frequencyDisplay; }

    // Get type
    VFOType type() const { return m_type; }

    // Check if frequency entry is active (in edit mode)
    bool isFrequencyEntryActive() const;

signals:
    void normalContentClicked();                      // User clicked normal view → show mini-pan
    void miniPanClicked();                            // User clicked mini-pan → show normal view
    void frequencyEntered(const QString &freqString); // User entered new frequency
    void frequencyScrolled(int steps);                // User scrolled wheel over frequency

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setupUi();

    VFOType m_type;
    QString m_primaryColor;

    // Tuning rate indicator (0-5: 1Hz to 100kHz)
    int m_tuningRate = 3; // Default 1kHz

    // Widgets
    FrequencyDisplayWidget *m_frequencyDisplay;
    QLabel *m_agcLabel;
    QLabel *m_preampLabel;
    QLabel *m_attLabel;
    QLabel *m_nbLabel;
    QLabel *m_nrLabel;
    QLabel *m_ntchLabel;
    QLabel *m_apfLabel;

    // Stacked widget for normal/mini-pan toggle
    QStackedWidget *m_stackedWidget;
    QWidget *m_normalContent;
    MiniPanRhiWidget *m_miniPan;

    // TX Meter (multifunction meter for TX)
    TxMeterWidget *m_txMeter;

    // Pending mini-pan configuration (applied when created)
    QString m_pendingMode;
    int m_pendingDataSubMode = 0;
    int m_pendingFilterBw = 2400;
    int m_pendingIfShift = 50;
    int m_pendingCwPitch = 600;
    bool m_pendingNotchEnabled = false;
    int m_pendingNotchPitchHz = 0;
    QColor m_pendingPassbandColor;
    int m_pendingWaterfallHeight = -1; // -1 = unset, use MiniPan default
    int m_pendingAveraging = 1;
};

#endif // VFOWIDGET_H
