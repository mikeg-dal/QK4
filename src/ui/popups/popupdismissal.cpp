#include "ui/popups/popupdismissal.h"

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWidget>

#include <utility>

namespace {
// Grace period after a hide during which justHidden() stays true, so the press that dismissed the
// popup by click-away is not read as a request to reopen it. Long enough to span one press→release.
constexpr int ReopenGuardMs = 250;
} // namespace

PopupDismissal::PopupDismissal(QWidget *popup, CloseFunction close, PressBelongs pressBelongs)
    : QObject(popup), m_popup(popup), m_close(std::move(close)), m_pressBelongs(std::move(pressBelongs)) {}

void PopupDismissal::watch(QWidget *anchor) {
    m_anchor = anchor;
    m_anchorWindow = anchor ? anchor->window() : nullptr;
    qApp->removeEventFilter(this); // a re-show must not install it twice
    qApp->installEventFilter(this);
    m_watching = true;
}

void PopupDismissal::stop() {
    // WHY only when watching: Qt also sends a hide event to a widget that was never shown (a child
    // hidden in its constructor), and that must not arm the guard against the first open.
    if (!m_watching)
        return;
    m_watching = false;
    qApp->removeEventFilter(this);
    m_hiddenTimer.restart();
}

bool PopupDismissal::justHidden() const {
    return m_hiddenTimer.isValid() && m_hiddenTimer.elapsed() < ReopenGuardMs;
}

bool PopupDismissal::isVisibleOrJustHidden() const {
    return m_popup->isVisible() || justHidden();
}

bool PopupDismissal::eventFilter(QObject *watched, QEvent *event) {
    // WHY an app-wide filter, not WindowDeactivate: the top-level popups are Qt::Tool windows
    // parented to the main window, so Qt keeps them "active" whenever the main window is active —
    // WindowDeactivate never fires for an in-app click — and the side overlays are not windows at
    // all. A position test against a global mouse press is the reliable way for both. The same
    // filter sees the anchor's and its window's events, so nothing has to remember to close a popup
    // when what it hangs from goes away.
    if (!m_popup->isVisible())
        return QObject::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick: {
        const QPoint globalPos = static_cast<QMouseEvent *>(event)->globalPosition().toPoint();
        // A window's frame for a top-level popup; a child widget's own rectangle, mapped out.
        const QRect area =
            m_popup->isWindow() ? m_popup->frameGeometry() : QRect(m_popup->mapToGlobal(QPoint(0, 0)), m_popup->size());
        if (!area.contains(globalPos) && !(m_pressBelongs && m_pressBelongs(globalPos)))
            m_close();
        break;
    }
    case QEvent::KeyPress:
        // Escape anywhere in the app, not only with the popup focused: a Qt::Tool window does not
        // always hold the keyboard. Not consumed, so Escape still reaches the main window too.
        if (static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape)
            m_close();
        break;
    case QEvent::Hide:
        if (watched == m_anchor || watched == m_anchorWindow)
            m_close();
        break;
    case QEvent::Move:
    case QEvent::Resize:
    case QEvent::WindowStateChange:
        // A top-level Qt::Tool window does not follow its parent: left open it would float where
        // the anchor used to be. A side overlay is placed over keys the window's layout may just
        // have moved. Minimise, maximise and fullscreen arrive as WindowStateChange.
        if (watched == m_anchorWindow)
            m_close();
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}
