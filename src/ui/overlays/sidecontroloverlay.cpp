#include "ui/overlays/sidecontroloverlay.h"
#include "ui/popups/popupdismissal.h"
#include "ui/styling/k4constants.h"
#include <QHideEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QShowEvent>
#include <QWheelEvent>

SideControlOverlay::SideControlOverlay(Context ctx, QWidget *parent)
    : QWidget(parent), m_context(ctx), m_dismissal(new PopupDismissal(this, [this]() { hide(); })) {
    // Start hidden
    hide();
}

SideControlOverlay::~SideControlOverlay() {
    // WHY: m_dismissal is a child, deleted only after ~QWidget, which still sends events. Stop its
    // app-wide filter now so it cannot hide an overlay already half destroyed.
    m_dismissal->stop();
}

bool SideControlOverlay::isVisibleOrJustHidden() const {
    return m_dismissal->isVisibleOrJustHidden();
}

void SideControlOverlay::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    // The side panel is the anchor: it hiding, or its window moving or resizing, closes the overlay.
    m_dismissal->watch(parentWidget());
}

void SideControlOverlay::hideEvent(QHideEvent *event) {
    QWidget::hideEvent(event);
    m_dismissal->stop();
}

void SideControlOverlay::showOverGroup(QWidget *topWidget, QWidget *bottomWidget) {
    if (!topWidget || !bottomWidget || !parentWidget()) {
        return;
    }
    // WHY: the press on this overlay's own key closed it by click-away a moment ago; the key's
    // clicked() that follows is that same press, not a request to open it again.
    if (m_dismissal->justHidden())
        return;

    // Calculate geometry to cover both widgets
    QPoint topLeft = topWidget->mapTo(parentWidget(), QPoint(0, 0));
    QPoint bottomRight = bottomWidget->mapTo(parentWidget(), QPoint(bottomWidget->width(), bottomWidget->height()));

    int x = topLeft.x();
    int y = topLeft.y();
    int w = bottomRight.x() - topLeft.x();
    int h = bottomRight.y() - topLeft.y();

    setGeometry(x, y, w, h);
    raise(); // Bring to front
    show();
}

void SideControlOverlay::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QRect r = rect();

    // Draw dark background with rounded corners
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(K4Styles::Colors::DarkBackground));
    painter.drawRoundedRect(r, CornerRadius, CornerRadius);

    // Draw indicator bar on the left
    QRect barRect(0, 0, IndicatorBarWidth, r.height());
    painter.setBrush(indicatorColor());
    painter.drawRoundedRect(barRect, CornerRadius / 2, CornerRadius / 2);

    // Draw subtle border
    painter.setPen(QPen(QColor(K4Styles::Colors::BorderNormal), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(r.adjusted(0, 0, -1, -1), CornerRadius, CornerRadius);
}

void SideControlOverlay::wheelEvent(QWheelEvent *event) {
    m_wheelAccumulator.accumulate(event);
    event->accept();
}

void SideControlOverlay::mousePressEvent(QMouseEvent *event) {
    // Close on any click within the overlay
    // Subclasses can override for different behavior
    Q_UNUSED(event)
    hide();
}

QColor SideControlOverlay::indicatorColor() const {
    switch (m_context) {
    case Global:
        return QColor(K4Styles::Colors::AccentAmber);
    case MainRx:
        return QColor(K4Styles::Colors::VfoACyan);
    case SubRx:
        return QColor(K4Styles::Colors::VfoBGreen);
    default:
        return QColor(K4Styles::Colors::AccentAmber);
    }
}
