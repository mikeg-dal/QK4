#include "ui/styling/k4styles.h"

#include <QWidget>

namespace {
// Internal helper to generate CSS linear-gradient strings
QString gradientCss(const char *top, const char *mid1, const char *mid2, const char *bottom) {
    return QString("qlineargradient(x1:0, y1:0, x2:0, y2:1,"
                   "stop:0 %1, stop:0.4 %2, stop:0.6 %3, stop:1 %4)")
        .arg(top, mid1, mid2, bottom);
}

// Reversed gradient for pressed states
QString gradientCssReversed(const char *top, const char *mid1, const char *mid2, const char *bottom) {
    return QString("qlineargradient(x1:0, y1:0, x2:0, y2:1,"
                   "stop:0 %1, stop:0.4 %2, stop:0.6 %3, stop:1 %4)")
        .arg(bottom, mid2, mid1, top);
}

QString normalGradientCss() {
    using namespace K4Styles::Colors;
    return gradientCss(GradientTop, GradientMid1, GradientMid2, GradientBottom);
}

QString hoverGradientCss() {
    using namespace K4Styles::Colors;
    return gradientCss(HoverTop, HoverMid1, HoverMid2, HoverBottom);
}

QString pressedGradientCss() {
    using namespace K4Styles::Colors;
    return gradientCssReversed(GradientTop, GradientMid1, GradientMid2, GradientBottom);
}

QString selectedGradientCss() {
    using namespace K4Styles::Colors;
    return gradientCss(SelectedTop, SelectedMid1, SelectedMid2, SelectedBottom);
}

// The standard dark gradient button shared by the popup and menu-bar helpers. An empty padding omits
// the declaration; pressedBorder false keeps the normal border while pressed.
QString gradientButtonCss(int fontSizePx, const QString &padding, bool pressedBorder) {
    using namespace K4Styles;
    const QString paddingDecl = padding.isEmpty() ? QString() : QString("padding: %1;").arg(padding);
    const QString pressedBorderDecl =
        pressedBorder ? QString("border: 2px solid %1;").arg(Colors::BorderPressed) : QString();
    return QString(R"(
        QPushButton {
            background: %1;
            color: %2;
            border: 2px solid %3;
            border-radius: 6px;
            %4
            font-size: %5px;
            font-weight: 600;
        }
        QPushButton:hover {
            background: %6;
            border: 2px solid %7;
        }
        QPushButton:pressed {
            background: %8;
            %9
        }
    )")
        .arg(normalGradientCss(), Colors::TextPrimary, Colors::BorderNormal, paddingDecl, QString::number(fontSizePx),
             hoverGradientCss(), Colors::BorderHover, pressedGradientCss(), pressedBorderDecl);
}

// Which buttons a selected-look rule applies to: every button (the standalone selected sheets), or only
// a checked one or one marked by K4Styles::setSelected() (appended to an unselected sheet).
enum class SelectorScope : quint8 { Always, Selectable };

// The selector for `state` (empty, ":hover" or ":pressed") within `scope`.
QString buttonSelector(SelectorScope scope, const char *state = "") {
    if (scope == SelectorScope::Always)
        return QStringLiteral("QPushButton") + QLatin1StringView(state);
    return QStringLiteral(R"(QPushButton:checked%1, QPushButton[%2="true"]%1)")
        .arg(QLatin1StringView(state), QLatin1StringView(K4Styles::SelectedProperty));
}

// WHY: a selectable sheet is the unselected sheet followed by these rules narrowed to the selected
// state. They come last and are at least as specific as the unselected :hover/:pressed rules, so they
// win every declaration they make (The Style Sheet Syntax: Conflict Resolution) and the selected look
// is exactly the one the standalone sheet gives. Each therefore declares background and border, the
// two properties the unselected :hover/:pressed rules set.
QString popupSelectedCss(SelectorScope scope) {
    using namespace K4Styles;
    return QString(R"(
        %7 {
            background: %1;
            color: %2;
            border: 2px solid %3;
            border-radius: 6px;
            font-size: %4px;
            font-weight: 600;
        }
        %8 {
            background: %5;
        }
        %9 {
            background: %6;
        }
    )")
        .arg(selectedGradientCss(), Colors::TextOnLight, Colors::BorderSelected,
             QString::number(Dimensions::FontSizeBody),
             gradientCss(Colors::SelectedHoverTop, Colors::SelectedHoverMid1, Colors::SelectedHoverMid2,
                         Colors::SelectedHoverBottom),
             gradientCssReversed(Colors::SelectedTop, Colors::SelectedMid1, Colors::SelectedMid2,
                                 Colors::SelectedBottom),
             buttonSelector(scope), buttonSelector(scope, ":hover"), buttonSelector(scope, ":pressed"));
}

QString menuBarActiveCss(SelectorScope scope) {
    using namespace K4Styles;
    return QString(R"(
        %5 {
            background: %1;
            color: %2;
            border: 2px solid %3;
            border-radius: 6px;
            padding: 6px 12px;
            font-size: %4px;
            font-weight: 600;
        }
    )")
        .arg(Colors::White, Colors::TextOnLight, Colors::BorderSelected, QString::number(Dimensions::FontSizeBody),
             buttonSelector(scope));
}

// Engaged state: amber-accent border + brighter background so the icon reads as "open".
QString sidePanelActiveCss(SelectorScope scope) {
    using namespace K4Styles;
    return QString(R"(
        %8 {
            background: %1;
            color: %2;
            border: %3px solid %4;
            border-radius: %5px;
            font-size: %6px;
            font-weight: bold;
            padding: 0px;
        }
        %9 {
            background: %1;
            border: %3px solid %4;
        }
        %10 {
            background: %7;
            border: %3px solid %4;
        }
    )")
        .arg(gradientCss(Colors::HoverTop, Colors::HoverMid1, Colors::HoverMid2, Colors::HoverBottom),
             Colors::TextPrimary, QString::number(Dimensions::BorderWidth), Colors::AccentAmber,
             QString::number(Dimensions::BorderRadius), QString::number(Dimensions::FontSizeCaption),
             gradientCssReversed(Colors::HoverTop, Colors::HoverMid1, Colors::HoverMid2, Colors::HoverBottom),
             buttonSelector(scope), buttonSelector(scope, ":hover"), buttonSelector(scope, ":pressed"));
}

// Transmitting: solid red PTT key. The standalone sheet has no :hover/:pressed rules, so the narrowed
// :hover and :pressed rules repeat the resting background and border over menuBarButton()'s.
QString pttPressedCss(SelectorScope scope) {
    using namespace K4Styles;
    return QString(R"(
        %4 {
            background: %1;
            color: %2;
            border: 2px solid %1;
            border-radius: 6px;
            padding: 6px 12px;
            font-size: %3px;
            font-weight: 600;
        }
        %5 {
            background: %1;
            border: 2px solid %1;
        }
        %6 {
            background: %1;
            border: 2px solid %1;
        }
    )")
        .arg(Colors::TxActiveFill, Colors::TextPrimary, QString::number(Dimensions::FontSizeBody),
             buttonSelector(scope), buttonSelector(scope, ":hover"), buttonSelector(scope, ":pressed"));
}
} // anonymous namespace

