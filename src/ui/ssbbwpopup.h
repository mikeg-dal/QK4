#ifndef SSBBWPOPUP_H
#define SSBBWPOPUP_H

#include <QWidget>
#include <QPushButton>
#include "wheelaccumulator.h"

class SsbBwPopupWidget : public QWidget {
    Q_OBJECT
public:
    explicit SsbBwPopupWidget(QWidget *parent = nullptr);

    void setEssbEnabled(bool enabled); // Affects title and valid range
    void setBandwidth(int bw);         // SSB: 24-28 (2.4-2.8 kHz), ESSB: 30-45 (3.0-4.5 kHz)

    bool essbEnabled() const { return m_essbEnabled; }
    int bandwidth() const { return m_bandwidth; }

    void showAboveWidget(QWidget *widget);
    void hidePopup();

signals:
    void bandwidthChanged(int bw);

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void setupUi();
    void updateTitle();
    void updateValueDisplay();
    void adjustValue(int delta);

    QPushButton *m_titleLabel;
    QPushButton *m_valueLabel;
    QPushButton *m_decrementBtn;
    QPushButton *m_incrementBtn;
    QPushButton *m_closeBtn;

    QWidget *m_referenceWidget = nullptr;

    bool m_essbEnabled = false;
    int m_bandwidth = 28; // SSB default: 2.8 kHz
    WheelAccumulator m_wheelAccumulator;
};

#endif // SSBBWPOPUP_H
