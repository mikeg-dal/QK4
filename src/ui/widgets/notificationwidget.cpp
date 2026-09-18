#include "ui/widgets/notificationwidget.h"
#include "ui/styling/k4constants.h"
#include <QPainter>
#include <QFontMetrics>
#include <QEvent>

namespace {
// Notification theme — pulled from K4Styles so color/dimension changes propagate globally.
const QString BackgroundColor = K4Styles::Colors::GradientBottom; // #2a2a2a
const QString BorderColor = K4Styles::Colors::AccentAmber;        // #FFB000
const QString TextColor = K4Styles::Colors::TextWhite;            // #FFFFFF
constexpr int Padding = 20;
constexpr int BorderRadius = K4Styles::Dimensions::BorderRadiusLarge; // 8
constexpr int BorderWidth = K4Styles::Dimensions::BorderWidth;        // 2
constexpr int kMaxWidth = 560;  // beyond this the banner reads as a wall rather than a message
constexpr int kMinWidth = 200;  // keeps a one-word message from collapsing
constexpr int kSideMargin = 24; // clear of the window edge even when the window is narrow
} // namespace

NotificationWidget::NotificationWidget(QWidget *parent) : QWidget(parent), m_timer(new QTimer(this)) {
    // Create label for message text
    m_label = new QLabel(this);
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setStyleSheet(QString("QLabel { color: %1; background: transparent; }").arg(TextColor));

    QFont font = m_label->font();
    font.setPixelSize(K4Styles::Dimensions::FontSizePopup);
    font.setBold(true);
    m_label->setFont(font);

    // Setup timer for auto-dismiss
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &NotificationWidget::onTimeout);

    // Install event filter on parent to track resize
    if (parent) {
        parent->installEventFilter(this);
    }

    // Initially hidden
    hide();

    // Make sure we're above other widgets
    raise();
}

void NotificationWidget::showMessage(const QString &message, int durationMs) {
    m_label->setText(message);

    // Size to the text, but never wider than the window it sits in. A connection failure names the
    // host and the port, which on one line is wider than the app at its default size - the banner
    // ran off both edges and the ends of the message were unreadable. Wrapping is what makes this
    // usable for anything longer than a device-status line.
    QFontMetrics fm(m_label->font());
    const int available = parentWidget() ? parentWidget()->width() - kSideMargin * 2 : kMaxWidth;
    const int maxTextWidth = qMax(kMinWidth, qMin(kMaxWidth, available) - Padding * 2);

    QRect textRect = fm.boundingRect(QRect(0, 0, maxTextWidth, 0), Qt::TextWordWrap | Qt::AlignCenter, message);
    m_label->setWordWrap(textRect.height() > fm.height());

    int width = textRect.width() + (Padding * 2);
    int height = textRect.height() + (Padding * 2);

    // Minimum size
    width = qMax(width, 200);
    height = qMax(height, 50);

    setFixedSize(width, height);
    m_label->setGeometry(Padding, Padding, width - (Padding * 2), height - (Padding * 2));

    // Position in center of parent
    updatePosition();

    // Show and start timer
    show();
    raise();
    m_timer->start(durationMs);
}

void NotificationWidget::updatePosition() {
    if (parentWidget()) {
        QPoint center = parentWidget()->rect().center();
        move(center.x() - width() / 2, center.y() - height() / 2);
    }
}

void NotificationWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Draw rounded rectangle background
    QRect bgRect = rect().adjusted(BorderWidth / 2, BorderWidth / 2, -BorderWidth / 2, -BorderWidth / 2);

    // Background fill
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(BackgroundColor));
    painter.drawRoundedRect(bgRect, BorderRadius, BorderRadius);

    // Border
    painter.setPen(QPen(QColor(BorderColor), BorderWidth));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(bgRect, BorderRadius, BorderRadius);
}

bool NotificationWidget::eventFilter(QObject *obj, QEvent *event) {
    if (obj == parentWidget() && event->type() == QEvent::Resize) {
        if (isVisible()) {
            updatePosition();
        }
    }
    return QWidget::eventFilter(obj, event);
}

void NotificationWidget::onTimeout() {
    hide();
}
