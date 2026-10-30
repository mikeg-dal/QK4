#include "ui/popups/k4popupbase.h"
#include "ui/styling/k4styles.h"
#include <QApplication>
#include <QGuiApplication>
#include <QEvent>
#include <QHideEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>

#include <algorithm>

namespace {
// Grace period after a hide during which isVisibleOrJustHidden() stays true,
// so the click that dismissed the popup via click-away is not re-read as a
// request to reopen it. Long enough to span one button press→release.
const int ReopenGuardMs = 250;
} // namespace

K4PopupBase::K4PopupBase(QWidget *parent) : QWidget(parent) {
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);

    // WHY: on macOS a Qt::Tool window "will disappear when the application is inactive" and comes
    // back with it (Qt::WindowType). Close it instead, so switching back to QK4 does not bring back
    // a popup nobody asked for. Other platforms leave it on screen over the other application.
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state != Qt::ApplicationActive && isVisible())
            hidePopup();
    });
}

QMargins K4PopupBase::contentMargins() const {
    int sm = K4Styles::Dimensions::ShadowMargin;
    int cm = K4Styles::Dimensions::PopupContentMargin;

    return QMargins(sm + cm, // left
                    sm + cm, // top
                    sm + cm, // right
                    sm + cm  // bottom
    );
}

void K4PopupBase::initPopup() {
    QSize cs = contentSize();
    int totalWidth = cs.width() + 2 * K4Styles::Dimensions::ShadowMargin;
    int totalHeight = cs.height() + 2 * K4Styles::Dimensions::ShadowMargin;
    setFixedSize(totalWidth, totalHeight);
}

QRect K4PopupBase::contentRect() const {
    QSize cs = contentSize();
    return QRect(K4Styles::Dimensions::ShadowMargin, K4Styles::Dimensions::ShadowMargin, cs.width(), cs.height());
}

QRect K4PopupBase::anchorGlobalRect(const QWidget *anchor) {
    // WHY: a popup's window is mostly transparent shadow. Placing against it would leave a
    // sub-popup a whole shadow margin higher than one placed against a key.
    const auto *popupAnchor = qobject_cast<const K4PopupBase *>(anchor);
    const QRect local = popupAnchor ? popupAnchor->contentRect() : anchor->rect();
    return QRect(anchor->mapToGlobal(local.topLeft()), local.size());
}

QRect K4PopupBase::cardGlobalRect() const {
    return anchorGlobalRect(this);
}

QPoint K4PopupBase::placeCard(const QRect &anchor, const QSize &card, const QRect &available) {
    const int gap = K4Styles::Dimensions::ShadowMargin; // only the shadow lies between card and anchor

    int x = anchor.x() + (anchor.width() - card.width()) / 2;
    int y = anchor.y() - gap - card.height();

    // Screen edges only. Sideways first; a card wider than the screen keeps its left edge on it.
    const int maxX = available.x() + available.width() - card.width();
    x = (std::max)(available.x(), (std::min)(x, maxX));
    // No room above: the same gap below the anchor, then held on screen.
    if (y < available.y())
        y = anchor.y() + anchor.height() + gap;
    const int maxY = available.y() + available.height() - card.height();
    y = (std::max)(available.y(), (std::min)(y, maxY));
    return {x, y};
}

void K4PopupBase::showAboveWidget(QWidget *anchor) {
    if (!anchor)
        return;

    m_anchor = anchor;
    m_anchorWindow = anchor->window();

    const QRect anchorRect = anchorGlobalRect(anchor);
    const QScreen *screen = QGuiApplication::screenAt(anchorRect.center());
    if (!screen)
        screen = anchor->screen();

    // initPopup() fixed the size from contentSize(), so it is final here: one move, then one show,
    // and no first frame at a default position.
    const int sm = K4Styles::Dimensions::ShadowMargin;
    const QPoint card = placeCard(anchorRect, contentSize(), screen->availableGeometry());
    move(card.x() - sm, card.y() - sm);

    // Watch application-wide events while shown (see eventFilter). Removed again in hideEvent().
    qApp->removeEventFilter(this); // a re-show must not install it twice
    qApp->installEventFilter(this);
    show();
    raise();
    setFocus();
}

void K4PopupBase::hidePopup() {
    hide();
    // closed() signal is emitted by hideEvent()
}

bool K4PopupBase::isVisibleOrJustHidden() const {
    return isVisible() || (m_hiddenTimer.isValid() && m_hiddenTimer.elapsed() < ReopenGuardMs);
}

