#include "featuremenubar.h"
#include "k4styles.h"
#include <QHBoxLayout>
#include <QPainter>
#include <QApplication>
#include <QScreen>
#include <QKeyEvent>
#include <QWheelEvent>

namespace {
const int ContentHeight = 52; // Height of content area (was setFixedHeight)
const int ContentMargin = 12; // Horizontal margin inside content
} // namespace

FeatureMenuBar::FeatureMenuBar(QWidget *parent) : QWidget(parent) {
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);
    setupUi();
    hide(); // Hidden by default
}

void FeatureMenuBar::setupUi() {
    // Height includes shadow margins on top and bottom
    setFixedHeight(ContentHeight + 2 * K4Styles::Dimensions::ShadowMargin);

    auto *layout = new QHBoxLayout(this);
    // Margins include shadow space on all sides
    layout->setContentsMargins(
        K4Styles::Dimensions::ShadowMargin + ContentMargin, K4Styles::Dimensions::ShadowMargin + 8,
        K4Styles::Dimensions::ShadowMargin + ContentMargin, K4Styles::Dimensions::ShadowMargin + 8);
    layout->setSpacing(8);

    // Title — non-interactive QPushButton so text rendering matches all other buttons
    m_titleLabel = new QPushButton("ATTENUATOR", this);
    m_titleLabel->setFixedSize(140, K4Styles::Dimensions::ButtonHeightMedium);
    m_titleLabel->setFocusPolicy(Qt::NoFocus);
    m_titleLabel->setStyleSheet(K4Styles::menuBarButtonSmall());

    // OFF/ON toggle button — use menuBarButtonSmall (no vertical padding, 12px font)
    // instead of menuBarButton (6px padding, 11px font) so text baseline matches labels
    m_toggleBtn = new QPushButton("OFF", this);
    m_toggleBtn->setMinimumWidth(60);
    m_toggleBtn->setFixedHeight(K4Styles::Dimensions::ButtonHeightMedium);
    m_toggleBtn->setCursor(Qt::PointingHandCursor);
    m_toggleBtn->setStyleSheet(K4Styles::menuBarButtonSmall());

    // Extra button (only shown for NB LEVEL - "FILTER NONE/NARROW/WIDE")
    m_extraBtn = new QPushButton("FILTER\nNONE", this);
    m_extraBtn->setMinimumWidth(90);
    m_extraBtn->setFixedHeight(K4Styles::Dimensions::ButtonHeightMedium);
    m_extraBtn->setCursor(Qt::PointingHandCursor);
    m_extraBtn->setStyleSheet(K4Styles::menuBarButtonSmall());
    m_extraBtn->hide(); // Hidden by default

    // Value display — non-interactive QPushButton so text rendering matches buttons
    m_valueLabel = new QPushButton("0", this);
    m_valueLabel->setFixedHeight(K4Styles::Dimensions::ButtonHeightMedium);
    m_valueLabel->setMinimumWidth(80);
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
    m_decrementBtn->setFixedSize(K4Styles::Dimensions::ButtonHeightLarge, K4Styles::Dimensions::ButtonHeightMedium);
    m_decrementBtn->setCursor(Qt::PointingHandCursor);
    m_decrementBtn->setStyleSheet(K4Styles::menuBarButtonSmall());

    // Increment button
    m_incrementBtn = new QPushButton("+", this);
    m_incrementBtn->setFixedSize(K4Styles::Dimensions::ButtonHeightLarge, K4Styles::Dimensions::ButtonHeightMedium);
    m_incrementBtn->setCursor(Qt::PointingHandCursor);
    m_incrementBtn->setStyleSheet(K4Styles::menuBarButtonSmall());

    // Layout - compact, no stretches (popup is centered by showAboveWidget)
    layout->addWidget(m_titleLabel, 0, Qt::AlignVCenter);
    layout->addWidget(m_toggleBtn, 0, Qt::AlignVCenter);
    layout->addWidget(m_extraBtn, 0, Qt::AlignVCenter);
    layout->addWidget(m_valueLabel, 0, Qt::AlignVCenter);
    layout->addWidget(m_decrementBtn, 0, Qt::AlignVCenter);
    layout->addWidget(m_incrementBtn, 0, Qt::AlignVCenter);

    // Connect signals
    connect(m_toggleBtn, &QPushButton::clicked, this, &FeatureMenuBar::toggleRequested);
    connect(m_decrementBtn, &QPushButton::clicked, this, &FeatureMenuBar::decrementRequested);
    connect(m_incrementBtn, &QPushButton::clicked, this, &FeatureMenuBar::incrementRequested);
    connect(m_extraBtn, &QPushButton::clicked, this, &FeatureMenuBar::extraButtonClicked);
}

void FeatureMenuBar::showForFeature(Feature feature) {
    m_currentFeature = feature;
    updateForFeature();
    // Force layout recalculation before showing (extra button changes width)
    layout()->activate();
    adjustSize(); // Calculate proper size for content

    // Position above reference widget if set
    if (m_referenceWidget) {
        showAboveWidget(m_referenceWidget);
    } else {
        update();
        show();
        setFocus();
    }
}

