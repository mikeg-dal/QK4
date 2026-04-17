#ifndef LINEINPOPUP_H
#define LINEINPOPUP_H

#include <QWidget>
#include <QPushButton>
#include "wheelaccumulator.h"

class LineInPopupWidget : public QWidget {
    Q_OBJECT
public:
    explicit LineInPopupWidget(QWidget *parent = nullptr);

    void setSoundCardLevel(int level);  // 0-250
    void setLineInJackLevel(int level); // 0-250
    void setSource(int source);         // 0=SoundCard, 1=LineInJack

    int soundCardLevel() const { return m_soundCardLevel; }
    int lineInJackLevel() const { return m_lineInJackLevel; }
    int source() const { return m_source; }

    void showAboveWidget(QWidget *widget);
    void hidePopup();

signals:
    void soundCardLevelChanged(int level);
    void lineInJackLevelChanged(int level);
    void sourceChanged(int source);

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void setupUi();
    void updateButtonStyles();
    void updateValueDisplay();
    void adjustValue(int delta);

    QPushButton *m_titleLabel;
    QPushButton *m_soundCardBtn;
    QPushButton *m_lineInJackBtn;
    QPushButton *m_valueLabel;
    QPushButton *m_decrementBtn;
    QPushButton *m_incrementBtn;
    QPushButton *m_closeBtn;

    QWidget *m_referenceWidget = nullptr;

    int m_soundCardLevel = 0;
    int m_lineInJackLevel = 0;
    int m_source = 0; // 0=SoundCard, 1=LineInJack
    WheelAccumulator m_wheelAccumulator;
};

#endif // LINEINPOPUP_H
