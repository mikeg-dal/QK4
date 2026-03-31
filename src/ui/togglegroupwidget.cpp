#include "togglegroupwidget.h"
#include "k4styles.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace {
const int ToggleGroupHeight = 32;
const int TogglePadding = 4;
const int ToggleTriangleWidth = 10;
const int ToggleButtonSpacing = 2;
} // namespace

ToggleGroupWidget::ToggleGroupWidget(const QString &leftLabel, const QString &rightLabel, QWidget *parent)
    : QWidget(parent), m_leftLabel(leftLabel), m_rightLabel(rightLabel), m_leftSelected(true), m_rightSelected(false),
      m_rightEnabled(true) {
    // Calculate equal width for all three buttons (left, &, right)
    QFontMetrics fm(font());
    int leftTextWidth = fm.horizontalAdvance(leftLabel) + 16;
    int rightTextWidth = fm.horizontalAdvance(rightLabel) + 16;
    int ampTextWidth = fm.horizontalAdvance("&") + 16;

    // Use the maximum width for all three buttons
    int buttonWidth = qMax(qMax(leftTextWidth, rightTextWidth), ampTextWidth);
    buttonWidth = qMax(buttonWidth, 36); // Minimum button width

    int totalWidth = buttonWidth * 3 + ToggleButtonSpacing * 2 + TogglePadding * 2 + ToggleTriangleWidth;

    setFixedSize(totalWidth, ToggleGroupHeight);
    setCursor(Qt::PointingHandCursor);
}

void ToggleGroupWidget::setLeftSelected(bool selected) {
    if (m_leftSelected != selected) {
        m_leftSelected = selected;
        update();
    }
}

void ToggleGroupWidget::setRightSelected(bool selected) {
    if (m_rightSelected != selected) {
        m_rightSelected = selected;
        update();
    }
}

void ToggleGroupWidget::setRightEnabled(bool enabled) {
    if (m_rightEnabled != enabled) {
        m_rightEnabled = enabled;
        update();
    }
}

void ToggleGroupWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Calculate equal button width for all three buttons
    int mainWidth = width() - ToggleTriangleWidth;
    int availableWidth = mainWidth - TogglePadding * 2 - ToggleButtonSpacing * 2;
    int buttonWidth = availableWidth / 3;
    int buttonHeight = height() - 6;

    // Calculate button rectangles with equal widths
    int x = TogglePadding;
    m_leftRect = QRect(x, 3, buttonWidth, buttonHeight);
    x += buttonWidth + ToggleButtonSpacing;
    m_ampRect = QRect(x, 3, buttonWidth, buttonHeight);
    x += buttonWidth + ToggleButtonSpacing;
    m_rightRect = QRect(x, 3, buttonWidth, buttonHeight);

    // Draw container background with gradient
    QLinearGradient grad = K4Styles::buttonGradient(0, height());

    // Create path with rounded left corners and triangle on right
    QPainterPath containerPath;
    int cornerRadius = 6;

    // Start from top-left corner
    containerPath.moveTo(cornerRadius, 0);
    // Top edge to where triangle starts
    containerPath.lineTo(mainWidth, 0);
    // Triangle pointing right
    containerPath.lineTo(mainWidth, height() / 2 - 6);
    containerPath.lineTo(width(), height() / 2);
    containerPath.lineTo(mainWidth, height() / 2 + 6);
    // Continue to bottom
    containerPath.lineTo(mainWidth, height());
    // Bottom edge
    containerPath.lineTo(cornerRadius, height());
    // Bottom-left rounded corner
    containerPath.quadTo(0, height(), 0, height() - cornerRadius);
    // Left edge
    containerPath.lineTo(0, cornerRadius);
    // Top-left rounded corner
    containerPath.quadTo(0, 0, cornerRadius, 0);
    containerPath.closeSubpath();

    painter.setBrush(grad);
    painter.setPen(QPen(K4Styles::borderColor(), 2));
    painter.drawPath(containerPath);

    // Draw individual button backgrounds with rounded corners and borders
    QFont labelFont = font();
    labelFont.setPixelSize(K4Styles::Dimensions::FontSizeLarge);
    labelFont.setBold(true);

    auto drawButton = [&](const QRect &rect, bool selected, bool enabled, const QString &text) {
        QPainterPath buttonPath;
        buttonPath.addRoundedRect(rect, 4, 4);

        if (selected) {
            // Selected: light grey gradient (matches BandPopupWidget selected style)
            QLinearGradient selGrad = K4Styles::selectedGradient(rect.top(), rect.bottom());
            painter.setBrush(selGrad);
            painter.setPen(QPen(K4Styles::borderColorSelected(), 1));
            painter.drawPath(buttonPath);
            painter.setPen(QColor(K4Styles::Colors::TextDark));
        } else {
            // Unselected: subtle gradient with light border
            QLinearGradient btnGrad(rect.topLeft(), rect.bottomLeft());
            btnGrad.setColorAt(0, QColor(K4Styles::Colors::GradientMid1));
            btnGrad.setColorAt(1, QColor(K4Styles::Colors::GradientBottom));
            painter.setBrush(btnGrad);
            painter.setPen(QPen(QColor(K4Styles::Colors::BorderHover), 1));
            painter.drawPath(buttonPath);
            painter.setPen(enabled ? Qt::white : QColor(K4Styles::Colors::InactiveGray));
        }

        painter.setFont(labelFont);
        painter.drawText(rect, Qt::AlignCenter, text);
    };

    // Draw left button (always enabled)
    drawButton(m_leftRect, m_leftSelected, true, m_leftLabel);

    // Draw & button (selected when both left and right are selected)
    bool ampSelected = m_leftSelected && m_rightSelected;
    drawButton(m_ampRect, ampSelected, m_rightEnabled, "&");

    // Draw right button
    drawButton(m_rightRect, m_rightSelected, m_rightEnabled, m_rightLabel);
}

void ToggleGroupWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        QPoint pos = event->pos();
        if (m_leftRect.contains(pos)) {
            emit leftClicked();
        } else if (m_ampRect.contains(pos) && m_rightEnabled) {
            emit bothClicked();
        } else if (m_rightRect.contains(pos) && m_rightEnabled) {
            emit rightClicked();
        }
    }
}
