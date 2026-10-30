#ifndef TEXTDECODECONTROLLER_H
#define TEXTDECODECONTROLLER_H

#include <QObject>

class RadioState;
class ConnectionController;
class TextDecodeWindow;
class QWidget;

// Owns the two floating text-decode windows (Main RX + Sub RX), their CAT
// dispatch keeping the K4 in sync with user-driven config changes, and the
// RadioState observers that keep the windows' operating mode and data rate
// in sync with the radio. Replaces ~180 lines of inline wiring + two
// ~30-line popup-button handler blocks in MainWindow.
//
// See PATTERNS.md → Controller Pattern. Constructor takes RadioState and
// ConnectionController by pointer (not owned); parentWidget is used as the
// Qt parent for the two TextDecodeWindow instances so Qt's parent-based
// deletion handles their lifetime.
class TextDecodeController : public QObject {
    Q_OBJECT

public:
    explicit TextDecodeController(RadioState *radioState, ConnectionController *connection, QWidget *parentWidget,
                                  QObject *parent = nullptr);

    // Task-level API — show the window configured for the current radio
    // mode + data sub-mode, enable decode if not already enabled.
    // Called from the Main RX / Sub RX popup "TEXT DECODE" button handlers.
    void showMainRx();
    void showSubRx();

private:
    void closeWithoutSending(TextDecodeWindow *window);
    RadioState *m_radioState;           // injected, not owned
    ConnectionController *m_connection; // injected, not owned
    TextDecodeWindow *m_mainWindow;     // owned via Qt parent mechanism
    TextDecodeWindow *m_subWindow;      // owned via Qt parent mechanism

    void sendTextDecodeCmd(TextDecodeWindow *window, bool isMainRx);
};

#endif // TEXTDECODECONTROLLER_H