namespace K4Styles {

QString popupButtonNormal() {
    return gradientButtonCss(Dimensions::FontSizeBody, QString(), false);
}

QString popupButtonSelected() {
    return popupSelectedCss(SelectorScope::Always);
}

QString popupButton() {
    return popupButtonNormal() + popupSelectedCss(SelectorScope::Selectable);
}

namespace {
// WHY: a property selector is not re-matched when the property changes. Qt's documented remedy
// (The Style Sheet Syntax, Selector Types) is to unset the style sheet and set it again.
void reapplySheet(QWidget *widget) {
    const QString sheet = widget->styleSheet();
    widget->setStyleSheet(QString());
    widget->setStyleSheet(sheet);
}
} // namespace

void setStyleFlag(QWidget *widget, const char *property, bool on) {
    if (widget->property(property).toBool() == on)
        return;
    widget->setProperty(property, on);
    reapplySheet(widget);
}

void setStyleState(QWidget *widget, const char *property, const QString &value) {
    if (widget->property(property).toString() == value)
        return;
    widget->setProperty(property, value);
    reapplySheet(widget);
}

void setSelected(QWidget *widget, bool selected) {
    setStyleFlag(widget, SelectedProperty, selected);
}

QString whenFlag(const char *type, const char *property, const char *pseudo) {
    return QStringLiteral(R"(%1[%2="true"]%3)")
        .arg(QLatin1StringView(type), QLatin1StringView(property), QLatin1StringView(pseudo));
}

QString whenState(const char *type, const char *property, const char *value, const char *pseudo) {
    return QStringLiteral(R"(%1[%2="%3"]%4)")
        .arg(QLatin1StringView(type), QLatin1StringView(property), QLatin1StringView(value), QLatin1StringView(pseudo));
}

QString popupValueLabel(const QString &color) {
    return QString("QPushButton {"
                   "  color: %1;"
                   "  font-size: %2px;"
                   "  font-weight: 600;"
                   "  background: transparent;"
                   "  border: %3px solid transparent;"
                   "  border-radius: %4px;"
                   "}")
        .arg(color)
        .arg(Dimensions::FontSizeReadout)
        .arg(Dimensions::BorderWidth)
        .arg(Dimensions::BorderRadius);
}

QString menuBarButton() {
    return gradientButtonCss(Dimensions::FontSizeBody, QStringLiteral("6px 12px"), true);
}

QString menuBarButtonSelectable() {
    return menuBarButton() + menuBarActiveCss(SelectorScope::Selectable);
}

QString menuBarButtonActive() {
    return menuBarActiveCss(SelectorScope::Always);
}

QString menuBarButtonSmall() {
    return gradientButtonCss(Dimensions::FontSizeReadout, QString(), true);
}

QString menuBarButtonPttPressed() {
    return pttPressedCss(SelectorScope::Always);
}

QColor withAlpha(const char *color, int alpha) {
    QColor c(color);
    c.setAlpha(alpha);
    return c;
}

QString cssRgba(const char *color, int alpha) {
    const QColor c(color);
    return QString("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha);
}

void drawDropShadow(QPainter &painter, const QRect &contentRect, int cornerRadius) {
    painter.setPen(Qt::NoPen);
    for (int i = Dimensions::ShadowLayers; i > 0; --i) {
        int blur = i * 2;
        int alpha = 12 + (Dimensions::ShadowLayers - i) * 3;
        QRect shadowRect = contentRect.adjusted(-blur, -blur, blur, blur);
        shadowRect.translate(Dimensions::ShadowOffsetX, Dimensions::ShadowOffsetY);
        painter.setBrush(withAlpha(Colors::Black, alpha));
        painter.drawRoundedRect(shadowRect, cornerRadius + blur / 2, cornerRadius + blur / 2);
    }
}

void drawHaloText(QPainter &painter, const QPointF &baseline, const QString &text, const QColor &color) {
    painter.save();
    painter.setPen(withAlpha(Colors::Black, Colors::Alpha::Halo));
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            if (dx != 0 || dy != 0)
                painter.drawText(baseline + QPointF(dx, dy), text);
        }
    }
    painter.setPen(color);
    painter.drawText(baseline, text);
    painter.restore();
}

