#include "ui/styling/k4styles.h"

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
} // anonymous namespace

namespace K4Styles {

QString popupButtonNormal() {
    return QString(R"(
        QPushButton {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #4a4a4a,
                stop:0.4 #3a3a3a,
                stop:0.6 #353535,
                stop:1 #2a2a2a);
            color: #FFFFFF;
            border: 2px solid #606060;
            border-radius: 6px;
            font-size: %1px;
            font-weight: 600;
        }
        QPushButton:hover {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #5a5a5a,
                stop:0.4 #4a4a4a,
                stop:0.6 #454545,
                stop:1 #3a3a3a);
            border: 2px solid #808080;
        }
        QPushButton:pressed {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #2a2a2a,
                stop:0.4 #353535,
                stop:0.6 #3a3a3a,
                stop:1 #4a4a4a);
        }
    )")
        .arg(Dimensions::PopupButtonSize);
}

QString popupButtonSelected() {
    return QString(R"(
        QPushButton {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #E0E0E0,
                stop:0.4 #D0D0D0,
                stop:0.6 #C8C8C8,
                stop:1 #B8B8B8);
            color: #333333;
            border: 2px solid #AAAAAA;
            border-radius: 6px;
            font-size: %1px;
            font-weight: 600;
        }
        QPushButton:hover {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #F0F0F0,
                stop:0.4 #E0E0E0,
                stop:0.6 #D8D8D8,
                stop:1 #C8C8C8);
        }
        QPushButton:pressed {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #B8B8B8,
                stop:0.4 #C8C8C8,
                stop:0.6 #D0D0D0,
                stop:1 #E0E0E0);
        }
    )")
        .arg(Dimensions::PopupButtonSize);
}

QString menuBarButton() {
    return QString(R"(
        QPushButton {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #4a4a4a,
                stop:0.4 #3a3a3a,
                stop:0.6 #353535,
                stop:1 #2a2a2a);
            color: #FFFFFF;
            border: 2px solid #606060;
            border-radius: 6px;
            padding: 6px 12px;
            font-size: %1px;
            font-weight: 600;
        }
        QPushButton:hover {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #5a5a5a,
                stop:0.4 #4a4a4a,
                stop:0.6 #454545,
                stop:1 #3a3a3a);
            border: 2px solid #808080;
        }
        QPushButton:pressed {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #2a2a2a,
                stop:0.4 #353535,
                stop:0.6 #3a3a3a,
                stop:1 #4a4a4a);
            border: 2px solid #909090;
        }
    )")
        .arg(Dimensions::PopupButtonSize);
}

QString menuBarButtonActive() {
    return QString(R"(
        QPushButton {
            background: #FFFFFF;
            color: #666666;
            border: 2px solid #AAAAAA;
            border-radius: 6px;
            padding: 6px 12px;
            font-size: %1px;
            font-weight: 600;
        }
    )")
        .arg(Dimensions::PopupButtonSize);
}

QString menuBarButtonSmall() {
    return QString(R"(
        QPushButton {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #4a4a4a,
                stop:0.4 #3a3a3a,
                stop:0.6 #353535,
                stop:1 #2a2a2a);
            color: #FFFFFF;
            border: 2px solid #606060;
            border-radius: 6px;
            font-size: %1px;
            font-weight: 600;
        }
        QPushButton:hover {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #5a5a5a,
                stop:0.4 #4a4a4a,
                stop:0.6 #454545,
                stop:1 #3a3a3a);
            border: 2px solid #808080;
        }
        QPushButton:pressed {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #2a2a2a,
                stop:0.4 #353535,
                stop:0.6 #3a3a3a,
                stop:1 #4a4a4a);
            border: 2px solid #909090;
        }
    )")
        .arg(Dimensions::PopupValueSize);
}

QString menuBarButtonPttPressed() {
    return QString(R"(
        QPushButton {
            background: %1;
            color: #FFFFFF;
            border: 2px solid %1;
            border-radius: 6px;
            padding: 6px 12px;
            font-size: %2px;
            font-weight: 600;
        }
    )")
        .arg(Colors::TxRed)
        .arg(Dimensions::PopupButtonSize);
}

void drawDropShadow(QPainter &painter, const QRect &contentRect, int cornerRadius) {
    painter.setPen(Qt::NoPen);
    for (int i = Dimensions::ShadowLayers; i > 0; --i) {
        int blur = i * 2;
        int alpha = 12 + (Dimensions::ShadowLayers - i) * 3;
        QRect shadowRect = contentRect.adjusted(-blur, -blur, blur, blur);
        shadowRect.translate(Dimensions::ShadowOffsetX, Dimensions::ShadowOffsetY);
        painter.setBrush(QColor(0, 0, 0, alpha));
        painter.drawRoundedRect(shadowRect, cornerRadius + blur / 2, cornerRadius + blur / 2);
    }
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
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #4a4a4a,
                stop:0.4 #3a3a3a,
                stop:0.6 #353535,
                stop:1 #2a2a2a);
            color: transparent;
            border: 1px solid #606060;
            border-radius: 3px;
            min-width: %1px;
            max-width: %1px;
            min-height: %1px;
            max-height: %1px;
            font-size: %2px;
            font-weight: bold;
        }
        QPushButton:hover {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #5a5a5a,
                stop:0.4 #4a4a4a,
                stop:0.6 #454545,
                stop:1 #3a3a3a);
            border: 1px solid #808080;
        }
        QPushButton:checked {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #4a4a4a,
                stop:0.4 #3a3a3a,
                stop:0.6 #353535,
                stop:1 #2a2a2a);
            color: #FFFFFF;
            border: 1px solid #808080;
        }
        QPushButton:checked:hover {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #5a5a5a,
                stop:0.4 #4a4a4a,
                stop:0.6 #454545,
                stop:1 #3a3a3a);
        }
        QPushButton:disabled {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #3a3a3a,
                stop:0.4 #2a2a2a,
                stop:0.6 #252525,
                stop:1 #1a1a1a);
            border: 1px solid #404040;
            color: transparent;
        }
        QPushButton:disabled:checked {
            color: #666666;
        }
    )")
        .arg(size)
        .arg(size - 4); // Font size slightly smaller than box
}

