#ifndef BOTTOMMENUBAR_H
#define BOTTOMMENUBAR_H

#include <QPushButton>
#include <QTimer>
#include <QWidget>

/**
 * BottomMenuBar - Horizontal menu bar at bottom of QK4
 *
 * Contains 7 menu buttons: MENU, Fn, DISPLAY, BAND, MAIN RX, SUB RX, TX
 * Plus PTT button at far right with stretch separator.
 * (Icon buttons moved to SideControlPanel)
 *
 * Each menu button triggers a popup menu or panel when pressed.
 * PTT uses press/release for momentary microphone activation.
 * Styled with subtle rounded edges, gradient background, white border.
 */
class BottomMenuBar : public QWidget {
    Q_OBJECT

public:
    explicit BottomMenuBar(QWidget *parent = nullptr);
    ~BottomMenuBar() = default;

    // Getters for button positioning (for popup placement)
    QPushButton *bandButton() const { return m_bandBtn; }
    QPushButton *displayButton() const { return m_displayBtn; }
    QPushButton *fnButton() const { return m_fnBtn; }
    QPushButton *mainRxButton() const { return m_mainRxBtn; }
    QPushButton *subRxButton() const { return m_subRxBtn; }
    QPushButton *txButton() const { return m_txBtn; }
    QPushButton *pttButton() const { return m_pttBtn; }

public slots:
    void setMenuActive(bool active);    // Toggle MENU button inverse colors
    void setDisplayActive(bool active); // Toggle DISPLAY button inverse colors
    void setBandActive(bool active);    // Toggle BAND button inverse colors
    void setFnActive(bool active);      // Toggle Fn button inverse colors
    void setMainRxActive(bool active);  // Toggle MAIN RX button inverse colors
    void setSubRxActive(bool active);   // Toggle SUB RX button inverse colors
    void setTxActive(bool active);      // Toggle TX button inverse colors
    // Paint the PTT button. Driven ONLY by TransmitController::transmittingChanged - never by a
    // click on this widget, because a click is a request and requests can be refused (APP-005).
    void setPttActive(bool active);

    // Whether the operator's right-click latch actually took. MainWindow answers this from
    // TransmitController::engage()'s return value; the widget must not assume it did.
    void setPttLatched(bool latched);

signals:
    void menuClicked();
    void fnClicked();
    void displayClicked();
    void bandClicked();
    void mainRxClicked();
    void subRxClicked();
    void txClicked();
    void pttPressed();  // PTT button pressed (start TX audio)
    void pttReleased(); // PTT button released (stop TX audio)

    // Right-click latch requested (true) or given up (false). Separate from pttPressed/pttReleased
    // so the handler can report back whether the latch was granted.
    void pttLatchRequested(bool latched);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setupUi();
    QPushButton *createMenuButton(const QString &text);

    // Menu buttons
    QPushButton *m_menuBtn;
    QPushButton *m_fnBtn;
    QPushButton *m_displayBtn;
    QPushButton *m_bandBtn;
    QPushButton *m_mainRxBtn;
    QPushButton *m_subRxBtn;
    QPushButton *m_txBtn;
    QPushButton *m_pttBtn;

    bool m_pttLocked = false;
    QTimer *m_pttLockTimer = nullptr;
};

#endif // BOTTOMMENUBAR_H
