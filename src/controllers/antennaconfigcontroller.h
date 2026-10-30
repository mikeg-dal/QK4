#ifndef ANTENNACONFIGCONTROLLER_H
#define ANTENNACONFIGCONTROLLER_H

#include <QObject>

class RadioState;
class ConnectionController;
class AntennaCfgPopupWidget;
class QWidget;

// Owns the three antenna-configuration popups (Main RX, Sub RX, TX) and
// keeps their displayed mask / display-all / per-antenna names in sync
// with RadioState. MainWindow retains the three antenna labels in the
// VFO row (m_rxAntALabel, m_txAntennaLabel, m_rxAntBLabel) and their
// onAntennaChanged / onAntennaNameChanged slots — this controller's job
// is the popup surface, not the label surface. Labels may migrate in a
// future pass (direct observation from VfoRowWidget).
//
// See PATTERNS.md → Controller Pattern.
class AntennaConfigController : public QObject {
    Q_OBJECT

public:
    explicit AntennaConfigController(RadioState *radioState, ConnectionController *connection, QWidget *parentWidget,
                                     QObject *parent = nullptr);

    // Task-level toggle API: opens centred directly above the anchor (the
    // open button-row popup), or closes when the ANT CFG key is pressed
    // again. The caller passes the anchor in because it lives outside
    // this controller's ownership; the popup closes when the anchor hides.
    void showMainRxPopupAbove(QWidget *trigger);
    void showSubRxPopupAbove(QWidget *trigger);
    void showTxPopupAbove(QWidget *trigger);

    // Hide all three popups (MainWindow's closeAllPopups and the disconnect reset).
    void closeAll();

private:
    static void toggleAbove(AntennaCfgPopupWidget *popup, QWidget *anchor);

    RadioState *m_radioState;           // injected, not owned
    ConnectionController *m_connection; // injected, not owned

    AntennaCfgPopupWidget *m_mainRxPopup; // owned via Qt parent-ownership (parentWidget)
    AntennaCfgPopupWidget *m_subRxPopup;
    AntennaCfgPopupWidget *m_txPopup;
};

#endif // ANTENNACONFIGCONTROLLER_H
