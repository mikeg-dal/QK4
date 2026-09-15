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
    void onAuthFailed();
    void onCatResponse(const QString &response);
    void showRadioManager();
    void connectToRadio(const RadioEntry &radio);
    void toggleDisplayPopup();
    void toggleBandPopup();
    void toggleFnPopup();
    void toggleMainRxPopup();
    void toggleSubRxPopup();
    void toggleTxPopup();
    void closeAllPopups();

private:
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

    ConnectionController *m_connectionController;
    RadioState *m_radioState;

    // Audio controller owns AudioEngine, Opus codecs, audio thread, and PTT state
    AudioController *m_audioController;

    // Spectrum controller owns panadapters, span buttons, VFO indicators, and spectrum wiring
    SpectrumController *m_spectrumController;

    // Top status bar — owned by StatusBarController (see src/controllers/).
    StatusBarController *m_statusBarController;

    // VFO widgets (modular, reusable components). Each owns its own multifunction S/Po/ALC/COMP/
    // SWR/Id meter (VFOWidget::m_txMeter) so there is no standalone TX meter member here.
    VFOWidget *m_vfoA;
    VFOWidget *m_vfoB;

    // Mode labels (in center section, not in VFOWidget)
    QLabel *m_modeALabel;
    QLabel *m_modeBLabel;

    // RX Antenna labels (in antenna row below VFOs)
    QLabel *m_rxAntALabel;
    QLabel *m_rxAntBLabel;

    // Center section - first row with absolute positioning
    VfoRowWidget *m_vfoRow;

    // Center section labels (pointers to VfoRowWidget children)
    QWidget *m_vfoASquare; // VfoSquareWidget - used for event filter
    QLabel *m_txTriangle;  // Left triangle (pointing at A) - shown when split OFF
    QLabel *m_txTriangleB; // Right triangle (pointing at B) - shown when split ON
    QLabel *m_txIndicator;
    QWidget *m_vfoBSquare; // VfoSquareWidget - used for event filter
    QLabel *m_splitLabel;
    QLabel *m_subLabel; // SUB indicator (green when sub RX enabled)
    QLabel *m_divLabel; // DIV indicator (green when diversity enabled)
    QLabel *m_msgBankLabel;
    QWidget *m_ritXitBox;
    QLabel *m_ritLabel;
    QLabel *m_xitLabel;
    QLabel *m_ritXitValueLabel;
    QLabel *m_atuLabel;
    FilterIndicatorWidget *m_filterAWidget; // VFO A filter indicator
    FilterIndicatorWidget *m_filterBWidget; // VFO B filter indicator

    // Memory buttons (M1-M4, REC, STORE, RCL) live in
    // MemoryButtonsController — no pointers retained here.
    QLabel *m_voxLabel;
    QLabel *m_qskLabel;
    QLabel *m_txAntennaLabel;

    // Server Manager (RadioManagerDialog) shown modeless + toggled by the side-panel globe icon;
    // null when closed (see showRadioManager()).
    RadioManagerDialog *m_radioManager = nullptr;

    // Control panels (L-shaped layout)
    SideControlPanel *m_sideControlPanel;
    RightSidePanel *m_rightSidePanel;
    BottomMenuBar *m_bottomMenuBar;

    // Menu system — owned by MenuController (src/controllers/).
    MenuController *m_menuController;
    PopupManager *m_popupManager;
    BandNavigationController *m_bandNavController;
    ButtonRowDispatcher *m_buttonRowDispatcher;
    MacroController *m_macroController;
    ProcessingDisplayController *m_processingDisplayController;
    VfoRowIndicatorController *m_vfoRowIndicatorController;
    RitXitController *m_ritXitController;
    ModeLabelController *m_modeLabelController;
    VfoFrequencyController *m_vfoFrequencyController;
    SubDivIndicatorController *m_subDivIndicatorController;
    TxStateController *m_txStateController;
    SideControlDisplayController *m_sideControlDisplayController;
    SideControlScrollController *m_sideControlScrollController;
    RightSideController *m_rightSideController;
    MemoryButtonsController *m_memoryButtonsController;
    TextDecodeController *m_textDecodeController;
    AntennaConfigController *m_antennaCfgController;
    AntennaDisplayController *m_antennaDisplayController;
    FeatureMenuController *m_featureMenuController;
    ModePopupController *m_modePopupController;

    // Hardware controller (owns KPOD, HaliKey, IambicKeyer, SidetoneGenerator and their threads)
    HardwareController *m_hardwareController;
    CwController *m_cwController;

    // KPA1500 amplifier UI controller (owns the KPA1500Client)
    KPA1500UiController *m_kpa1500UiController;

    // DX Cluster controller
    DxClusterController *m_dxClusterController;

    // CAT server for external app integration (WSJT-X, MacLoggerDX, etc.)
    CatServer *m_catServer;

    // TCI server: the same job over a WebSocket, carrying audio as well as CAT. Independent of
    // CatServer - neither goes through the other.
    TciController *m_tciController = nullptr;

    // Persistent Options dialog (lazy-created on first open)
    OptionsDialog *m_optionsDialog = nullptr;

    // Notification popup for K4 error/status messages (ERxx:)
    NotificationWidget *m_notificationWidget;
};

#endif // MAINWINDOW_H
