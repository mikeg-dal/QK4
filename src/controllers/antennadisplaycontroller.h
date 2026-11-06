#ifndef ANTENNADISPLAYCONTROLLER_H
#define ANTENNADISPLAYCONTROLLER_H

#include <QObject>

class RadioState;
class BandNavigationController;
class QLabel;

// Observes RadioState::antennaChanged (AN/AR/AR$) and antennaNameChanged
// (ACN echo) and formats the three antenna labels — TX, RX Main, RX Sub —
// according to the K4's AR/AR$ value semantics (disconnected, XVTR, RX
// USES TX ANT, ATU RX ANT1-3, etc.). Labels remain MainWindow-owned and
// are passed in by pointer; the controller only updates their text.
//
// This is a presentation-only controller; no CAT is sent. Popup config
// stays with AntennaConfigController. See PATTERNS.md → Controller Pattern.
class AntennaDisplayController : public QObject {
    Q_OBJECT

public:
    explicit AntennaDisplayController(RadioState *radioState, BandNavigationController *bandNav, QLabel *txAntennaLabel,
                                      QLabel *rxAntALabel, QLabel *rxAntBLabel, QObject *parent = nullptr);

    // Clears all three antenna labels on K4 disconnect.
    void reset();

private slots:
    void refreshLabels();
    void onAntennaChanged(int txAnt, int rxAntMain, int rxAntSub);

private:
    RadioState *m_radioState;            // injected, not owned
    BandNavigationController *m_bandNav; // injected, not owned
    QLabel *m_txAntennaLabel;            // injected, not owned
    QLabel *m_rxAntALabel;               // injected, not owned
    QLabel *m_rxAntBLabel;               // injected, not owned
};

#endif // ANTENNADISPLAYCONTROLLER_H
