#include "ui/overlays/monoverlay.h"
#include "ui/styling/k4constants.h"
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QFont>

MonOverlay::MonOverlay(QWidget *parent) : SideControlOverlay(Global, parent) {
    setupUi();
}

void MonOverlay::setupUi() {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(IndicatorBarWidth + 8, 8, 8, 8);
    layout->setSpacing(0);

    m_titleLabel = new QLabel("MON", this);
    QFont titleFont = m_titleLabel->font();
    titleFont.setPixelSize(K4Styles::Dimensions::FontSizeReadout);
    titleFont.setBold(false);
    m_titleLabel->setFont(titleFont);
    m_titleLabel->setStyleSheet(QString("color: %1;").arg(K4Styles::Colors::TextPrimary));
    layout->addWidget(m_titleLabel);

    m_subtitleLabel = new QLabel("LEVEL", this);
    QFont subtitleFont = m_subtitleLabel->font();
    subtitleFont.setPixelSize(K4Styles::Dimensions::FontSizeReadout);
    subtitleFont.setBold(false);
    m_subtitleLabel->setFont(subtitleFont);
    m_subtitleLabel->setStyleSheet(QString("color: %1;").arg(K4Styles::Colors::TextPrimary));
    layout->addWidget(m_subtitleLabel);

    layout->addStretch();

    m_valueLabel = new QLabel("0", this);
    QFont valueFont = m_valueLabel->font();
    valueFont.setPixelSize(K4Styles::Dimensions::FontSizeReadout);
    valueFont.setBold(false);
    m_valueLabel->setFont(valueFont);
    m_valueLabel->setStyleSheet(QString("color: %1;").arg(K4Styles::Colors::TextPrimary));
    layout->addWidget(m_valueLabel);
}

void MonOverlay::setValue(int value) {
    m_value = qBound(0, value, 100);
    updateValueDisplay();
}

void MonOverlay::setMode(int mode) {
    m_mode = qBound(0, mode, 2);
}

void MonOverlay::updateValueDisplay() {
    m_valueLabel->setText(QString::number(m_value));
}

void MonOverlay::wheelEvent(QWheelEvent *event) {
    int steps = m_wheelAccumulator.accumulate(event);
    if (steps != 0) {
        int newValue = qBound(0, m_value + steps, 100);
        if (newValue != m_value) {
            m_value = newValue;
            updateValueDisplay();
            emit levelChangeRequested(m_mode, m_value);
        }
    }
    event->accept();
}

void MonOverlay::mousePressEvent(QMouseEvent *event) {
    // WHY: overrides the base close-on-click; a click inside is ignored so the wheel can adjust.
    Q_UNUSED(event)
}
