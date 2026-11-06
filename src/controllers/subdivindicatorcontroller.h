#ifndef SUBDIVINDICATORCONTROLLER_H
#define SUBDIVINDICATORCONTROLLER_H

#include <QObject>

class RadioState;
class SpectrumController;
class VFOWidget;
class QLabel;

// Observes RadioState's subRxEnabled and diversityEnabled signals and
// renders the SUB / DIV badge styles + the VFO B dim-state (frequency
// display color + mode label color). Also calls SpectrumController to
// auto-hide mini pan B when SUB RX turns off.
//
// Cross-field logic: the DIV badge is green only when BOTH diversity AND sub
// RX are enabled (K4 constraint — DIV requires SUB), so both handlers need to
// consult the other signal's current state.
class SubDivIndicatorController : public QObject {
    Q_OBJECT

public:
    explicit SubDivIndicatorController(RadioState *radioState, SpectrumController *spectrum, VFOWidget *vfoB,
                                       QLabel *subLabel, QLabel *divLabel, QLabel *modeBLabel,
                                       QObject *parent = nullptr);

    // Forces SUB/DIV badges to disabled styling and dims VFO B (also styles
    // the VFO B mode label and sets the frequency-display color) on K4
    // disconnect. Same visual state as "SUB RX off, DIV off".
    void reset();

private slots:
    void onSubRxEnabledChanged(bool enabled);
    void onDiversityChanged(bool enabled);

private:
    void setSubLabelActive(bool active);
    void setDivLabelActive(bool active);
    void setVfoBDimmed(bool dimmed);

    RadioState *m_radioState;       // injected, not owned
    SpectrumController *m_spectrum; // injected, not owned
    VFOWidget *m_vfoB;              // injected, not owned
    QLabel *m_subLabel;             // injected, not owned
    QLabel *m_divLabel;             // injected, not owned
    QLabel *m_modeBLabel;           // injected, not owned
};

#endif // SUBDIVINDICATORCONTROLLER_H
