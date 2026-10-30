#ifndef MEMORYBUTTONSCONTROLLER_H
#define MEMORYBUTTONSCONTROLLER_H

#include <QObject>

class ConnectionController;
class QPushButton;

// Owns the wiring for the K4 message memory buttons that sit just under
// the VFO row: M1-M4 (CW/voice/data message recall), REC (record), STORE
// (store / save), RCL (recall). Each has a left-click action (the K4's
// primary SW command) and a right-click action installed via an event
// filter on the controller. M1-M4 send the K4's hold commands to repeat;
// REC/STORE/RCL send BANK / AF REC / AF PLAY.
//
// Extracted from MainWindow as part of the Phase A architectural pass —
// the eventFilter cases for these three buttons used to live in
// MainWindow::eventFilter alongside unrelated cases, breaking the
// principle that MainWindow only routes events to controllers (not
// implements them).
class MemoryButtonsController : public QObject {
    Q_OBJECT

public:
    MemoryButtonsController(ConnectionController *connection, QPushButton *m1, QPushButton *m2, QPushButton *m3,
                            QPushButton *m4, QPushButton *rec, QPushButton *store, QPushButton *rcl,
                            QObject *parent = nullptr);

protected:
    // Right-click sends the hold/alternate SW command; other events fall through.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    ConnectionController *m_connection; // injected, not owned
    QPushButton *m_m1Btn;               // not owned (parent-managed)
    QPushButton *m_m2Btn;               // not owned
    QPushButton *m_m3Btn;               // not owned
    QPushButton *m_m4Btn;               // not owned
    QPushButton *m_recBtn;              // not owned
    QPushButton *m_storeBtn;            // not owned
    QPushButton *m_rclBtn;              // not owned
};

#endif // MEMORYBUTTONSCONTROLLER_H
