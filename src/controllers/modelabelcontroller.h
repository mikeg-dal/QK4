#ifndef MODELABELCONTROLLER_H
#define MODELABELCONTROLLER_H

#include <QObject>

class RadioState;
class QLabel;

// Owns the VFO A / VFO B mode label text. Observes RadioState's mode,
// data-submode, and ESSB signals and re-renders both labels as a pair
// (the ESSB "+" suffix only applies to USB/LSB, but it applies equally
// to whichever VFO is in that mode).
//
// The labels belong to VfoRowWidget; this controller only sets their text.
// VFO B's SUB RX dimming is SubDivIndicatorController's.
class ModeLabelController : public QObject {
    Q_OBJECT

public:
    explicit ModeLabelController(RadioState *radioState, QLabel *modeALabel, QLabel *modeBLabel,
                                 QObject *parent = nullptr);

    // Recompute + set both labels from current RadioState.
    void refresh();

    // Clears both labels on disconnect. RadioState::reset() does not emit
    // signals, so controllers must explicitly re-render.
    void reset();

private:
    RadioState *m_radioState; // injected, not owned
    QLabel *m_modeALabel;     // injected, not owned
    QLabel *m_modeBLabel;     // injected, not owned
};

#endif // MODELABELCONTROLLER_H
