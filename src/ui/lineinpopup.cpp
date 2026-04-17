#include "lineinpopup.h"
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
const int MaxLevel = 250;
} // namespace

LineInPopupWidget::LineInPopupWidget(QWidget *parent) : QWidget(parent) {
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);
    setupUi();
    hide();
}

void LineInPopupWidget::setupUi() {
    setFixedHeight(ContentHeight + 2 * K4Styles::Dimensions::ShadowMargin);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(
        K4Styles::Dimensions::ShadowMargin + ContentMargin, K4Styles::Dimensions::ShadowMargin + 8,
        K4Styles::Dimensions::ShadowMargin + ContentMargin, K4Styles::Dimensions::ShadowMargin + 8);
    layout->setSpacing(6);

    // Title label - "LINE IN"
    m_titleLabel = new QPushButton("LINE IN", this);
    m_titleLabel->setFixedSize(K4Styles::Dimensions::InputFieldWidthMedium, K4Styles::Dimensions::ButtonHeightMedium);
    m_titleLabel->setFocusPolicy(Qt::NoFocus);
    m_titleLabel->setStyleSheet(K4Styles::menuBarButtonSmall());

    // SOUND CARD button - selectable (two lines)
    m_soundCardBtn = new QPushButton("SOUND\nCARD", this);
    m_soundCardBtn->setFixedSize(K4Styles::Dimensions::PopupButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_soundCardBtn->setCheckable(true);
    m_soundCardBtn->setChecked(true); // Sound card selected by default (source=0)
    m_soundCardBtn->setCursor(Qt::PointingHandCursor);

    // LINE IN JACK button - selectable (two lines)
    m_lineInJackBtn = new QPushButton("LINE IN\nJACK", this);
    m_lineInJackBtn->setFixedSize(K4Styles::Dimensions::PopupButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_lineInJackBtn->setCheckable(true);
    m_lineInJackBtn->setChecked(false);
    m_lineInJackBtn->setCursor(Qt::PointingHandCursor);

    // Value display label
    m_valueLabel = new QPushButton(QString::number(m_soundCardLevel), this);
    m_valueLabel->setFixedSize(K4Styles::Dimensions::NavButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
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
    m_closeBtn = new QPushButton("\u21A9", this); // ↩
    m_closeBtn->setFixedSize(K4Styles::Dimensions::NavButtonWidth, K4Styles::Dimensions::ButtonHeightMedium);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setStyleSheet(K4Styles::menuBarButtonSmall());

    // Add to layout
    layout->addWidget(m_titleLabel);
    layout->addWidget(m_soundCardBtn);
    layout->addWidget(m_lineInJackBtn);
    layout->addWidget(m_valueLabel);
    layout->addWidget(m_decrementBtn);
    layout->addWidget(m_incrementBtn);
    layout->addWidget(m_closeBtn);

    // Initial button styles
    updateButtonStyles();

    // Connect signals
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
        // Shift held = adjust by 10, otherwise by 1
        int delta = (QApplication::keyboardModifiers() & Qt::ShiftModifier) ? -10 : -1;
        adjustValue(delta);
    });

    connect(m_incrementBtn, &QPushButton::clicked, this, [this]() {
        // Shift held = adjust by 10, otherwise by 1
        int delta = (QApplication::keyboardModifiers() & Qt::ShiftModifier) ? 10 : 1;
        adjustValue(delta);
    });

    connect(m_closeBtn, &QPushButton::clicked, this, &LineInPopupWidget::hidePopup);
}

void LineInPopupWidget::updateButtonStyles() {
    // SOUND CARD button - selected style when checked
    m_soundCardBtn->setStyleSheet(m_soundCardBtn->isChecked() ? K4Styles::popupButtonSelected()
                                                              : K4Styles::popupButtonNormal());

    // LINE IN JACK button - selected style when checked
    m_lineInJackBtn->setStyleSheet(m_lineInJackBtn->isChecked() ? K4Styles::popupButtonSelected()
                                                                : K4Styles::popupButtonNormal());
}

void LineInPopupWidget::updateValueDisplay() {
    // Show value for the currently selected source
    int value = (m_source == 0) ? m_soundCardLevel : m_lineInJackLevel;
    m_valueLabel->setText(QString::number(value));
}

void LineInPopupWidget::adjustValue(int delta) {
    if (m_source == 0) {
        // Adjusting Sound Card level
        int newLevel = qBound(0, m_soundCardLevel + delta, MaxLevel);
        if (newLevel != m_soundCardLevel) {
            m_soundCardLevel = newLevel;
            updateValueDisplay();
            emit soundCardLevelChanged(newLevel);
        }
    } else {
        // Adjusting LINE IN Jack level
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

void LineInPopupWidget::showAboveWidget(QWidget *referenceWidget) {
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

void LineInPopupWidget::hidePopup() {
    hide();
}

void LineInPopupWidget::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        hidePopup();
    } else {
        QWidget::keyPressEvent(event);
    }
}

void LineInPopupWidget::wheelEvent(QWheelEvent *event) {
    int steps = m_wheelAccumulator.accumulate(event);
    if (steps != 0)
        adjustValue(steps);
    event->accept();
}

void LineInPopupWidget::paintEvent(QPaintEvent *event) {
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
    drawDelimiter(m_lineInJackBtn);
    drawDelimiter(m_incrementBtn);
}
