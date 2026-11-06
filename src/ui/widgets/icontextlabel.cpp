#include "ui/widgets/icontextlabel.h"

#include "ui/styling/k4styles.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QEvent>
#include <QMouseEvent>

namespace {
constexpr int kIconSize = K4Styles::Dimensions::SmallIconSize;
constexpr const char *kEmptyPlaceholder = "--";
} // namespace

IconTextLabel::IconTextLabel(QWidget *parent)
    : QWidget(parent), m_iconLabel(new QLabel(this)), m_prefixLabel(new QLabel(this)), m_valueLabel(new QLabel(this)),
      m_unitLabel(new QLabel(this)), m_valueColor(K4Styles::Colors::AccentAmber),
      m_glyphColor(K4Styles::Colors::AccentAmber) {
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    m_iconLabel->setFixedSize(kIconSize, kIconSize);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_iconLabel);

    m_prefixLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    m_prefixLabel->setFont(K4Styles::Fonts::paintFont(K4Styles::Dimensions::FontSizeCaption, QFont::Bold));
    m_prefixLabel->setStyleSheet(QString("color: %1;").arg(K4Styles::Colors::TextSecondary));
    m_prefixLabel->hide();
    layout->addWidget(m_prefixLabel);

    m_valueLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    m_valueLabel->setFont(K4Styles::Fonts::dataFont(K4Styles::Dimensions::FontSizeHeading));
    layout->addWidget(m_valueLabel);

    m_unitLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    m_unitLabel->setFont(K4Styles::Fonts::paintFont(K4Styles::Dimensions::FontSizeCaption, QFont::Normal));
    m_unitLabel->setStyleSheet(QString("color: %1;").arg(K4Styles::Colors::TextSecondary));
    m_unitLabel->hide();
    layout->addWidget(m_unitLabel);

    applyValueStyle(m_valueColor);
    clear();
}

void IconTextLabel::setGlyph(K4Glyphs::Glyph glyph) {
    m_glyph = std::move(glyph);
    // Render once immediately. While the field is still in the empty state,
    // honour the muted "no data" color; otherwise use the most recently set
    // glyph color so setGlyph + setGlyphColor are order-independent.
    const QColor initial =
        m_valueLabel->text() == kEmptyPlaceholder ? QColor(K4Styles::Colors::TextSecondary) : m_glyphColor;
    renderGlyph(initial);
}

void IconTextLabel::setGlyphColor(const QColor &color) {
    m_glyphColor = color;
    renderGlyph(color);
}

void IconTextLabel::renderGlyph(const QColor &color) {
    if (!m_glyph)
        return;
    m_renderedGlyphColor = color;
    m_iconLabel->setPixmap(m_glyph(color).pixmap(QSize(kIconSize, kIconSize), m_iconLabel->devicePixelRatioF()));
}

bool IconTextLabel::event(QEvent *event) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    // WHY: the pixmap carries the ratio it was drawn at; moving to a screen with another ratio redraws it.
    if (event->type() == QEvent::DevicePixelRatioChange)
        renderGlyph(m_renderedGlyphColor);
#endif
    return QWidget::event(event);
}

void IconTextLabel::setLabel(const QString &label) {
    if (label.isEmpty()) {
        m_prefixLabel->clear();
        m_prefixLabel->hide();
        return;
    }
    m_prefixLabel->setText(label);
    m_prefixLabel->show();
}

void IconTextLabel::setValue(const QString &text) {
    if (text.isEmpty()) {
        clear();
        return;
    }
    m_valueLabel->setText(text);
    applyValueStyle(m_valueColor);
}

void IconTextLabel::setUnit(const QString &unit) {
    if (unit.isEmpty()) {
        m_unitLabel->clear();
        m_unitLabel->hide();
        return;
    }
    m_unitLabel->setText(unit);
    m_unitLabel->show();
}

void IconTextLabel::setValueColor(const QColor &color) {
    m_valueColor = color;
    applyValueStyle(m_valueColor);
}

void IconTextLabel::clear() {
    m_valueLabel->setText(kEmptyPlaceholder);
    // Disconnected/empty state: mute both the value and the glyph uniformly,
    // so the slot reads as "no data" regardless of any semantic glyph color
    // the controller had set. The semantic color returns the next time the
    // controller calls setGlyphColor() on a real reading.
    const QColor muted(K4Styles::Colors::TextSecondary);
    applyValueStyle(muted);
    renderGlyph(muted);
}

void IconTextLabel::applyValueStyle(const QColor &color) {
    m_valueLabel->setStyleSheet(QString("color: %1;").arg(color.name()));
}

void IconTextLabel::setClickable(bool clickable) {
    m_clickable = clickable;
    setCursor(clickable ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void IconTextLabel::mousePressEvent(QMouseEvent *event) {
    // WHY the child labels are not wired individually: the QLabels fill this widget, but they do
    // not accept mouse events, so the press propagates to the parent and one handler covers the
    // whole field - icon, prefix, value and unit alike.
    if (m_clickable && event->button() == Qt::LeftButton) {
        emit clicked();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}
