#ifndef MENUCONTROLLER_H
#define MENUCONTROLLER_H

#include <QObject>

class ConnectionController;
class RadioState;
class SpectrumController;
class MenuModel;
class MenuOverlayWidget;
class QWidget;

// Owns the K4 MEDF menu system: the MenuModel (menu item cache), the
// MenuOverlayWidget (full-screen menu browser shown over the spectrum),
// and the discovery-state sentinels for a few special menu items whose
// IDs the K4 reports dynamically (Mouse L/R QSY, FSK Mark-Tone, Spectrum
// Amplitude Units).
//
// MainWindow wires the bottom menu bar (menuClicked → toggleOverlay) and
// routes MEDF/ME through the ingestMedf() / ingestMe() task methods below;
// the model is kept private.
//
// See PATTERNS.md → Controller Pattern.
class MenuController : public QObject {
    Q_OBJECT

public:
    // radioState receives the XVTR band setup the menu changes (see mirrorXvtrItem); it may be null.
    explicit MenuController(ConnectionController *connection, RadioState *radioState, SpectrumController *spectrum,
                            QWidget *parentWidget, QObject *parent = nullptr);

    // CAT routing entry points used by MainWindow::onCatResponse. Each
    // forwards into the encapsulated MenuModel. Returning void (the model's
    // bool return is discarded by all current callers).
    void ingestMedf(const QString &cmd);
    void ingestMe(const QString &cmd);
    void clearModel();

    // One menu item's current value. Deliberately narrower than handing out the MenuModel: the
    // only outside consumer needs a single number, and a getter for the whole model invites
    // reaching into it. Returns false when the menu has not been read yet (no radio) or the id
    // is unknown.
    bool menuValue(int menuId, int *valueOut) const;

    // Task-level overlay API. Positions the overlay above the spectrum
    // container (resolved internally from the injected SpectrumController).
    void toggleOverlay();
    void closeOverlay();
    bool isOverlayVisible() const;

    // MainWindow::onRadioReady adds a synthetic Display FPS menu item
    // (value updates from radio echoes). Exposed as a task-level method.
    void addSyntheticDisplayFpsItem(int initialFps);

    // RadioState::displayFpsChanged comes from a MEDF#-prefix handler and
    // needs to propagate to the synthetic menu item so the overlay shows
    // the live value. MainWindow keeps the RadioState signal connection;
    // forwards to this setter.
    void setDisplayFps(int fps);

    // Menu overlay geometry (relative to parent) is derived from the
    // spectrum container. Called by toggleOverlay().
    void repositionOverlay();

signals:
    // A menu item's value changed, from the radio or from us. Re-emitted from MenuModel so
    // consumers do not need the model itself.
    void menuValueChanged(int menuId, int newValue);

    // Overlay was hidden (by escape, click-outside, or a button dismiss).
    // MainWindow connects this to BottomMenuBar::setMenuActive(false).
    void overlayClosed();

private:
    ConnectionController *m_connection; // injected, not owned
    RadioState *m_radioState;           // injected, not owned; may be null
    SpectrumController *m_spectrum;     // injected, not owned
    QWidget *m_parentWidget;            // used for overlay geometry mapping

    MenuModel *m_menuModel;           // owned: plain QObject, deleted via this
    MenuOverlayWidget *m_menuOverlay; // owned via Qt parent-ownership (parentWidget)

    // Sentinel-initialized so we can detect "menu ID not yet discovered".
    // When the K4 advertises these items in MEDF, onMenuItemAdded stores
    // the ID so onMenuModelValueChanged can recognize later edits.
    int m_mouseQsyMode = 0;
    int m_mouseQsyMenuId = -999;
    int m_fskMarkToneMenuId = -999;

    void onMenuValueChangeRequested(int menuId, const QString &action);
    // A value typed into the menu, already in the item's stored units and range.
    void onMenuValueSetRequested(int menuId, int value);
    void onMenuModelValueChanged(int menuId, int newValue);
    // WHY: the band popup's XVTR keys read RadioState's band setup, which is filled from the radio's
    // XVN/XVR/... reports. At AI4 the K4 does not report a change QK4 made, so a menu edit to "XVTR Band
    // n R.F." left the key showing the old frequency. Every per-band XVTR menu value is copied there.
    void mirrorXvtrItem(int menuId, int value);
    void onMenuItemAdded(int menuId);
};

#endif // MENUCONTROLLER_H
