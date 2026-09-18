#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include <QMenuBar>
#include <QMenu>
#include <QTimer>
#include <QThread>
#include "controllers/connectioncontroller.h"
#include "settings/radiosettings.h"
#include "models/radiostate.h"
#include "ui/widgets/vfowidget.h"

class AudioController;
class TransmitController;
class SpectrumController;
class StatusBarController;
class SideControlPanel;
class RightSidePanel;
class BottomMenuBar;
class MenuController;
class PopupManager;
class BandNavigationController;
class ButtonRowDispatcher;
class MacroController;
class ProcessingDisplayController;
class VfoRowIndicatorController;
class RitXitController;
class ModeLabelController;
class VfoFrequencyController;
class SubDivIndicatorController;
class TxStateController;
class SideControlDisplayController;
class SideControlScrollController;
class RightSideController;
class MemoryButtonsController;
class AntennaConfigController;
class AntennaDisplayController;
class TextDecodeController;
class FilterIndicatorWidget;
class FeatureMenuController;
class ModePopupController;
class HardwareController;
class CwController;
class DxClusterController;
class KPA1500UiController;
class CatServer;
class TciController;
class OptionsDialog;
class NotificationWidget;
class VfoRowWidget;
class RadioManagerDialog;
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    // Names a radio to open instead of the one ticked in the list - the --connect command-line
    // option, so a desktop shortcut can target a particular K4. Must be set before the event loop
    // runs, because the startup connect fires on its first pass.
    void setStartupRadioOverride(const QString &name) { m_startupRadioOverride = name; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onConnectionStateChanged(TcpClient::ConnectionState state);
    void onConnectionError(const QString &error);
    void onHardwareError(const QString &error);
    void onRadioReady();
    void onCatResponse(const QString &response);
    void showRadioManager();
    void connectToRadio(const RadioEntry &radio);

    // Connects to the radio flagged connectAtStartup, if any. Deferred to the event loop rather
    // than run in the constructor - see the call site.
    void connectToStartupRadio();

    void toggleDisplayPopup();
    void toggleBandPopup();
    void toggleFnPopup();
    void toggleMainRxPopup();
    void toggleSubRxPopup();
    void toggleTxPopup();
    void closeAllPopups();

private:
    QString m_startupRadioOverride;

    void setupMenuBar();
    void setupUi();
    void setupVfoSection(QWidget *parent);

    void setupControllers();
    void setupNotificationWidget();
    void setupConnectionWiring();
    void setupRadioStateWiring();
    void setupSpectrumDataRouting();
    void setupHardwareController();
    void setupCatServer();

    void updateConnectionState(TcpClient::ConnectionState state);
    // Disconnect-path helper — owners of their own state each implement a
    // reset method (VFOWidget::resetToDefaults, SideControlPanel::resetToDefaults,
    // StatusBarController::clearReadings, SpectrumController::clearDisplays,
    // KPA1500UiController::disconnectFromHost, RadioState::reset). This helper
    // covers only the labels MainWindow still owns directly.
    void resetUiForDisconnect();

    // Closes menu overlay, antenna-config popups, and mode popup — i.e.,
    // every popup NOT owned by PopupManager. Used by the per-popup toggle
    // handlers so PopupManager::toggleX can still see its target popup's
    // current visibility state (toggle-close must work).
    void closeNonPopupManagerPopups();

    ConnectionController *m_connectionController = nullptr;
    RadioState *m_radioState = nullptr;

    // Audio controller owns AudioEngine, Opus codecs, audio thread, and PTT state
    AudioController *m_audioController = nullptr;
    TransmitController *m_transmitController = nullptr;

    // Spectrum controller owns panadapters, span buttons, VFO indicators, and spectrum wiring
    SpectrumController *m_spectrumController = nullptr;

    // Top status bar — owned by StatusBarController (see src/controllers/).
    StatusBarController *m_statusBarController = nullptr;

    // VFO widgets (modular, reusable components). Each owns its own multifunction S/Po/ALC/COMP/
    // SWR/Id meter (VFOWidget::m_txMeter) so there is no standalone TX meter member here.
    VFOWidget *m_vfoA = nullptr;
    VFOWidget *m_vfoB = nullptr;

    // Mode labels (in center section, not in VFOWidget)
    QLabel *m_modeALabel = nullptr;
    QLabel *m_modeBLabel = nullptr;

    // RX Antenna labels (in antenna row below VFOs)
    QLabel *m_rxAntALabel = nullptr;
    QLabel *m_rxAntBLabel = nullptr;

    // Center section - first row with absolute positioning
    VfoRowWidget *m_vfoRow = nullptr;

    // Center section labels (pointers to VfoRowWidget children)
    QWidget *m_vfoASquare = nullptr; // VfoSquareWidget - used for event filter
    QLabel *m_txTriangle = nullptr;  // Left triangle (pointing at A) - shown when split OFF
    QLabel *m_txTriangleB = nullptr; // Right triangle (pointing at B) - shown when split ON
    QLabel *m_txIndicator = nullptr;
    QWidget *m_vfoBSquare = nullptr; // VfoSquareWidget - used for event filter
    QLabel *m_splitLabel = nullptr;
    QLabel *m_subLabel = nullptr; // SUB indicator (green when sub RX enabled)
    QLabel *m_divLabel = nullptr; // DIV indicator (green when diversity enabled)
    QLabel *m_msgBankLabel = nullptr;
    QWidget *m_ritXitBox = nullptr;
    QLabel *m_ritLabel = nullptr;
    QLabel *m_xitLabel = nullptr;
    QLabel *m_ritXitValueLabel = nullptr;
    QLabel *m_atuLabel = nullptr;
    FilterIndicatorWidget *m_filterAWidget = nullptr; // VFO A filter indicator
    FilterIndicatorWidget *m_filterBWidget = nullptr; // VFO B filter indicator

    // Memory buttons (M1-M4, REC, STORE, RCL) live in
    // MemoryButtonsController — no pointers retained here.
    QLabel *m_voxLabel = nullptr;
    QLabel *m_qskLabel = nullptr;
    QLabel *m_txAntennaLabel = nullptr;

    // Server Manager (RadioManagerDialog) shown modeless + toggled by the side-panel globe icon;
    // null when closed (see showRadioManager()).
    RadioManagerDialog *m_radioManager = nullptr;

    // Control panels (L-shaped layout)
    SideControlPanel *m_sideControlPanel = nullptr;
    RightSidePanel *m_rightSidePanel = nullptr;
    BottomMenuBar *m_bottomMenuBar = nullptr;

    // Menu system — owned by MenuController (src/controllers/).
    MenuController *m_menuController = nullptr;
    PopupManager *m_popupManager = nullptr;
    BandNavigationController *m_bandNavController = nullptr;
    ButtonRowDispatcher *m_buttonRowDispatcher = nullptr;
    MacroController *m_macroController = nullptr;
    ProcessingDisplayController *m_processingDisplayController = nullptr;
    VfoRowIndicatorController *m_vfoRowIndicatorController = nullptr;
    RitXitController *m_ritXitController = nullptr;
    ModeLabelController *m_modeLabelController = nullptr;
    VfoFrequencyController *m_vfoFrequencyController = nullptr;
    SubDivIndicatorController *m_subDivIndicatorController = nullptr;
    TxStateController *m_txStateController = nullptr;
    SideControlDisplayController *m_sideControlDisplayController = nullptr;
    SideControlScrollController *m_sideControlScrollController = nullptr;
    RightSideController *m_rightSideController = nullptr;
    MemoryButtonsController *m_memoryButtonsController = nullptr;
    TextDecodeController *m_textDecodeController = nullptr;
    AntennaConfigController *m_antennaCfgController = nullptr;
    AntennaDisplayController *m_antennaDisplayController = nullptr;
    FeatureMenuController *m_featureMenuController = nullptr;
    ModePopupController *m_modePopupController = nullptr;

    // Hardware controller (owns KPOD, HaliKey, IambicKeyer, SidetoneGenerator and their threads)
    HardwareController *m_hardwareController = nullptr;
    CwController *m_cwController = nullptr;

    // KPA1500 amplifier UI controller (owns the KPA1500Client)
    KPA1500UiController *m_kpa1500UiController = nullptr;

    // DX Cluster controller
    DxClusterController *m_dxClusterController = nullptr;

    // CAT server for external app integration (WSJT-X, MacLoggerDX, etc.)
    CatServer *m_catServer = nullptr;

    // TCI server: the same job over a WebSocket, carrying audio as well as CAT. Independent of
    // CatServer - neither goes through the other.
    TciController *m_tciController = nullptr;

    // Persistent Options dialog (lazy-created on first open)
    OptionsDialog *m_optionsDialog = nullptr;

    // Notification popup for K4 error/status messages (ERxx:)
    NotificationWidget *m_notificationWidget = nullptr;
};

#endif // MAINWINDOW_H
