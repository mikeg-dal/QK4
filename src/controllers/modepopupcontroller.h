#ifndef MODEPOPUPCONTROLLER_H
#define MODEPOPUPCONTROLLER_H

#include <QObject>

class RadioState;
class ConnectionController;
class ModePopupWidget;
class QWidget;

// Owns the ModePopupWidget (the grid of mode buttons shown above the
// bottom menu bar) and keeps it in sync with RadioState. The popup is
// opened from three places: (1) RightSideController's MODE button — uses
// current BSet state to target VFO A or VFO B, (2) and (3) MainWindow's
// clicks on the VFO A / VFO B mode label or square. All three routes target the same
// single popup instance — the popup's "BSet enabled" flag selects A or
// B before display. See PATTERNS.md → Controller Pattern.
//
// User mode selections ride CAT (MD / MD$ / DT / DT$). K4 doesn't echo
// DT SET commands, so the controller optimistically updates RadioState's
// dataSubMode / dataSubModeB when a DT-bearing command is dispatched.
class ModePopupController : public QObject {
    Q_OBJECT

public:
    explicit ModePopupController(RadioState *radioState, ConnectionController *connection, QWidget *parentWidget,
                                 QObject *parent = nullptr);

    // Task-level toggle entry points. Each captures current-visibility
    // first, then hides OR shows with the target-VFO preset.
    void toggleForVfoA(QWidget *anchor); // click on VFO-A label / square
    void toggleForVfoB(QWidget *anchor); // click on VFO-B label / square
    void toggleForBSet(QWidget *anchor); // RightSidePanel MODE button — uses RadioState::bSetEnabled

    void close();
    bool isVisible() const;

private:
    RadioState *m_radioState;           // injected, not owned
    ConnectionController *m_connection; // injected, not owned
    ModePopupWidget *m_popup;           // owned via Qt parent-ownership (parentWidget)

    void showForVfoA(QWidget *anchor);
    void showForVfoB(QWidget *anchor);
};

#endif // MODEPOPUPCONTROLLER_H
