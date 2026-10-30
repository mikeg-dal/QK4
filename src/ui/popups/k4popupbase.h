#ifndef K4POPUPBASE_H
#define K4POPUPBASE_H

#include <QElapsedTimer>
#include <QPointer>
#include <initializer_list>
#include <QWidget>

/**
 * @brief Base class for QK4 popup widgets.
 *
 * Provides centralized handling of:
 * - Window flags and translucent background setup
 * - Popup positioning above trigger buttons
 * - Drop shadow rendering
 * - Hide/close behavior with closed() signal
 * - Escape key handling
 *
 * Subclasses must implement:
 * - contentSize(): Return the size of the content area (buttons, controls)
 *
 * Subclasses should:
 * - Call initPopup() at the end of their constructor
 * - Use contentMargins() when setting up their layout
 * - Override paintContent() if they need custom painting beyond child widgets
 *
 * Example usage in subclass constructor:
 * @code
 * MyPopup::MyPopup(QWidget* parent) : K4PopupBase(parent) {
 *     auto* layout = new QVBoxLayout(this);
 *     layout->setContentsMargins(contentMargins());
 *     // ... add widgets to layout ...
 *     initPopup();  // Must be called last
 * }
 * @endcode
 */
class K4PopupBase : public QWidget {
    Q_OBJECT

public:
    explicit K4PopupBase(QWidget *parent = nullptr);

    /**
     * @brief Show the popup directly above @p anchor, its card centred horizontally on it.
     *
     * The card (the visible box inside the shadow margin) is centred on the anchor and its bottom
     * edge sits one ShadowMargin above the anchor's top edge, so only the shadow lies between them.
     * When the anchor is itself a K4PopupBase its card is the reference, not its transparent
     * window. Pass the widget the popup belongs over: the bottom menu bar for the bar's family, the
     * open button row for a row's sub-popup, the key itself for a lone key. Nothing but the screen's
     * available geometry moves the popup off that spot (see placeCard()).
     *
     * While shown the popup closes itself on: a mouse press outside it, Escape, the anchor being
     * hidden or destroyed, the anchor's window moving, resizing or changing state (minimise,
     * fullscreen), and the application going inactive.
     */
    void showAboveWidget(QWidget *anchor);

    /**
     * @brief Where the card goes: centred on @p anchor and directly above it.
     *
     * Pure geometry in global logical pixels, shared by every popup. Clamped to @p available only
     * where it would leave it: shifted sideways at a left/right screen edge, and put below the
     * anchor (same gap) when there is no room above, then kept on screen.
     *
     * @return the card's top-left corner; the window goes ShadowMargin up and left of it.
     */
    static QPoint placeCard(const QRect &anchor, const QSize &card, const QRect &available);

    /** @brief The visible card in global coordinates (excludes the transparent shadow margin). */
    QRect cardGlobalRect() const;

    /** @brief The widget the popup was last shown above, or null once it is destroyed. */
    QWidget *anchor() const { return m_anchor; }

    /**
     * @brief Hide the popup and emit closed() signal.
     */
    void hidePopup();

    /**
     * @brief True while shown, and for a brief grace period after hiding.
     *
     * Toggle handlers (e.g. PopupManager::toggleFn) must use this instead of
     * isVisible(): a click on the trigger button dismisses the popup via
     * click-away (window deactivation on mouse-down) before the button's
     * clicked() fires on mouse-up. Plain isVisible() would then read false
     * and the toggle would immediately reopen the popup.
     */
    bool isVisibleOrJustHidden() const;

    /**
     * @brief For a toggle key: close the popup and return true when it is open, or was just
     *        closed by this same press (click-away); return false when the key should open it.
     */
    bool closeIfOpenForToggle();

signals:
    /**
     * @brief Emitted when the popup is hidden.
     */
    void closed();

protected:
    /**
     * @brief Return the content size (width, height) excluding shadow margins.
     *
     * Subclasses must implement this to specify their content dimensions.
     * The content size should include:
     * - Button grid/row dimensions
     * - Content margins (typically PopupContentMargin on each side)
     *
     * @return Size of the content area
     */
    virtual QSize contentSize() const = 0;

    // The rectangle a popup is placed against: an anchor popup's card, any other widget's rect.
    static QRect anchorGlobalRect(const QWidget *anchor);

    /**
     * @brief Get the margins to use for the main layout.
     *
     * Returns margins that account for shadow space and content padding.
     * Use this when setting up the layout in the subclass constructor.
     *
     * @return QMargins for the layout's setContentsMargins()
     */
    QMargins contentMargins() const;

    /**
     * @brief Initialize the popup after subclass setup.
     *
     * Must be called at the end of the subclass constructor after
     * setting up the layout and child widgets. This calculates and
     * sets the final widget size. Call again whenever contentSize() changes, before the next show.
     */
    void initPopup();

    /**
     * @brief Get the content rectangle (inside shadow margins).
     *
     * Useful for custom painting in subclasses.
     *
     * @return Rectangle for the content area
     */
    QRect contentRect() const;

    /**
     * @brief Override to perform custom painting after background/shadow.
     *
     * Called by paintEvent() after drawing the shadow and background.
     * Default implementation does nothing.
     *
     * @param painter The painter to use
     * @param contentRect The content rectangle (inside shadow margins)
     */
    virtual void paintContent(QPainter &painter, const QRect &contentRect);

    // Single-row popups (title | controls | value | -/+ | close) share one content height and margin.
    static constexpr int CompactRowContentHeight = 52;
    static constexpr int CompactRowContentMargin = 12;

    /**
     * @brief Paint a single-row popup's frame: button gradient, border, and a vertical
     *        delimiter after each visible widget in @p delimitedWidgets.
     *
     * For use from paintContent(). @p rowSpacing is the row layout's spacing, so each
     * delimiter falls in the middle of the gap to the right of its widget.
     */
    void paintCompactRowFrame(QPainter &painter, const QRect &contentRect,
                              std::initializer_list<const QWidget *> delimitedWidgets, int rowSpacing = 6) const;

    // Standard Qt event handlers
    void paintEvent(QPaintEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    // Application-wide filter, installed on qApp while shown and removed on hide: closes the popup
    // on a press outside it, Escape, and the anchor or its window going away (see showAboveWidget).
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // Restarted on every hide; powers isVisibleOrJustHidden().
    QElapsedTimer m_hiddenTimer;
    bool pressIsInAChildPopup(const QPoint &globalPos) const;
    QPointer<QWidget> m_anchor;       // what the popup is shown above; it closes when this goes
    QPointer<QWidget> m_anchorWindow; // the anchor's top-level window, watched for move/resize/state
};

#endif // K4POPUPBASE_H
