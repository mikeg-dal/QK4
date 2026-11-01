#include "ui/popups/keyingweightpopup.h"
#include "ui/styling/k4styles.h"
#include "utils/radioutils.h"
#include <QHBoxLayout>
#include <QPainter>
#include <QWheelEvent>

namespace {
const int MinWeight = RadioUtils::KEYING_WEIGHT_MIN;
const int MaxWeight = RadioUtils::KEYING_WEIGHT_MAX;
const int WeightStep = RadioUtils::KEYING_WEIGHT_STEP;
const int TitleWidth = 150;
const int ValueWidth = 80;
} // namespace

KeyingWeightPopupWidget::KeyingWeightPopupWidget(QWidget *parent) : K4PopupBase(parent) {
    setupUi();
    initPopup();
}

void KeyingWeightPopupWidget::setupUi() {
    auto *layout = new QHBoxLayout(this);
    // Original tight margins (sm + 8 vertical, sm + 12 horizontal) — base's
    // contentMargins() returns sm + 12 all around which is 4px taller than
    // this popup's original layout.
    int sm = K4Styles::Dimensions::ShadowMargin;
    layout->setContentsMargins(sm + CompactRowContentMargin, sm + 8, sm + CompactRowContentMargin, sm + 8);
    layout->setSpacing(6);

    m_titleLabel = new QPushButton("KEYING WEIGHT", this);
    m_titleLabel->setFixedSize(TitleWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_titleLabel->setFocusPolicy(Qt::NoFocus);
    m_titleLabel->setStyleSheet(K4Styles::menuBarButtonSmall());

    m_valueLabel = new QPushButton("1.00", this);
    m_valueLabel->setFixedSize(ValueWidth, K4Styles::Dimensions::ButtonHeightMedium);
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

    m_closeBtn = new QPushButton("↩", this); // U+21A9 leftwards arrow with hook
    m_closeBtn->setFixedSize(K4Styles::Dimensions::NavButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setStyleSheet(K4Styles::menuBarButtonSmall());

    layout->addWidget(m_titleLabel, 0, Qt::AlignVCenter);
    layout->addWidget(m_valueLabel, 0, Qt::AlignVCenter);
    layout->addWidget(m_decrementBtn, 0, Qt::AlignVCenter);
    layout->addWidget(m_incrementBtn, 0, Qt::AlignVCenter);
    layout->addWidget(m_closeBtn, 0, Qt::AlignVCenter);

    connect(m_decrementBtn, &QPushButton::clicked, this, [this]() { adjustValue(-WeightStep); });
    connect(m_incrementBtn, &QPushButton::clicked, this, [this]() { adjustValue(WeightStep); });
    connect(m_closeBtn, &QPushButton::clicked, this, &K4PopupBase::hidePopup);

    updateValueDisplay();
}

QSize KeyingWeightPopupWidget::contentSize() const {
    // Inner content (excludes the base's shadow margins, which it adds back).
    // Original outer widget was 480x92 (with shadow); inner is 440x52.
    const int contentWidth = CompactRowContentMargin + TitleWidth + 6 + ValueWidth + 6 +
                             K4Styles::Dimensions::NavButtonWidth + 6 + K4Styles::Dimensions::NavButtonWidth + 6 +
                             K4Styles::Dimensions::NavButtonWidth + CompactRowContentMargin;
    return QSize(contentWidth, CompactRowContentHeight);
}

void KeyingWeightPopupWidget::updateValueDisplay() {
    double ratio = m_weight / 100.0;
    m_valueLabel->setText(QString::number(ratio, 'f', 2));
}

void KeyingWeightPopupWidget::adjustValue(int delta) {
    int newWeight = qBound(MinWeight, m_weight + delta, MaxWeight);
    if (newWeight != m_weight) {
        m_weight = newWeight;
        updateValueDisplay();
        emit weightChanged(newWeight);
    }
}

void KeyingWeightPopupWidget::setWeight(int weight) {
    m_weight = qBound(MinWeight, weight, MaxWeight);
    updateValueDisplay();
}

void KeyingWeightPopupWidget::wheelEvent(QWheelEvent *event) {
    int steps = m_wheelAccumulator.accumulate(event);
    if (steps != 0)
        adjustValue(steps * WeightStep);
    event->accept();
}

void KeyingWeightPopupWidget::paintContent(QPainter &painter, const QRect &contentRect) {
    paintCompactRowFrame(painter, contentRect, {m_titleLabel, m_incrementBtn});
}