bool K4PopupBase::closeIfOpenForToggle() {
    if (!isVisibleOrJustHidden())
        return false;
    hidePopup();
    return true;
}

void K4PopupBase::hideEvent(QHideEvent *event) {
    QWidget::hideEvent(event);
    if (event->spontaneous()) {
        // The window system unmapped us (the main window was minimised): "after receiving a
        // spontaneous hide event, a widget is still considered visible" (QWidget::hideEvent), so
        // it would map again on restore. Close for real once this event has been handled.
        QMetaObject::invokeMethod(this, &K4PopupBase::hidePopup, Qt::QueuedConnection);
        return;
    }
    qApp->removeEventFilter(this);
    m_hiddenTimer.restart();
    emit closed();
}

bool K4PopupBase::eventFilter(QObject *watched, QEvent *event) {
    // Nothing here consumes the event: a press outside still reaches its target, so clicking
    // another control works in the same gesture. The toggle handlers' isVisibleOrJustHidden()
    // guard absorbs the case where that target is the popup's own trigger.
    //
    // WHY an app-wide filter, not WindowDeactivate: these popups are Qt::Tool windows parented to
    // the main window, so Qt keeps them "active" whenever the main window is active —
    // WindowDeactivate never fires for an in-app click. A position test against a global mouse
    // press is the reliable way. The same filter sees the anchor's and its window's events, so
    // nothing has to remember to close a popup when what it hangs from goes away.
    if (!isVisible())
        return QWidget::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick: {
        const QPoint globalPos = static_cast<QMouseEvent *>(event)->globalPosition().toPoint();
        if (!frameGeometry().contains(globalPos))
            hidePopup();
        break;
    }
    case QEvent::KeyPress:
        // Escape anywhere in the app, not only with the popup focused: a Qt::Tool window does not
        // always hold the keyboard. Not consumed, so Escape still reaches the main window too.
        if (static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape)
            hidePopup();
        break;
    case QEvent::Hide:
        if (watched == m_anchor || watched == m_anchorWindow)
            hidePopup();
        break;
    case QEvent::Move:
    case QEvent::Resize:
    case QEvent::WindowStateChange:
        // A top-level Qt::Tool window does not follow its parent: left open it would float where
        // the anchor used to be. Minimise, maximise and fullscreen arrive as WindowStateChange.
        if (watched == m_anchorWindow)
            hidePopup();
        break;
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

void K4PopupBase::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        hidePopup();
    } else {
        QWidget::keyPressEvent(event);
    }
}

void K4PopupBase::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Get content rect for drawing
    QRect cr = contentRect();

    // Draw drop shadow
    K4Styles::drawDropShadow(painter, cr, K4Styles::Dimensions::BorderRadiusLarge);

    // Main popup background
    painter.setBrush(QColor(K4Styles::Colors::PopupBackground));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(cr, K4Styles::Dimensions::BorderRadiusLarge, K4Styles::Dimensions::BorderRadiusLarge);

    // Allow subclass to draw additional content
    paintContent(painter, cr);
}

void K4PopupBase::paintCompactRowFrame(QPainter &painter, const QRect &contentRect,
                                       std::initializer_list<const QWidget *> delimitedWidgets, int rowSpacing) const {
    constexpr int DelimiterInset = 7;           // Gap between a delimiter's ends and the frame's top/bottom
    const int delimiterOffset = rowSpacing / 2; // Mid-gap, past the widget's inclusive right()

    painter.setBrush(K4Styles::buttonGradient(contentRect.top(), contentRect.bottom()));
    painter.setPen(QPen(K4Styles::borderColor(), 1));
    painter.drawRoundedRect(contentRect, K4Styles::Dimensions::BorderRadiusLarge,
                            K4Styles::Dimensions::BorderRadiusLarge);

    const int lineTop = contentRect.top() + DelimiterInset;
    const int lineBottom = contentRect.bottom() - DelimiterInset;
    for (const QWidget *widget : delimitedWidgets) {
        if (widget && widget->isVisible()) {
            const int x = widget->geometry().right() + delimiterOffset;
            painter.drawLine(x, lineTop, x, lineBottom);
        }
    }
}

void K4PopupBase::paintContent(QPainter &painter, const QRect &contentRect) {
    // Default implementation does nothing
    // Subclasses can override for custom painting
    Q_UNUSED(painter)
    Q_UNUSED(contentRect)
}
