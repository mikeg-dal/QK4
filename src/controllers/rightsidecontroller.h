#ifndef RIGHTSIDECONTROLLER_H
#define RIGHTSIDECONTROLLER_H

#include <QObject>

class RadioState;
class ConnectionController;
class RightSidePanel;
class ModePopupController;
class FeatureMenuController;
class MacroController;
class QWidget;

// Owns the signal-to-CAT wiring for RightSidePanel. Every primary (left-click)
// and secondary (right-click) button on the right-side panel routes through
// here — direct CAT dispatch for SW commands, plus delegations to
// ModePopupController (MODE button), FeatureMenuController (PRE/NB/NR/NTCH/FIL
// right-click → feature menus), and MacroController (PF1-4 right-clicks fall
// through to user-configured macros).
//
// Extracted from MainWindow as part of the Phase 6 → Phase A architectural
// pass. Mirror of SideControlScrollController for RightSidePanel.
//
// B-SET-aware logic lives here:
//   - APF toggle (FIL right-click) routes AP/; vs AP$/; based on bSet state
//   - RATE cycling (RATE left-click) uses VT vs VT$ based on bSet state
//   - KHZ jump (RATE right-click) uses VT3; vs VT$3; based on bSet state
//   - FREQ ENT is re-emitted as frequencyEntryRequested; VfoFrequencyController picks the VFO
//
// Also owns cycleFilterPreset, driven by clicks on the FIL1/2/3 indicators under each VFO.
class RightSideController : public QObject {
    Q_OBJECT

public:
    RightSideController(RadioState *radioState, ConnectionController *connection, RightSidePanel *panel,
                        ModePopupController *modePopup, FeatureMenuController *featureMenu,
                        MacroController *macroController, QWidget *featureAnchor, QObject *parent = nullptr);
    ~RightSideController() override;

    // Step one VFO's filter preset 1 -> 3 -> 2 -> 1 with FP / FP$. Unlike the FIL button (SW33), this
    // names the VFO explicitly, so it never depends on which VFO the radio considers selected.
    void cycleFilterPreset(bool vfoB);

signals:
    // FREQ ENT pressed. MainWindow routes this to VfoFrequencyController, which is constructed after
    // this controller, so it cannot be injected here.
    void frequencyEntryRequested();

private:
    RadioState *m_radioState;             // injected, not owned
    ConnectionController *m_connection;   // injected, not owned
    RightSidePanel *m_panel;              // injected, not owned
    ModePopupController *m_modePopup;     // injected, not owned
    FeatureMenuController *m_featureMenu; // injected, not owned
    MacroController *m_macroController;   // injected, not owned
    QWidget *m_featureAnchor;             // anchor widget for popup positioning; not owned
};

#endif // RIGHTSIDECONTROLLER_H