QLinearGradient buttonGradient(int top, int bottom, bool hovered) {
    QLinearGradient grad(0, top, 0, bottom);
    if (hovered) {
        grad.setColorAt(0, QColor(Colors::HoverTop));
        grad.setColorAt(0.4, QColor(Colors::HoverMid1));
        grad.setColorAt(0.6, QColor(Colors::HoverMid2));
        grad.setColorAt(1, QColor(Colors::HoverBottom));
    } else {
        grad.setColorAt(0, QColor(Colors::GradientTop));
        grad.setColorAt(0.4, QColor(Colors::GradientMid1));
        grad.setColorAt(0.6, QColor(Colors::GradientMid2));
        grad.setColorAt(1, QColor(Colors::GradientBottom));
    }
    return grad;
}

QLinearGradient selectedGradient(int top, int bottom) {
    QLinearGradient grad(0, top, 0, bottom);
    grad.setColorAt(0, QColor(Colors::SelectedTop));
    grad.setColorAt(0.4, QColor(Colors::SelectedMid1));
    grad.setColorAt(0.6, QColor(Colors::SelectedMid2));
    grad.setColorAt(1, QColor(Colors::SelectedBottom));
    return grad;
}

QColor borderColor() {
    return QColor(Colors::BorderNormal);
}

QColor borderColorSelected() {
    return QColor(Colors::BorderSelected);
}

QLinearGradient meterGradient(qreal x1, qreal y1, qreal x2, qreal y2) {
    QLinearGradient gradient(x1, y1, x2, y2);
    gradient.setColorAt(0.00, QColor(Colors::MeterGreenDark));
    gradient.setColorAt(0.15, QColor(Colors::MeterGreen));
    gradient.setColorAt(0.30, QColor(Colors::MeterYellowGreen));
    gradient.setColorAt(0.45, QColor(Colors::MeterYellow));
    gradient.setColorAt(0.60, QColor(Colors::MeterOrange));
    gradient.setColorAt(0.80, QColor(Colors::MeterOrangeRed));
    gradient.setColorAt(1.00, QColor(Colors::MeterRed));
    return gradient;
}

