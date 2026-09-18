#ifndef STATUSBARCONTROLLER_H
#define STATUSBARCONTROLLER_H

#include <QObject>

class ConnectionController;
class ConfirmPopup;
class IconTextLabel;
class NetHealthWidget;
class NetworkMetrics;
class PowerStatusButton;
class QLabel;
class QTimer;
class QWidget;
class RadioState;

// Owns the top status bar: title, date/time clock, at-a-glance metric slots
// (temperature, voltage), KPA1500 status, K4 connection status, NetHealthWidget,
// and the K4 remote-power button on the right.
//
// Observes RadioState directly for voltage and power-state updates so MainWindow
// no longer needs to route those slots. The bar dropped its W / SWR / A readouts
// (redundant with the rest of the UI); future at-a-glance fields are added as
// IconTextLabel instances and wired to whatever RadioState signal feeds them.
//
// The power button on the right is the click target for the K4 remote power-off
// flow: a ConfirmPopup is shown above it, and on confirm the controller sends
// "PS0;" via the injected ConnectionController. The K4 closes the socket; the
// normal disconnected flow handles the cleanup.
//
// See PATTERNS.md → Controller Pattern.
class StatusBarController : public QObject {
    Q_OBJECT

public:
    StatusBarController(RadioState *radioState, ConnectionController *connectionController,
                        NetworkMetrics *networkMetrics, QWidget *parentWidget, QObject *parent = nullptr);
    ~StatusBarController() override;

    // Container widget — MainWindow adds this into its top layout.
    QWidget *widget() const;

    // Task-level API.
    void setTitle(const QString &text);
    void clearReadings();
    void setKpa1500Visible(bool visible);
    void setKpa1500Status(const QString &text, const QString &styleSheet);

    // Connection-state indicator transitions. Each sets the K4 status label
    // text and style appropriate for that state. Disconnected also resets
    // the title to its default. Used by MainWindow::updateConnectionState.
    void showDisconnected();
    void showConnecting();                       // amber bold — same style for Connecting + Authenticating
    void showConnected();                        // green bold
    void showError(const QString &errorMessage); // red bold — survives the disconnect that follows // red bold —
                                                 // "Error: <message>"

private:
    void onPowerButtonClicked();

    RadioState *m_radioState;                     // injected, not owned
    ConnectionController *m_connectionController; // injected, not owned
    QWidget *m_container;                         // owned via Qt parent (parentWidget)
    QLabel *m_titleLabel;
    QLabel *m_dateTimeLabel;
    IconTextLabel *m_lpaTempField;
    IconTextLabel *m_paTempField;
    IconTextLabel *m_voltageField;
    QLabel *m_connectionStatusLabel;
    QLabel *m_kpa1500StatusLabel;
    NetHealthWidget *m_netHealthWidget;
    PowerStatusButton *m_powerButton;
    QTimer *m_clockTimer;
    ConfirmPopup *m_confirmPopup = nullptr; // lazily constructed on first click

    // A connection error is on the label and must outlive the Disconnected update that follows it.
    // Cleared when the next attempt starts. See showDisconnected() for why this is needed.
    bool m_errorShown = false;

    void updateDateTime();
};

#endif // STATUSBARCONTROLLER_H
