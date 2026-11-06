#ifndef POPUPDISMISSAL_H
#define POPUPDISMISSAL_H

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>

#include <functional>

class QWidget;
class QPoint;

/**
 * @brief The one rule for when a QK4 popup closes by itself, shared by every kind of popup.
 *
 * While watching, an application-wide event filter closes the popup on:
 * - a mouse press outside it (unless the press "belongs" to it, see the constructor),
 * - Escape anywhere in the application,
 * - its anchor, or the anchor's top-level window, being hidden,
 * - that window moving, resizing or changing state (minimise, maximise, fullscreen).
 *
 * Nothing is consumed: the press that closes the popup still reaches what it landed on, so clicking
 * another control works in the same gesture. When that control is the popup's own toggle key, the
 * key must not read the popup as closed and open it again: toggles ask isVisibleOrJustHidden().
 *
 * Used by K4PopupBase (top-level Qt::Tool popups) and SideControlOverlay (child widgets over the
 * side panel's knob keys), so both close the same way.
 */
class PopupDismissal : public QObject {
    Q_OBJECT

public:
    using CloseFunction = std::function<void()>;
    // True when a press outside the popup is still the popup's (a press on a popup opened from it).
    using PressBelongs = std::function<bool(const QPoint &globalPos)>;

    /**
     * @param popup  the widget that closes; also this object's parent
     * @param close  how to close it (the popup's own hide path, so its closed() signal fires)
     * @param pressBelongs optional: presses outside @p popup for which it stays open
     */
    PopupDismissal(QWidget *popup, CloseFunction close, PressBelongs pressBelongs = {});

    /** @brief Start watching; @p anchor (may be null) and its window close the popup when they go. */
    void watch(QWidget *anchor);

    /** @brief Stop watching and start the reopen guard. Call when the popup is hidden. */
    void stop();

    /** @brief True for a short grace period after stop(): the press that closed it is not a reopen. */
    bool justHidden() const;

    /** @brief The popup is shown, or was closed by the press now in progress (see justHidden()). */
    bool isVisibleOrJustHidden() const;

    /** @brief The anchor passed to watch(), or null once it is destroyed. */
    QWidget *anchor() const { return m_anchor; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *const m_popup;
    const CloseFunction m_close;
    const PressBelongs m_pressBelongs;
    QPointer<QWidget> m_anchor;       // what the popup belongs to; it closes when this goes
    QPointer<QWidget> m_anchorWindow; // the anchor's top-level window, watched for move/resize/state
    QElapsedTimer m_hiddenTimer;      // restarted by stop(); powers justHidden()
    bool m_watching = false;          // between watch() and stop()
};

#endif // POPUPDISMISSAL_H
