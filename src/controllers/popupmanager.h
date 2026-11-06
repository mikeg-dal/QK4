#ifndef POPUPMANAGER_H
#define POPUPMANAGER_H

#include <QObject>

class RadioState;
class ConnectionController;
class SpectrumController;
class VFOWidget;
class BottomMenuBar;
class BandPopupWidget;
class DisplayPopupWidget;
class FnPopupWidget;
class MacroDialog;
class ButtonRowPopup;
class RxEqPopupWidget;
class LineOutPopupWidget;
class LineInPopupWidget;
class MicInputPopupWidget;
class MicConfigPopupWidget;
class VoxPopupWidget;
class SsbBwPopupWidget;
class KeyingWeightPopupWidget;
class SoftwareListPopupWidget;
class QTimer;
class QWidget;

// Owns MainWindow's primary popup family: band selector, display controls,
// Fn (macro trigger) popup, and the full-screen macro configuration
// dialog. Also encapsulates the increment/decrement/toggle action handlers
// for DisplayPopup (scale, ref level, span, averaging, waterfall height,
// DDC NB level).
//
// The Fn popup's macro trigger goes to MacroController and the band popup's
// bandSelected to BandNavigationController; both are re-emitted as signals
// here so no one reaches into the owned widgets.
//
// See PATTERNS.md → Controller Pattern.
class PopupManager : public QObject {
    Q_OBJECT

public:
    explicit PopupManager(RadioState *radioState, ConnectionController *connection, SpectrumController *spectrum,
                          VFOWidget *vfoA, VFOWidget *vfoB, QWidget *parentWidget, QObject *parent = nullptr);

    // BottomMenuBar and VFO widgets must be injected after construction
    // because they are created by MainWindow::setupUi() AFTER controllers.
    // Use setters. setupUi wires BottomMenuBar signals to MainWindow slots
    // that call PopupManager, so PopupManager must exist before setupUi() runs.
    void setBottomMenuBar(BottomMenuBar *bottomMenuBar);
    void setVfos(VFOWidget *vfoA, VFOWidget *vfoB);

    // Task-level toggles (called from BottomMenuBar button clicks).
    void toggleBand();
    void toggleDisplay();
    void toggleFn();

    // Task-level macro dialog open (Fn → Macros, via MacroController::macroDialogRequested).
    void openMacroDialog();

    // Task-level open of the read-only Software List popup (Fn → SW LIST).
    // Reads firmware versions from RadioState and anchors above the Fn button.
    void openSoftwareList();

    // Close the popups PopupManager owns. MainWindow::closeAllPopups()
    // calls this after closing the other controllers' popups.
    void closeOwnedPopups();

    // Band popup accessors — BandNavigationController uses the popup's
    // internal band-number↔band-name map (yes, that map really lives on
    // the popup widget).
    int bandNumberForName(const QString &bandName) const;
    void setSelectedBandByNumber(int bandNum);

    // Identifies which of the three button-row lanes a secondary popup
    // should anchor against. ButtonRowDispatcher invokes secondary popups
    // by naming the lane it is currently in; PopupManager resolves the
    // lane to the right anchor widget internally.
    enum class ButtonRowLane { MainRx, SubRx, Tx };

    void toggleMainRx();
    void toggleSubRx();
    void toggleTx();

    // WHY (CONVENTIONS Rule 2 documented exception): ButtonRowDispatcher
    // needs a row widget to anchor AntennaConfigController's popups against.
    QWidget *anchorForLane(ButtonRowLane lane) const;

    void setTxButtonLabel(int index, const QString &primary, const QString &alternate, bool alternateIsAmber = true);

    // Secondary popups (RX/TX EQ, line in/out, mic input/config, VOX
    // gain & anti-VOX, SSB TX bandwidth, CW keying weight). Callers pass
    // the ButtonRowLane they're currently in; PopupManager resolves the
    // anchor internally. Each opens centred directly above its row and
    // closes when the row does; pressing the same key again closes it.
    void showRxEqAbove(ButtonRowLane lane);
    void showTxEqAbove(ButtonRowLane lane);
    void showLineOutAbove(ButtonRowLane lane);
    void showLineInAbove(ButtonRowLane lane);
    void showMicInputAbove(ButtonRowLane lane);
    void showMicConfigAbove(ButtonRowLane lane); // Mic type is inferred from RadioState.
    void showVoxGainAbove(ButtonRowLane lane);   // configures popup for gain mode first
    void showAntiVoxAbove(ButtonRowLane lane);   // configures popup for anti-vox mode first
    void showSsbBwAbove(ButtonRowLane lane);
    void showKeyingWeightAbove(ButtonRowLane lane);

signals:
    // User picked a band from the band popup. BandNavigationController::onBandSelected
    // handles the band-switch logic (CAT commands).
    void bandSelected(const QString &bandName);

    // Fn popup invoked a macro binding. MacroController::onFunctionTriggered resolves
    // the binding to its CAT command and sends it.
    void macroFunctionTriggered(const QString &functionId);

    // Button-row popup click events — ButtonRowDispatcher handles the
    // per-index semantics (which secondary popup to show,
    // which CAT command to send). PopupManager just forwards the clicks.
    void mainRxButtonClicked(int index);
    void mainRxButtonRightClicked(int index);
    void subRxButtonClicked(int index);
    void subRxButtonRightClicked(int index);
    void txButtonClicked(int index);
    void txButtonRightClicked(int index);

private:
    RadioState *m_radioState;                 // injected, not owned
    ConnectionController *m_connection;       // injected, not owned
    SpectrumController *m_spectrum;           // injected, not owned
    VFOWidget *m_vfoA;                        // injected, not owned
    VFOWidget *m_vfoB;                        // injected, not owned
    BottomMenuBar *m_bottomMenuBar = nullptr; // late-injected via setter
    QWidget *m_parentWidget;

    BandPopupWidget *m_bandPopup; // owned via Qt parent-ownership (parentWidget)
    DisplayPopupWidget *m_displayPopup;
    FnPopupWidget *m_fnPopup;
    MacroDialog *m_macroDialog;
    ButtonRowPopup *m_mainRxRow;
    ButtonRowPopup *m_subRxRow;
    ButtonRowPopup *m_txRow;
    RxEqPopupWidget *m_rxEqPopup;
    RxEqPopupWidget *m_txEqPopup;
    QTimer *m_rxEqDebounce;
    QTimer *m_txEqDebounce;
    LineOutPopupWidget *m_lineOutPopup;
    LineInPopupWidget *m_lineInPopup;
    MicInputPopupWidget *m_micInputPopup;
    MicConfigPopupWidget *m_micConfigPopup;
    VoxPopupWidget *m_voxPopup;
    SsbBwPopupWidget *m_ssbBwPopup;
    KeyingWeightPopupWidget *m_keyingWeightPopup;
    SoftwareListPopupWidget *m_softwareListPopup;

    void wireDisplayPopup();
    void wireEqPopups();
    void wireLinePopups();
    void wireMicPopups();
    void wireVoxAndSsbPopups();
    void wireKeyingWeightPopup();
    // Wires RadioState observers that update Main RX / Sub RX button-row
    // labels for AFX, AGC, and APF. APF also forwards to the VFO APF
    // indicator since it's the same state.
    void wireRxRowButtonLabels();
};

#endif // POPUPMANAGER_H
