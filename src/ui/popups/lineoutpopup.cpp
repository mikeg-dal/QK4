#include "ui/popups/lineoutpopup.h"
#include "models/radiostate/audioeffectsstate.h"
#include "ui/styling/k4styles.h"
#include <QHBoxLayout>
#include <QPainter>
#include <QWheelEvent>

LineOutPopupWidget::LineOutPopupWidget(QWidget *parent) : K4PopupBase(parent) {
    setupUi();
    initPopup();
}

void LineOutPopupWidget::setupUi() {
    int sm = K4Styles::Dimensions::ShadowMargin;
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(sm + CompactRowContentMargin, sm + 8, sm + CompactRowContentMargin, sm + 8);
    layout->setSpacing(6);

    m_titleLabel = new QPushButton("LINE OUT", this);
    m_titleLabel->setFixedSize(K4Styles::Dimensions::InputFieldWidthMedium, K4Styles::Dimensions::ButtonHeightMedium);
    m_titleLabel->setFocusPolicy(Qt::NoFocus);
    m_titleLabel->setStyleSheet(K4Styles::menuBarButtonSmall());

    m_leftBtn = new QPushButton("LEFT", this);
    m_leftBtn->setFixedSize(K4Styles::Dimensions::PopupButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_leftBtn->setCheckable(true);
    m_leftBtn->setCursor(Qt::PointingHandCursor);
    m_leftBtn->setStyleSheet(K4Styles::popupButton());

    m_leftValueLabel = new QPushButton(QString::number(m_leftLevel), this);
    m_leftValueLabel->setFixedSize(K4Styles::Dimensions::NavButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_leftValueLabel->setFocusPolicy(Qt::NoFocus);
    m_leftValueLabel->setStyleSheet(K4Styles::popupValueLabel(K4Styles::Colors::TextPrimary));

    m_rightBtn = new QPushButton("RIGHT", this);
    m_rightBtn->setFixedSize(K4Styles::Dimensions::PopupButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_rightBtn->setCheckable(true);
    m_rightBtn->setCursor(Qt::PointingHandCursor);
    m_rightBtn->setStyleSheet(K4Styles::popupButtonDimmable());

    m_rightValueLabel = new QPushButton(QString::number(m_rightLevel), this);
    m_rightValueLabel->setFixedSize(K4Styles::Dimensions::NavButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_rightValueLabel->setFocusPolicy(Qt::NoFocus);
    m_rightValueLabel->setStyleSheet(K4Styles::popupValueLabelDimmable());

    m_rightEqualsLeftBtn = new QPushButton("RIGHT\n=LEFT", this);
    m_rightEqualsLeftBtn->setFixedSize(K4Styles::Dimensions::PopupButtonWidth,
                                       K4Styles::Dimensions::ButtonHeightMedium);
    m_rightEqualsLeftBtn->setCheckable(true);
    m_rightEqualsLeftBtn->setCursor(Qt::PointingHandCursor);
    m_rightEqualsLeftBtn->setStyleSheet(K4Styles::popupButton());

    m_closeBtn = new QPushButton("↩", this); // U+21A9
    m_closeBtn->setFixedSize(K4Styles::Dimensions::NavButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setStyleSheet(K4Styles::menuBarButtonSmall());

    layout->addWidget(m_titleLabel);
    layout->addWidget(m_leftBtn);
    layout->addWidget(m_leftValueLabel);
    layout->addWidget(m_rightBtn);
    layout->addWidget(m_rightValueLabel);
    layout->addWidget(m_rightEqualsLeftBtn);
    layout->addWidget(m_closeBtn);

    updateButtonStyles();

    connect(m_leftBtn, &QPushButton::clicked, this, [this]() {
        m_leftSelected = true;
        updateButtonStyles();
    });
    connect(m_rightBtn, &QPushButton::clicked, this, [this]() {
        if (!m_rightEqualsLeft)
            m_leftSelected = false;
        updateButtonStyles();
    });
    connect(m_rightEqualsLeftBtn, &QPushButton::clicked, this, [this]() {
        m_rightEqualsLeft = !m_rightEqualsLeft;
        if (m_rightEqualsLeft) {
            m_leftSelected = true;
            m_rightLevel = m_leftLevel;
            updateValueLabels();
        }
        updateButtonStyles();
        emit rightEqualsLeftChanged(m_rightEqualsLeft);
    });
    connect(m_closeBtn, &QPushButton::clicked, this, &K4PopupBase::hidePopup);
}

QSize LineOutPopupWidget::contentSize() const {
    const int contentWidth = CompactRowContentMargin + K4Styles::Dimensions::InputFieldWidthMedium + 6 +
                             K4Styles::Dimensions::PopupButtonWidth + 6 + K4Styles::Dimensions::NavButtonWidth + 6 +
                             K4Styles::Dimensions::PopupButtonWidth + 6 + K4Styles::Dimensions::NavButtonWidth + 6 +
                             K4Styles::Dimensions::PopupButtonWidth + 6 + K4Styles::Dimensions::NavButtonWidth +
                             CompactRowContentMargin;
    return QSize(contentWidth, CompactRowContentHeight);
}

void LineOutPopupWidget::updateButtonStyles() {
    // WHY: the buttons are checkable, so a click flips their checked state on its own. Re-derive all three
    // from the model so a refused click (RIGHT while R=L is on) leaves nothing behind.
    m_leftBtn->setChecked(m_leftSelected);
    m_rightBtn->setChecked(!m_leftSelected);
    m_rightEqualsLeftBtn->setChecked(m_rightEqualsLeft);

    // RIGHT is never checked while R=L is on (LEFT is forced), so it only ever greys unselected.
    K4Styles::setStyleFlag(m_rightBtn, K4Styles::DimmedProperty, m_rightEqualsLeft);
    K4Styles::setStyleFlag(m_rightValueLabel, K4Styles::DimmedProperty, m_rightEqualsLeft);
}

void LineOutPopupWidget::updateValueLabels() {
    m_leftValueLabel->setText(QString::number(m_leftLevel));
    m_rightValueLabel->setText(QString::number(m_rightLevel));
}

void LineOutPopupWidget::setLeftLevel(int level) {
    m_leftLevel = qBound(0, level, AudioEffectsState::LineOutMax);
    m_leftValueLabel->setText(QString::number(m_leftLevel));
    if (m_rightEqualsLeft) {
        m_rightLevel = m_leftLevel;
        m_rightValueLabel->setText(QString::number(m_rightLevel));
    }
}

void LineOutPopupWidget::setRightLevel(int level) {
    m_rightLevel = qBound(0, level, AudioEffectsState::LineOutMax);
    m_rightValueLabel->setText(QString::number(m_rightLevel));
}

void LineOutPopupWidget::setRightEqualsLeft(bool enabled) {
    if (m_rightEqualsLeft != enabled) {
        m_rightEqualsLeft = enabled;
        if (enabled)
            m_leftSelected = true;
        updateButtonStyles();
    }
}

void LineOutPopupWidget::wheelEvent(QWheelEvent *event) {
    int steps = m_wheelAccumulator.accumulate(event);
    if (steps == 0) {
        event->accept();
        return;
    }

    if (m_leftSelected) {
        int newLevel = qBound(0, m_leftLevel + steps, AudioEffectsState::LineOutMax);
        if (newLevel != m_leftLevel) {
            m_leftLevel = newLevel;
            m_leftValueLabel->setText(QString::number(newLevel));
            emit leftLevelChanged(newLevel);
            if (m_rightEqualsLeft) {
                m_rightLevel = newLevel;
                m_rightValueLabel->setText(QString::number(newLevel));
            }
        }
    } else {
        if (!m_rightEqualsLeft) {
            int newLevel = qBound(0, m_rightLevel + steps, AudioEffectsState::LineOutMax);
            if (newLevel != m_rightLevel) {
                m_rightLevel = newLevel;
                m_rightValueLabel->setText(QString::number(newLevel));
                emit rightLevelChanged(newLevel);
            }
        }
    }
    event->accept();
}

void LineOutPopupWidget::paintContent(QPainter &painter, const QRect &contentRect) {
    paintCompactRowFrame(painter, contentRect,
                         {m_titleLabel, m_leftValueLabel, m_rightValueLabel, m_rightEqualsLeftBtn});
}
