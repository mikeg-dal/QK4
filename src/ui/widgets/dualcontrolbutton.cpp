#include "ui/widgets/dualcontrolbutton.h"
#include "ui/styling/k4constants.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QLinearGradient>
#include <QFontMetrics>

namespace D = K4Styles::Dimensions;

QFont DualControlButton::labelFont(const QWidget *context) {
    QFont font = context->font();
    font.setPixelSize(D::FontSizeBody);
    font.setBold(true);
    return font;
}

QFont DualControlButton::valueFont(const QWidget *context) {
    QFont font = context->font();
    font.setPixelSize(D::FontSizeReadout);
    font.setBold(true);
    return font;
}

QFont DualControlButton::altFont(const QWidget *context) {
    QFont font = context->font();
    font.setPixelSize(D::FontSizeCaption);
    return font;
}

int DualControlButton::measuredHeight(const QWidget *context) {
    const QFontMetrics label(labelFont(context), context);
    const QFontMetrics value(valueFont(context), context);
    const QFontMetrics alt(altFont(context), context);
    const int primaryLine = qMax(label.ascent(), value.ascent()) + qMax(label.descent(), value.descent());
    return D::KnobTextInset + primaryLine + D::KnobLineGap + alt.height() + D::KnobTextInset;
}

DualControlButton::DualControlButton(QWidget *parent) : QWidget(parent) {
    // WHY fixed: the six knob keys never resize with the window.
    setFixedSize(D::KnobButtonWidth, D::KnobButtonHeight);
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
}

void DualControlButton::setPrimaryLabel(const QString &label) {
    m_primaryLabel = label;
    update();
}

void DualControlButton::setPrimaryValue(const QString &value) {
    m_primaryValue = value;
    update();
}

void DualControlButton::setAlternateLabel(const QString &label) {
    m_alternateLabel = label;
    update();
}

void DualControlButton::setAlternateValue(const QString &value) {
    m_alternateValue = value;
    update();
}

void DualControlButton::setContext(Context context) {
    m_context = context;
    update();
}

void DualControlButton::setShowIndicator(bool show) {
    m_showIndicator = show;
    update();
}

void DualControlButton::swapFunctions() {
    // Swap labels
    QString tempLabel = m_primaryLabel;
    m_primaryLabel = m_alternateLabel;
    m_alternateLabel = tempLabel;

    // Swap values
    QString tempValue = m_primaryValue;
    m_primaryValue = m_alternateValue;
    m_alternateValue = tempValue;

    update();
}

QColor DualControlButton::contextColor() const {
    switch (m_context) {
    case Global:
        return QColor(K4Styles::Colors::AccentAmber);
    case MainRx:
        return QColor(K4Styles::Colors::VfoACyan);
    case SubRx:
        return QColor(K4Styles::Colors::VfoBGreen);
    }
    return QColor(K4Styles::Colors::VfoACyan);
}

void DualControlButton::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    int w = width();
    int h = height();
    int barWidth = 5; // Context indicator bar width
    int radius = 4;   // Subtle rounded corners
    int margin = 1;

    // Background with subtle gradient
    QLinearGradient bgGradient(0, 0, 0, h);
    if (m_isHovered) {
        bgGradient.setColorAt(0.0, QColor(K4Styles::Colors::HoverMid2));
        bgGradient.setColorAt(1.0, QColor(K4Styles::Colors::GradientMid2));
    } else {
        bgGradient.setColorAt(0.0, QColor(K4Styles::Colors::GradientMid1));
        bgGradient.setColorAt(1.0, QColor(K4Styles::Colors::GradientBottom));
    }

    // Main button area (always reserve space for indicator bar so button doesn't shift)
    int buttonLeft = barWidth + margin + 2;
    QRectF buttonRect(buttonLeft, margin, w - buttonLeft - margin, h - margin * 2);
    QPainterPath buttonPath;
    buttonPath.addRoundedRect(buttonRect, radius, radius);

    painter.fillPath(buttonPath, bgGradient);

    // Border (slightly brighter when indicator is shown)
    QColor borderColor =
        m_showIndicator ? QColor(K4Styles::Colors::BorderHover) : QColor(K4Styles::Colors::BorderNormal);
    painter.setPen(QPen(borderColor, 2));
    painter.drawPath(buttonPath);

    // Context indicator bar on the left (only if active in group)
    if (m_showIndicator) {
        QRectF barRect(margin, margin + 4, barWidth, h - margin * 2 - 8);
        QPainterPath barPath;
        barPath.addRoundedRect(barRect, 2, 2);
        painter.fillPath(barPath, contextColor());
    }

    // Text setup
    const QFont labelFont = DualControlButton::labelFont(this);
    const QFont valueFont = DualControlButton::valueFont(this);
    const QFont altFont = DualControlButton::altFont(this);

    int textLeft = buttonLeft + 6;
    int textRight = w - 6;

    // Calculate vertical positions using font metrics for cross-platform consistency
    // Button height is divided into upper (primary) and lower (alt) regions
    QFontMetrics fmLabel(labelFont);
    QFontMetrics fmValue(valueFont);
    QFontMetrics fmAlt(altFont);

    const int topMargin = D::KnobTextInset;
    const int bottomMargin = D::KnobTextInset;

    // Primary baseline: top inset + ascent
    int primaryBaseline = topMargin + qMax(fmLabel.ascent(), fmValue.ascent());

    // Alt baseline: button height - bottom margin - descent
    int altBaseline = h - bottomMargin - fmAlt.descent();

    // Primary label (left side, white) - e.g., "WPM"
    painter.setFont(labelFont);
    painter.setPen(QColor(K4Styles::Colors::TextPrimary));
    painter.drawText(textLeft, primaryBaseline, m_primaryLabel);

    // Primary value (right side, white) - e.g., "15"
    painter.setFont(valueFont);
    int valueWidth = fmValue.horizontalAdvance(m_primaryValue);
    painter.drawText(textRight - valueWidth, primaryBaseline, m_primaryValue);

    // Alternate label and value (bottom, yellow/amber) - e.g., "PTCH 600"
    painter.setFont(altFont);
    painter.setPen(QColor(K4Styles::Colors::AccentAmber));

    QString altText = m_alternateLabel;
    if (!m_alternateValue.isEmpty()) {
        altText += " " + m_alternateValue;
    }
    painter.drawText(textLeft, altBaseline, altText);
}

void DualControlButton::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        if (!m_showIndicator) {
            // First click on inactive button: just activate, don't swap
            emit becameActive();
        } else {
            // Already active: swap functions
            swapFunctions();
            emit swapped();
        }
        event->accept();
    } else {
        QWidget::mousePressEvent(event);
    }
}

void DualControlButton::wheelEvent(QWheelEvent *event) {
    int steps = m_wheelAccumulator.accumulate(event);
    if (steps != 0) {
        if (!m_showIndicator)
            emit becameActive();
        emit valueScrolled(steps);
    }
    event->accept();
}

void DualControlButton::enterEvent(QEnterEvent *event) {
    m_isHovered = true;
    update();
    QWidget::enterEvent(event);
}

void DualControlButton::leaveEvent(QEvent *event) {
    m_isHovered = false;
    update();
    QWidget::leaveEvent(event);
}
