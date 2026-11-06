#include "ui/popups/micconfigpopup.h"
#include "ui/styling/k4styles.h"
#include <QHBoxLayout>
#include <QPainter>

namespace {
const int TitleWidthFront = 180; // "MIC CONFIG, FRONT"
const int TitleWidthRear = 170;  // "MIC CONFIG, REAR"
} // namespace

MicConfigPopupWidget::MicConfigPopupWidget(QWidget *parent) : K4PopupBase(parent) {
    setupUi();
    initPopup();
}

void MicConfigPopupWidget::setupUi() {
    int sm = K4Styles::Dimensions::ShadowMargin;
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(sm + CompactRowContentMargin, sm + 8, sm + CompactRowContentMargin, sm + 8);
    layout->setSpacing(6);

    m_titleLabel = new QPushButton("MIC CONFIG, FRONT", this);
    m_titleLabel->setFixedSize(TitleWidthFront, K4Styles::Dimensions::ButtonHeightMedium);
    m_titleLabel->setFocusPolicy(Qt::NoFocus);
    m_titleLabel->setStyleSheet(K4Styles::menuBarButtonSmall());

    m_biasBtn = new QPushButton("BIAS\nON", this);
    m_biasBtn->setFixedSize(K4Styles::Dimensions::PopupButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_biasBtn->setCursor(Qt::PointingHandCursor);
    m_biasBtn->setStyleSheet(K4Styles::popupButtonNormal());

    m_preampBtn = new QPushButton("PREAMP\nOFF", this);
    m_preampBtn->setFixedSize(K4Styles::Dimensions::PopupButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_preampBtn->setCursor(Qt::PointingHandCursor);
    m_preampBtn->setStyleSheet(K4Styles::popupButtonNormal());

    m_buttonsBtn = new QPushButton("BUTTONS:\nUP/DN", this);
    m_buttonsBtn->setFixedSize(K4Styles::Dimensions::PopupButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_buttonsBtn->setCursor(Qt::PointingHandCursor);
    m_buttonsBtn->setStyleSheet(K4Styles::popupButtonNormal());

    m_closeBtn = new QPushButton("↩", this); // U+21A9
    m_closeBtn->setFixedSize(K4Styles::Dimensions::NavButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setStyleSheet(K4Styles::menuBarButtonSmall());

    layout->addWidget(m_titleLabel, 0, Qt::AlignVCenter);
    layout->addWidget(m_biasBtn, 0, Qt::AlignVCenter);
    layout->addWidget(m_preampBtn, 0, Qt::AlignVCenter);
    layout->addWidget(m_buttonsBtn, 0, Qt::AlignVCenter);
    layout->addWidget(m_closeBtn, 0, Qt::AlignVCenter);

    connect(m_biasBtn, &QPushButton::clicked, this, [this]() {
        int newBias = (m_bias == 0) ? 1 : 0;
        m_bias = newBias;
        updateButtonLabels();
        emit biasChanged(newBias);
    });
    connect(m_preampBtn, &QPushButton::clicked, this, [this]() {
        int newPreamp;
        if (m_micType == Front) {
            newPreamp = (m_preamp + 1) % 3;
        } else {
            newPreamp = (m_preamp + 1) % 2;
        }
        m_preamp = newPreamp;
        updateButtonLabels();
        emit preampChanged(newPreamp);
    });
    connect(m_buttonsBtn, &QPushButton::clicked, this, [this]() {
        if (m_micType == Front) {
            int newButtons = (m_buttons == 0) ? 1 : 0;
            m_buttons = newButtons;
            updateButtonLabels();
            emit buttonsChanged(newButtons);
        }
    });
    connect(m_closeBtn, &QPushButton::clicked, this, &K4PopupBase::hidePopup);

    updateButtonLabels();
}

QSize MicConfigPopupWidget::contentSize() const {
    const int titleWidth = (m_micType == Front) ? TitleWidthFront : TitleWidthRear;
    int contentWidth = CompactRowContentMargin + titleWidth + 6 + K4Styles::Dimensions::PopupButtonWidth + 6 +
                       K4Styles::Dimensions::PopupButtonWidth;
    if (m_micType == Front) {
        contentWidth += 6 + K4Styles::Dimensions::PopupButtonWidth;
    }
    contentWidth += 6 + K4Styles::Dimensions::NavButtonWidth + CompactRowContentMargin;
    return QSize(contentWidth, CompactRowContentHeight);
}

void MicConfigPopupWidget::setMicType(MicType type) {
    if (type != m_micType) {
        m_micType = type;
        updateLayout();
        updateButtonLabels();
        // contentSize() depends on m_micType — re-fix widget size to the
        // new dimensions (Front is wider and shows the BUTTONS slot).
        initPopup();
    }
}

void MicConfigPopupWidget::updateLayout() {
    if (m_micType == Front) {
        m_titleLabel->setText("MIC CONFIG, FRONT");
        m_titleLabel->setFixedWidth(TitleWidthFront);
        m_buttonsBtn->setVisible(true);
    } else {
        m_titleLabel->setText("MIC CONFIG, REAR");
        m_titleLabel->setFixedWidth(TitleWidthRear);
        m_buttonsBtn->setVisible(false);
    }
}

void MicConfigPopupWidget::updateButtonLabels() {
    m_biasBtn->setText(QString("BIAS\n%1").arg(m_bias ? "ON" : "OFF"));

    QString preampText;
    if (m_micType == Front) {
        switch (m_preamp) {
        case 0:
            preampText = "OFF";
            break;
        case 1:
            preampText = "10 dB";
            break;
        case 2:
            preampText = "20 dB";
            break;
        default:
            preampText = "OFF";
            break;
        }
    } else {
        preampText = (m_preamp == 0) ? "OFF" : "14 dB";
    }
    m_preampBtn->setText(QString("PREAMP\n%1").arg(preampText));

    if (m_micType == Front) {
        m_buttonsBtn->setText(QString("BUTTONS:\n%1").arg(m_buttons ? "UP/DN" : "OFF"));
    }
}

void MicConfigPopupWidget::setBias(int bias) {
    if ((bias == 0 || bias == 1) && bias != m_bias) {
        m_bias = bias;
        updateButtonLabels();
    }
}

void MicConfigPopupWidget::setPreamp(int preamp) {
    int maxPreamp = (m_micType == Front) ? 2 : 1;
    if (preamp >= 0 && preamp <= maxPreamp && preamp != m_preamp) {
        m_preamp = preamp;
        updateButtonLabels();
    }
}

void MicConfigPopupWidget::setButtons(int buttons) {
    if ((buttons == 0 || buttons == 1) && buttons != m_buttons) {
        m_buttons = buttons;
        updateButtonLabels();
    }
}

void MicConfigPopupWidget::paintContent(QPainter &painter, const QRect &contentRect) {
    // The delimiter follows the last input-affecting button: BUTTONS for Front, PREAMP for Rear (where
    // BUTTONS is hidden).
    paintCompactRowFrame(painter, contentRect, {m_titleLabel, m_micType == Front ? m_buttonsBtn : m_preampBtn});
}
