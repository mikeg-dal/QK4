#ifndef ICONTEXTLABEL_H
#define ICONTEXTLABEL_H

#include "ui/styling/k4glyphs.h"

#include <QPixmap>
#include <QWidget>

class QMouseEvent;

class QLabel;

// Small composite widget for the top status bar: an icon followed by a value
// label (and optional unit label). Each new at-a-glance metric in the status
// bar should be one instance of this widget.
//
// Empty-state convention: clear() (or setValue("")) renders "--" so the slot
// reads as "no data" rather than blank space.
class IconTextLabel : public QWidget {
    Q_OBJECT

public:
    explicit IconTextLabel(QWidget *parent = nullptr);

    void setIcon(const QPixmap &pixmap);
    // Bind a procedural glyph (see K4Glyphs). After binding, the glyph's
    // color is driven independently from the value text by setGlyphColor().
    // Wins over setIcon().
    void setGlyph(K4Glyphs::Glyph glyph);
    // Re-render the bound glyph in the given color. No-op if no glyph is set.
    void setGlyphColor(const QColor &color);
    // Optional prefix label ("LPA", "PA", "FAN", ...). Rendered between the
    // icon and the value, in a muted color. Persists across clear() so the
    // disconnected state reads as e.g. "LPA --" instead of just "--".
    void setLabel(const QString &label);
    void setValue(const QString &text);
    void setUnit(const QString &unit);
    void setValueColor(const QColor &color);
    // Render the empty-state placeholder ("--"). Preserves any prefix label.
    void clear();

    // Opt in to click handling: gives the widget a pointing-hand cursor and makes
    // it emit clicked(). Off by default so a purely informational field (voltage,
    // and anything added later) is not advertised as interactive.
    void setClickable(bool clickable);

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *event) override;

private:
    void applyValueStyle(const QColor &color);
    void renderGlyph(const QColor &color);

    QLabel *m_iconLabel;
    QLabel *m_prefixLabel;
    QLabel *m_valueLabel;
    QLabel *m_unitLabel;
    QColor m_valueColor;
    QColor m_glyphColor;
    K4Glyphs::Glyph m_glyph;
    bool m_clickable = false;
};

#endif // ICONTEXTLABEL_H
