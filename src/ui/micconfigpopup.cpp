#include "micconfigpopup.h"
#include "k4styles.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QScreen>

namespace {
const int ContentHeight = 52;
const int ContentMargin = 12;
const int TitleWidthFront = 180; // "MIC CONFIG, FRONT"
const int TitleWidthRear = 170;  // "MIC CONFIG, REAR"
} // namespace

MicConfigPopupWidget::MicConfigPopupWidget(QWidget *parent) : QWidget(parent) {
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);
    setupUi();
    hide();
}

void MicConfigPopupWidget::setupUi() {
    setFixedHeight(ContentHeight + 2 * K4Styles::Dimensions::ShadowMargin);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(
        K4Styles::Dimensions::ShadowMargin + ContentMargin, K4Styles::Dimensions::ShadowMargin + 8,
        K4Styles::Dimensions::ShadowMargin + ContentMargin, K4Styles::Dimensions::ShadowMargin + 8);
    layout->setSpacing(6);

    // Title label - will be updated based on mic type
    m_titleLabel = new QPushButton("MIC CONFIG, FRONT", this);
    m_titleLabel->setFixedSize(TitleWidthFront, K4Styles::Dimensions::ButtonHeightMedium);
    m_titleLabel->setFocusPolicy(Qt::NoFocus);
    m_titleLabel->setStyleSheet(K4Styles::menuBarButtonSmall());

    // BIAS button - toggles ON/OFF
    m_biasBtn = new QPushButton("BIAS\nON", this);
    m_biasBtn->setFixedSize(K4Styles::Dimensions::PopupButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_biasBtn->setCursor(Qt::PointingHandCursor);
    m_biasBtn->setStyleSheet(K4Styles::popupButtonNormal());

    // PREAMP button - cycles through preamp levels
    m_preampBtn = new QPushButton("PREAMP\nOFF", this);
    m_preampBtn->setFixedSize(K4Styles::Dimensions::PopupButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_preampBtn->setCursor(Qt::PointingHandCursor);
    m_preampBtn->setStyleSheet(K4Styles::popupButtonNormal());

    // BUTTONS button - toggles UP/DN enable (Front mic only)
    m_buttonsBtn = new QPushButton("BUTTONS:\nUP/DN", this);
    m_buttonsBtn->setFixedSize(K4Styles::Dimensions::PopupButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_buttonsBtn->setCursor(Qt::PointingHandCursor);
    m_buttonsBtn->setStyleSheet(K4Styles::popupButtonNormal());

    // Close button
    m_closeBtn = new QPushButton("\u21A9", this); // ↩
    m_closeBtn->setFixedSize(K4Styles::Dimensions::NavButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setStyleSheet(K4Styles::menuBarButtonSmall());

    // Add to layout
    layout->addWidget(m_titleLabel, 0, Qt::AlignVCenter);
    layout->addWidget(m_biasBtn, 0, Qt::AlignVCenter);
    layout->addWidget(m_preampBtn, 0, Qt::AlignVCenter);
    layout->addWidget(m_buttonsBtn, 0, Qt::AlignVCenter);
    layout->addWidget(m_closeBtn, 0, Qt::AlignVCenter);

    // Connect signals
    connect(m_biasBtn, &QPushButton::clicked, this, [this]() {
        // Toggle bias ON/OFF
        int newBias = (m_bias == 0) ? 1 : 0;
        m_bias = newBias;
        updateButtonLabels();
        emit biasChanged(newBias);
    });

    connect(m_preampBtn, &QPushButton::clicked, this, [this]() {
        // Cycle preamp
        int newPreamp;
        if (m_micType == Front) {
            // Front: 0 → 1 → 2 → 0 (OFF → 10dB → 20dB → OFF)
            newPreamp = (m_preamp + 1) % 3;
        } else {
            // Rear: 0 → 1 → 0 (OFF → 14dB → OFF)
            newPreamp = (m_preamp + 1) % 2;
        }
        m_preamp = newPreamp;
        updateButtonLabels();
        emit preampChanged(newPreamp);
    });

    connect(m_buttonsBtn, &QPushButton::clicked, this, [this]() {
        // Toggle buttons enable (Front only)
        if (m_micType == Front) {
            int newButtons = (m_buttons == 0) ? 1 : 0;
            m_buttons = newButtons;
            updateButtonLabels();
            emit buttonsChanged(newButtons);
        }
    });

    connect(m_closeBtn, &QPushButton::clicked, this, &MicConfigPopupWidget::hidePopup);

    updateButtonLabels();
}

void MicConfigPopupWidget::setMicType(MicType type) {
    if (type != m_micType) {
        m_micType = type;
        updateLayout();
        updateButtonLabels();
    }
}

void MicConfigPopupWidget::updateLayout() {
    // Update title and visibility based on mic type
    if (m_micType == Front) {
        m_titleLabel->setText("MIC CONFIG, FRONT");
        m_titleLabel->setFixedWidth(TitleWidthFront);
        m_buttonsBtn->setVisible(true);
    } else {
        m_titleLabel->setText("MIC CONFIG, REAR");
        m_titleLabel->setFixedWidth(TitleWidthRear);
        m_buttonsBtn->setVisible(false);
    }

    // Resize to fit new layout
    layout()->activate();
    adjustSize();
}

void MicConfigPopupWidget::updateButtonLabels() {
    // BIAS label
    m_biasBtn->setText(QString("BIAS\n%1").arg(m_bias ? "ON" : "OFF"));

    // PREAMP label depends on mic type
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
        // Rear
        preampText = (m_preamp == 0) ? "OFF" : "14 dB";
    }
    m_preampBtn->setText(QString("PREAMP\n%1").arg(preampText));

    // BUTTONS label (Front only)
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

void MicConfigPopupWidget::showAboveWidget(QWidget *referenceWidget) {
    if (!referenceWidget)
        return;

    m_referenceWidget = referenceWidget;

    // Make sure layout is updated for current mic type
    updateLayout();

    layout()->activate();
    adjustSize();

    QPoint refGlobal = referenceWidget->mapToGlobal(QPoint(0, 0));
    int refCenterX = refGlobal.x() + referenceWidget->width() / 2;

    int contentWidth = width() - 2 * K4Styles::Dimensions::ShadowMargin;
    int popupX = refCenterX - contentWidth / 2 - K4Styles::Dimensions::ShadowMargin;
    int popupY = refGlobal.y() - height() - 4;

    QRect screenGeom = referenceWidget->screen()->availableGeometry();
    if (popupX < screenGeom.left() - K4Styles::Dimensions::ShadowMargin) {
        popupX = screenGeom.left() - K4Styles::Dimensions::ShadowMargin;
    } else if (popupX + width() > screenGeom.right() + K4Styles::Dimensions::ShadowMargin) {
        popupX = screenGeom.right() + K4Styles::Dimensions::ShadowMargin - width();
    }
    if (popupY < screenGeom.top() - K4Styles::Dimensions::ShadowMargin) {
        popupY = refGlobal.y() + referenceWidget->height() + 4 - K4Styles::Dimensions::ShadowMargin;
    }

    move(popupX, popupY);
    show();
    setFocus();
    update();
}

void MicConfigPopupWidget::hidePopup() {
    hide();
}

void MicConfigPopupWidget::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        hidePopup();
    } else {
        QWidget::keyPressEvent(event);
    }
}

void MicConfigPopupWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Calculate tight bounding box
    int left = m_titleLabel->geometry().left() - 8;
    int right = m_closeBtn->geometry().right() + 8;
    int top = m_titleLabel->geometry().top() - 4;
    int bottom = m_titleLabel->geometry().bottom() + 4;
    QRect contentRect(left, top, right - left, bottom - top + 1);

    // Draw drop shadow
    K4Styles::drawDropShadow(painter, contentRect, 8);

    // Gradient background
    QLinearGradient grad = K4Styles::buttonGradient(contentRect.top(), contentRect.bottom());

    painter.setBrush(grad);
    painter.setPen(QPen(K4Styles::borderColor(), 1));
    painter.drawRoundedRect(contentRect, 8, 8);

    // Draw vertical delimiter lines
    painter.setPen(QPen(K4Styles::borderColor(), 1));
    int lineTop = contentRect.top() + 7;
    int lineBottom = contentRect.bottom() - 7;

    auto drawDelimiter = [&](QWidget *widget) {
        if (widget && widget->isVisible()) {
            int x = widget->geometry().right() + 3;
            painter.drawLine(x, lineTop, x, lineBottom);
        }
    };

    drawDelimiter(m_titleLabel);
    // Draw delimiter after preamp if buttons is hidden (Rear), or after buttons if visible (Front)
    if (m_micType == Front) {
        drawDelimiter(m_buttonsBtn);
    } else {
        drawDelimiter(m_preampBtn);
    }
}
