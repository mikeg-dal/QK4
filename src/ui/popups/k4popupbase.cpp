#include "ui/popups/k4popupbase.h"
#include "ui/popups/popupdismissal.h"
#include "ui/styling/k4styles.h"
#include <QApplication>
#include <QGuiApplication>
#include <QHideEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegion>
#include <QResizeEvent>
#include <QScreen>

#include <algorithm>
#include <functional>
#include <utility>

namespace {

// The opaque surface K4PopupBase::contentPanel() hands out. The WHY for its existence is on that
// function; what matters here is the contract WA_OpaquePaintEvent carries - "the widget paints all
// its pixels when it receives a paint event" - which paintEvent honours by filling the whole dirty
// rect before anything else is drawn.
class PopupContentPanel : public QWidget {
public:
    using PaintFn = std::function<void(QPainter &, const QRect &)>;

    PopupContentPanel(QWidget *parent, PaintFn paint) : QWidget(parent), m_paint(std::move(paint)) {
        setAttribute(Qt::WA_OpaquePaintEvent);
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        QPainter painter(this);
        painter.fillRect(event->rect(), QColor(K4Styles::Colors::PopupBackground));
        painter.setRenderHint(QPainter::Antialiasing);
        m_paint(painter, rect());
    }

    void resizeEvent(QResizeEvent *event) override {
        QWidget::resizeEvent(event);
        // WHY a mask and not a rounded fill: WA_OpaquePaintEvent promises every pixel is painted, so
        // the panel cannot leave its corners transparent by painting round. Masking them away instead
        // leaves the card's own antialiased corner, drawn by K4PopupBase::paintEvent, showing through.
        // Nothing repaints out there on its own, so the corners never go stale.
        const int r = K4Styles::Dimensions::BorderRadiusLarge;
        QPainterPath path;
        path.addRoundedRect(QRectF(rect()), r, r);
        setMask(QRegion(path.toFillPolygon().toPolygon()));
    }

private:
    PaintFn m_paint;
};

} // namespace

K4PopupBase::K4PopupBase(QWidget *parent)
    : QWidget(parent), m_dismissal(new PopupDismissal(
                           this, [this]() { hidePopup(); },
                           [this](const QPoint &globalPos) { return pressIsInAChildPopup(globalPos); })) {
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

K4PopupBase::~K4PopupBase() {
    // WHY: m_dismissal is a child, deleted only after ~QWidget, which still sends events. Stop its
    // app-wide filter now so it cannot call hidePopup() on a popup already half destroyed.
    m_dismissal->stop();
}

QMargins K4PopupBase::contentMargins() const {
    int sm = K4Styles::Dimensions::ShadowMargin;
    int cm = K4Styles::Dimensions::PopupContentMargin;

    return QMargins(sm + cm, sm + cm, sm + cm, sm + cm);
}

QMargins K4PopupBase::panelMargins() {
    const int cm = K4Styles::Dimensions::PopupContentMargin;
    return QMargins(cm, cm, cm, cm);
}

QWidget *K4PopupBase::contentPanel() {
    if (!m_panel) {
        // The panel paints the popup's decorations itself, so paintContent() runs against an opaque
        // surface rather than against whatever the buffer happened to hold.
        m_panel = new PopupContentPanel(
            this, [this](QPainter &painter, const QRect &panelRect) { paintContent(painter, panelRect); });
    }
    return m_panel;
}

void K4PopupBase::initPopup() {
    QSize cs = contentSize();
    int totalWidth = cs.width() + 2 * K4Styles::Dimensions::ShadowMargin;
    int totalHeight = cs.height() + 2 * K4Styles::Dimensions::ShadowMargin;
    setFixedSize(totalWidth, totalHeight);

    // Re-applied on every fit: a popup whose content size follows its state (FeatureMenuBar) is sized
    // again before each show, and the panel has to follow the card.
    if (m_panel)
        m_panel->setGeometry(contentRect());
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

    const QRect anchorRect = anchorGlobalRect(anchor);
    const QScreen *screen = QGuiApplication::screenAt(anchorRect.center());
    if (!screen)
        screen = anchor->screen();

    // initPopup() fixed the size from contentSize(), so it is final here: one move, then one show,
    // and no first frame at a default position.
    const int sm = K4Styles::Dimensions::ShadowMargin;
    const QPoint card = placeCard(anchorRect, contentSize(), screen->availableGeometry());
    move(card.x() - sm, card.y() - sm);

    // Watch application-wide events while shown (PopupDismissal). Stopped again in hideEvent().
    m_dismissal->watch(anchor);
    show();
    raise();
    setFocus();
}

void K4PopupBase::hidePopup() {
    hide();
    // closed() signal is emitted by hideEvent()
}

QWidget *K4PopupBase::anchor() const {
    return m_dismissal->anchor();
}

bool K4PopupBase::isVisibleOrJustHidden() const {
    return m_dismissal->isVisibleOrJustHidden();
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
    m_dismissal->stop();
    emit closed();
}

// A popup opened from this one (a row's SSB BW, VOX, EQ...) sits outside this popup's frame, but a
// press on its - / + is not a click away from the row: closing the row would close the sub-popup
// with it (bench 2026-10-30). A press lands "in a child" when it hits a visible K4PopupBase whose
// anchor chain - anchor, the anchor's window, that popup's anchor, and so on - leads back here.
bool K4PopupBase::pressIsInAChildPopup(const QPoint &globalPos) const {
    for (QWidget *top : QApplication::topLevelWidgets()) {
        auto *popup = qobject_cast<K4PopupBase *>(top);
        if (!popup || popup == this || !popup->isVisible() || !popup->frameGeometry().contains(globalPos))
            continue;
        // Walk up the anchors; bounded so a malformed cycle cannot spin.
        const K4PopupBase *link = popup;
        for (int depth = 0; link && depth < 8; ++depth) {
            QWidget *anchor = link->anchor();
            if (!anchor)
                break;
            QWidget *anchorWindow = anchor->window();
            if (anchorWindow == this)
                return true;
            link = qobject_cast<const K4PopupBase *>(anchorWindow);
        }
    }
    return false;
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

    QRect cr = contentRect();

    K4Styles::drawDropShadow(painter, cr, K4Styles::Dimensions::BorderRadiusLarge);

    painter.setBrush(QColor(K4Styles::Colors::PopupBackground));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(cr, K4Styles::Dimensions::BorderRadiusLarge, K4Styles::Dimensions::BorderRadiusLarge);

    // With a panel the decorations belong to it, painted over an opaque surface; painting them here
    // too would put them under it and leave them to go stale.
    if (!m_panel)
        paintContent(painter, cr);
}

void K4PopupBase::paintCompactRowFrame(QPainter &painter, const QRect &contentRect,
                                       std::initializer_list<const QWidget *> delimitedWidgets, int rowSpacing) const {
    constexpr int DelimiterInset = 7;           // Gap between a delimiter's ends and the frame's top/bottom
    const int delimiterOffset = rowSpacing / 2; // Mid-gap, past the widget's inclusive right()

    painter.setBrush(K4Styles::buttonGradient(contentRect.top(), contentRect.bottom()));
    painter.setPen(QPen(K4Styles::borderColor(), 1));
    // WHY the half-pixel inset: a 1 px pen straddles the path, so on a rect that reaches the card's
    // own edge - which the content panel's rect does - the outer half would be clipped away.
    painter.drawRoundedRect(QRectF(contentRect).adjusted(0.5, 0.5, -0.5, -0.5), K4Styles::Dimensions::BorderRadiusLarge,
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
    Q_UNUSED(painter)
    Q_UNUSED(contentRect)
}
