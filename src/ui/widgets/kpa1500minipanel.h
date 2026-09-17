#ifndef KPA1500MINIPANEL_H
#define KPA1500MINIPANEL_H

#include <QWidget>
#include <QPushButton>
#include <QTimer>

/**
 * @brief Compact KPA1500 amplifier panel docked in the right-side panel. Shows forward/reflected
 *        power + SWR + temp meters (with peak hold + decay animation), a two-line LCD with the
 *        operate/fault state, active antenna, band and ATU state, and the MODE / ATU / ANT / TUNE
 *        buttons. Driven by KPA1500Client meter and state signals.
 */
class Kpa1500MiniPanel : public QWidget {
    Q_OBJECT

public:
    explicit Kpa1500MiniPanel(QWidget *parent = nullptr);

    // Meter setters (same API as KPA1500Panel)
    void setForwardPower(float watts);
    void setReflectedPower(float watts);
    void setSWR(float swr);
    void setTemperature(float celsius);

    // State setters
    void setMode(bool operate);
    void setAtuMode(bool modeInline);    // ^AM: ATU mode (enabled/disabled)
    void setAtuInline(bool relayInline); // ^AI: ATU relay state (in-circuit/bypassed)
    // Active antenna 1-32 and the connector it is routed through (1/2, or 0 while unknown).
    void setAntenna(int antenna, int connector);
    void setBand(const QString &bandLabel);
    void setFault(bool fault);
    void setConnected(bool connected);

signals:
    void modeToggled(bool operate);
    void atuTuneRequested();
    void atuModeToggled(bool in);
    void nextAntennaRequested(); // ANT button: step to the next antenna enabled on the amp

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void onDecayTimer();

private:
    void updateButtonLabels();

    // Animation timer
    QTimer *m_decayTimer;

    // Actual values (from client)
    float m_forwardPower = 0.0f;
    float m_reflectedPower = 0.0f;
    float m_swr = 1.0f;
    float m_temperature = 0.0f;

    // Display values (smoothed for animation)
    float m_displayForward = 0.0f;
    float m_displayReflected = 0.0f;
    float m_displaySwr = 1.0f;
    float m_displayTemp = 0.0f;

    // Peak values (hold + decay)
    float m_peakForward = 0.0f;
    float m_peakReflected = 0.0f;
    float m_peakSwr = 1.0f;

    // State
    bool m_operate = false;
    bool m_atuModeInline = false;  // ^AM: ATU mode enabled
    bool m_atuRelayInline = false; // ^AI: ATU relays in-circuit
    int m_antenna = 1;
    int m_antennaConnector = 1;
    QString m_band;
    bool m_fault = false;
    bool m_connected = false;

    // Buttons
    QPushButton *m_modeBtn;
    QPushButton *m_atuBtn;
    QPushButton *m_antBtn;
    QPushButton *m_tuneBtn;

    // Animation constants
    static constexpr int DECAY_INTERVAL_MS = 33;
    static constexpr float ATTACK_RATE = 0.35f;
    static constexpr float DECAY_RATE = 0.06f;
    static constexpr float PEAK_DECAY_RATE = 0.04f;

    // Layout constants
    static constexpr int PANEL_PAD = 6; // Internal padding for panel background edges
    static constexpr int TOP_PAD = 14;  // Above header
    static constexpr int HEADER_HEIGHT = 14;
    static constexpr int BAR_HEIGHT = 7;
    static constexpr int METER_SPACING = 16;
    static constexpr int LABEL_WIDTH = 24;
    static constexpr int VALUE_WIDTH = 30;
    static constexpr int METER_START_Y = TOP_PAD + HEADER_HEIGHT + 4;

    // LCD constants (occupies the space the LED indicator cards used)
    static constexpr int LCD_ROW_HEIGHT = 12; // Per text line
    static constexpr int LCD_TOP_PAD = 5;     // Space above the LCD
    static constexpr int LCD_TEXT_PAD = 4;    // Horizontal inset of text inside the LCD
    static constexpr int LCD_HEIGHT = LCD_TOP_PAD + (LCD_ROW_HEIGHT * 2) + 6;
};

#endif // KPA1500MINIPANEL_H