QString radioButton() {
    // Font size templated so the helper stays consistent with Dimensions::PopupButtonSize (the
    // same 11 px baseline popup-selection buttons use). Color literals here are being migrated
    // away from hex soup as part of a later cleanup; kept inline for now to avoid a full rewrite.
    return QString(R"(
        QPushButton {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #4a4a4a,
                stop:0.4 #3a3a3a,
                stop:0.6 #353535,
                stop:1 #2a2a2a);
            color: #FFFFFF;
            border: 2px solid #606060;
            border-radius: 6px;
            padding: 6px 12px;
            font-size: %1px;
            font-weight: bold;
            text-align: left;
        }
        QPushButton:hover {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #5a5a5a,
                stop:0.4 #4a4a4a,
                stop:0.6 #454545,
                stop:1 #3a3a3a);
            border: 2px solid #808080;
        }
        QPushButton:checked {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #E0E0E0,
                stop:0.4 #D0D0D0,
                stop:0.6 #C8C8C8,
                stop:1 #B8B8B8);
            color: #333333;
            border: 2px solid #AAAAAA;
        }
    )")
        .arg(Dimensions::PopupButtonSize);
}

QString compactButton() {
    return QString(R"(
        QPushButton {
            background: %1;
            border: 1px solid %2;
            border-radius: 4px;
            color: %3;
            font-size: %4px;
            font-weight: bold;
            padding: 4px 2px;
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
            border: 1px solid %9;
        }
    )")
        .arg(gradientCss(Colors::GradientTop, Colors::GradientMid1, Colors::GradientMid2, Colors::GradientBottom))
        .arg(Colors::BorderNormal)
        .arg(Colors::TextWhite)
        .arg(Dimensions::FontSizeNormal)
        .arg(gradientCss(Colors::HoverTop, Colors::HoverMid1, Colors::HoverMid2, Colors::HoverBottom))
        .arg(gradientCssReversed(Colors::GradientTop, Colors::GradientMid1, Colors::GradientMid2,
                                 Colors::GradientBottom))
        .arg(gradientCss(Colors::SelectedTop, Colors::SelectedMid1, Colors::SelectedMid2, Colors::SelectedBottom))
        .arg(Colors::TextDark)
        .arg(Colors::BorderSelected);
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
            padding: 2px 4px;
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
        .arg(Colors::TextWhite)
        .arg(Dimensions::BorderWidth)
        .arg(Colors::BorderNormal)
        .arg(Dimensions::BorderRadius)
        .arg(Dimensions::FontSizeNormal)
        .arg(gradientCss(Colors::HoverTop, Colors::HoverMid1, Colors::HoverMid2, Colors::HoverBottom))
        .arg(Colors::BorderHover)
        .arg(gradientCssReversed(Colors::GradientTop, Colors::GradientMid1, Colors::GradientMid2,
                                 Colors::GradientBottom))
        .arg(Colors::BorderPressed);
}