QString sliderHorizontal(const QString &grooveColor, const QString &handleColor) {
    return QString("QSlider::groove:horizontal { background: %1; height: %2px; border-radius: %3px; }"
                   "QSlider::handle:horizontal { background: %4; width: %5px; height: %8px; margin: %6px 0; "
                   "border-radius: %7px; }"
                   "QSlider::sub-page:horizontal { background: %4; border-radius: %3px; }")
        .arg(grooveColor)
        .arg(Dimensions::SliderGrooveHeight)
        .arg(Dimensions::SliderBorderRadius)
        .arg(handleColor)
        .arg(Dimensions::SliderHandleWidth)
        .arg(Dimensions::SliderHandleMargin)
        .arg(Dimensions::SliderHandleRadius)
        .arg(Dimensions::SliderHandleHeight);
}

QString checkboxButton(int size) {
    // Checkbox shows ✓ when checked, empty when unchecked
    // Uses dark gradient unchecked, same dark gradient + white checkmark when checked
    return QString(R"(
        QPushButton {
            background: %3;
            color: transparent;
            border: 1px solid %4;
            border-radius: 3px;
            min-width: %1px;
            max-width: %1px;
            min-height: %1px;
            max-height: %1px;
            font-size: %2px;
            font-weight: bold;
        }
        QPushButton:hover {
            background: %5;
            border: 1px solid %6;
        }
        QPushButton:checked {
            background: %3;
            color: %7;
            border: 1px solid %6;
        }
        QPushButton:checked:hover {
            background: %5;
        }
        QPushButton:disabled {
            background: %8;
            border: 1px solid %9;
            color: transparent;
        }
        QPushButton:disabled:checked {
            color: %10;
        }
    )")
        .arg(QString::number(size), QString::number(size - 4), // Font size slightly smaller than box
             normalGradientCss(), Colors::BorderNormal, hoverGradientCss(), Colors::BorderHover, Colors::TextPrimary,
             gradientCss(Colors::DisabledGradientTop, Colors::DisabledGradientMid1, Colors::DisabledGradientMid2,
                         Colors::DisabledGradientBottom),
             Colors::BorderDisabled)
        .arg(Colors::TextDisabled);
}

QString radioButton() {
    return QString(R"(
        QPushButton {
            background: %1;
            color: %2;
            border: 2px solid %3;
            border-radius: 6px;
            padding: 6px 12px;
            font-size: %4px;
            font-weight: bold;
            text-align: left;
        }
        QPushButton:hover {
            background: %5;
            border: 2px solid %6;
        }
        QPushButton:checked {
            background: %7;
            color: %8;
            border: 2px solid %9;
        }
    )")
        .arg(normalGradientCss(), Colors::TextPrimary, Colors::BorderNormal, QString::number(Dimensions::FontSizeBody),
             hoverGradientCss(), Colors::BorderHover, selectedGradientCss(), Colors::TextOnLight,
             Colors::BorderSelected);
}

QString compactButton() {
    return QString(R"(
        QPushButton {
            background: %1;
            border: %10px solid %2;
            border-radius: %11px;
            color: %3;
            font-size: %4px;
            font-weight: bold;
            padding: 0px;
        }
        QPushButton:hover {
            background: %5;
        }
        QPushButton:pressed {
            background: %6;
        }
        QPushButton:checked {
            background: %7;
            color: %8;
            border: %10px solid %9;
        }
    )")
        .arg(gradientCss(Colors::GradientTop, Colors::GradientMid1, Colors::GradientMid2, Colors::GradientBottom))
        .arg(Colors::BorderNormal)
        .arg(Colors::TextPrimary)
        .arg(Dimensions::FontSizeCaption)
        .arg(gradientCss(Colors::HoverTop, Colors::HoverMid1, Colors::HoverMid2, Colors::HoverBottom))
        .arg(gradientCssReversed(Colors::GradientTop, Colors::GradientMid1, Colors::GradientMid2,
                                 Colors::GradientBottom))
        .arg(gradientCss(Colors::SelectedTop, Colors::SelectedMid1, Colors::SelectedMid2, Colors::SelectedBottom))
        .arg(Colors::TextOnLight)
        .arg(Colors::BorderSelected)
        .arg(Dimensions::SideCompactBorderWidth)
        .arg(Dimensions::SideCompactBorderRadius);
}

