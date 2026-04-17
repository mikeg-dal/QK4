#include "lineoutpopup.h"
#include "k4styles.h"
#include <QHBoxLayout>
#include <QPainter>
#include <QApplication>
#include <QScreen>
#include <QKeyEvent>
#include <QWheelEvent>

namespace {
const int ContentHeight = 52;
const int ContentMargin = 12;
} // namespace

LineOutPopupWidget::LineOutPopupWidget(QWidget *parent) : QWidget(parent) {
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);
    setupUi();
    hide();
}

void LineOutPopupWidget::setupUi() {
    setFixedHeight(ContentHeight + 2 * K4Styles::Dimensions::ShadowMargin);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(
        K4Styles::Dimensions::ShadowMargin + ContentMargin, K4Styles::Dimensions::ShadowMargin + 8,
        K4Styles::Dimensions::ShadowMargin + ContentMargin, K4Styles::Dimensions::ShadowMargin + 8);
    layout->setSpacing(6);

    // Title label - "LINE OUT"
    m_titleLabel = new QPushButton("LINE OUT", this);
    m_titleLabel->setFixedSize(K4Styles::Dimensions::InputFieldWidthMedium, K4Styles::Dimensions::ButtonHeightMedium);
    m_titleLabel->setFocusPolicy(Qt::NoFocus);
    m_titleLabel->setStyleSheet(K4Styles::menuBarButtonSmall());

    // LEFT button - selectable
    m_leftBtn = new QPushButton("LEFT", this);
    m_leftBtn->setFixedSize(K4Styles::Dimensions::PopupButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_leftBtn->setCheckable(true);
    m_leftBtn->setChecked(true); // LEFT selected by default
    m_leftBtn->setCursor(Qt::PointingHandCursor);

    // Left value label
    m_leftValueLabel = new QPushButton(QString::number(m_leftLevel), this);
    m_leftValueLabel->setFixedSize(K4Styles::Dimensions::NavButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_leftValueLabel->setFocusPolicy(Qt::NoFocus);
    m_leftValueLabel->setStyleSheet(QString("QPushButton {"
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

    // RIGHT button - selectable
    m_rightBtn = new QPushButton("RIGHT", this);
    m_rightBtn->setFixedSize(K4Styles::Dimensions::PopupButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_rightBtn->setCheckable(true);
    m_rightBtn->setChecked(false);
    m_rightBtn->setCursor(Qt::PointingHandCursor);

    // Right value label
    m_rightValueLabel = new QPushButton(QString::number(m_rightLevel), this);
    m_rightValueLabel->setFixedSize(K4Styles::Dimensions::NavButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_rightValueLabel->setFocusPolicy(Qt::NoFocus);
    m_rightValueLabel->setStyleSheet(QString("QPushButton {"
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

    // RIGHT=LEFT toggle button
    m_rightEqualsLeftBtn = new QPushButton("RIGHT\n=LEFT", this);
    m_rightEqualsLeftBtn->setFixedSize(K4Styles::Dimensions::PopupButtonWidth,
                                       K4Styles::Dimensions::ButtonHeightMedium);
    m_rightEqualsLeftBtn->setCheckable(true);
    m_rightEqualsLeftBtn->setChecked(false);
    m_rightEqualsLeftBtn->setCursor(Qt::PointingHandCursor);

    // Close button
    m_closeBtn = new QPushButton("\u21A9", this); // ↩
    m_closeBtn->setFixedSize(K4Styles::Dimensions::NavButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setStyleSheet(K4Styles::menuBarButtonSmall());

    // Add to layout
    layout->addWidget(m_titleLabel);
    layout->addWidget(m_leftBtn);
    layout->addWidget(m_leftValueLabel);
    layout->addWidget(m_rightBtn);
    layout->addWidget(m_rightValueLabel);
    layout->addWidget(m_rightEqualsLeftBtn);
    layout->addWidget(m_closeBtn);

    // Initial button styles
    updateButtonStyles();

    // Connect signals
    connect(m_leftBtn, &QPushButton::clicked, this, [this]() {
        m_leftSelected = true;
        m_leftBtn->setChecked(true);
        m_rightBtn->setChecked(false);
        updateButtonStyles();
    });

    connect(m_rightBtn, &QPushButton::clicked, this, [this]() {
        // Only allow selecting RIGHT if not in RIGHT=LEFT mode
        if (!m_rightEqualsLeft) {
            m_leftSelected = false;
            m_leftBtn->setChecked(false);
            m_rightBtn->setChecked(true);
            updateButtonStyles();
        }
    });

    connect(m_rightEqualsLeftBtn, &QPushButton::clicked, this, [this]() {
        m_rightEqualsLeft = !m_rightEqualsLeft;
        m_rightEqualsLeftBtn->setChecked(m_rightEqualsLeft);

        if (m_rightEqualsLeft) {
            // Force LEFT selection when enabling RIGHT=LEFT mode
            m_leftSelected = true;
            m_leftBtn->setChecked(true);
            m_rightBtn->setChecked(false);
            // Sync right value to left
            m_rightLevel = m_leftLevel;
            updateValueLabels();
        }

        updateButtonStyles();
        emit rightEqualsLeftChanged(m_rightEqualsLeft);
    });

    connect(m_closeBtn, &QPushButton::clicked, this, &LineOutPopupWidget::hidePopup);
}

void LineOutPopupWidget::updateButtonStyles() {
    // LEFT button - selected style when checked
    m_leftBtn->setStyleSheet(m_leftBtn->isChecked() ? K4Styles::popupButtonSelected() : K4Styles::popupButtonNormal());

    // RIGHT button - selected style when checked, disabled appearance when RIGHT=LEFT
    if (m_rightEqualsLeft) {
        // Dimmed appearance when RIGHT=LEFT mode is active
        m_rightBtn->setStyleSheet(K4Styles::popupButtonNormal() +
                                  QString("QPushButton { color: %1; }").arg(K4Styles::Colors::TextGray));
    } else {
        m_rightBtn->setStyleSheet(m_rightBtn->isChecked() ? K4Styles::popupButtonSelected()
                                                          : K4Styles::popupButtonNormal());
    }

    // RIGHT=LEFT button - selected when enabled
    m_rightEqualsLeftBtn->setStyleSheet(m_rightEqualsLeft ? K4Styles::popupButtonSelected()
                                                          : K4Styles::popupButtonNormal());

    // Right value label - dimmed when RIGHT=LEFT mode
    m_rightValueLabel->setStyleSheet(
        QString("QPushButton {"
                "  color: %1;"
                "  font-size: %2px;"
                "  font-weight: 600;"
                "  background: transparent;"
                "  border: %3px solid transparent;"
                "  border-radius: %4px;"
                "}")
            .arg(m_rightEqualsLeft ? K4Styles::Colors::TextGray : K4Styles::Colors::TextWhite)
            .arg(K4Styles::Dimensions::PopupValueSize)
            .arg(K4Styles::Dimensions::BorderWidth)
            .arg(K4Styles::Dimensions::BorderRadius));
}

void LineOutPopupWidget::updateValueLabels() {
    m_leftValueLabel->setText(QString::number(m_leftLevel));
    m_rightValueLabel->setText(QString::number(m_rightLevel));
}

void LineOutPopupWidget::setLeftLevel(int level) {
    m_leftLevel = qBound(0, level, 40);
    m_leftValueLabel->setText(QString::number(m_leftLevel));

    // If RIGHT=LEFT mode, sync right value
    if (m_rightEqualsLeft) {
        m_rightLevel = m_leftLevel;
        m_rightValueLabel->setText(QString::number(m_rightLevel));
    }
}

void LineOutPopupWidget::setRightLevel(int level) {
    m_rightLevel = qBound(0, level, 40);
    m_rightValueLabel->setText(QString::number(m_rightLevel));
}

void LineOutPopupWidget::setRightEqualsLeft(bool enabled) {
    if (m_rightEqualsLeft != enabled) {
        m_rightEqualsLeft = enabled;
        m_rightEqualsLeftBtn->setChecked(enabled);

        if (enabled) {
            // Force LEFT selection
            m_leftSelected = true;
            m_leftBtn->setChecked(true);
            m_rightBtn->setChecked(false);
        }

        updateButtonStyles();
    }
}

void LineOutPopupWidget::showAboveWidget(QWidget *referenceWidget) {
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

void LineOutPopupWidget::hidePopup() {
    hide();
}

void LineOutPopupWidget::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        hidePopup();
    } else {
        QWidget::keyPressEvent(event);
    }
}

void LineOutPopupWidget::wheelEvent(QWheelEvent *event) {
    int steps = m_wheelAccumulator.accumulate(event);
    if (steps == 0) {
        event->accept();
        return;
    }

    if (m_leftSelected) {
        int newLevel = qBound(0, m_leftLevel + steps, 40);
        if (newLevel != m_leftLevel) {
            m_leftLevel = newLevel;
            m_leftValueLabel->setText(QString::number(newLevel));
            emit leftLevelChanged(newLevel);

            // If RIGHT=LEFT mode, also update right display (but only one signal)
            if (m_rightEqualsLeft) {
                m_rightLevel = newLevel;
                m_rightValueLabel->setText(QString::number(newLevel));
            }
        }
    } else {
        // Adjust right level (only if not in RIGHT=LEFT mode)
        if (!m_rightEqualsLeft) {
            int newLevel = qBound(0, m_rightLevel + steps, 40);
            if (newLevel != m_rightLevel) {
                m_rightLevel = newLevel;
                m_rightValueLabel->setText(QString::number(newLevel));
                emit rightLevelChanged(newLevel);
            }
        }
    }
    event->accept();
}

void LineOutPopupWidget::paintEvent(QPaintEvent *event) {
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
    drawDelimiter(m_leftValueLabel);
    drawDelimiter(m_rightValueLabel);
    drawDelimiter(m_rightEqualsLeftBtn);
}
