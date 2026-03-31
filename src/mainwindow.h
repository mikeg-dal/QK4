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
#include "ui/vfowidget.h"
#include "ui/wheelaccumulator.h"

class AudioController;
class SpectrumController;
class NetHealthWidget;
class SideControlPanel;
class RightSidePanel;
class BottomMenuBar;
class MenuModel;
class MenuOverlayWidget;
class BandPopupWidget;
class ButtonRowPopup;
class DisplayPopupWidget;
class FnPopupWidget;
class RxEqPopupWidget;
class AntennaCfgPopupWidget;
class LineOutPopupWidget;
class LineInPopupWidget;
class MicInputPopupWidget;
class MicConfigPopupWidget;
class VoxPopupWidget;
class SsbBwPopupWidget;
class KeyingWeightPopupWidget;
class TextDecodeWindow;
class MacroDialog;
class FilterIndicatorWidget;
class FeatureMenuBar;
class ModePopupWidget;
class HardwareController;
class KPA1500Client;
class CatServer;
class OptionsDialog;
class NotificationWidget;
class VfoRowWidget;
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void moveEvent(QMoveEvent *event) override;

private slots:
    void onConnectionStateChanged(TcpClient::ConnectionState state);
    void onConnectionError(const QString &error);
    void onRadioReady();
    void onAuthFailed();
    void onCatResponse(const QString &response);
    void onFrequencyChanged(quint64 freq);
    void onFrequencyBChanged(quint64 freq);
    void onModeChanged(RadioState::Mode mode);
    void onModeBChanged(RadioState::Mode mode);
    void onSMeterChanged(double value);
    void onSMeterBChanged(double value);
    void onRfPowerChanged(double watts, bool isQrp);
    void onSupplyVoltageChanged(double volts);
    void onSupplyCurrentChanged(double amps);
    void onSwrChanged(double swr);
    void onSplitChanged(bool enabled);
    void onAntennaChanged(int txAnt, int rxAntMain, int rxAntSub);
    void onAntennaNameChanged(int index, const QString &name);
    void onVoxChanged(bool enabled);
    void onQskEnabledChanged(bool enabled);
    void onTestModeChanged(bool enabled);
    void onAtuModeChanged(int mode);
    void onRitXitChanged(bool ritEnabled, bool xitEnabled, int offset);
    void onMessageBankChanged(int bank);
    void onProcessingChanged();
    void onProcessingChangedB();
    void showRadioManager();
    void connectToRadio(const RadioEntry &radio);
    void updateDateTime();
    void showMenuOverlay();
    void onMenuValueChangeRequested(int menuId, const QString &action);
    void onMenuModelValueChanged(int menuId, int newValue);
    void onBandSelected(const QString &bandName);
    void updateBandSelection(int bandNum);
    void updateBandSelectionB(int bandNum);
    void toggleDisplayPopup();
    void toggleBandPopup();
    void toggleFnPopup();
    void toggleMainRxPopup();
    void toggleSubRxPopup();
    void toggleTxPopup();
    void closeAllPopups();

    // KPA1500 slots
    void onKpa1500Connected();
    void onKpa1500Disconnected();
    void onKpa1500Error(const QString &error);
    void onKpa1500EnabledChanged(bool enabled);
    void onKpa1500SettingsChanged();
    void updateKpa1500Status();

    // Error/notification from K4 (ERxx: messages)
    void onErrorNotification(int errorCode, const QString &message);

    // Display FPS (synthetic menu item)
    void onDisplayFpsChanged(int fps);

    // Fn popup / macro slots
    void onFnFunctionTriggered(const QString &functionId);
    void executeMacro(const QString &functionId);
    void openMacroDialog();

    // MAIN RX / SUB RX popup slots
    void onMainRxButtonClicked(int index);
    void onMainRxButtonRightClicked(int index);
    void onSubRxButtonClicked(int index);
    void onSubRxButtonRightClicked(int index);

