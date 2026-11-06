#ifndef SIDECONTROLDISPLAYCONTROLLER_H
#define SIDECONTROLDISPLAYCONTROLLER_H

#include <QObject>

class RadioState;
class SideControlPanel;

// Mirrors RadioState knob / filter / mode values onto the left-side
// control panel display (bandwidth, shift, high cut, low cut, power,
// mic gain, compression, keyer speed, CW pitch, QSK delay, RF gain,
// squelch for both VFO A and VFO B). Also flips the panel's CW vs
// voice/data display mode on modeChanged.
//
// BW/SHIFT rendering is BSET-aware: when BSET is on, the displayed
// values reflect VFO B's filter state (sub RX filter settings).
// HI/LO cuts are derived from BW + shift; shown in kHz.
class SideControlDisplayController : public QObject {
    Q_OBJECT

public:
    explicit SideControlDisplayController(RadioState *radioState, SideControlPanel *panel, QObject *parent = nullptr);

private slots:
    void refreshFilterDisplay();
    void onModeChanged();

private:
    RadioState *m_radioState;  // injected, not owned
    SideControlPanel *m_panel; // injected, not owned
};

#endif // SIDECONTROLDISPLAYCONTROLLER_H
