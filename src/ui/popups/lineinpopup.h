#ifndef LINEINPOPUP_H
#define LINEINPOPUP_H

#include "ui/popups/k4popupbase.h"
#include "utils/wheelaccumulator.h"
#include <QPushButton>

// Floating popup for LINE IN source + level (SoundCard vs LineInJack).
// Sends back level (0-250) and source (0/1) changes for MainWindow to
// emit as K4 LMC / MIC-related CAT.
class LineInPopupWidget : public K4PopupBase {
    Q_OBJECT
public:
    explicit LineInPopupWidget(QWidget *parent = nullptr);

    void setSoundCardLevel(int level);  // 0-250
    void setLineInJackLevel(int level); // 0-250
    void setSource(int source);         // 0=SoundCard, 1=LineInJack

    int source() const { return m_source; }

signals:
    void soundCardLevelChanged(int level);
    void lineInJackLevelChanged(int level);
    void sourceChanged(int source);

protected:
    QSize contentSize() const override;
    void paintContent(QPainter &painter, const QRect &contentRect) override;
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

    int m_soundCardLevel = 0;
    int m_lineInJackLevel = 0;
    int m_source = 0; // 0=SoundCard, 1=LineInJack
    WheelAccumulator m_wheelAccumulator;
};

#endif // LINEINPOPUP_H
