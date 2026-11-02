#include "ui/widgets/controlgroupwidget.h"
#include "ui/styling/k4styles.h"
#include <QFontMetrics>
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
        updateWidth(); // room for the AUTO key
        update();
    }
}

void ControlGroupWidget::setCompact(bool compact) {
    if (compact) {
        QFont labelFont = font();
        labelFont.setPixelSize(K4Styles::Dimensions::FontSizeBody);
        labelFont.setBold(true);
        m_labelWidth = QFontMetrics(labelFont).horizontalAdvance(m_label) + 6;
        m_valueWidth = 36; // "100%" is 31 px in Inter
        m_buttonWidth = K4Styles::Dimensions::MinClickTarget;
    } else {
        m_labelWidth = 60;
        m_valueWidth = 52;
        m_buttonWidth = 32;
    }
    updateWidth();
    update();
}

void ControlGroupWidget::updateWidth() {
    constexpr int kInset = 4;
    constexpr int kAutoWidth = 40;
    setFixedSize(kInset + m_labelWidth + (m_showAutoButton ? kAutoWidth : 0) + m_valueWidth + 2 * m_buttonWidth,
                 ControlGroupHeight);
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
    const int labelWidth = m_labelWidth;
    const int autoWidth = m_showAutoButton ? 40 : 0;
    const int valueWidth = m_valueWidth;
    const int buttonWidth = m_buttonWidth;

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

    const QColor textPrimary(K4Styles::Colors::TextPrimary);

    // Draw label
    QFont labelFont = font();
    labelFont.setPixelSize(K4Styles::Dimensions::FontSizeBody);
    labelFont.setBold(true);
    painter.setFont(labelFont);
    painter.setPen(textPrimary);
    painter.drawText(labelRect, Qt::AlignCenter, m_label);

    // Draw AUTO button if shown
    if (m_showAutoButton) {
        if (m_autoEnabled) {
            painter.fillRect(m_autoRect, QColor(K4Styles::Colors::White));
            painter.setPen(QColor(K4Styles::Colors::TextOnLight));
        } else {
            painter.setPen(textPrimary);
        }
        QFont autoFont = font();
        autoFont.setPixelSize(K4Styles::Dimensions::FontSizeCaption);
        autoFont.setBold(true);
        painter.setFont(autoFont);
        painter.drawText(m_autoRect, Qt::AlignCenter, "AUTO");
        painter.setFont(labelFont);
        painter.setPen(textPrimary);
    }

    // Draw value (grey when auto mode sets it)
    if (m_valueFaded) {
        painter.setPen(QColor(K4Styles::Colors::TextSecondaryOnRaised));
    } else {
        painter.setPen(textPrimary);
    }
    painter.drawText(valueRect, Qt::AlignCenter, m_value);
    painter.setPen(textPrimary); // Reset for buttons

    // Draw minus/plus buttons (faded when auto mode disables them)
    QFont buttonFont = font();
    buttonFont.setPixelSize(K4Styles::Dimensions::FontSizeIndicator);
    buttonFont.setBold(true);
    painter.setFont(buttonFont);
    painter.setPen(m_autoEnabled ? QColor(K4Styles::Colors::TextDisabled) : textPrimary);
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
