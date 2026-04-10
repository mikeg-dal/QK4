#include "mousevfoindicator.h"
#include "k4styles.h"
#include <QPainter>
#include <QPainterPath>

MouseVfoIndicator::MouseVfoIndicator(QWidget *parent) : QWidget(parent) {
    setFixedSize(30, 40);
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

void MouseVfoIndicator::setActiveVfo(bool isB) {
    if (m_isB != isB) {
        m_isB = isB;
        update();
    }
}

void MouseVfoIndicator::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QColor vfoColor = m_isB ? QColor(K4Styles::Colors::VfoBGreen) : QColor(K4Styles::Colors::VfoACyan);
    QColor borderColor(K4Styles::Colors::AccentAmber);
    QColor bgColor(0x1A, 0x1A, 0x1A);

    QRectF r(1, 1, width() - 2, height() - 2);

    // Background fill with rounder corners
    QPainterPath path;
    path.addRoundedRect(r, 8, 8);
    p.fillPath(path, bgColor);

    // Amber border
    p.setPen(QPen(borderColor, 2));
    p.drawRoundedRect(r, 8, 8);

    // Mouse body (rounded pill) centered at top
    int mouseW = 10;
    int mouseH = 15;
    int mouseX = (width() - mouseW) / 2;
    int mouseY = 4;
    QRectF mouseRect(mouseX, mouseY, mouseW, mouseH);
    p.setPen(QPen(vfoColor, 1.3));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(mouseRect, 4, 4);

    // Scroll wheel blob — narrow rectangle with slight rounding
    qreal blobW = 3.5;
    qreal blobH = 4.5;
    qreal blobX = mouseX + (mouseW - blobW) / 2.0;
    qreal blobY = mouseY + 2.5;
    p.setPen(Qt::NoPen);
    p.setBrush(vfoColor);
    p.drawRoundedRect(QRectF(blobX, blobY, blobW, blobH), 1, 1);

    // VFO letter below mouse
    QFont font = K4Styles::Fonts::paintFont(K4Styles::Dimensions::FontSizePopup);
    p.setFont(font);
    p.setPen(vfoColor);
    QRectF textRect(0, mouseY + mouseH, width(), height() - mouseY - mouseH - 1);
    p.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop, m_isB ? "B" : "A");
}