QString sidePanelButton() {
    return QString(R"(
        QPushButton {
            background: %1;
            color: %2;
            border: %3px solid %4;
            border-radius: %5px;
            font-size: %6px;
            font-weight: bold;
            padding: 0px;
        }
        QPushButton:hover {
            background: %7;
            border: %3px solid %8;
        }
        QPushButton:pressed {
            background: %9;
            border: %3px solid %10;
        }
    )")
        .arg(gradientCss(Colors::GradientTop, Colors::GradientMid1, Colors::GradientMid2, Colors::GradientBottom))
        .arg(Colors::TextPrimary)
        .arg(Dimensions::BorderWidth)
        .arg(Colors::BorderNormal)
        .arg(Dimensions::BorderRadius)
        .arg(Dimensions::FontSizeCaption)
        .arg(gradientCss(Colors::HoverTop, Colors::HoverMid1, Colors::HoverMid2, Colors::HoverBottom))
        .arg(Colors::BorderHover)
        .arg(gradientCssReversed(Colors::GradientTop, Colors::GradientMid1, Colors::GradientMid2,
                                 Colors::GradientBottom))
        .arg(Colors::BorderPressed);
}

QString sidePanelButtonActive() {
    return sidePanelActiveCss(SelectorScope::Always);
}

QString sidePanelButtonSelectable() {
    return sidePanelButton() + sidePanelActiveCss(SelectorScope::Selectable);
}

QString sidePanelButtonLight() {
    return QString(R"(
        QPushButton {
            background: %1;
            color: %2;
            border: %3px solid %4;
            border-radius: %5px;
            font-size: %6px;
            font-weight: bold;
            padding: 0px;
        }
        QPushButton:hover {
            border: %3px solid %7;
        }
        QPushButton:pressed {
            background: %8;
        }
    )")
        .arg(gradientCss(Colors::LightGradientTop, Colors::LightGradientMid1, Colors::LightGradientMid2,
                         Colors::LightGradientBottom))
        .arg(Colors::TextPrimary)
        .arg(Dimensions::BorderWidth)
        .arg(Colors::BorderPressed)
        .arg(Dimensions::BorderRadius)
        .arg(Dimensions::FontSizeCaption)
        .arg(Colors::BorderSelected)
        .arg(gradientCssReversed(Colors::LightGradientTop, Colors::LightGradientMid1, Colors::LightGradientMid2,
                                 Colors::LightGradientBottom));
}

QString dialogButton() {
    return QString(R"(
        QPushButton {
            background: %1;
            color: %2;
            border: 2px solid %3;
            border-radius: 6px;
            padding: 6px 12px;
            font-size: %4px;
            font-weight: bold;
        }
        QPushButton:hover {
            background: %5;
            border: 2px solid %6;
        }
        QPushButton:pressed {
            background: %7;
            border: 2px solid %8;
        }
        QPushButton:disabled {
            background: %9;
            color: %10;
            border: 2px solid %11;
        }
    )")
        .arg(gradientCss(Colors::GradientTop, Colors::GradientMid1, Colors::GradientMid2, Colors::GradientBottom))
        .arg(Colors::TextPrimary)
        .arg(Colors::BorderNormal)
        .arg(Dimensions::FontSizeBody)
        .arg(gradientCss(Colors::HoverTop, Colors::HoverMid1, Colors::HoverMid2, Colors::HoverBottom))
        .arg(Colors::BorderHover)
        .arg(gradientCssReversed(Colors::GradientTop, Colors::GradientMid1, Colors::GradientMid2,
                                 Colors::GradientBottom))
        .arg(Colors::BorderPressed)
        .arg(gradientCss(Colors::GradientMid1, Colors::GradientMid1, Colors::GradientBottom, Colors::GradientBottom))
        .arg(Colors::TextDisabled)
        .arg(Colors::DialogBorder);
}

