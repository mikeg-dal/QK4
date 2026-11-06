#include "ui/popups/voxpopup.h"
#include "ui/styling/k4styles.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QPainter>
#include <QWheelEvent>

namespace {
const int MaxLevel = 60;
const int TitleWidthVoxGain = 160; // "VOX GAIN, VOICE" or "VOX GAIN, DATA"
const int TitleWidthAntiVox = 110; // "ANTI-VOX"
} // namespace

VoxPopupWidget::VoxPopupWidget(QWidget *parent) : K4PopupBase(parent) {
    setupUi();
    initPopup();
}

void VoxPopupWidget::setupUi() {
    int sm = K4Styles::Dimensions::ShadowMargin;
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(sm + CompactRowContentMargin, sm + 8, sm + CompactRowContentMargin, sm + 8);
    layout->setSpacing(6);

    m_titleLabel = new QPushButton("VOX GAIN, VOICE", this);
    m_titleLabel->setFixedSize(TitleWidthVoxGain, K4Styles::Dimensions::ButtonHeightMedium);
    m_titleLabel->setFocusPolicy(Qt::NoFocus);
    m_titleLabel->setStyleSheet(K4Styles::menuBarButtonSmall());

    m_voxBtn = new QPushButton("VOX\nOFF", this);
    m_voxBtn->setFixedSize(K4Styles::Dimensions::PopupButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_voxBtn->setCursor(Qt::PointingHandCursor);
    m_voxBtn->setStyleSheet(K4Styles::popupButton());

    m_valueLabel = new QPushButton(QString::number(m_value), this);
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

    layout->addWidget(m_titleLabel, 0, Qt::AlignVCenter);
    layout->addWidget(m_voxBtn, 0, Qt::AlignVCenter);
    layout->addWidget(m_valueLabel, 0, Qt::AlignVCenter);
    layout->addWidget(m_decrementBtn, 0, Qt::AlignVCenter);
    layout->addWidget(m_incrementBtn, 0, Qt::AlignVCenter);
    layout->addWidget(m_closeBtn, 0, Qt::AlignVCenter);

    connect(m_voxBtn, &QPushButton::clicked, this, [this]() {
        m_voxEnabled = !m_voxEnabled;
        updateVoxButton();
        emit voxToggled(m_voxEnabled);
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

    updateTitle();
    updateVoxButton();
    updateValueDisplay();
}

QSize VoxPopupWidget::contentSize() const {
    const int titleWidth = (m_popupMode == VoxGain) ? TitleWidthVoxGain : TitleWidthAntiVox;
    const int contentWidth = CompactRowContentMargin + titleWidth + 6 + K4Styles::Dimensions::PopupButtonWidth + 6 +
                             K4Styles::Dimensions::NavButtonWidth + 6 + K4Styles::Dimensions::NavButtonWidth + 6 +
                             K4Styles::Dimensions::NavButtonWidth + 6 + K4Styles::Dimensions::NavButtonWidth +
                             CompactRowContentMargin;
    return QSize(contentWidth, CompactRowContentHeight);
}

void VoxPopupWidget::setPopupMode(PopupMode mode) {
    if (mode != m_popupMode) {
        m_popupMode = mode;
        updateTitle();
        // Re-fix the widget size since contentSize() depends on m_popupMode.
        initPopup();
    }
}

void VoxPopupWidget::setDataMode(bool isDataMode) {
    if (isDataMode != m_isDataMode) {
        m_isDataMode = isDataMode;
        updateTitle();
    }
}

void VoxPopupWidget::updateTitle() {
    QString title;
    int width;

    if (m_popupMode == VoxGain) {
        title = m_isDataMode ? "VOX GAIN, DATA" : "VOX GAIN, VOICE";
        width = TitleWidthVoxGain;
    } else {
        title = "ANTI-VOX";
        width = TitleWidthAntiVox;
    }

    m_titleLabel->setText(title);
    m_titleLabel->setFixedWidth(width);
}

void VoxPopupWidget::updateVoxButton() {
    m_voxBtn->setText(m_voxEnabled ? "VOX\nON" : "VOX\nOFF");
    K4Styles::setSelected(m_voxBtn, m_voxEnabled);
}

void VoxPopupWidget::updateValueDisplay() {
    m_valueLabel->setText(QString::number(m_value));
}

void VoxPopupWidget::adjustValue(int delta) {
    int newValue = qBound(0, m_value + delta, MaxLevel);
    if (newValue != m_value) {
        m_value = newValue;
        updateValueDisplay();
        emit valueChanged(newValue);
    }
}

void VoxPopupWidget::setValue(int value) {
    m_value = qBound(0, value, MaxLevel);
    updateValueDisplay();
}

void VoxPopupWidget::setVoxEnabled(bool enabled) {
    if (enabled != m_voxEnabled) {
        m_voxEnabled = enabled;
        updateVoxButton();
    }
}

void VoxPopupWidget::wheelEvent(QWheelEvent *event) {
    int steps = m_wheelAccumulator.accumulate(event);
    if (steps != 0)
        adjustValue(steps);
    event->accept();
}

void VoxPopupWidget::paintContent(QPainter &painter, const QRect &contentRect) {
    paintCompactRowFrame(painter, contentRect, {m_titleLabel, m_voxBtn, m_incrementBtn});
}
