#ifndef BANDNAVIGATIONCONTROLLER_H
#define BANDNAVIGATIONCONTROLLER_H

#include <QObject>

class RadioState;
class ConnectionController;
class PopupManager;

// Routes band-popup selections to the K4's BN / BN$ CAT commands. BSet
// selects which VFO the band change targets; a tap on the already-
// selected band invokes the K4 band stack (BN^ / BN$^) rather than a
// redundant band change.
//
// The current band per VFO is RadioState's (BN / BN$, reset on disconnect);
// this controller keeps the band popup's selected-band indicator on it.
class BandNavigationController : public QObject {
    Q_OBJECT

public:
    explicit BandNavigationController(RadioState *radioState, ConnectionController *connection,
                                      PopupManager *popupManager, QObject *parent = nullptr);

    // The radio's band number for the indicated VFO (RadioState::bandNumber / bandNumberB).
    // -1 until the radio reports one. RadioUtils::isXvtrBandNumber() names the XVTR bands.
    int currentBand(bool forVfoB = false) const;

signals:
    // Emitted when the K4's current band changes (BN / BN$). The
    // antenna-label and other UI controllers that need to know "are we on
    // XVTR" subscribe here. forVfoB distinguishes BN$ (true) from BN (false).
    void currentBandChanged(int band, bool forVfoB);

private slots:
    // Connected to PopupManager::bandSelected in constructor.
    void onBandSelected(const QString &bandName);

private:
    // Show the band of the VFO the popup targets (A when !BSet, B when BSet) as selected.
    void refreshSelectedBand();

private:
    RadioState *m_radioState;           // injected, not owned
    ConnectionController *m_connection; // injected, not owned
    PopupManager *m_popupManager;       // injected, not owned
};

#endif // BANDNAVIGATIONCONTROLLER_H