QString controlButton(bool selected) {
    if (selected) {
        return QString(R"(
            QPushButton {
                background: %1;
                color: %2;
                border: 1px solid %3;
                border-radius: 4px;
                font-size: %4px;
                font-weight: bold;
                padding: 2px 4px;
            }
            QPushButton:hover {
                border: 1px solid %5;
            }
            QPushButton:pressed {
                background: %6;
            }
        )")
            .arg(gradientCss(Colors::SelectedTop, Colors::SelectedMid1, Colors::SelectedMid2, Colors::SelectedBottom))
            .arg(Colors::TextOnLight)
            .arg(Colors::BorderSelected)
            .arg(Dimensions::FontSizeCaption)
            .arg(Colors::BorderHover)
            .arg(gradientCssReversed(Colors::SelectedTop, Colors::SelectedMid1, Colors::SelectedMid2,
                                     Colors::SelectedBottom));
    } else {
        return QString(R"(
            QPushButton {
                background: %1;
                color: %2;
                border: 1px solid %3;
                border-radius: 4px;
                font-size: %4px;
                font-weight: bold;
                padding: 2px 4px;
            }
            QPushButton:hover {
                background: %5;
                border: 1px solid %6;
            }
            QPushButton:pressed {
                background: %7;
            }
            QPushButton:disabled {
                background: %8;
                color: %9;
                border: 1px solid %3;
            }
        )")
            .arg(gradientCss(Colors::GradientTop, Colors::GradientMid1, Colors::GradientMid2, Colors::GradientBottom))
            .arg(Colors::TextPrimary)
            .arg(Colors::BorderNormal)
            .arg(Dimensions::FontSizeCaption)
            .arg(gradientCss(Colors::HoverTop, Colors::HoverMid1, Colors::HoverMid2, Colors::HoverBottom))
            .arg(Colors::BorderHover)
            .arg(gradientCssReversed(Colors::GradientTop, Colors::GradientMid1, Colors::GradientMid2,
                                     Colors::GradientBottom))
            .arg(Colors::DisabledBackground)
            .arg(Colors::TextDisabled);
    }
}

namespace Fonts {

QFont paintFont(int pixelSize, QFont::Weight weight) {
    QFont font(Primary);
    font.setPixelSize(pixelSize);
    font.setWeight(weight);
    font.setStyleHint(QFont::SansSerif);
    font.setHintingPreference(QFont::PreferFullHinting);
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
}

QFont dataFont(int pixelSize, QFont::Weight weight) {
    QFont font(Data);
    font.setPixelSize(pixelSize);
    font.setWeight(weight);
    font.setStyleHint(QFont::SansSerif);
    font.setHintingPreference(QFont::PreferFullHinting);
    font.setStyleStrategy(QFont::PreferAntialias);
    // WHY: Inter's default figures are proportional, so a changing frequency or meter readout would shift
    // sideways. The OpenType `tnum` feature gives every digit one advance. Set through QFont because Qt
    // style sheets have no font-feature-settings. QFont::setFeature is Qt 6.7+, the app's compile floor.
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    font.setFeature(QFont::Tag("tnum"), 1);
#endif
    return font;
}

} // namespace Fonts

