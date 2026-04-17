#ifndef LINEOUTPOPUP_H
#define LINEOUTPOPUP_H

#include <QWidget>
#include <QPushButton>
#include "wheelaccumulator.h"

class LineOutPopupWidget : public QWidget {
    Q_OBJECT

public:
    explicit LineOutPopupWidget(QWidget *parent = nullptr);

    void setLeftLevel(int level);  // 0-40
    void setRightLevel(int level); // 0-40
    void setRightEqualsLeft(bool enabled);
    void showAboveWidget(QWidget *widget);
    void hidePopup();

    int leftLevel() const { return m_leftLevel; }
    int rightLevel() const { return m_rightLevel; }
    bool rightEqualsLeft() const { return m_rightEqualsLeft; }

signals:
    void leftLevelChanged(int level);
    void rightLevelChanged(int level);
    void rightEqualsLeftChanged(bool enabled);

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void setupUi();
    void updateButtonStyles();
    void updateValueLabels();

    int m_leftLevel = 10;
    int m_rightLevel = 10;
    bool m_rightEqualsLeft = false;
    bool m_leftSelected = true; // Which channel wheel adjusts

    QPushButton *m_titleLabel;
    QPushButton *m_leftBtn;
    QPushButton *m_leftValueLabel;
    QPushButton *m_rightBtn;
    QPushButton *m_rightValueLabel;
    QPushButton *m_rightEqualsLeftBtn;
    QPushButton *m_closeBtn;

    QWidget *m_referenceWidget = nullptr;
    WheelAccumulator m_wheelAccumulator;
};

#endif // LINEOUTPOPUP_H
