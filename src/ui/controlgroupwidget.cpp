#include "controlgroupwidget.h"
#include "k4styles.h"
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

namespace {
const int ControlGroupHeight = 32;
} // namespace

ControlGroupWidget::ControlGroupWidget(const QString &label, QWidget *parent)
    : QWidget(parent), m_label(label), m_value("100.0") {
    // Fixed width for control group: label + value + - + + with generous spacing
    setFixedSize(180, ControlGroupHeight);
    setCursor(Qt::PointingHandCursor);
}

void ControlGroupWidget::setValue(const QString &value) {
    if (m_value != value) {
        m_value = value;
        update();
    }
}

void ControlGroupWidget::setShowAutoButton(bool show) {
    if (m_showAutoButton != show) {
        m_showAutoButton = show;
        // Resize widget to accommodate AUTO button
        if (show) {
            setFixedSize(220, ControlGroupHeight);
        } else {
            setFixedSize(180, ControlGroupHeight);
        }
        update();
    }
}

void ControlGroupWidget::setAutoEnabled(bool enabled) {
    if (m_autoEnabled != enabled) {
        m_autoEnabled = enabled;
        update();
    }
}

void ControlGroupWidget::setValueFaded(bool faded) {
    if (m_valueFaded != faded) {
        m_valueFaded = faded;
        update();
    }
}

void ControlGroupWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Layout depends on whether AUTO button is shown
    const int labelWidth = 60; // Fits "AVERAGE" label
    const int autoWidth = m_showAutoButton ? 40 : 0;
    const int valueWidth = 52;
    const int buttonWidth = 32;

    // Draw container background with gradient
    QLinearGradient grad = K4Styles::buttonGradient(0, height());

    // Rounded rectangle with softer corners
    int cornerRadius = 6;
    painter.setBrush(grad);
    painter.setPen(QPen(K4Styles::borderColor(), 2));
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), cornerRadius, cornerRadius);

    // Calculate positions
    int x = 4;
    QRect labelRect(x, 2, labelWidth, height() - 4);
    x += labelWidth;

    // Vertical line after label
    painter.drawLine(x, 4, x, height() - 4);

    // AUTO button (if shown)
    if (m_showAutoButton) {
        m_autoRect = QRect(x, 2, autoWidth, height() - 4);
        x += autoWidth;

        // Vertical line after AUTO
        painter.drawLine(x, 4, x, height() - 4);
    }

    QRect valueRect(x, 2, valueWidth, height() - 4);
    x += valueWidth;

    // Vertical line after value
    painter.drawLine(x, 4, x, height() - 4);

    m_minusRect = QRect(x, 2, buttonWidth, height() - 4);
    x += buttonWidth;

    // Vertical line after minus
    painter.drawLine(x, 4, x, height() - 4);

    m_plusRect = QRect(x, 2, buttonWidth, height() - 4);

    // Draw label
    QFont labelFont = font();
    labelFont.setPixelSize(K4Styles::Dimensions::FontSizeLarge);
    labelFont.setBold(true);
    painter.setFont(labelFont);
    painter.setPen(Qt::white);
    painter.drawText(labelRect, Qt::AlignCenter, m_label);

    // Draw AUTO button if shown
    if (m_showAutoButton) {
        if (m_autoEnabled) {
            painter.fillRect(m_autoRect, Qt::white);
            painter.setPen(QColor(K4Styles::Colors::InactiveGray));
        } else {
            painter.setPen(Qt::white);
        }
        QFont autoFont = font();
        autoFont.setPixelSize(K4Styles::Dimensions::FontSizeMedium);
        autoFont.setBold(true);
        painter.setFont(autoFont);
        painter.drawText(m_autoRect, Qt::AlignCenter, "AUTO");
        painter.setFont(labelFont);
        painter.setPen(Qt::white);
    }

    // Draw value (faded grey when auto mode)
    if (m_valueFaded) {
        painter.setPen(QColor(K4Styles::Colors::TextFaded));
    } else {
        painter.setPen(Qt::white);
    }
    painter.drawText(valueRect, Qt::AlignCenter, m_value);
    painter.setPen(Qt::white); // Reset for buttons

    // Draw minus/plus buttons (faded when auto mode disables them)
    QFont buttonFont = font();
    buttonFont.setPixelSize(K4Styles::Dimensions::FontSizeTitle);
    buttonFont.setBold(true);
    painter.setFont(buttonFont);
    painter.setPen(m_autoEnabled ? QColor(K4Styles::Colors::TextFaded) : Qt::white);
    painter.drawText(m_minusRect, Qt::AlignCenter, "-");
    painter.drawText(m_plusRect, Qt::AlignCenter, "+");
}

void ControlGroupWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        QPoint pos = event->pos();
        if (m_showAutoButton && m_autoRect.contains(pos)) {
            emit autoClicked();
        } else if (m_minusRect.contains(pos)) {
            if (!m_autoEnabled)
                emit decrementClicked();
        } else if (m_plusRect.contains(pos)) {
            if (!m_autoEnabled)
                emit incrementClicked();
        }
    }
}

void ControlGroupWidget::wheelEvent(QWheelEvent *event) {
    if (m_autoEnabled)
        return;
    int steps = m_wheelAccumulator.accumulate(event);
    for (int i = 0; i < qAbs(steps); ++i) {
        if (steps > 0)
            emit incrementClicked();
        else
            emit decrementClicked();
    }
    event->accept();
}
