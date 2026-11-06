#ifndef TXSTATECONTROLLER_H
#define TXSTATECONTROLLER_H

#include <QObject>

class RadioState;
class SideControlPanel;
class VfoFrequencyController;
class VFOWidget;
class QLabel;

// Observes RadioState's transmit-state and TX-meter signals and renders
// the downstream TX UI:
//
//   - transmitStateChanged → flips the active-TX VFO's multifunction
//     meter into Po/ALC/COMP/SWR mode (the other VFO stays on S-meter).
//     Colors the TX indicator + triangles red during TX, amber at RX.
//     Kicks VfoFrequencyController to re-render the TX VFO frequency
//     when XIT is active (shown freq = dial + XIT offset).
//
//   - txMeterChanged → routes meter tuple to the active TX VFO's meter
//     and to the side-panel forward-power display. PA drain current Id
//     comes from RadioState::paDrainCurrent (SIRF, smoothed at TM rate).
//
// Split-state dispatch: SPLIT OFF → VFO A transmits; SPLIT ON → VFO B.
class TxStateController : public QObject {
    Q_OBJECT

public:
    explicit TxStateController(RadioState *radioState, SideControlPanel *sideControlPanel,
                               VfoFrequencyController *vfoFrequencyController, VFOWidget *vfoA, VFOWidget *vfoB,
                               QLabel *txIndicator, QLabel *txTriangle, QLabel *txTriangleB, QObject *parent = nullptr);

    // Resets the TX triangles to their RX-state default on K4 disconnect.
    void reset();

private slots:
    void onTransmitStateChanged(bool transmitting);
    void onTxMeterChanged(int alc, int comp, double fwdPower, double swr);

private:
    RadioState *m_radioState;                         // injected, not owned
    SideControlPanel *m_sideControlPanel;             // injected, not owned
    VfoFrequencyController *m_vfoFrequencyController; // injected, not owned
    VFOWidget *m_vfoA;                                // injected, not owned
    VFOWidget *m_vfoB;                                // injected, not owned
    QLabel *m_txIndicator;                            // injected, not owned
    QLabel *m_txTriangle;                             // injected, not owned
    QLabel *m_txTriangleB;                            // injected, not owned
};

#endif // TXSTATECONTROLLER_H
