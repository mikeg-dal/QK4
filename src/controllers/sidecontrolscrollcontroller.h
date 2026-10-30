#ifndef SIDECONTROLSCROLLCONTROLLER_H
#define SIDECONTROLSCROLLCONTROLLER_H

#include <QObject>

class RadioState;
class ConnectionController;
class SideControlPanel;

// Owns the scroll-handler routing for SideControlPanel: WPM, pitch, mic gain,
// compression, power (stepping within the radio's L/H/X range), delay (mode-aware SD), filter
// bandwidth, HI/LO cut (paired BW + IS math with mode-specific data-submode
// limits), shift, RF gain (Main + Sub), squelch (Main + Sub).
//
// Each handler clamps the new value to the K4's documented range, dispatches
// the appropriate CAT command, and optimistically updates RadioState since
// most of these commands are silent SETs (no echo from the K4). The HI/LO
// cut paths share applyFilterEdgeChange() — the audit flagged the previous
// inline-in-MainWindow versions as structurally identical with ~50 LOC of
// duplicated arithmetic.
//
// Extracted from MainWindow as part of the Phase 6 structural audit.
class SideControlScrollController : public QObject {
    Q_OBJECT

public:
    SideControlScrollController(RadioState *radioState, ConnectionController *connection, SideControlPanel *panel,
                                QObject *parent = nullptr);

private:
    // Shared between highCutChanged and lowCutChanged. When adjustHi is true,
    // the upper filter edge moves (HI cut); when false, the lower edge moves
    // (LO cut). Both update BW + IS together, respecting mode-specific limits
    // and the IS-locked flag for FSK-D / PSK-D data submodes.
    void applyFilterEdgeChange(bool adjustHi, int delta);

    RadioState *m_radioState;           // injected, not owned
    ConnectionController *m_connection; // injected, not owned
    SideControlPanel *m_panel;          // injected, not owned
};

#endif // SIDECONTROLSCROLLCONTROLLER_H