void FeatureMenuBar::showAboveWidget(QWidget *referenceWidget) {
    if (!referenceWidget)
        return;

    m_referenceWidget = referenceWidget;

    // Force size calculation
    layout()->activate();
    adjustSize();

    // Get the reference widget's global position
    QPoint refGlobal = referenceWidget->mapToGlobal(QPoint(0, 0));
    int refCenterX = refGlobal.x() + referenceWidget->width() / 2;

    // Content width (widget width minus shadow margins)
    int contentWidth = width() - 2 * K4Styles::Dimensions::ShadowMargin;

    // Center content area horizontally above reference widget (account for shadow margin)
    int popupX = refCenterX - contentWidth / 2 - K4Styles::Dimensions::ShadowMargin;
    // Position popup so its bottom edge (including shadow margin) is 4px above the reference widget
    int popupY = refGlobal.y() - height() - 4;

    // Ensure popup stays on screen
    QRect screenGeom = referenceWidget->screen()->availableGeometry();
    if (popupX < screenGeom.left() - K4Styles::Dimensions::ShadowMargin) {
        popupX = screenGeom.left() - K4Styles::Dimensions::ShadowMargin;
    } else if (popupX + width() > screenGeom.right() + K4Styles::Dimensions::ShadowMargin) {
        popupX = screenGeom.right() + K4Styles::Dimensions::ShadowMargin - width();
    }
    if (popupY < screenGeom.top() - K4Styles::Dimensions::ShadowMargin) {
        // If not enough room above, show below instead
        popupY = refGlobal.y() + referenceWidget->height() + 4 - K4Styles::Dimensions::ShadowMargin;
    }

    move(popupX, popupY);
    show();
    setFocus();
    update();
}

void FeatureMenuBar::hideMenu() {
    hide();
}

void FeatureMenuBar::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        hideMenu();
    } else {
        QWidget::keyPressEvent(event);
    }
}

void FeatureMenuBar::wheelEvent(QWheelEvent *event) {
    int steps = m_wheelAccumulator.accumulate(event);
    for (int i = 0; i < qAbs(steps); ++i) {
        if (steps > 0)
            emit incrementRequested();
        else
            emit decrementRequested();
    }
    event->accept();
}

void FeatureMenuBar::updateForFeature() {
    switch (m_currentFeature) {
    case Attenuator:
        m_titleLabel->setText("ATTENUATOR");
        m_extraBtn->hide();
        m_valueUnit = " dB";
        break;
    case NbLevel:
        m_titleLabel->setText("NB LEVEL");
        m_extraBtn->setText("FILTER\nNONE");
        m_extraBtn->show();
        m_valueUnit = "";
        break;
    case NrAdjust:
        m_titleLabel->setText("NR ADJUST");
        m_extraBtn->hide();
        m_valueUnit = "";
        break;
    case ManualNotch:
        m_titleLabel->setText("MANUAL NOTCH");
        m_extraBtn->hide();
        m_valueUnit = " Hz";
        break;
    }

    // Update value display with unit
    setValue(m_value);
}

void FeatureMenuBar::setFeatureEnabled(bool enabled) {
    m_featureEnabled = enabled;
    m_toggleBtn->setText(enabled ? "ON" : "OFF");
}

void FeatureMenuBar::setValue(int value) {
    m_value = value;
    m_valueLabel->setText(QString::number(value) + m_valueUnit);
}

void FeatureMenuBar::setValueUnit(const QString &unit) {
    m_valueUnit = unit;
    setValue(m_value); // Refresh display
}

void FeatureMenuBar::setNbFilter(int filter) {
    m_nbFilter = qBound(0, filter, 2);
    // Update extra button text to show current filter
    static const char *filterNames[] = {"FILTER\nNONE", "FILTER\nNARROW", "FILTER\nWIDE"};
    m_extraBtn->setText(filterNames[m_nbFilter]);
}

void FeatureMenuBar::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Calculate tight bounding box from actual widget geometry
    int left = m_titleLabel->geometry().left() - 8;
    int right = m_incrementBtn->geometry().right() + 8;
    int top = m_titleLabel->geometry().top() - 4;
    int bottom = m_titleLabel->geometry().bottom() + 4;
    QRect contentRect(left, top, right - left, bottom - top + 1);

    // Draw drop shadow
    K4Styles::drawDropShadow(painter, contentRect, 8);

    // Gradient background (matches ControlGroupWidget style)
    QLinearGradient grad = K4Styles::buttonGradient(contentRect.top(), contentRect.bottom());

    // Draw rounded rectangle with border around content area only
    painter.setBrush(grad);
    painter.setPen(QPen(K4Styles::borderColor(), 1));
    painter.drawRoundedRect(contentRect, 8, 8);

    // Draw vertical delimiter lines between widget groups
    painter.setPen(QPen(K4Styles::borderColor(), 1));
    int lineTop = contentRect.top() + 7;
    int lineBottom = contentRect.bottom() - 7;

    // Helper to draw delimiter after a widget
    auto drawDelimiter = [&](QWidget *widget) {
        if (widget && widget->isVisible()) {
            int x = widget->geometry().right() + 4; // 4px after widget (half of spacing)
            painter.drawLine(x, lineTop, x, lineBottom);
        }
    };

    // Draw delimiters after each section
    drawDelimiter(m_titleLabel); // After title
    drawDelimiter(m_toggleBtn);  // After OFF/ON
    if (m_extraBtn->isVisible()) {
        drawDelimiter(m_extraBtn); // After FILTER NONE (if shown)
    }
    drawDelimiter(m_valueLabel); // After value
    // No delimiter after +/- buttons (they're at the end now)
}
