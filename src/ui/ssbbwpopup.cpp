#include "ssbbwpopup.h"
#include "k4styles.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QScreen>
#include <QWheelEvent>

namespace {
const int ContentHeight = 52;
const int ContentMargin = 12;
// SSB mode (ESSB off): 24-28 (2.4-2.8 kHz)
const int SsbMinBw = 24;
const int SsbMaxBw = 28;
// ESSB mode (ESSB on): 30-45 (3.0-4.5 kHz)
const int EssbMinBw = 30;
const int EssbMaxBw = 45;
const int TitleWidth = 180; // "SSB TX BANDWIDTH" or "ESSB TX BANDWIDTH"
} // namespace

SsbBwPopupWidget::SsbBwPopupWidget(QWidget *parent) : QWidget(parent) {
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);
    setupUi();
    hide();
}

void SsbBwPopupWidget::setupUi() {
    setFixedHeight(ContentHeight + 2 * K4Styles::Dimensions::ShadowMargin);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(
        K4Styles::Dimensions::ShadowMargin + ContentMargin, K4Styles::Dimensions::ShadowMargin + 8,
        K4Styles::Dimensions::ShadowMargin + ContentMargin, K4Styles::Dimensions::ShadowMargin + 8);
    layout->setSpacing(6);

    // Title label - will be updated based on ESSB state
    m_titleLabel = new QPushButton("SSB TX BANDWIDTH", this);
    m_titleLabel->setFixedSize(TitleWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_titleLabel->setFocusPolicy(Qt::NoFocus);
    m_titleLabel->setStyleSheet(K4Styles::menuBarButtonSmall());

    // Value display label - shows bandwidth as X.X kHz
    m_valueLabel = new QPushButton("3.0 kHz", this);
    m_valueLabel->setFixedSize(80, K4Styles::Dimensions::ButtonHeightMedium);
    m_valueLabel->setFocusPolicy(Qt::NoFocus);
    m_valueLabel->setStyleSheet(QString("QPushButton {"
                                        "  color: %1;"
                                        "  font-size: %2px;"
                                        "  font-weight: 600;"
                                        "  background: transparent;"
                                        "  border: %3px solid transparent;"
                                        "  border-radius: %4px;"
                                        "}")
                                    .arg(K4Styles::Colors::TextWhite)
                                    .arg(K4Styles::Dimensions::PopupValueSize)
                                    .arg(K4Styles::Dimensions::BorderWidth)
                                    .arg(K4Styles::Dimensions::BorderRadius));

    // Decrement button
    m_decrementBtn = new QPushButton("-", this);
    m_decrementBtn->setFixedSize(K4Styles::Dimensions::NavButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_decrementBtn->setCursor(Qt::PointingHandCursor);
    m_decrementBtn->setStyleSheet(K4Styles::menuBarButtonSmall());

    // Increment button
    m_incrementBtn = new QPushButton("+", this);
    m_incrementBtn->setFixedSize(K4Styles::Dimensions::NavButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_incrementBtn->setCursor(Qt::PointingHandCursor);
    m_incrementBtn->setStyleSheet(K4Styles::menuBarButtonSmall());

    // Close button
    m_closeBtn = new QPushButton("\u21A9", this); // U+21A9 leftwards arrow with hook
    m_closeBtn->setFixedSize(K4Styles::Dimensions::NavButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setStyleSheet(K4Styles::menuBarButtonSmall());

    // Add to layout
    layout->addWidget(m_titleLabel, 0, Qt::AlignVCenter);
    layout->addWidget(m_valueLabel, 0, Qt::AlignVCenter);
    layout->addWidget(m_decrementBtn, 0, Qt::AlignVCenter);
    layout->addWidget(m_incrementBtn, 0, Qt::AlignVCenter);
    layout->addWidget(m_closeBtn, 0, Qt::AlignVCenter);

    // Connect signals
    connect(m_decrementBtn, &QPushButton::clicked, this, [this]() { adjustValue(-1); });

    connect(m_incrementBtn, &QPushButton::clicked, this, [this]() { adjustValue(1); });

    connect(m_closeBtn, &QPushButton::clicked, this, &SsbBwPopupWidget::hidePopup);

    updateTitle();
    updateValueDisplay();
}

void SsbBwPopupWidget::setEssbEnabled(bool enabled) {
    if (enabled != m_essbEnabled) {
        m_essbEnabled = enabled;
        updateTitle();
    }
}

void SsbBwPopupWidget::updateTitle() {
    m_titleLabel->setText(m_essbEnabled ? "ESSB TX BANDWIDTH" : "SSB TX BANDWIDTH");
}

void SsbBwPopupWidget::updateValueDisplay() {
    // Convert 30-45 to "3.0 kHz" - "4.5 kHz"
    double kHz = m_bandwidth / 10.0;
    m_valueLabel->setText(QString("%1 kHz").arg(kHz, 0, 'f', 1));
}

void SsbBwPopupWidget::adjustValue(int delta) {
    int minBw = m_essbEnabled ? EssbMinBw : SsbMinBw;
    int maxBw = m_essbEnabled ? EssbMaxBw : SsbMaxBw;
    int newBw = qBound(minBw, m_bandwidth + delta, maxBw);
    if (newBw != m_bandwidth) {
        m_bandwidth = newBw;
        updateValueDisplay();
        emit bandwidthChanged(newBw);
    }
}

void SsbBwPopupWidget::setBandwidth(int bw) {
    int minBw = m_essbEnabled ? EssbMinBw : SsbMinBw;
    int maxBw = m_essbEnabled ? EssbMaxBw : SsbMaxBw;
    m_bandwidth = qBound(minBw, bw, maxBw);
    updateValueDisplay();
}

void SsbBwPopupWidget::showAboveWidget(QWidget *referenceWidget) {
    if (!referenceWidget)
        return;

    m_referenceWidget = referenceWidget;

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

void SsbBwPopupWidget::hidePopup() {
    hide();
}

void SsbBwPopupWidget::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        hidePopup();
    } else {
        QWidget::keyPressEvent(event);
    }
}

void SsbBwPopupWidget::wheelEvent(QWheelEvent *event) {
    int steps = m_wheelAccumulator.accumulate(event);
    if (steps != 0)
        adjustValue(steps);
    event->accept();
}

void SsbBwPopupWidget::paintEvent(QPaintEvent *event) {
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
    drawDelimiter(m_incrementBtn);
}