private:
    void setupMenuBar();
    void setupUi();
    void setupTopStatusBar(QWidget *parent);
    void setupVfoSection(QWidget *parent);
    void updateConnectionState(TcpClient::ConnectionState state);
    QString formatFrequency(quint64 freq);
    void updateModeLabels();

    ConnectionController *m_connectionController;
    RadioState *m_radioState;
    QTimer *m_clockTimer;

    // Audio controller owns AudioEngine, Opus codecs, audio thread, and PTT state
    AudioController *m_audioController;

    // Spectrum controller owns panadapters, span buttons, VFO indicators, and spectrum wiring
    SpectrumController *m_spectrumController;

    // Top status bar
    QLabel *m_titleLabel;
    QLabel *m_dateTimeLabel;
    QLabel *m_powerLabel;
    QLabel *m_swrLabel;
    QLabel *m_voltageLabel;
    QLabel *m_currentLabel;
    QLabel *m_connectionStatusLabel;
    NetHealthWidget *m_netHealthWidget;
    QLabel *m_kpa1500StatusLabel;

    // VFO widgets (modular, reusable components)
    VFOWidget *m_vfoA;
    VFOWidget *m_vfoB;

    // NOTE: TX meters are now integrated into VFOWidgets as multifunction S/Po meters
    // (see VFOWidget::m_txMeter - displays S-meter when RX, Po when TX)

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
    QLabel *m_bSetLabel;
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

    // Memory buttons (M1-M4, REC, STORE, RCL)
    QPushButton *m_m1Btn;
    QPushButton *m_m2Btn;
    QPushButton *m_m3Btn;
    QPushButton *m_m4Btn;
    QPushButton *m_recBtn;
    QPushButton *m_storeBtn;
    QPushButton *m_rclBtn;
    QLabel *m_voxLabel;
    QLabel *m_qskLabel;
    QLabel *m_txAntennaLabel;

    // Control panels (L-shaped layout)
    SideControlPanel *m_sideControlPanel;
    RightSidePanel *m_rightSidePanel;
    BottomMenuBar *m_bottomMenuBar;

    // Menu system
    MenuModel *m_menuModel;
    MenuOverlayWidget *m_menuOverlay;
    BandPopupWidget *m_bandPopup;
    DisplayPopupWidget *m_displayPopup;
    FnPopupWidget *m_fnPopup;
    MacroDialog *m_macroDialog;
    ButtonRowPopup *m_mainRxPopup;
    ButtonRowPopup *m_subRxPopup;
    ButtonRowPopup *m_txPopup;
    RxEqPopupWidget *m_rxEqPopup;
    RxEqPopupWidget *m_txEqPopup;
    LineOutPopupWidget *m_lineOutPopup;
    LineInPopupWidget *m_lineInPopup;
    MicInputPopupWidget *m_micInputPopup;
    MicConfigPopupWidget *m_micConfigPopup;
    VoxPopupWidget *m_voxPopup;
    SsbBwPopupWidget *m_ssbBwPopup;
    KeyingWeightPopupWidget *m_keyingWeightPopup;
    TextDecodeWindow *m_textDecodeWindowMain;
    TextDecodeWindow *m_textDecodeWindowSub;
    AntennaCfgPopupWidget *m_mainRxAntCfgPopup;
    AntennaCfgPopupWidget *m_subRxAntCfgPopup;
    AntennaCfgPopupWidget *m_txAntCfgPopup;
    FeatureMenuBar *m_featureMenuBar;
    ModePopupWidget *m_modePopup;

    int m_currentBandNum = -1;  // Current band number for VFO A (BN command)
    int m_currentBandNumB = -1; // Current band number for VFO B (BN$ command)

    // Hardware controller (owns KPOD, HaliKey, IambicKeyer, SidetoneGenerator and their threads)
    HardwareController *m_hardwareController;

    // KPA1500 amplifier client
    KPA1500Client *m_kpa1500Client;

    // CAT server for external app integration (WSJT-X, MacLoggerDX, etc.)
    CatServer *m_catServer;

    // Persistent Options dialog (lazy-created on first open)
    OptionsDialog *m_optionsDialog = nullptr;

    // Notification popup for K4 error/status messages (ERxx:)
    NotificationWidget *m_notificationWidget;

    // Debounce timer for RX EQ slider changes
    QTimer *m_rxEqDebounceTimer;

    // Debounce timer for TX EQ slider changes
    QTimer *m_txEqDebounceTimer;

    // K4 "Mouse L/R Button QSY" menu setting
    int m_mouseQsyMode = 0;         // 0=Left Only, 1=L=A R=B
    int m_mouseQsyMenuId = -999;    // Menu ID from MEDF (sentinel = not yet discovered)
    int m_fskMarkToneMenuId = -999; // "FSK Mark-Tone" menu ID (sentinel = not yet discovered)

    WheelAccumulator m_ritWheelAccumulator;
};

#endif // MAINWINDOW_H