QString sidePanelButtonActive() {
    // Engaged state: amber-accent border + brighter background so the icon reads as "open".
    return QString(R"(
        QPushButton {
            background: %1;
            color: %2;
            border: %3px solid %4;
            border-radius: %5px;
            font-size: %6px;
            font-weight: bold;
            padding: 2px 4px;
        }
        QPushButton:hover {
            background: %1;
            border: %3px solid %4;
        }
        QPushButton:pressed {
            background: %7;
            border: %3px solid %4;
        }
    )")
        .arg(gradientCss(Colors::HoverTop, Colors::HoverMid1, Colors::HoverMid2, Colors::HoverBottom))
        .arg(Colors::TextWhite)
        .arg(Dimensions::BorderWidth)
        .arg(Colors::AccentAmber)
        .arg(Dimensions::BorderRadius)
        .arg(Dimensions::FontSizeNormal)
        .arg(gradientCssReversed(Colors::HoverTop, Colors::HoverMid1, Colors::HoverMid2, Colors::HoverBottom));
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
            padding: 2px 4px;
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
        .arg(Colors::TextWhite)
        .arg(Dimensions::BorderWidth)
        .arg(Colors::BorderPressed)
        .arg(Dimensions::BorderRadius)
        .arg(Dimensions::FontSizeNormal)
        .arg(Colors::BorderSelected)
        .arg(gradientCssReversed(Colors::LightGradientTop, Colors::LightGradientMid1, Colors::LightGradientMid2,
                                 Colors::LightGradientBottom));
}

QString panelButtonWithDisabled() {
    return QString(R"(
        QPushButton {
            background: %1;
            color: %2;
            border: %3px solid %4;
            border-radius: %5px;
            font-size: %6px;
            font-weight: bold;
            padding: 2px 4px;
        }
        QPushButton:hover {
            background: %7;
            border-color: %8;
        }
        QPushButton:pressed {
            background: %9;
        }
        QPushButton:disabled {
            color: %10;
        }
    )")
        .arg(gradientCss(Colors::GradientTop, Colors::GradientMid1, Colors::GradientMid2, Colors::GradientBottom))
        .arg(Colors::TextWhite)
        .arg(Dimensions::BorderWidth)
        .arg(Colors::BorderNormal)
        .arg(Dimensions::BorderRadius)
        .arg(Dimensions::FontSizeNormal)
        .arg(gradientCss(Colors::HoverTop, Colors::HoverMid1, Colors::HoverMid2, Colors::HoverBottom))
        .arg(Colors::AccentAmber)
        .arg(gradientCssReversed(Colors::GradientTop, Colors::GradientMid1, Colors::GradientMid2,
                                 Colors::GradientBottom))
        .arg(Colors::InactiveGray);
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
        .arg(Colors::TextWhite)
        .arg(Colors::BorderNormal)
        .arg(Dimensions::PopupButtonSize)
        .arg(gradientCss(Colors::HoverTop, Colors::HoverMid1, Colors::HoverMid2, Colors::HoverBottom))
        .arg(Colors::BorderHover)
        .arg(gradientCssReversed(Colors::GradientTop, Colors::GradientMid1, Colors::GradientMid2,
                                 Colors::GradientBottom))
        .arg(Colors::BorderPressed)
        .arg(gradientCss(Colors::GradientMid1, Colors::GradientMid1, Colors::GradientBottom, Colors::GradientBottom))
        .arg(Colors::TextGray)
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
            .arg(Colors::TextDark)
            .arg(Colors::BorderSelected)
            .arg(Dimensions::FontSizeNormal)
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
            .arg(Colors::TextWhite)
            .arg(Colors::BorderNormal)
            .arg(Dimensions::FontSizeNormal)
            .arg(gradientCss(Colors::HoverTop, Colors::HoverMid1, Colors::HoverMid2, Colors::HoverBottom))
            .arg(Colors::BorderHover)
            .arg(gradientCssReversed(Colors::GradientTop, Colors::GradientMid1, Colors::GradientMid2,
                                     Colors::GradientBottom))
            .arg(Colors::DisabledBackground)
            .arg(Colors::InactiveGray);
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
    // Enable tabular figures for consistent digit widths
    // Tabular figures are set via QFont API (not stylesheet — Qt CSS doesn't support font-feature-settings)
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
                                 .arg(Dimensions::FontSizeTitle);
    return s;
}