namespace Dialog {

const QString &pageBackground() {
    static const QString s = QString("background-color: %1;").arg(Colors::Background);
    return s;
}

const QString &separator() {
    static const QString s = QString("background-color: %1;").arg(Colors::DialogBorder);
    return s;
}

const QString &titleLabel() {
    static const QString s = QString("color: %1; font-size: %2px; font-weight: bold;")
                                 .arg(Colors::AccentAmber)
                                 .arg(Dimensions::FontSizeIndicator);
    return s;
}

const QString &formLabel() {
    static const QString s =
        QString("color: %1; font-size: %2px;").arg(Colors::TextSecondary).arg(Dimensions::FontSizeHeading);
    return s;
}

const QString &formValue() {
    static const QString s = QString("color: %1; font-size: %2px; font-weight: bold;")
                                 .arg(Colors::TextPrimary)
                                 .arg(Dimensions::FontSizeHeading);
    return s;
}

const QString &helpText() {
    static const QString s = QString("color: %1; font-size: %2px; font-style: italic;")
                                 .arg(Colors::TextSecondary)
                                 .arg(Dimensions::FontSizeBody);
    return s;
}

const QString &sectionHeader() {
    static const QString s = QString("color: %1; font-size: %2px; font-weight: bold;")
                                 .arg(Colors::TextPrimary)
                                 .arg(Dimensions::FontSizeHeading);
    return s;
}

const QString &statusLabel(const QString &color) {
    // Not cached: colour is dynamic. Main-thread only (settings pages), so a plain static scratch is safe.
    static QString s;
    s = QString("color: %1; font-size: %2px; font-weight: bold;").arg(color).arg(Dimensions::FontSizeHeading);
    return s;
}

QString labelText(const QString &color, int sizePx) {
    return QString("color: %1; font-size: %2px;").arg(color).arg(sizePx);
}

QString labelTextBold(const QString &color, int sizePx) {
    return QString("color: %1; font-size: %2px; font-weight: bold;").arg(color).arg(sizePx);
}

const QString &comboBox() {
    static const QString s =
        QString("QComboBox { background-color: %1; color: %2; border: 1px solid %3; "
                "           padding: %6px; font-size: %5px; border-radius: %7px; }"
                "QComboBox:focus { border-color: %4; }"
                "QComboBox::drop-down { border: none; width: 20px; }"
                "QComboBox::down-arrow { image: url(:/icons/arrow-down.svg); width: 10px; height: 6px; }"
                "QComboBox QAbstractItemView { background-color: %1; color: %2; selection-background-color: %4; "
                "                              selection-color: %1; }")
            .arg(Colors::DarkBackground, Colors::TextPrimary, Colors::DialogBorder, Colors::AccentAmber)
            .arg(Dimensions::FontSizeHeading)
            .arg(Dimensions::PaddingSmall)
            .arg(Dimensions::SliderBorderRadius);
    return s;
}

const QString &lineEdit() {
    static const QString s =
        QString("QLineEdit { background-color: %1; color: %2; border: 1px solid %3; "
                "           padding: %6px; font-size: %5px; border-radius: %7px; }"
                "QLineEdit:focus { border-color: %4; }")
            .arg(Colors::DarkBackground, Colors::TextPrimary, Colors::DialogBorder, Colors::AccentAmber)
            .arg(Dimensions::FontSizeHeading)
            .arg(Dimensions::PaddingSmall)
            .arg(Dimensions::SliderBorderRadius);
    return s;
}

const QString &spinBox() {
    // WHY: the step buttons and arrows are styled here too. Styling only the frame leaves them to
    // the platform, which on Windows 11 dark mode renders them dark-on-dark - the checkbox
    // indicator failure again. The arrows are SVGs because the zero-size-border triangle trick
    // renders as a solid bar, not a triangle.
    static const QString s =
        QString("QSpinBox { background-color: %1; color: %2; border: 1px solid %3; "
                "           padding: %6px; padding-right: 18px; font-size: %5px; border-radius: %7px; }"
                "QSpinBox:focus { border-color: %4; }"
                "QSpinBox::up-button, QSpinBox::down-button { subcontrol-origin: border; width: 16px; "
                "           border: none; background: transparent; }"
                "QSpinBox::up-button { subcontrol-position: top right; }"
                "QSpinBox::down-button { subcontrol-position: bottom right; }"
                "QSpinBox::up-arrow { image: url(:/icons/arrow-up.svg); width: 10px; height: 6px; }"
                "QSpinBox::down-arrow { image: url(:/icons/arrow-down.svg); width: 10px; height: 6px; }")
            .arg(Colors::DarkBackground, Colors::TextPrimary, Colors::DialogBorder, Colors::AccentAmber)
            .arg(Dimensions::FontSizeHeading)
            .arg(Dimensions::PaddingSmall)
            .arg(Dimensions::SliderBorderRadius);
    return s;
}

const QString &checkBox() {
    // THE INDICATOR IS DRAWN, NOT LEFT TO THE PLATFORM. Sizing it and nothing else leaves the
    // unchecked box rendering near-black on these near-black dialogs - present, clickable and
    // invisible. An operator reported being unable to find a checkbox that was under the cursor.
    // Grey outline for off, amber fill for on, matching the accent used everywhere else.
    static const QString s = QString("QCheckBox { color: %1; font-size: %2px; spacing: %3px; }"
                                     "QCheckBox::indicator { width: %4px; height: %4px;"
                                     "  border: 1px solid %5; border-radius: 3px;"
                                     "  background-color: %6; }"
                                     "QCheckBox::indicator:checked { background-color: %7;"
                                     "  border-color: %7; }"
                                     "QCheckBox::indicator:hover { border-color: %7; }")
                                 .arg(Colors::TextPrimary)
                                 .arg(Dimensions::FontSizeHeading)
                                 .arg(Dimensions::BorderRadiusLarge)
                                 .arg(Dimensions::CheckboxSize)
                                 .arg(Colors::TextSecondary, Colors::DarkBackground, Colors::AccentAmber);
    return s;
}

const QString &actionButtonSmall() {
    static const QString s = QString("QPushButton { background-color: %1; color: %2; border: 1px solid %3; "
                                     "             padding: %6px 12px; font-size: %4px; border-radius: %7px; }"
                                     "QPushButton:hover { background-color: %5; }")
                                 .arg(Colors::DarkBackground, Colors::TextPrimary, Colors::DialogBorder)
                                 .arg(Dimensions::FontSizeHeading)
                                 .arg(Colors::GradientBottom)
                                 .arg(Dimensions::PaddingSmall)
                                 .arg(Dimensions::SliderBorderRadius);
    return s;
}

} // namespace Dialog

