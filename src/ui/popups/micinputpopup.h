#ifndef MICINPUTPOPUP_H
#define MICINPUTPOPUP_H

#include "ui/popups/k4popupbase.h"
#include <QPushButton>

// Floating popup for choosing the mic input source (Front / Rear /
// LINE IN / Front+Line / Rear+Line). Mirrors the K4 MC command options;
// emits inputChanged(0-4).
class MicInputPopupWidget : public K4PopupBase {
    Q_OBJECT
public:
    explicit MicInputPopupWidget(QWidget *parent = nullptr);

    void setCurrentInput(int input); // 0-4

signals:
    void inputChanged(int input);

protected:
    QSize contentSize() const override;
    void paintContent(QPainter &painter, const QRect &contentRect) override;

private:
    void setupUi();
    void updateButtonStyles();

    QPushButton *m_titleLabel;
    QPushButton *m_frontBtn;     // 0 = Front mic
    QPushButton *m_rearBtn;      // 1 = Rear mic
    QPushButton *m_lineInBtn;    // 2 = LINE IN
    QPushButton *m_frontLineBtn; // 3 = Front + LINE IN
    QPushButton *m_rearLineBtn;  // 4 = Rear + LINE IN
    QPushButton *m_closeBtn;

    int m_currentInput = 0;
};

#endif // MICINPUTPOPUP_H
