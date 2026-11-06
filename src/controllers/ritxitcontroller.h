#ifndef RITXITCONTROLLER_H
#define RITXITCONTROLLER_H

#include <QObject>

#include "utils/wheelaccumulator.h"

class RadioState;
class ConnectionController;
class SpectrumController;
class QLabel;
class QWheelEvent;

// Owns the RIT/XIT label trio (RIT, XIT, offset value) and the K4-specific
// RIT/XIT behavior: BSET-aware display routing, split-mode XIT offset
// register quirks (XIT in RO$ when split is on), and SW54 vs RT/; for
// the RIT click (SW54 is the K4's BSET-aware toggle).
//
// The wheel accumulator for the RIT/XIT box lives here too — MainWindow
// forwards wheel events via handleWheel(). RU/RD vs RU$/RD$ selection is
// BSET + XIT aware (see docs/k4-protocol-quirks.md §2).
//
// Emits displayRefreshRequested whenever the displayed RIT/XIT offset
// changes — VfoFrequencyController::refresh() re-renders the VFO
// frequencies (which apply the RIT offset).
class RitXitController : public QObject {
    Q_OBJECT

public:
    explicit RitXitController(RadioState *radioState, ConnectionController *connection, SpectrumController *spectrum,
                              QLabel *ritLabel, QLabel *xitLabel, QLabel *ritXitValueLabel, QObject *parent = nullptr);

    // Resets the RIT/XIT labels to their disconnected look: value "+0.00",
    // all three drawn inactive exactly as applyDisplay() draws RIT and XIT
    // off. Also run by the constructor, so launch and disconnect match.
    // Touches only the labels: no displayRefreshRequested, no spectrum update.
    void reset();

    // Event-handler entry points called from MainWindow::eventFilter.
    // Each returns true if the event was consumed.
    bool handleRitLabelClick();
    bool handleXitLabelClick();
    bool handleWheel(QWheelEvent *event);

signals:
    // Emitted whenever the displayed RIT/XIT offset or enabled-state
    // changes. VfoFrequencyController refreshes the VFO frequency display
    // (since displayed frequency depends on RIT offset).
    void displayRefreshRequested();

private:
    void applyDisplay(bool ritEnabled, bool xitEnabled, int offset);
    void applyLabels(bool ritEnabled, bool xitEnabled, int offset);

    RadioState *m_radioState;           // injected, not owned
    ConnectionController *m_connection; // injected, not owned
    SpectrumController *m_spectrum;     // injected, not owned — passband + TX-marker refresh
    QLabel *m_ritLabel;                 // injected, not owned
    QLabel *m_xitLabel;                 // injected, not owned
    QLabel *m_ritXitValueLabel;         // injected, not owned
    WheelAccumulator m_wheelAccumulator;
};

#endif // RITXITCONTROLLER_H
