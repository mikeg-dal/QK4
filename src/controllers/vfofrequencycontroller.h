#ifndef VFOFREQUENCYCONTROLLER_H
#define VFOFREQUENCYCONTROLLER_H

#include <QObject>

class RadioState;
class ConnectionController;
class VFOWidget;

// Observes RadioState frequency changes and renders the dial-frequency
// display on VFO A and VFO B, applying the RIT/XIT offset so the shown
// frequency matches what the K4 is actually receiving or transmitting.
//
// Display rules (per VFO):
//   - Transmitting with XIT and no split: A shows dial+XIT offset
//   - Transmitting with XIT and split: B shows dial+XIT offset (XIT is in RO$)
//   - RIT enabled: A shows dial+RIT offset (RO)
//   - RIT enabled on B (split/BSET): B shows dial+RO$
//   - Otherwise: raw dial frequency
//
// refresh() is public so RitXitController and MainWindow's transmitting
// observer can force a re-render when RIT/XIT state or TX state toggles
// without the dial frequency itself having changed.
class VfoFrequencyController : public QObject {
    Q_OBJECT

public:
    explicit VfoFrequencyController(RadioState *radioState, ConnectionController *connection, VFOWidget *vfoA,
                                    VFOWidget *vfoB, QObject *parent = nullptr);
    ~VfoFrequencyController() override;

    void refresh();     // recompute both VFOs
    void refreshVfoA(); // recompute VFO A only
    void refreshVfoB(); // recompute VFO B only

    // FREQ ENT: cancel an entry already open on either VFO, otherwise open one on VFO A, or on VFO B
    // when B SET is engaged.
    void toggleFrequencyEntry();

private slots:
    void onFrequencyChanged(quint64 freq);
    void onFrequencyBChanged(quint64 freq);

private:
    // A clicked digit becomes that VFO's tuning rate: VT for VFO A, VT$ for VFO B, whatever B SET says.
    void setTuningRateFromDigit(bool vfoB, int digitFromRight);

    RadioState *m_radioState;           // injected, not owned
    ConnectionController *m_connection; // injected, not owned
    VFOWidget *m_vfoA;                  // injected, not owned
    VFOWidget *m_vfoB;                  // injected, not owned
};

#endif // VFOFREQUENCYCONTROLLER_H
