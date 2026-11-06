#include "ui/popups/lineinpopup.h"
#include "ui/styling/k4styles.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QPainter>
#include <QWheelEvent>

namespace {
const int MaxLevel = 250;
} // namespace

LineInPopupWidget::LineInPopupWidget(QWidget *parent) : K4PopupBase(parent) {
    setupUi();
    initPopup();
}

void LineInPopupWidget::setupUi() {
    int sm = K4Styles::Dimensions::ShadowMargin;
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(sm + CompactRowContentMargin, sm + 8, sm + CompactRowContentMargin, sm + 8);
    layout->setSpacing(6);

    m_titleLabel = new QPushButton("LINE IN", this);
    m_titleLabel->setFixedSize(K4Styles::Dimensions::InputFieldWidthMedium, K4Styles::Dimensions::ButtonHeightMedium);
    m_titleLabel->setFocusPolicy(Qt::NoFocus);
    m_titleLabel->setStyleSheet(K4Styles::menuBarButtonSmall());

    m_soundCardBtn = new QPushButton("SOUND\nCARD", this);
    m_soundCardBtn->setFixedSize(K4Styles::Dimensions::PopupButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_soundCardBtn->setCheckable(true);
    m_soundCardBtn->setChecked(true); // Default source = 0
    m_soundCardBtn->setCursor(Qt::PointingHandCursor);
    m_soundCardBtn->setStyleSheet(K4Styles::popupButton());

    m_lineInJackBtn = new QPushButton("LINE IN\nJACK", this);
    m_lineInJackBtn->setFixedSize(K4Styles::Dimensions::PopupButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_lineInJackBtn->setCheckable(true);
    m_lineInJackBtn->setChecked(false);
    m_lineInJackBtn->setCursor(Qt::PointingHandCursor);
    m_lineInJackBtn->setStyleSheet(K4Styles::popupButton());

    m_valueLabel = new QPushButton(QString::number(m_soundCardLevel), this);
    m_valueLabel->setFixedSize(K4Styles::Dimensions::NavButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_valueLabel->setFocusPolicy(Qt::NoFocus);
    m_valueLabel->setStyleSheet(K4Styles::popupValueLabel(K4Styles::Colors::TextPrimary));

    m_decrementBtn = new QPushButton("-", this);
    m_decrementBtn->setFixedSize(K4Styles::Dimensions::NavButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_decrementBtn->setCursor(Qt::PointingHandCursor);
    m_decrementBtn->setStyleSheet(K4Styles::menuBarButtonSmall());

    m_incrementBtn = new QPushButton("+", this);
    m_incrementBtn->setFixedSize(K4Styles::Dimensions::NavButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_incrementBtn->setCursor(Qt::PointingHandCursor);
    m_incrementBtn->setStyleSheet(K4Styles::menuBarButtonSmall());

    m_closeBtn = new QPushButton("↩", this); // U+21A9
    m_closeBtn->setFixedSize(K4Styles::Dimensions::NavButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setStyleSheet(K4Styles::menuBarButtonSmall());

    layout->addWidget(m_titleLabel);
    layout->addWidget(m_soundCardBtn);
    layout->addWidget(m_lineInJackBtn);
    layout->addWidget(m_valueLabel);
    layout->addWidget(m_decrementBtn);
    layout->addWidget(m_incrementBtn);
    layout->addWidget(m_closeBtn);

    updateButtonStyles();

    connect(m_soundCardBtn, &QPushButton::clicked, this, [this]() {
        if (m_source != 0) {
            m_source = 0;
            m_soundCardBtn->setChecked(true);
            m_lineInJackBtn->setChecked(false);
            updateButtonStyles();
            updateValueDisplay();
            emit sourceChanged(m_source);
        }
    });
    connect(m_lineInJackBtn, &QPushButton::clicked, this, [this]() {
        if (m_source != 1) {
            m_source = 1;
            m_soundCardBtn->setChecked(false);
            m_lineInJackBtn->setChecked(true);
            updateButtonStyles();
            updateValueDisplay();
            emit sourceChanged(m_source);
        }
    });
    connect(m_decrementBtn, &QPushButton::clicked, this, [this]() {
        int delta = (QApplication::keyboardModifiers() & Qt::ShiftModifier) ? -10 : -1;
        adjustValue(delta);
    });
    connect(m_incrementBtn, &QPushButton::clicked, this, [this]() {
        int delta = (QApplication::keyboardModifiers() & Qt::ShiftModifier) ? 10 : 1;
        adjustValue(delta);
    });
    connect(m_closeBtn, &QPushButton::clicked, this, &K4PopupBase::hidePopup);
}

QSize LineInPopupWidget::contentSize() const {
    const int contentWidth = CompactRowContentMargin + K4Styles::Dimensions::InputFieldWidthMedium + 6 +
                             K4Styles::Dimensions::PopupButtonWidth + 6 + K4Styles::Dimensions::PopupButtonWidth + 6 +
                             K4Styles::Dimensions::NavButtonWidth + 6 + K4Styles::Dimensions::NavButtonWidth + 6 +
                             K4Styles::Dimensions::NavButtonWidth + 6 + K4Styles::Dimensions::NavButtonWidth +
                             CompactRowContentMargin;
    return QSize(contentWidth, CompactRowContentHeight);
}

void LineInPopupWidget::updateButtonStyles() {
    // WHY: the selected look follows m_source, not isChecked(): clicking the current source flips it
    // unchecked without changing the source, and that key must keep its selected look.
    K4Styles::setSelected(m_soundCardBtn, m_source == 0);
    K4Styles::setSelected(m_lineInJackBtn, m_source == 1);
}

void LineInPopupWidget::updateValueDisplay() {
    int value = (m_source == 0) ? m_soundCardLevel : m_lineInJackLevel;
    m_valueLabel->setText(QString::number(value));
}

void LineInPopupWidget::adjustValue(int delta) {
    if (m_source == 0) {
        int newLevel = qBound(0, m_soundCardLevel + delta, MaxLevel);
        if (newLevel != m_soundCardLevel) {
            m_soundCardLevel = newLevel;
            updateValueDisplay();
            emit soundCardLevelChanged(newLevel);
        }
    } else {
        int newLevel = qBound(0, m_lineInJackLevel + delta, MaxLevel);
        if (newLevel != m_lineInJackLevel) {
            m_lineInJackLevel = newLevel;
            updateValueDisplay();
            emit lineInJackLevelChanged(newLevel);
        }
    }
}

void LineInPopupWidget::setSoundCardLevel(int level) {
    m_soundCardLevel = qBound(0, level, MaxLevel);
    if (m_source == 0) {
        updateValueDisplay();
    }
}

void LineInPopupWidget::setLineInJackLevel(int level) {
    m_lineInJackLevel = qBound(0, level, MaxLevel);
    if (m_source == 1) {
        updateValueDisplay();
    }
}

void LineInPopupWidget::setSource(int source) {
    if (source != m_source && (source == 0 || source == 1)) {
        m_source = source;
        m_soundCardBtn->setChecked(source == 0);
        m_lineInJackBtn->setChecked(source == 1);
        updateButtonStyles();
        updateValueDisplay();
    }
}

void LineInPopupWidget::wheelEvent(QWheelEvent *event) {
    int steps = m_wheelAccumulator.accumulate(event);
    if (steps != 0)
        adjustValue(steps);
    event->accept();
}

void LineInPopupWidget::paintContent(QPainter &painter, const QRect &contentRect) {
    paintCompactRowFrame(painter, contentRect, {m_titleLabel, m_lineInJackBtn, m_incrementBtn});
}
