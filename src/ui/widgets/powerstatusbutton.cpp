#include "ui/widgets/powerstatusbutton.h"

#include "ui/styling/k4constants.h"
#include "ui/styling/k4glyphs.h"

namespace {
constexpr int kIconSize = 22;
constexpr int kButtonSize = 28;
} // namespace

PowerStatusButton::PowerStatusButton(QWidget *parent) : QToolButton(parent), m_glyph(K4Glyphs::power(kIconSize)) {
    setFixedSize(kButtonSize, kButtonSize);
    setIconSize(QSize(kIconSize, kIconSize));
    setAutoRaise(true);
    setCursor(Qt::ArrowCursor);
    setFocusPolicy(Qt::NoFocus);
    setStyleSheet("QToolButton { background: transparent; border: none; }");
    applyAppearance();
}

void PowerStatusButton::setState(State state) {
    if (state == m_state)
        return;
    m_state = state;
    applyAppearance();
}

void PowerStatusButton::applyAppearance() {
    QColor color;
    QString tip;
    bool clickable = false;
    switch (m_state) {
    case State::On:
        color = QColor(K4Styles::Colors::StatusGreen);
        tip = QStringLiteral("K4 powered on — click to power off remote K4");
        clickable = true;
        break;
    case State::Off:
        color = QColor(K4Styles::Colors::TxRed);
        tip = QStringLiteral("K4 not connected");
        break;
    case State::Unknown:
    default:
        color = QColor(K4Styles::Colors::MutedGraphic);
        tip = QStringLiteral("K4 power state unknown");
        break;
    }
    setIcon(m_glyph(color));
    setToolTip(tip);
    setEnabled(clickable);
    setCursor(clickable ? Qt::PointingHandCursor : Qt::ArrowCursor);
}
