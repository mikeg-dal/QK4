#ifndef BUTTONROWDISPATCHER_H
#define BUTTONROWDISPATCHER_H

#include <QObject>

class RadioState;
class ConnectionController;
class PopupManager;
class AntennaConfigController;
class TextDecodeController;

// Routes click / right-click events from the Main RX, Sub RX, and TX
// button-row popups (7 buttons each) to their appropriate CAT command,
// secondary popup (EQ, antenna, line, mic, VOX, SSB BW, keying weight),
// or mode-dependent behavior. TX row is mode-aware — buttons 5 and 6
// dispatch differently in CW (paddle / keying weight) vs voice-data
// (SSB BW / ESSB).
//
// Purely signal-to-slot plumbing: no public API beyond construction.
// PopupManager emits the six click signals; the dispatcher forwards
// them to the right destination. See PATTERNS.md → Controller Pattern.
class ButtonRowDispatcher : public QObject {
    Q_OBJECT

public:
    explicit ButtonRowDispatcher(RadioState *radioState, ConnectionController *connection, PopupManager *popupManager,
                                 AntennaConfigController *antennaCfg, TextDecodeController *textDecode,
                                 QObject *parent = nullptr);

private slots:
    void onMainRxClicked(int index);
    void onMainRxRightClicked(int index);
    void onSubRxClicked(int index);
    void onSubRxRightClicked(int index);
    void onTxClicked(int index);
    void onTxRightClicked(int index);

    // Refresh TX row buttons 5/6 labels when the current mode changes
    // between CW and voice/data — CW mode shows paddle/iambic/weight
    // controls, other modes show SSB BW/ESSB.
    void refreshTxButtonsForMode();

private:
    RadioState *m_radioState;              // injected, not owned
    ConnectionController *m_connection;    // injected, not owned
    PopupManager *m_popupManager;          // injected, not owned
    AntennaConfigController *m_antennaCfg; // injected, not owned
    TextDecodeController *m_textDecode;    // injected, not owned
};

#endif // BUTTONROWDISPATCHER_H
