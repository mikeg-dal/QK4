#include "ui/styling/k4glyphs.h"

#include <QApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QStyle>
#include <QStyleOption>
#include <QtMath>

namespace K4Glyphs {

namespace {

using Draw = std::function<void(QPainter &p, const QColor &color)>;

// Draws the glyph in its design box (designPx square) scaled to whatever the icon is asked for, so the
// result is rendered at the target's device pixel ratio rather than stretched from a 1x bitmap.
class GlyphIconEngine final : public QIconEngine {
public:
    GlyphIconEngine(int designPx, Draw draw, QColor color)
        : m_designPx(designPx), m_draw(std::move(draw)), m_color(std::move(color)) {}

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode, QIcon::State) override {
        const qreal side = qMin(rect.width(), rect.height());
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->translate(rect.x() + (rect.width() - side) / 2.0, rect.y() + (rect.height() - side) / 2.0);
        painter->scale(side / m_designPx, side / m_designPx);
        m_draw(*painter, m_color);
        painter->restore();
    }

    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override {
        QPixmap pm(QSize(qCeil(size.width() * scale), qCeil(size.height() * scale)));
        pm.setDevicePixelRatio(scale);
        pm.fill(Qt::transparent);
        {
            QPainter p(&pm);
            paint(&p, QRect(QPoint(), size), mode, state);
        }
        // WHY: keep the style's greyed look for a disabled button, which a pixmap-based QIcon got for free.
        if (mode == QIcon::Disabled && qobject_cast<QApplication *>(QCoreApplication::instance())) {
            QStyleOption option;
            return QApplication::style()->generatedIconPixmap(QIcon::Disabled, pm, &option);
        }
        return pm;
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QIconEngine *clone() const override { return new GlyphIconEngine(*this); }

private:
    int m_designPx;
    Draw m_draw;
    QColor m_color;
};

Glyph makeGlyph(int designPx, Draw draw) {
    return [designPx, draw = std::move(draw)](const QColor &color) {
        return QIcon(new GlyphIconEngine(designPx, draw, color));
    };
}

} // namespace

Glyph power(int sizePx) {
    return makeGlyph(sizePx, [sizePx](QPainter &p, const QColor &color) {
        QPen pen(color, 2.2);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);

        // Open arc (lower 3/4) — universal power-symbol arc.
        const QRectF arcRect(2.5, 2.5, sizePx - 5, sizePx - 5);
        p.drawArc(arcRect, 110 * 16, 320 * 16);

        // Vertical stroke through the top gap.
        const qreal cx = sizePx / 2.0;
        p.drawLine(QPointF(cx, 2), QPointF(cx, sizePx / 2.0 + 1));
    });
}

Glyph thermometer(int sizePx) {
    return makeGlyph(sizePx, [sizePx](QPainter &p, const QColor &color) {
        // Geometry: vertical stem centered horizontally, filled bulb at the bottom.
        const qreal cx = sizePx / 2.0;
        const qreal bulbRadius = sizePx * 0.22;
        const qreal stemTop = sizePx * 0.12;
        const qreal stemWidth = sizePx * 0.18;
        const qreal bulbCenterY = sizePx - bulbRadius - 1.0;

        // Bulb (filled circle).
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QPointF(cx, bulbCenterY), bulbRadius, bulbRadius);

        // Stem (rounded rect from top down into the bulb).
        const QRectF stemRect(cx - stemWidth / 2.0, stemTop, stemWidth, bulbCenterY - stemTop + 1.0);
        p.drawRoundedRect(stemRect, stemWidth / 2.0, stemWidth / 2.0);

        // Two short tick marks on the right side; they make it read as a thermometer at icon size.
        QPen tickPen(color, 1.0);
        tickPen.setCapStyle(Qt::RoundCap);
        p.setPen(tickPen);
        const qreal tickX = cx + stemWidth / 2.0 + 1.5;
        const qreal tickLen = sizePx * 0.12;
        p.drawLine(QPointF(tickX, sizePx * 0.32), QPointF(tickX + tickLen, sizePx * 0.32));
        p.drawLine(QPointF(tickX, sizePx * 0.52), QPointF(tickX + tickLen, sizePx * 0.52));
    });
}

Glyph lightning(int sizePx) {
    return makeGlyph(sizePx, [sizePx](QPainter &p, const QColor &color) {
        // Stylized lightning bolt — six-point polygon traced from upper-left to
        // lower-right with two notches that give the classic Z shape.
        const qreal s = sizePx;
        QPainterPath bolt;
        bolt.moveTo(s * 0.55, s * 0.05);
        bolt.lineTo(s * 0.15, s * 0.55);
        bolt.lineTo(s * 0.42, s * 0.55);
        bolt.lineTo(s * 0.30, s * 0.95);
        bolt.lineTo(s * 0.85, s * 0.40);
        bolt.lineTo(s * 0.55, s * 0.40);
        bolt.closeSubpath();

        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawPath(bolt);
    });
}

} // namespace K4Glyphs