// =============================================================================
// State sheets: indicators (VFO, status bar, RIT/XIT, TX, SUB/DIV, VFO row)
// =============================================================================

// =============================================================================
// State sheets: dialogs and Options pages
// =============================================================================

// =============================================================================
// State sheets: popups, menu bars and the menu overlay
// =============================================================================

QString menuBarButtonPtt() {
    return menuBarButton() + pttPressedCss(SelectorScope::Selectable);
}

QString popupButtonDimmable() {
    // WHY: the dimmed rule is later than and as specific as popupButton()'s :checked rules, so it would
    // also grey a checked key. Callers never dim a checked key (LINE OUT forces LEFT while R=L is on).
    return popupButton() +
           QString("%1 { color: %2; }").arg(whenFlag("QPushButton", DimmedProperty), Colors::TextDisabled);
}

QString popupValueLabelDimmable() {
    return popupValueLabel(Colors::TextPrimary) +
           QString("%1 { color: %2; }").arg(whenFlag("QPushButton", DimmedProperty), Colors::TextSecondary);
}

QString eqPresetLoadButton() {
    return menuBarButtonSmall() + QString("QPushButton { font-size: %1px; padding: 2px; } %2 { color: %3; }")
                                      .arg(QString::number(Dimensions::FontSizeCaption),
                                           whenFlag("QPushButton", DimmedProperty), Colors::TextSecondaryOnRaised);
}

QString menuNormButton() {
    // WHY: the lit look keeps its dark text while pressed and the greyed look has no press look, so each
    // state's :pressed rule restates the colour and background over the resting :pressed rule.
    return QString("QPushButton { background-color: %1; color: %2; border: none; "
                   "border-radius: %3px; font-size: %4px; font-weight: bold; }"
                   "QPushButton:pressed { background-color: %5; color: %6; }"
                   "%7 { background-color: %8; color: %9; }"
                   "%10 { background-color: %11; color: %9; }"
                   "%12 { color: %13; }"
                   "%14 { background-color: %1; color: %13; }")
        .arg(Colors::OverlayNavButton, Colors::TextSecondaryOnRaised, QString::number(Dimensions::BorderRadius),
             QString::number(Dimensions::FontSizeCaption), Colors::OverlayNavButtonPressed, Colors::TextPrimary,
             whenFlag("QPushButton", SelectedProperty), Colors::SelectionLight, Colors::TextOnLight)
        .arg(whenFlag("QPushButton", SelectedProperty, ":pressed"), Colors::White,
             whenFlag("QPushButton", DimmedProperty), Colors::TextDisabled,
             whenFlag("QPushButton", DimmedProperty, ":pressed"));
}

namespace {
QString menuItemLabelCss(const char *weight, const char *browseColor, const char *editColor) {
    return QString("QLabel { color: %1; font-size: %2px;%3 } %4 { color: %5; } %6 { color: %7; }")
        .arg(Colors::TextSecondary, QString::number(Dimensions::FontSizeHeading), QLatin1StringView(weight),
             whenState("QLabel", StateProperty, MenuRowBrowse), QLatin1StringView(browseColor),
             whenState("QLabel", StateProperty, MenuRowEdit), QLatin1StringView(editColor));
}
} // namespace

QString menuItemNameLabel() {
    return menuItemLabelCss("", Colors::TextOnLight, Colors::SelectionLight);
}

QString menuItemValueLabel() {
    return menuItemLabelCss(" font-weight: bold;", Colors::TextPrimary, Colors::TextOnLight);
}

const QString &applicationStyleSheet() {
    // WHY: a control whose constructor forgets setStyleSheet is drawn by the OS theme, and Windows 11 dark
    // mode draws it dark-on-dark on these near-black dialogs. This app-wide fallback makes forgetting
    // harmless. tests/test_formcontroltheme.cpp requires a rule here for every form-control type in src/.
    static const QString s = Dialog::checkBox() + Dialog::comboBox() + Dialog::lineEdit() + Dialog::spinBox();
    return s;
}

} // namespace K4Styles
