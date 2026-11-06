#include "ui/overlays/baloverlay.h"
#include "ui/styling/k4constants.h"
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QFont>

BalOverlay::BalOverlay(QWidget *parent)
    : SideControlOverlay(MainRx, parent) // Cyan indicator bar
{
    setupUi();
}

void BalOverlay::setupUi() {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(IndicatorBarWidth + 8, 8, 8, 8);
    layout->setSpacing(2);

    // Mode button — "SUB AF\n= NOR" / "SUB AF\n= BAL"
    m_modeBtn = new QPushButton("SUB AF\n= NOR", this);
    m_modeBtn->setCheckable(true);
    m_modeBtn->setCursor(Qt::PointingHandCursor);
    m_modeBtn->setFixedHeight(K4Styles::Dimensions::PopupButtonHeight);
    m_modeBtn->setStyleSheet(QString(R"(
        QPushButton {
            background: %1;
            color: %2;
            border: 1px solid %3;
            border-radius: %4px;
            font-size: %5px;
            font-weight: bold;
            padding: 2px 4px;
        }
        QPushButton:checked {
            background: %6;
            color: %7;
            border: 1px solid %8;
        }
    )")
                                 .arg(QString("qlineargradient(x1:0, y1:0, x2:0, y2:1,"
                                              "stop:0 %1, stop:0.4 %2, stop:0.6 %3, stop:1 %4)")
                                          .arg(K4Styles::Colors::GradientTop)
                                          .arg(K4Styles::Colors::GradientMid1)
                                          .arg(K4Styles::Colors::GradientMid2)
                                          .arg(K4Styles::Colors::GradientBottom))
                                 .arg(K4Styles::Colors::TextPrimary)
                                 .arg(K4Styles::Colors::BorderNormal)
                                 .arg(K4Styles::Dimensions::BorderRadius)
                                 .arg(K4Styles::Dimensions::FontSizeReadout)
                                 .arg(QString("qlineargradient(x1:0, y1:0, x2:0, y2:1,"
                                              "stop:0 %1, stop:0.4 %2, stop:0.6 %3, stop:1 %4)")
                                          .arg(K4Styles::Colors::SelectedTop)
                                          .arg(K4Styles::Colors::SelectedMid1)
                                          .arg(K4Styles::Colors::SelectedMid2)
                                          .arg(K4Styles::Colors::SelectedBottom))
                                 .arg(K4Styles::Colors::TextOnLight)
                                 .arg(K4Styles::Colors::BorderSelected));
    layout->addWidget(m_modeBtn);

    connect(m_modeBtn, &QPushButton::clicked, this, [this]() {
        // Toggle mode: NOR(0)->BAL(1), BAL(1)->NOR(0), keep current offset
        int newMode = (m_mode == 0) ? 1 : 0;
        m_mode = newMode;
        updateDisplay();
        emit balanceChangeRequested(m_mode, m_offset);
    });

    // Push labels to the bottom
    layout->addStretch();

    m_mainLabel = new QLabel("MAIN:  50", this);
    QFont valueFont = m_mainLabel->font();
    valueFont.setPixelSize(K4Styles::Dimensions::FontSizeCaption);
    m_mainLabel->setFont(valueFont);
    m_mainLabel->setStyleSheet(QString("background: transparent; color: %1;").arg(K4Styles::Colors::TextSecondary));
    layout->addWidget(m_mainLabel);

    m_subLabel = new QLabel("SUB:   50", this);
    m_subLabel->setFont(valueFont);
    m_subLabel->setStyleSheet(QString("background: transparent; color: %1;").arg(K4Styles::Colors::TextSecondary));
    layout->addWidget(m_subLabel);
}

void BalOverlay::setBalance(int mode, int offset) {
    m_mode = qBound(0, mode, 1);
    m_offset = qBound(-50, offset, 50);
    updateDisplay();
}

void BalOverlay::updateDisplay() {
    QString modeStr = (m_mode == 1) ? "BAL" : "NOR";
    m_modeBtn->setText(QString("SUB AF\n= %1").arg(modeStr));
    m_modeBtn->setChecked(m_mode == 1);

    int mainVal = 50 - m_offset;
    int subVal = 50 + m_offset;
    m_mainLabel->setText(QString("MAIN:  %1").arg(mainVal));
    m_subLabel->setText(QString("SUB:   %1").arg(subVal));
}

void BalOverlay::wheelEvent(QWheelEvent *event) {
    int steps = m_wheelAccumulator.accumulate(event);
    if (steps != 0) {
        int newOffset = qBound(-50, m_offset + steps, 50);
        if (newOffset != m_offset) {
            m_offset = newOffset;
            updateDisplay();
            emit balanceChangeRequested(m_mode, m_offset);
        }
    }
    event->accept();
}

void BalOverlay::mousePressEvent(QMouseEvent *event) {
    // WHY: overrides the base close-on-click; a click inside is ignored so the wheel can adjust.
    Q_UNUSED(event)
}
