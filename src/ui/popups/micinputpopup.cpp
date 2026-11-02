#include "ui/popups/micinputpopup.h"
#include "ui/styling/k4styles.h"
#include <QHBoxLayout>
#include <QPainter>

namespace {
const int TitleWidth = 140; // Wider title for "MIC INPUT"
} // namespace

MicInputPopupWidget::MicInputPopupWidget(QWidget *parent) : K4PopupBase(parent) {
    setupUi();
    initPopup();
}

void MicInputPopupWidget::setupUi() {
    int sm = K4Styles::Dimensions::ShadowMargin;
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(sm + CompactRowContentMargin, sm + 8, sm + CompactRowContentMargin, sm + 8);
    layout->setSpacing(6);

    m_titleLabel = new QPushButton("MIC INPUT", this);
    m_titleLabel->setFixedSize(TitleWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_titleLabel->setFocusPolicy(Qt::NoFocus);
    m_titleLabel->setStyleSheet(K4Styles::menuBarButtonSmall());

    m_frontBtn = new QPushButton("FRONT", this);
    m_frontBtn->setFixedSize(K4Styles::Dimensions::PopupButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_frontBtn->setCheckable(true);
    m_frontBtn->setCursor(Qt::PointingHandCursor);
    m_frontBtn->setStyleSheet(K4Styles::popupButton());

    m_rearBtn = new QPushButton("REAR", this);
    m_rearBtn->setFixedSize(K4Styles::Dimensions::PopupButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_rearBtn->setCheckable(true);
    m_rearBtn->setCursor(Qt::PointingHandCursor);
    m_rearBtn->setStyleSheet(K4Styles::popupButton());

    m_lineInBtn = new QPushButton("LINE IN", this);
    m_lineInBtn->setFixedSize(K4Styles::Dimensions::PopupButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_lineInBtn->setCheckable(true);
    m_lineInBtn->setCursor(Qt::PointingHandCursor);
    m_lineInBtn->setStyleSheet(K4Styles::popupButton());

    m_frontLineBtn = new QPushButton("FRONT +\nLINE IN", this);
    m_frontLineBtn->setFixedSize(K4Styles::Dimensions::PopupButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_frontLineBtn->setCheckable(true);
    m_frontLineBtn->setCursor(Qt::PointingHandCursor);
    m_frontLineBtn->setStyleSheet(K4Styles::popupButton());

    m_rearLineBtn = new QPushButton("REAR +\nLINE IN", this);
    m_rearLineBtn->setFixedSize(K4Styles::Dimensions::PopupButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_rearLineBtn->setCheckable(true);
    m_rearLineBtn->setCursor(Qt::PointingHandCursor);
    m_rearLineBtn->setStyleSheet(K4Styles::popupButton());

    m_closeBtn = new QPushButton("↩", this); // U+21A9
    m_closeBtn->setFixedSize(K4Styles::Dimensions::NavButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setStyleSheet(K4Styles::menuBarButtonSmall());

    layout->addWidget(m_titleLabel);
    layout->addWidget(m_frontBtn);
    layout->addWidget(m_rearBtn);
    layout->addWidget(m_lineInBtn);
    layout->addWidget(m_frontLineBtn);
    layout->addWidget(m_rearLineBtn);
    layout->addWidget(m_closeBtn);

    updateButtonStyles();

    connect(m_frontBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentInput != 0) {
            m_currentInput = 0;
            updateButtonStyles();
            emit inputChanged(m_currentInput);
        }
    });
    connect(m_rearBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentInput != 1) {
            m_currentInput = 1;
            updateButtonStyles();
            emit inputChanged(m_currentInput);
        }
    });
    connect(m_lineInBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentInput != 2) {
            m_currentInput = 2;
            updateButtonStyles();
            emit inputChanged(m_currentInput);
        }
    });
    connect(m_frontLineBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentInput != 3) {
            m_currentInput = 3;
            updateButtonStyles();
            emit inputChanged(m_currentInput);
        }
    });
    connect(m_rearLineBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentInput != 4) {
            m_currentInput = 4;
            updateButtonStyles();
            emit inputChanged(m_currentInput);
        }
    });
    connect(m_closeBtn, &QPushButton::clicked, this, &K4PopupBase::hidePopup);
}

QSize MicInputPopupWidget::contentSize() const {
    const int contentWidth = CompactRowContentMargin + TitleWidth + 6 + K4Styles::Dimensions::PopupButtonWidth + 6 +
                             K4Styles::Dimensions::PopupButtonWidth + 6 + K4Styles::Dimensions::PopupButtonWidth + 6 +
                             K4Styles::Dimensions::PopupButtonWidth + 6 + K4Styles::Dimensions::PopupButtonWidth + 6 +
                             K4Styles::Dimensions::NavButtonWidth + CompactRowContentMargin;
    return QSize(contentWidth, CompactRowContentHeight);
}

void MicInputPopupWidget::updateButtonStyles() {
    m_frontBtn->setChecked(m_currentInput == 0);
    m_rearBtn->setChecked(m_currentInput == 1);
    m_lineInBtn->setChecked(m_currentInput == 2);
    m_frontLineBtn->setChecked(m_currentInput == 3);
    m_rearLineBtn->setChecked(m_currentInput == 4);

    // WHY: the selected look follows m_currentInput, not isChecked(): clicking the current input flips it
    // unchecked without changing the input, and that key must keep its selected look.
    K4Styles::setSelected(m_frontBtn, m_currentInput == 0);
    K4Styles::setSelected(m_rearBtn, m_currentInput == 1);
    K4Styles::setSelected(m_lineInBtn, m_currentInput == 2);
    K4Styles::setSelected(m_frontLineBtn, m_currentInput == 3);
    K4Styles::setSelected(m_rearLineBtn, m_currentInput == 4);
}

void MicInputPopupWidget::setCurrentInput(int input) {
    if (input >= 0 && input <= 4 && input != m_currentInput) {
        m_currentInput = input;
        updateButtonStyles();
    }
}

void MicInputPopupWidget::paintContent(QPainter &painter, const QRect &contentRect) {
    paintCompactRowFrame(painter, contentRect, {m_titleLabel, m_rearLineBtn});
}