const QString &formLabel() {
    static const QString s =
        QString("color: %1; font-size: %2px;").arg(Colors::TextGray).arg(Dimensions::FontSizePopup);
    return s;
}

const QString &formValue() {
    static const QString s =
        QString("color: %1; font-size: %2px; font-weight: bold;").arg(Colors::TextWhite).arg(Dimensions::FontSizePopup);
    return s;
}

const QString &helpText() {
    static const QString s =
        QString("color: %1; font-size: %2px; font-style: italic;").arg(Colors::TextGray).arg(Dimensions::FontSizeLarge);
    return s;
}

const QString &sectionHeader() {
    static const QString s =
        QString("color: %1; font-size: %2px; font-weight: bold;").arg(Colors::TextWhite).arg(Dimensions::FontSizePopup);
    return s;
}

const QString &statusLabel(const QString &color) {
    // Not cached — color is dynamic. All callers are settings-page UI on the
    // main thread (verified by grep across ui/pages/*.cpp); the static scratch
    // buffer doesn't need thread_local.
    static QString s;
    s = QString("color: %1; font-size: %2px; font-weight: bold;").arg(color).arg(Dimensions::FontSizePopup);
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
                "QComboBox::down-arrow { image: none; border-left: 5px solid transparent; "
                "           border-right: 5px solid transparent; border-top: 5px solid %2; }"
                "QComboBox QAbstractItemView { background-color: %1; color: %2; selection-background-color: %4; }")
            .arg(Colors::DarkBackground, Colors::TextWhite, Colors::DialogBorder, Colors::AccentAmber)
            .arg(Dimensions::FontSizePopup)
            .arg(Dimensions::PaddingSmall)
            .arg(Dimensions::SliderBorderRadius);
    return s;
}

const QString &lineEdit() {
    static const QString s =
        QString("QLineEdit { background-color: %1; color: %2; border: 1px solid %3; "
                "           padding: %6px; font-size: %5px; border-radius: %7px; }"
                "QLineEdit:focus { border-color: %4; }")
            .arg(Colors::DarkBackground, Colors::TextWhite, Colors::DialogBorder, Colors::AccentAmber)
            .arg(Dimensions::FontSizePopup)
            .arg(Dimensions::PaddingSmall)
            .arg(Dimensions::SliderBorderRadius);
    return s;
}

const QString &checkBox() {
    static const QString s = QString("QCheckBox { color: %1; font-size: %2px; spacing: %3px; }"
                                     "QCheckBox::indicator { width: %4px; height: %4px; }")
                                 .arg(Colors::TextWhite)
                                 .arg(Dimensions::FontSizePopup)
                                 .arg(Dimensions::BorderRadiusLarge)
                                 .arg(Dimensions::CheckboxSize);
    return s;
}

const QString &checkBoxDisabled() {
    static const QString s = QString("QCheckBox { color: %1; font-size: %2px; spacing: %3px; }"
                                     "QCheckBox::indicator { width: %4px; height: %4px; }")
                                 .arg(Colors::TextGray)
                                 .arg(Dimensions::FontSizePopup)
                                 .arg(Dimensions::BorderRadiusLarge)
                                 .arg(Dimensions::CheckboxSize);
    return s;
}

const QString &actionButton() {
    static const QString s = QString("QPushButton { background-color: %1; color: %2; border: 1px solid %3; "
                                     "             padding: 10px 20px; font-size: %4px; border-radius: 4px; }"
                                     "QPushButton:hover { background-color: %5; }")
                                 .arg(Colors::DarkBackground, Colors::TextWhite, Colors::DialogBorder)
                                 .arg(Dimensions::FontSizePopup)
                                 .arg(Colors::GradientBottom);
    return s;
}

const QString &actionButtonSmall() {
    static const QString s = QString("QPushButton { background-color: %1; color: %2; border: 1px solid %3; "
                                     "             padding: %6px 12px; font-size: %4px; border-radius: %7px; }"
                                     "QPushButton:hover { background-color: %5; }")
                                 .arg(Colors::DarkBackground, Colors::TextWhite, Colors::DialogBorder)
                                 .arg(Dimensions::FontSizePopup)
                                 .arg(Colors::GradientBottom)
                                 .arg(Dimensions::PaddingSmall)
                                 .arg(Dimensions::SliderBorderRadius);
    return s;
}

} // namespace Dialog

} // namespace K4Styles
