#include "micinputpopup.h"
#include "k4styles.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QScreen>

namespace {
const int ContentHeight = 52;
const int ContentMargin = 12;
const int TitleWidth = 140; // Wider title for "MIC INPUT"
} // namespace

MicInputPopupWidget::MicInputPopupWidget(QWidget *parent) : QWidget(parent) {
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);
    setupUi();
    hide();
}

void MicInputPopupWidget::setupUi() {
    setFixedHeight(ContentHeight + 2 * K4Styles::Dimensions::ShadowMargin);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(
        K4Styles::Dimensions::ShadowMargin + ContentMargin, K4Styles::Dimensions::ShadowMargin + 8,
        K4Styles::Dimensions::ShadowMargin + ContentMargin, K4Styles::Dimensions::ShadowMargin + 8);
    layout->setSpacing(6);

    // Title label - "MIC INPUT"
    m_titleLabel = new QPushButton("MIC INPUT", this);
    m_titleLabel->setFixedSize(TitleWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_titleLabel->setFocusPolicy(Qt::NoFocus);
    m_titleLabel->setStyleSheet(K4Styles::menuBarButtonSmall());

    // FRONT button
    m_frontBtn = new QPushButton("FRONT", this);
    m_frontBtn->setFixedSize(K4Styles::Dimensions::PopupButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_frontBtn->setCheckable(true);
    m_frontBtn->setCursor(Qt::PointingHandCursor);

    // REAR button
    m_rearBtn = new QPushButton("REAR", this);
    m_rearBtn->setFixedSize(K4Styles::Dimensions::PopupButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_rearBtn->setCheckable(true);
    m_rearBtn->setCursor(Qt::PointingHandCursor);

    // LINE IN button
    m_lineInBtn = new QPushButton("LINE IN", this);
    m_lineInBtn->setFixedSize(K4Styles::Dimensions::PopupButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_lineInBtn->setCheckable(true);
    m_lineInBtn->setCursor(Qt::PointingHandCursor);

    // FRONT + LINE IN button (two lines)
    m_frontLineBtn = new QPushButton("FRONT +\nLINE IN", this);
    m_frontLineBtn->setFixedSize(K4Styles::Dimensions::PopupButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_frontLineBtn->setCheckable(true);
    m_frontLineBtn->setCursor(Qt::PointingHandCursor);

    // REAR + LINE IN button (two lines)
    m_rearLineBtn = new QPushButton("REAR +\nLINE IN", this);
    m_rearLineBtn->setFixedSize(K4Styles::Dimensions::PopupButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_rearLineBtn->setCheckable(true);
    m_rearLineBtn->setCursor(Qt::PointingHandCursor);

    // Close button
    m_closeBtn = new QPushButton("\u21A9", this); // ↩
    m_closeBtn->setFixedSize(K4Styles::Dimensions::NavButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setStyleSheet(K4Styles::menuBarButtonSmall());

    // Add to layout
    layout->addWidget(m_titleLabel);
    layout->addWidget(m_frontBtn);
    layout->addWidget(m_rearBtn);
    layout->addWidget(m_lineInBtn);
    layout->addWidget(m_frontLineBtn);
    layout->addWidget(m_rearLineBtn);
    layout->addWidget(m_closeBtn);

    // Initial button styles
    updateButtonStyles();

    // Connect signals - each button selects its input
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

    connect(m_closeBtn, &QPushButton::clicked, this, &MicInputPopupWidget::hidePopup);
}

void MicInputPopupWidget::updateButtonStyles() {
    m_frontBtn->setChecked(m_currentInput == 0);
    m_rearBtn->setChecked(m_currentInput == 1);
    m_lineInBtn->setChecked(m_currentInput == 2);
    m_frontLineBtn->setChecked(m_currentInput == 3);
    m_rearLineBtn->setChecked(m_currentInput == 4);

    m_frontBtn->setStyleSheet(m_frontBtn->isChecked() ? K4Styles::popupButtonSelected()
                                                      : K4Styles::popupButtonNormal());
    m_rearBtn->setStyleSheet(m_rearBtn->isChecked() ? K4Styles::popupButtonSelected() : K4Styles::popupButtonNormal());
    m_lineInBtn->setStyleSheet(m_lineInBtn->isChecked() ? K4Styles::popupButtonSelected()
                                                        : K4Styles::popupButtonNormal());
    m_frontLineBtn->setStyleSheet(m_frontLineBtn->isChecked() ? K4Styles::popupButtonSelected()
                                                              : K4Styles::popupButtonNormal());
    m_rearLineBtn->setStyleSheet(m_rearLineBtn->isChecked() ? K4Styles::popupButtonSelected()
                                                            : K4Styles::popupButtonNormal());
}

void MicInputPopupWidget::setCurrentInput(int input) {
    if (input >= 0 && input <= 4 && input != m_currentInput) {
        m_currentInput = input;
        updateButtonStyles();
    }
}

void MicInputPopupWidget::showAboveWidget(QWidget *referenceWidget) {
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

void MicInputPopupWidget::hidePopup() {
    hide();
}

void MicInputPopupWidget::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        hidePopup();
    } else {
        QWidget::keyPressEvent(event);
    }
}

void MicInputPopupWidget::paintEvent(QPaintEvent *event) {
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
    drawDelimiter(m_rearLineBtn);
}
