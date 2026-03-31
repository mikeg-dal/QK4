#ifndef KPA1500MINIPANEL_H
#define KPA1500MINIPANEL_H

#include <QWidget>
#include <QPushButton>
#include <QTimer>

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
    void setAtuMode(bool in);
    void setAntenna(int ant);
    void setFault(bool fault);
    void setConnected(bool connected);

signals:
    void modeToggled(bool operate);
    void atuTuneRequested();
    void atuModeToggled(bool in);
    void antennaChanged(int ant);

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
    bool m_atuIn = false;
    int m_antenna = 1;
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
    static constexpr int TOP_PAD = 16;
    static constexpr int HEADER_HEIGHT = 16;
    static constexpr int BAR_HEIGHT = 8;
    static constexpr int METER_SPACING = 18;
    static constexpr int LABEL_WIDTH = 22;
    static constexpr int VALUE_WIDTH = 28;
    static constexpr int MARGIN = 2;
    static constexpr int METER_START_Y = TOP_PAD + HEADER_HEIGHT + 4;
    static constexpr int ATU_LABEL_HEIGHT = 14;
};

#endif // KPA1500MINIPANEL_H
