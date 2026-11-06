#ifndef KEYINGWEIGHTPOPUP_H
#define KEYINGWEIGHTPOPUP_H

#include "ui/popups/k4popupbase.h"
#include "utils/wheelaccumulator.h"
#include <QPushButton>

// Floating popup for CW keying weight: the KP weight field (090-125,
// dit/dah ratio x 100) behind a +/- interface. Emits weightChanged(raw);
// PopupManager sends the full KP frame.
class KeyingWeightPopupWidget : public K4PopupBase {
    Q_OBJECT
public:
    explicit KeyingWeightPopupWidget(QWidget *parent = nullptr);

    void setWeight(int weight); // Raw 090-125 value
    int weight() const { return m_weight; }

signals:
    void weightChanged(int weight); // Raw 090-125 value

protected:
    QSize contentSize() const override;
    void paintContent(QPainter &painter, const QRect &contentRect) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void setupUi();
    void updateValueDisplay();
    void adjustValue(int delta);

    QPushButton *m_titleLabel;
    QPushButton *m_valueLabel;
    QPushButton *m_decrementBtn;
    QPushButton *m_incrementBtn;
    QPushButton *m_closeBtn;

    int m_weight = 100; // Default 1.00 ratio (range 090-125)
    WheelAccumulator m_wheelAccumulator;
};

#endif // KEYINGWEIGHTPOPUP_H
