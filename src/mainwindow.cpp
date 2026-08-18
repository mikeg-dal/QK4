#include "mainwindow.h"
#include "utils/radioutils.h"
#include "ui/dialogs/radiomanagerdialog.h"
#include "ui/widgets/sidecontrolpanel.h"
#include "ui/widgets/rightsidepanel.h"
#include "ui/widgets/bottommenubar.h"
#include "controllers/featuremenucontroller.h"
#include "controllers/modepopupcontroller.h"
#include "ui/overlays/menuoverlay.h"
#include "controllers/bandnavigationcontroller.h"
#include "controllers/buttonrowdispatcher.h"
#include "controllers/macrocontroller.h"
#include "controllers/processingdisplaycontroller.h"
#include "controllers/vforowindicatorcontroller.h"
#include "controllers/ritxitcontroller.h"
#include "controllers/modelabelcontroller.h"
#include "controllers/vfofrequencycontroller.h"
#include "controllers/subdivindicatorcontroller.h"
#include "controllers/txstatecontroller.h"
#include "controllers/sidecontroldisplaycontroller.h"
#include "controllers/sidecontrolscrollcontroller.h"
#include "controllers/rightsidecontroller.h"
#include "controllers/memorybuttonscontroller.h"
#include "controllers/popupmanager.h"
#include "ui/dialogs/optionsdialog.h"
#include "ui/widgets/notificationwidget.h"
#include "ui/widgets/vforowwidget.h"
#include "ui/widgets/filterindicatorwidget.h"
#include "ui/styling/k4styles.h"
#include "controllers/antennaconfigcontroller.h"
#include "controllers/antennadisplaycontroller.h"
#include "controllers/textdecodecontroller.h"
#include "controllers/menucontroller.h"
#include "controllers/dxclustercontroller.h"
#include "controllers/spectrumcontroller.h"
#include "dsp/panadapter_rhi.h"
#include "dsp/minipan_rhi.h"
#include "ui/widgets/frequencydisplaywidget.h"
#include "controllers/audiocontroller.h"
#include "controllers/cwcontroller.h"
#include "controllers/hardwarecontroller.h"
#include "controllers/kpa1500uicontroller.h"
#include "network/catserver.h"
#include "network/networkmetrics.h"
#include "controllers/statusbarcontroller.h"
#include "settings/radiosettings.h"
#include <QVBoxLayout>
#include <QInputDialog>
#include <QHBoxLayout>
#include <QAction>
#include <QCoreApplication>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QDateTime>
#include <QFrame>
#include <QEvent>
#include <QResizeEvent>
#include <QRegularExpression>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QCloseEvent>
#include <QShortcut>

Q_LOGGING_CATEGORY(qk4Main, "qk4.main")

// ============== MainWindow Implementation ==============
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), m_radioState(new RadioState(this)) {
    setupControllers();

    // WHY: controllers referenced from setupUi() must exist at connect-time.
    // setupUi() wires BottomMenuBar signals to MenuController::toggleOverlay
    // (among others) — if m_menuController is uninitialized the connect() call
    // segfaults inside QObjectPrivate::connectImpl. Create controllers first,
    // then let setupUi() build widgets and wire them into the existing
    // controllers. VFO widgets are late-injected into PopupManager via
    // setVfos() after setupUi() creates them.
    m_menuController = new MenuController(m_connectionController, m_spectrumController, this, this);
    connect(m_menuController, &MenuController::overlayClosed, this, [this]() {
        if (m_bottomMenuBar)
            m_bottomMenuBar->setMenuActive(false);
    });
    m_popupManager =
        new PopupManager(m_radioState, m_connectionController, m_spectrumController, nullptr, nullptr, this, this);
    m_bandNavController = new BandNavigationController(m_radioState, m_connectionController, m_popupManager, this);
    // Mode label refresh on ESSB toggle is observed directly by
    // ModeLabelController via RadioState::essbChanged.
    m_macroController = new MacroController(m_connectionController, m_popupManager, /*dialogParent=*/this, this);
    connect(m_macroController, &MacroController::macroDialogRequested, this, [this]() {
        closeAllPopups();
        m_popupManager->openMacroDialog();
    });
    connect(m_macroController, &MacroController::softwareListRequested, this, [this]() {
        closeAllPopups();
        m_popupManager->openSoftwareList();
    });
    m_antennaCfgController = new AntennaConfigController(m_radioState, m_connectionController, this, this);
    m_textDecodeController = new TextDecodeController(m_radioState, m_connectionController, this, this);
    m_buttonRowDispatcher = new ButtonRowDispatcher(m_radioState, m_connectionController, m_popupManager,
                                                    m_antennaCfgController, m_textDecodeController, this);

    // IMPORTANT: setupUi() MUST be called BEFORE setupMenuBar()!
    // Qt 6.10.1 bug on macOS Tahoe: calling menuBar() before creating QRhiWidget
    // prevents the RHI backing store from being set up correctly, causing
    // "QRhiWidget: No QRhi" errors and blank panadapter display.
    setupUi();
    m_popupManager->setVfos(m_vfoA, m_vfoB);
    setupMenuBar();

    // ESC — halt all transmission regardless of which child widget has focus.
    // QShortcut fires at window scope; keyPressEvent only fires when the window frame itself has focus.
    // Clears both the K4 TX state (RX;) and QK4's internal PTT/audio state so the UI unlocks too.
    auto *escShortcut = new QShortcut(Qt::Key_Escape, this);
    connect(escShortcut, &QShortcut::activated, this, [this]() {
        if (m_connectionController->isConnected())
            m_connectionController->sendCAT("RX;");
        m_audioController->setPttActive(false);
        m_bottomMenuBar->setPttActive(false);
    });

    setupNotificationWidget();
    setupConnectionWiring();

    setupRadioStateWiring();

    setupSpectrumDataRouting();

    setupHardwareController();

    m_kpa1500UiController = new KPA1500UiController(m_statusBarController, m_rightSidePanel, this);

    m_processingDisplayController = new ProcessingDisplayController(m_radioState, m_vfoA, m_vfoB, this);

    m_antennaDisplayController = new AntennaDisplayController(m_radioState, m_bandNavController, m_txAntennaLabel,
                                                              m_rxAntALabel, m_rxAntBLabel, this);

    const VfoRowIndicatorController::Labels rowLabels{
        m_splitLabel, m_vfoRow->bSetLabel(), m_txTriangle, m_txTriangleB, m_voxLabel, m_qskLabel,
        m_atuLabel,   m_msgBankLabel};
    m_vfoRowIndicatorController =
        new VfoRowIndicatorController(m_radioState, m_spectrumController, m_vfoRow, rowLabels, this);

    m_modeLabelController = new ModeLabelController(m_radioState, m_modeALabel, m_modeBLabel, this);

    m_vfoFrequencyController = new VfoFrequencyController(m_radioState, m_vfoA, m_vfoB, this);

    m_subDivIndicatorController = new SubDivIndicatorController(m_radioState, m_spectrumController, m_vfoB, m_subLabel,
                                                                m_divLabel, m_modeBLabel, this);

    m_txStateController = new TxStateController(m_radioState, m_sideControlPanel, m_vfoFrequencyController, m_vfoA,
                                                m_vfoB, m_txIndicator, m_txTriangle, m_txTriangleB, this);

    m_sideControlDisplayController = new SideControlDisplayController(m_radioState, m_sideControlPanel, this);

    // SideControlScrollController owns all of SideControlPanel's scroll-handler
    // protocol logic (WPM, pitch, mic, comp, power QRP/QRO, delay, BW/HI/LO/SHIFT,
    // RF gain, squelch). Extracted from MainWindow's setupUi in PR 10.
    m_sideControlScrollController =
        new SideControlScrollController(m_radioState, m_connectionController, m_sideControlPanel, this);

    // Filter indicator widgets observe RadioState directly (PATTERNS.md
    // Direct Observation). The former FilterIndicatorController was a
    // pure-forwarding thin shell and has been deleted.
    m_filterAWidget->observe(m_radioState, FilterIndicatorWidget::Vfo::A);
    m_filterBWidget->observe(m_radioState, FilterIndicatorWidget::Vfo::B);

    m_ritXitController = new RitXitController(m_radioState, m_connectionController, m_spectrumController, m_ritLabel,
                                              m_xitLabel, m_ritXitValueLabel, this);
    // RIT offset shifts rendered VFO frequency — refresh displays on any
    // RIT/XIT state change emitted by the controller.
    connect(m_ritXitController, &RitXitController::displayRefreshRequested, m_vfoFrequencyController,
            &VfoFrequencyController::refresh);

    setupCatServer();
}

void MainWindow::closeEvent(QCloseEvent *event) {
    // Deterministic audio teardown BEFORE the event loop exits. On Linux with
    // the PipeWire Qt Multimedia backend, destroying an active QAudioSink from
    // libc atexit races the PipeWire RT worker thread and segfaults in
    // pw_stream_dequeue_buffer. Stopping the sinks here — while the audio and
    // sidetone thread event loops are still servicing BlockingQueuedConnection
    // — guarantees no live QAudioSink/QAudioSource remains at process exit.
    if (m_audioController) {
        m_audioController->shutdown();
    }
    if (m_hardwareController) {
        m_hardwareController->shutdownSidetone();
    }
    QMainWindow::closeEvent(event);
}

MainWindow::~MainWindow() {
    // Disconnect all signals targeting this object FIRST — prevents queued signals
    // from arriving during partial destruction (e.g., a RadioState signal firing
    // after some child widgets are already destroyed but before MainWindow is).
    disconnect(this);

    // HardwareController handles KPOD, HaliKey, Keyer, Sidetone shutdown
    // (it's a child of MainWindow, so Qt deletes it automatically —
    //  its destructor shuts down threads in the correct order)

    // ConnectionController handles I/O thread shutdown in its destructor
    // (it's a child of MainWindow, so Qt deletes it automatically)
    // AudioController handles audio thread shutdown in its destructor

    // KPA1500UiController's own destructor disconnects its signals and
    // disconnects from the amplifier — Qt handles its deletion as a child.
}

void MainWindow::setupControllers() {
    // Connection controller owns TcpClient, I/O thread, and NetworkMetrics
    m_connectionController = new ConnectionController(m_radioState, this);

    // Audio controller owns AudioEngine, Opus codecs, audio thread, and PTT state
    m_audioController = new AudioController(m_connectionController, m_radioState, this);

    // Spectrum controller owns panadapters, span buttons, and all spectrum wiring
    m_spectrumController = new SpectrumController(m_connectionController, m_radioState, this);

    // DX Cluster controller owns the cluster TCP client and spot cache
    m_dxClusterController = new DxClusterController(this);
    m_spectrumController->setDxClusterController(m_dxClusterController);
}

void MainWindow::setupNotificationWidget() {
    // Create notification popup for K4 error/status messages (ERxx:)
    m_notificationWidget = new NotificationWidget(this);
}

void MainWindow::setupConnectionWiring() {
    // ConnectionController signals
    connect(m_connectionController, &ConnectionController::connectionStateChanged, this,
            &MainWindow::onConnectionStateChanged);
    connect(m_connectionController, &ConnectionController::connectionError, this, &MainWindow::onConnectionError);
    connect(m_connectionController, &ConnectionController::radioReady, this, &MainWindow::onRadioReady);
    connect(m_connectionController, &ConnectionController::authFailed, this, &MainWindow::onAuthFailed);

    // Protocol CAT responses -> RadioState (via ConnectionController re-emitted signal)
    connect(m_connectionController, &ConnectionController::catResponseReceived, this, &MainWindow::onCatResponse);
}

void MainWindow::setupRadioStateWiring() {
    // RadioState signals -> UI updates (VFO A)
    // Frequency display (with RIT/XIT offset) owned by VfoFrequencyController.
    // Mode + data-sub-mode + ESSB label updates are owned by ModeLabelController.
    // TX button-row mode-dependent labels are observed by ButtonRowDispatcher.
    // VOX label refresh on mode change is handled by VfoRowIndicatorController
    // (K4 VOX state is per-mode-class, so the displayed color depends on current mode).
    // Direct-observation wires — RadioState → widget setter, no MainWindow slot.
    connect(m_radioState, &RadioState::sMeterChanged, m_vfoA, &VFOWidget::setSMeterValue);
    connect(m_radioState, &RadioState::sMeterBChanged, m_vfoB, &VFOWidget::setSMeterValue);
    // Auto-hide mini pan B when VFOs move to different bands (and SUB RX is off).
    connect(m_radioState, &RadioState::frequencyChanged, m_spectrumController,
            &SpectrumController::checkAndHideMiniPanB);
    connect(m_radioState, &RadioState::frequencyBChanged, m_spectrumController,
            &SpectrumController::checkAndHideMiniPanB);

    // RadioState signals -> status / side-panel readings (direct-observation).
    // rfPowerChanged carries (value, range) — the VFO TX meter scales by QRP vs
    // QRO; for XVTR the meter scale is also "small" so we treat XVTR like QRP.
    connect(m_radioState, &RadioState::rfPowerChanged, this, [this](double, LevelsState::PowerRange range) {
        const bool smallScale = (range == LevelsState::PowerRange::Qrp || range == LevelsState::PowerRange::Xvtr);
        m_vfoA->setTxMeterQrp(smallScale);
        m_vfoB->setTxMeterQrp(smallScale);
    });
    connect(m_radioState, &RadioState::supplyVoltageChanged, m_sideControlPanel, &SideControlPanel::setVoltage);
    // Side panel's "X.XA" mirrors the K4 front-panel meter: supply current (IS) at idle,
    // PA drain current (PM/768) during TX. Threshold gate (drain > 0.1 A) avoids the brief
    // 0 A flash on TX-edge — the K4 doesn't emit a TX-time SIRF for ~100–200 ms after the
    // TX flag flips, so paDrainCurrent is stale-zero until then. We hold the supply value
    // through that window.
    auto updateSidePanelAmps = [this]() {
        const double drain = m_radioState->paDrainCurrent();
        const bool useDrain = m_radioState->isTransmitting() && drain > 0.1;
        const double amps = useDrain ? drain : m_radioState->supplyCurrent();
        m_sideControlPanel->setCurrent(amps);
    };
    connect(m_radioState, &RadioState::supplyCurrentChanged, this, updateSidePanelAmps);
    connect(m_radioState, &RadioState::paDrainCurrentChanged, this, updateSidePanelAmps);
    connect(m_radioState, &RadioState::transmitStateChanged, this, updateSidePanelAmps);
    connect(m_radioState, &RadioState::swrChanged, m_sideControlPanel, &SideControlPanel::setSwr);

    // Synthetic "Display FPS" menu item value tracks the radio's echoed FPS.
    connect(m_radioState, &RadioState::displayFpsChanged, m_menuController, &MenuController::setDisplayFps);

    // Error/notification messages from K4 (ERxx:) → notification popup.
    connect(m_radioState, &RadioState::errorNotificationReceived, this,
            [this](int, const QString &message) { m_notificationWidget->showMessage(message, 2000); });

    // TX meter data + TX state (indicator colors, VFO meter-mode flip,
    // XIT-aware frequency refresh, PA current calc) owned by TxStateController.

    // SUB / DIV indicator styling + VFO B dim-state + auto-hide mini pan B
    // are owned by SubDivIndicatorController (created later in constructor).

    // VFO Lock indicators — direct-observation.
    connect(m_radioState, &RadioState::lockAChanged, m_vfoRow, &VfoRowWidget::setLockA);
    connect(m_radioState, &RadioState::lockBChanged, m_vfoRow, &VfoRowWidget::setLockB);

    // Side control panel state (BW/SHFT/HI/LO, knob values, CW↔voice display
    // swap) owned by SideControlDisplayController (created later in ctor).

    // RadioState signals -> Center section updates
    // VFO-row indicator labels (split / vox / qsk / test / atu / msg bank)
    // are observed by VfoRowIndicatorController; see setupUi constructor.
    // RIT/XIT label state + wheel/click handling owned by RitXitController.

    // Filter position indicators
    // Filter indicator widgets (VFO A/B) observe RadioState directly via
    // FilterIndicatorWidget::observe() — see setupControllers().
    // AFX / AGC / APF button labels + APF VFO indicator driven by PopupManager's
    // wireRxRowButtonLabels() — logical home since PopupManager owns those buttons.

    // RadioState waterfall height -> mini-pans (panadapter wiring is in SpectrumController)
    connect(m_radioState, &RadioState::waterfallHeightChanged, this, [this](int percent) {
        m_vfoA->setMiniPanWaterfallHeight(percent);
        m_vfoB->setMiniPanWaterfallHeight(percent);
    });
}

void MainWindow::setupSpectrumDataRouting() {
    // Protocol spectrum data -> SpectrumController (via ConnectionController re-emitted signals)
    connect(m_connectionController, &ConnectionController::spectrumDataReceived, m_spectrumController,
            &SpectrumController::onSpectrumData);
    connect(m_connectionController, &ConnectionController::miniSpectrumDataReceived, m_spectrumController,
            &SpectrumController::onMiniSpectrumData);
}

void MainWindow::setupHardwareController() {
    // Hardware controller owns KPOD, HaliKey, IambicKeyer, SidetoneGenerator and their threads
    m_hardwareController = new HardwareController(m_radioState, m_connectionController, this);

    // CW controller wires the CW signal graph across the HardwareController-owned
    // devices. Constructed immediately after so the devices exist; takes them by
    // injected pointer. See cwcontroller.h.
    m_cwController = new CwController(m_radioState, m_connectionController, m_hardwareController->iambicKeyer(),
                                      m_hardwareController->sidetoneGenerator(), m_hardwareController->halikeyDevice(),
                                      m_hardwareController->kpodPlusDevice(), this);

    // KPOD button presses → macro execution
    connect(m_hardwareController, &HardwareController::macroRequested, m_macroController,
            &MacroController::executeMacro);

    // HaliKey footswitch PTT → TX audio + UI indicator
    connect(m_cwController, &CwController::pttRequested, this, [this](bool active) {
        if (m_connectionController->isConnected()) {
            m_audioController->setPttActive(active);
            m_bottomMenuBar->setPttActive(active);
        }
    });

    // RC-28 TRANSMIT is momentary PTT. Audio packets key the K4; the
    // radio's transmit-state echo drives the physical red LED.
    connect(m_hardwareController, &HardwareController::pttRequested, this, [this](bool active) {
        if (m_connectionController->isConnected()) {
            m_audioController->setPttActive(active);
            m_bottomMenuBar->setPttActive(active);
        }
    });

    // Hardware-side errors (HaliKey port-open failures today) → notification overlay
    connect(m_hardwareController, &HardwareController::hardwareError, this, &MainWindow::onHardwareError);

    // Transient hardware notices (RC-28 / FlexControl knob tuning-target change)
    // → notification overlay, brief.
    connect(m_hardwareController, &HardwareController::hardwareNotice, this, [this](const QString &message) {
        if (m_notificationWidget)
            m_notificationWidget->showMessage(message, 1500);
    });
}

void MainWindow::setupCatServer() {
    // CAT server for external app integration (WSJT-X, MacLoggerDX, etc.)
    // Apps connect using their built-in K4 support - no protocol translation needed
    m_catServer = new CatServer(m_radioState, this);
    m_catServer->setTcpClient(m_connectionController->tcpClient());

    // Forward CAT commands from external apps to the real K4
    connect(m_catServer, &CatServer::catCommandReceived, this, [this](const QString &command) {
        m_connectionController->sendCAT(command);
        // Optimistically update RadioState so the panadapter passband tracks immediately,
        // without waiting for the K4's AI4 roundtrip. Spectrum packets from the K4 arrive
        // before the CAT echo, so m_centerFreq moves while m_tunedFreq is still stale —
        // the passband goes off-screen until the echo lands. Mirrors what the VFO scroll
        // wheel handler already does: sendCAT + parseCATCommand together.
        m_radioState->parseCATCommand(command);
    });

    // Surface CAT server bind failures to the user
    connect(m_catServer, &CatServer::errorOccurred, this,
            [this](const QString &error) { qWarning() << "CAT server:" << error; });

    // TX;/RX; from external apps controls audio input gate
    // Audio stream itself triggers K4 TX - timing-critical for FT8/FT4
    connect(m_catServer, &CatServer::pttRequested, this, [this](bool on) {
        m_audioController->setPttActive(on);
        m_bottomMenuBar->setPttActive(on);
    });

    // Connect to settings for CAT server enable/disable
    connect(RadioSettings::instance(), &RadioSettings::catServerEnabledChanged, this, [this](bool enabled) {
        if (enabled) {
            m_catServer->start(RadioSettings::instance()->catServerPort());
        } else {
            m_catServer->stop();
        }
    });
    connect(RadioSettings::instance(), &RadioSettings::catServerPortChanged, this, [this](quint16 port) {
        if (RadioSettings::instance()->catServerEnabled()) {
            m_catServer->stop();
            m_catServer->start(port);
        }
    });

    // Start CAT server if enabled
    if (RadioSettings::instance()->catServerEnabled()) {
        m_catServer->start(RadioSettings::instance()->catServerPort());
    }
}

void MainWindow::setupMenuBar() {
    // Standard menu bar order: File, Connect, Tools, View, Help
    // On macOS, Qt automatically creates the app menu with About/Preferences
    menuBar()->setStyleSheet(QString("QMenuBar { background-color: %1; color: %2; }"
                                     "QMenuBar::item:selected { background-color: #333; }")
                                 .arg(K4Styles::Colors::DarkBackground, K4Styles::Colors::TextWhite));

    // File menu (first, per Windows convention)
    QMenu *fileMenu = menuBar()->addMenu("&File");
    QAction *quitAction = new QAction("E&xit", this);
    quitAction->setMenuRole(QAction::QuitRole); // macOS: moves to app menu
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QMainWindow::close);
    fileMenu->addAction(quitAction);

    // Tools menu
    QMenu *toolsMenu = menuBar()->addMenu("&Tools");
    QAction *optionsAction = new QAction("&Settings...", this);
    optionsAction->setMenuRole(QAction::PreferencesRole); // macOS: moves to app menu as Preferences
    connect(optionsAction, &QAction::triggered, this, [this]() {
        if (!m_optionsDialog) {
            m_optionsDialog = new OptionsDialog(m_radioState, m_audioController, m_hardwareController, m_catServer,
                                                m_kpa1500UiController->client(), m_dxClusterController, this);
        }
        m_optionsDialog->show();
        m_optionsDialog->raise();
        m_optionsDialog->activateWindow();
    });
    toolsMenu->addAction(optionsAction);

    // Help menu
    QMenu *helpMenu = menuBar()->addMenu("&Help");
    QAction *aboutAction = new QAction("&About QK4", this);
    aboutAction->setMenuRole(QAction::AboutRole); // macOS: moves to app menu
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, "About QK4",
                           QString("<h2>QK4</h2>"
                                   "<p>Version %1</p>"
                                   "<p>Remote control application for Elecraft K4 radios.</p>"
                                   "<p>Copyright &copy; 2024-2025 KF5O</p>"
                                   "<p><a href='https://github.com/mikeg-dal/QK4'>github.com/mikeg-dal/QK4</a></p>")
                               .arg(QCoreApplication::applicationVersion()));
    });
    helpMenu->addAction(aboutAction);
}

void MainWindow::setupUi() {
    setWindowTitle("QK4");
    setMinimumSize(1340, 840);
    resize(1340, 840); // Default to minimum size on launch

    // WHY: WA_NativeWindow is not set here.
    // Qt 6.10.1 bug on macOS Tahoe: WA_NativeWindow forces native window creation
    // before QRhiWidget can configure it for MetalSurface, causing
    // "QMetalSwapChain only supports MetalSurface windows" crash.

    setStyleSheet(QString("QMainWindow { background-color: %1; }").arg(K4Styles::Colors::Background));

    auto *centralWidget = new QWidget(this);
    centralWidget->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::Background));
    setCentralWidget(centralWidget);

    // Main vertical layout
    auto *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Top status bar — owned by StatusBarController
    m_statusBarController = new StatusBarController(m_radioState, m_connectionController,
                                                    m_connectionController->networkMetrics(), centralWidget, this);
    mainLayout->addWidget(m_statusBarController->widget());

    // Middle section: Side Panel + Main Content (L-shaped)
    auto *middleWidget = new QWidget(centralWidget);
    auto *middleLayout = new QHBoxLayout(middleWidget);
    middleLayout->setContentsMargins(0, 0, 0, 0);
    middleLayout->setSpacing(0);

    // Side Control Panel (left)
    m_sideControlPanel = new SideControlPanel(middleWidget);
    middleLayout->addWidget(m_sideControlPanel);

    // Main content (VFO + Spectrum)
    auto *contentWidget = new QWidget(middleWidget);
    auto *contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(4, 4, 4, 4);
    contentLayout->setSpacing(2);

    // VFO section (A | Center | B)
    auto *vfoWidget = new QWidget(contentWidget);
    setupVfoSection(vfoWidget);
    contentLayout->addWidget(vfoWidget);

    // Spectrum/Waterfall display (owned by SpectrumController)
    m_spectrumController->setupSpectrumUI(contentWidget, m_vfoA, m_vfoB);
    contentLayout->addWidget(m_spectrumController->spectrumContainer(), 1);

    middleLayout->addWidget(contentWidget, 1);

    // Right Side Panel (mirrors left panel dimensions)
    m_rightSidePanel = new RightSidePanel(middleWidget);
    middleLayout->addWidget(m_rightSidePanel);

    mainLayout->addWidget(middleWidget, 1);

    // Feature Menu Bar (popup, positioned above bottom menu bar when shown)
    m_featureMenuController = new FeatureMenuController(m_radioState, m_connectionController, this, this);

    // Mode popup (grid of mode buttons above the bottom menu bar) — owned
    // by ModePopupController, which also handles CAT dispatch + optimistic
    // DT updates + RadioState → popup sync. See controllers/modepopupcontroller.h.
    m_modePopupController = new ModePopupController(m_radioState, m_connectionController, this, this);

    // B-SET handling (SPLIT/B-SET label swap + side panel active-receiver
    // color) is split across VfoRowIndicatorController and
    // SideControlDisplayController. RIT/XIT redraw on BSET toggle lives in
    // RitXitController.

    // Bottom Menu Bar
    m_bottomMenuBar = new BottomMenuBar(centralWidget);
    m_popupManager->setBottomMenuBar(m_bottomMenuBar);
    mainLayout->addWidget(m_bottomMenuBar);

    // Connect side control panel icon buttons
    connect(m_sideControlPanel, &SideControlPanel::connectClicked, this, &MainWindow::showRadioManager);

    // Connect volume slider to AudioController (Main RX / VFO A)
    connect(m_sideControlPanel, &SideControlPanel::volumeChanged, this, [this](int value) {
        m_audioController->setMainVolume(value / 100.0f);
        RadioSettings::instance()->setVolume(value); // Persist setting
    });

    // Connect sub volume slider to AudioController (Sub RX / VFO B)
    // In BAL mode, this slider controls L/R balance offset instead of sub volume
    connect(m_sideControlPanel, &SideControlPanel::subVolumeChanged, this, [this](int value) {
        if (m_radioState->balanceMode() == 1) {
            // BAL mode: slider controls L/R balance (0-100 maps to -50..+50)
            int offset = value - 50;
            m_audioController->setBalanceOffset(offset);
            // Send BL command to radio with current mode and new offset
            QString sign = offset >= 0 ? "+" : "-";
            QString cmd = QString("BL1%1%2;").arg(sign).arg(qAbs(offset), 2, 10, QChar('0'));
            m_connectionController->sendCAT(cmd);
            m_radioState->setBalance(1, offset);
        } else {
            // NOR mode: slider controls sub RX volume
            m_audioController->setSubVolume(value / 100.0f);
        }
        RadioSettings::instance()->setSubVolume(value); // Persist setting
    });

    // SideControlPanel scroll signals are owned by SideControlScrollController
    // (constructed in setupControllers). 14 handlers + the shared HI/LO filter
    // edge math live there now.

    // Connect TX function button signals to CAT commands
    connect(m_sideControlPanel, &SideControlPanel::tuneClicked, this,
            [this]() { m_connectionController->sendCAT("SW16;"); });
    connect(m_sideControlPanel, &SideControlPanel::tuneLpClicked, this,
            [this]() { m_connectionController->sendCAT("SW131;"); });
    connect(m_sideControlPanel, &SideControlPanel::xmitClicked, this, [this]() {
        bool goTx = !m_radioState->isTransmitting();
        m_connectionController->sendCAT(goTx ? "TX;" : "RX;");
        m_audioController->setPttActive(goTx);
        m_bottomMenuBar->setPttActive(goTx);
    });
    connect(m_sideControlPanel, &SideControlPanel::testClicked, this,
            [this]() { m_connectionController->sendCAT("SW132;"); });
    connect(m_sideControlPanel, &SideControlPanel::atuClicked, this,
            [this]() { m_connectionController->sendCAT("SW158;"); });
    connect(m_sideControlPanel, &SideControlPanel::atuTuneClicked, this,
            [this]() { m_connectionController->sendCAT("SW40;"); });
    connect(m_sideControlPanel, &SideControlPanel::voxClicked, this,
            [this]() { m_connectionController->sendCAT("SW50;"); });
    connect(m_sideControlPanel, &SideControlPanel::qskClicked, this,
            [this]() { m_connectionController->sendCAT("SW134;"); });
    connect(m_sideControlPanel, &SideControlPanel::antClicked, this,
            [this]() { m_connectionController->sendCAT("SW60;"); });
    connect(m_sideControlPanel, &SideControlPanel::rxAntClicked, this,
            [this]() { m_connectionController->sendCAT("SW70;"); });
    connect(m_sideControlPanel, &SideControlPanel::subAntClicked, this,
            [this]() { m_connectionController->sendCAT("SW157;"); });

    // Connect MON/NORM/BAL SW commands
    connect(m_sideControlPanel, &SideControlPanel::swCommandRequested, this,
            [this](const QString &cmd) { m_connectionController->sendCAT(cmd); });

    // Connect monitor level change (ML command)
    connect(m_sideControlPanel, &SideControlPanel::monLevelChangeRequested, this, [this](int mode, int level) {
        QString cmd = QString("ML%1%2;").arg(mode).arg(level, 3, 10, QChar('0'));
        m_connectionController->sendCAT(cmd);
        // Optimistic update
        m_radioState->setMonitorLevel(mode, level);
    });

    // Update MON overlay when RadioState changes
    connect(m_radioState, &RadioState::monitorLevelChanged, m_sideControlPanel, &SideControlPanel::updateMonitorLevel);
    connect(m_radioState, &RadioState::modeChanged, this, [this](RadioState::Mode mode) {
        // Update MON overlay mode based on current operating mode
        int monMode = 2; // Default to voice
        if (mode == RadioState::CW || mode == RadioState::CW_R) {
            monMode = 0;
        } else if (mode == RadioState::DATA || mode == RadioState::DATA_R) {
            monMode = 1;
        }
        m_sideControlPanel->updateMonitorMode(monMode);
    });

    // Connect balance wheel signal (BL command)
    connect(m_sideControlPanel, &SideControlPanel::balChangeRequested, this, [this](int mode, int offset) {
        QString sign = offset >= 0 ? "+" : "-";
        QString cmd = QString("BL%1%2%3;").arg(mode).arg(sign).arg(qAbs(offset), 2, 10, QChar('0'));
        m_connectionController->sendCAT(cmd);
        m_radioState->setBalance(mode, offset);
    });

    // Update BAL overlay and button when RadioState changes
    connect(m_radioState, &RadioState::balanceChanged, m_sideControlPanel, &SideControlPanel::updateBalance);

    // RightSidePanel signal-to-CAT wiring lives on RightSideController.
    // Extracted from MainWindow as part of the Phase 6 → Phase A pass; mirror
    // of SideControlScrollController. Constructed here because all six of its
    // dependencies (RadioState, ConnectionController, RightSidePanel,
    // ModePopupController, FeatureMenuController, MacroController, and the
    // BottomMenuBar anchor) exist at this point in setupUi().
    m_rightSideController =
        new RightSideController(m_radioState, m_connectionController, m_rightSidePanel, m_modePopupController,
                                m_featureMenuController, m_macroController, m_bottomMenuBar, this);

    // Memory buttons (M1-M4, REC, STORE, RCL) and their right-click event
    // filter live on MemoryButtonsController. The buttons themselves are
    // created in setupVfoSection() for layout placement; the controller is
    // constructed there too because that's where the button pointers exist.

    // Connect bottom menu bar signals
    connect(m_bottomMenuBar, &BottomMenuBar::menuClicked, m_menuController, &MenuController::toggleOverlay);
    connect(m_bottomMenuBar, &BottomMenuBar::fnClicked, this, &MainWindow::toggleFnPopup);
    connect(m_bottomMenuBar, &BottomMenuBar::displayClicked, this, &MainWindow::toggleDisplayPopup);
    connect(m_bottomMenuBar, &BottomMenuBar::bandClicked, this, &MainWindow::toggleBandPopup);
    connect(m_bottomMenuBar, &BottomMenuBar::mainRxClicked, this, &MainWindow::toggleMainRxPopup);
    connect(m_bottomMenuBar, &BottomMenuBar::subRxClicked, this, &MainWindow::toggleSubRxPopup);
    connect(m_bottomMenuBar, &BottomMenuBar::txClicked, this, &MainWindow::toggleTxPopup);

    // PTT button connections
    connect(m_bottomMenuBar, &BottomMenuBar::pttPressed, this, [this]() {
        if (m_connectionController->isConnected()) {
            m_audioController->setPttActive(true);
            m_bottomMenuBar->setPttActive(true);
        }
    });
    connect(m_bottomMenuBar, &BottomMenuBar::pttReleased, this, [this]() {
        m_audioController->setPttActive(false);
        m_bottomMenuBar->setPttActive(false);
    });

    // WHY: no audio flush on mode/filter change. AudioEngine runs on a dedicated thread with
    // a properly sized jitter buffer, so stale audio doesn't accumulate; a flush here would
    // cause a brief dropout on every mode/filter switch.
}

void MainWindow::setupVfoSection(QWidget *parent) {
    // Main vertical layout: VFO row on top, antenna row below
    auto *mainVLayout = new QVBoxLayout(parent);
    mainVLayout->setContentsMargins(4, 4, 4, 4);
    mainVLayout->setSpacing(4);

    // Top row: VFO A | Center | VFO B
    auto *vfoRowWidget = new QWidget(parent);
    auto *layout = new QHBoxLayout(vfoRowWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    // ===== VFO A (Left - Amber) - Using VFOWidget =====
    m_vfoA = new VFOWidget(VFOWidget::VFO_A, parent);

    // Connect VFO A click to toggle mini-pan (send CAT to enable Mini-Pan streaming)
    connect(m_vfoA, &VFOWidget::normalContentClicked, this, [this]() {
        m_vfoA->showMiniPan();
        m_radioState->setMiniPanAEnabled(true);   // Set state BEFORE sending CAT (K4 doesn't echo)
        m_connectionController->sendCAT("#MP1;"); // Enable Mini-Pan A streaming
    });
    connect(m_vfoA, &VFOWidget::miniPanClicked, this, [this]() {
        m_radioState->setMiniPanAEnabled(false);  // Set state BEFORE sending CAT
        m_connectionController->sendCAT("#MP0;"); // Disable Mini-Pan A streaming
    });

    // Connect VFO A frequency entry - send FA command then query to refresh display
    connect(m_vfoA, &VFOWidget::frequencyEntered, this, [this](const QString &freqString) {
        // FA accepts 1-11 digits: 1-2 = MHz, 3-5 = kHz, 6+ = Hz
        m_connectionController->sendCAT(QString("FA%1;FA;").arg(freqString));
    });

    // Connect VFO A wheel tuning - same pattern as panadapter wheel tuning
    connect(m_vfoA, &VFOWidget::frequencyScrolled, this, [this](int steps) {
        if (!m_connectionController->isConnected())
            return;
        quint64 currentFreq = m_radioState->vfoA();
        int stepHz = RadioUtils::tuningStepToHz(m_radioState->tuningStep());
        qint64 newFreq = static_cast<qint64>(currentFreq) + static_cast<qint64>(steps) * stepHz;
        if (newFreq > 0) {
            QString cmd = QString("FA%1;").arg(static_cast<quint64>(newFreq), 11, 10, QChar('0'));
            m_connectionController->sendCAT(cmd);
            m_radioState->parseCATCommand(cmd);
        }
    });

    // Set Mini-Pan A passband color to cyan (matching VFO A theme)
    QColor vfoAPassband(K4Styles::Colors::VfoACyan);
    vfoAPassband.setAlpha(64);
    m_vfoA->setMiniPanPassbandColor(vfoAPassband);

    m_vfoA->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    layout->addWidget(m_vfoA, 1);

    // ===== Center Section =====
    auto *centerWidget = new QWidget(parent);
    centerWidget->setFixedWidth(330);
    centerWidget->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::Background));
    auto *centerLayout = new QVBoxLayout(centerWidget);
    centerLayout->setContentsMargins(4, 1, 4, 4);
    centerLayout->setSpacing(3);

    // Row 1: VFO Row with absolute positioning for perfect TX centering
    // Uses VfoRowWidget to position A, TX, B, SUB/DIV independently
    m_vfoRow = new VfoRowWidget(centerWidget);
    centerLayout->addWidget(m_vfoRow);

    // Filter/RIT/XIT row — declared early so it can be added to layout here,
    // populated later after the RIT/XIT box and filter widgets are constructed
    auto *filterRitXitRow = new QHBoxLayout();
    centerLayout->addLayout(filterRitXitRow);

    // Get pointers to VfoRowWidget children for signal connections
    m_vfoASquare = m_vfoRow->vfoASquare();
    m_vfoBSquare = m_vfoRow->vfoBSquare();
    m_modeALabel = m_vfoRow->modeALabel();
    m_modeBLabel = m_vfoRow->modeBLabel();
    m_txIndicator = m_vfoRow->txIndicator();
    m_txIndicator->setCursor(Qt::PointingHandCursor);
    m_txIndicator->installEventFilter(this);
    m_txTriangle = m_vfoRow->txTriangle();
    m_txTriangleB = m_vfoRow->txTriangleB();
    m_subLabel = m_vfoRow->subLabel();
    m_divLabel = m_vfoRow->divLabel();

    // Install event filters for clickable labels
    m_vfoASquare->installEventFilter(this);
    m_vfoBSquare->installEventFilter(this);
    m_modeALabel->installEventFilter(this);
    m_modeBLabel->installEventFilter(this);

    // SPLIT and MSG Bank labels live in VfoRowWidget (positioned under TX).
    // B-SET label is also a VfoRowWidget child but is accessed directly via
    // m_vfoRow->bSetLabel() at VfoRowIndicatorController construction — no
    // MainWindow member needed.
    m_splitLabel = m_vfoRow->splitLabel();
    m_msgBankLabel = m_vfoRow->msgBankLabel();

    // RIT/XIT Box with border - constrained size
    // Supports mouse wheel to adjust RIT/XIT offset
    m_ritXitBox = new QWidget(centerWidget);
    m_ritXitBox->setObjectName("ritXitBox");
    m_ritXitBox->setStyleSheet(QString("#ritXitBox { border: 1px solid %1; }").arg(K4Styles::Colors::InactiveGray));
    m_ritXitBox->setMaximumWidth(80);
    m_ritXitBox->setMaximumHeight(40);
    m_ritXitBox->installEventFilter(this);
    auto *ritXitLayout = new QVBoxLayout(m_ritXitBox);
    ritXitLayout->setContentsMargins(1, 2, 1, 2);
    ritXitLayout->setSpacing(1);

    auto *ritXitLabelsRow = new QHBoxLayout();
    ritXitLabelsRow->setContentsMargins(11, 0, 11, 0);
    ritXitLabelsRow->setSpacing(8);

    m_ritLabel = new QLabel("RIT", m_ritXitBox);
    m_ritLabel->setStyleSheet(QString("color: %1; font-size: %2px; border: none;")
                                  .arg(K4Styles::Colors::InactiveGray)
                                  .arg(K4Styles::Dimensions::FontSizeMedium));
    m_ritLabel->setCursor(Qt::PointingHandCursor);
    m_ritLabel->installEventFilter(this);
    ritXitLabelsRow->addWidget(m_ritLabel);

    m_xitLabel = new QLabel("XIT", m_ritXitBox);
    m_xitLabel->setStyleSheet(QString("color: %1; font-size: %2px; border: none;")
                                  .arg(K4Styles::Colors::InactiveGray)
                                  .arg(K4Styles::Dimensions::FontSizeMedium));
    m_xitLabel->setCursor(Qt::PointingHandCursor);
    m_xitLabel->installEventFilter(this);
    ritXitLabelsRow->addWidget(m_xitLabel);

    ritXitLabelsRow->setAlignment(Qt::AlignCenter);
    ritXitLayout->addLayout(ritXitLabelsRow);

    // Separator line between labels and value (spans full width)
    auto *ritXitSeparator = new QFrame(m_ritXitBox);
    ritXitSeparator->setFrameShape(QFrame::HLine);
    ritXitSeparator->setFrameShadow(QFrame::Plain);
    ritXitSeparator->setStyleSheet(QString("background-color: %1; border: none;").arg(K4Styles::Colors::InactiveGray));
    ritXitSeparator->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    ritXitLayout->addWidget(ritXitSeparator);

    m_ritXitValueLabel = new QLabel("+0.00", m_ritXitBox);
    m_ritXitValueLabel->setAlignment(Qt::AlignCenter);
    m_ritXitValueLabel->setStyleSheet(
        QString("color: %1; font-size: %2px; font-weight: bold; border: none; padding: 0 11px;")
            .arg(K4Styles::Colors::InactiveGray)
            .arg(K4Styles::Dimensions::FontSizePopup)); // Grey until RIT/XIT is enabled
    m_ritXitValueLabel->installEventFilter(this);
    ritXitLayout->addWidget(m_ritXitValueLabel);

    // Populate filter/RIT/XIT row (layout was declared and added to centerLayout earlier)
    filterRitXitRow->setContentsMargins(0, 0, 0, 0);
    filterRitXitRow->setSpacing(0);

    // VFO A filter indicator (cyan — matches VFO A square/slider theme)
    m_filterAWidget = new FilterIndicatorWidget(centerWidget);
    const QColor vfoACyan(K4Styles::Colors::VfoACyan);
    m_filterAWidget->setShapeColor(vfoACyan, vfoACyan);

    // VFO B filter indicator (green — matches VFO B square/slider theme)
    m_filterBWidget = new FilterIndicatorWidget(centerWidget);
    const QColor vfoBGreen(K4Styles::Colors::VfoBGreen);
    m_filterBWidget->setShapeColor(vfoBGreen, vfoBGreen);

    // Layout: [stretch] [FIL_A] [spacer] [RIT/XIT] [spacer] [FIL_B] [stretch]
    // Spacers push filters outward to align under VFO A/B squares above
    filterRitXitRow->addStretch();
    filterRitXitRow->addWidget(m_filterAWidget);
    filterRitXitRow->addSpacing(18);
    filterRitXitRow->addWidget(m_ritXitBox);
    filterRitXitRow->addSpacing(18);
    filterRitXitRow->addWidget(m_filterBWidget);
    filterRitXitRow->addStretch();

    // VOX / ATU / QSK indicator row (fixed-height container so visibility toggles don't shift layout)
    auto *indicatorContainer = new QWidget(centerWidget);
    indicatorContainer->setFixedHeight(K4Styles::Dimensions::DialogMargin);
    auto *indicatorLayout = new QHBoxLayout(indicatorContainer);
    indicatorLayout->setContentsMargins(0, 0, 0, 0);
    indicatorLayout->setSpacing(8);

    indicatorLayout->addStretch();

    // VOX indicator - orange when on, grey when off
    m_voxLabel = new QLabel("VOX", indicatorContainer);
    m_voxLabel->setAlignment(Qt::AlignCenter);
    m_voxLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                  .arg(K4Styles::Colors::TextGray)
                                  .arg(K4Styles::Dimensions::FontSizeLarge));
    indicatorLayout->addWidget(m_voxLabel);

    // ATU indicator (orange when AUTO, grey when off)
    m_atuLabel = new QLabel("ATU", indicatorContainer);
    m_atuLabel->setAlignment(Qt::AlignCenter);
    m_atuLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                  .arg(K4Styles::Colors::TextGray)
                                  .arg(K4Styles::Dimensions::FontSizeLarge));
    indicatorLayout->addWidget(m_atuLabel);

    // QSK indicator - white when on, grey when off
    m_qskLabel = new QLabel("QSK", indicatorContainer);
    m_qskLabel->setAlignment(Qt::AlignCenter);
    m_qskLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                  .arg(K4Styles::Colors::TextGray)
                                  .arg(K4Styles::Dimensions::FontSizeLarge));
    indicatorLayout->addWidget(m_qskLabel);

    indicatorLayout->addStretch();

    centerLayout->addWidget(indicatorContainer);

    // ===== Memory Buttons Row (M1-M4, REC, STORE, RCL) =====
    centerLayout->addStretch(); // Push buttons to vertical center

    // Helper lambda to create memory button with optional sub-label
    // Uses sidePanelButton/sidePanelButtonLight styles for consistency
    // Container: VBox with 2px spacing, button centered, sub-label below
    // Button: MemoryButtonWidth x ButtonHeightSmall (42x28)
    // Sub-label: FontSizeSmall (8px), AccentAmber color
    auto createMemoryButton = [centerWidget](const QString &label, const QString &subLabel,
                                             bool isLighter) -> QWidget * {
        auto *container = new QWidget(centerWidget);
        auto *layout = new QVBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(2);

        auto *btn = new QPushButton(label, container);
        btn->setFixedSize(K4Styles::Dimensions::MemoryButtonWidth, K4Styles::Dimensions::ButtonHeightSmall);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(isLighter ? K4Styles::sidePanelButtonLight() : K4Styles::sidePanelButton());
        layout->addWidget(btn, 0, Qt::AlignHCenter);

        // Add sub-label if provided
        if (!subLabel.isEmpty()) {
            auto *sub = new QLabel(subLabel, container);
            sub->setStyleSheet(QString("color: %1; font-size: %2px;")
                                   .arg(K4Styles::Colors::AccentAmber)
                                   .arg(K4Styles::Dimensions::FontSizeSmall));
            sub->setAlignment(Qt::AlignCenter);
            layout->addWidget(sub);
        }

        return container;
    };

    // Single row: M1-M4 group, REC, STORE, RCL (all centered)
    auto *memoryRow = new QHBoxLayout();
    memoryRow->setContentsMargins(0, 0, 0, 0);
    memoryRow->setSpacing(4);

    memoryRow->addStretch();

    // M1-M4 group with MESSAGE label underneath
    auto *messageGroup = new QWidget(centerWidget);
    auto *messageGroupLayout = new QVBoxLayout(messageGroup);
    messageGroupLayout->setContentsMargins(0, 0, 0, 0);
    messageGroupLayout->setSpacing(2);

    // M1-M4 button row
    auto *m1m4Row = new QHBoxLayout();
    m1m4Row->setContentsMargins(0, 0, 0, 0);
    m1m4Row->setSpacing(4);

    // Helper to create just a button (no sub-label container)
    // Button: MemoryButtonWidth x ButtonHeightSmall (42x28), dark sidePanelButton style
    auto createSimpleButton = [centerWidget](const QString &label) -> QPushButton * {
        auto *btn = new QPushButton(label, centerWidget);
        btn->setFixedSize(K4Styles::Dimensions::MemoryButtonWidth, K4Styles::Dimensions::ButtonHeightSmall);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(K4Styles::sidePanelButton());
        return btn;
    };

    QPushButton *m1Btn = createSimpleButton("M1");
    m1m4Row->addWidget(m1Btn);

    QPushButton *m2Btn = createSimpleButton("M2");
    m1m4Row->addWidget(m2Btn);

    QPushButton *m3Btn = createSimpleButton("M3");
    m1m4Row->addWidget(m3Btn);

    QPushButton *m4Btn = createSimpleButton("M4");
    m1m4Row->addWidget(m4Btn);

    messageGroupLayout->addLayout(m1m4Row);

    // MESSAGE label with connecting lines: ——— MESSAGE ———
    auto *messageLabel = new QWidget(messageGroup);
    auto *messageLabelLayout = new QHBoxLayout(messageLabel);
    messageLabelLayout->setContentsMargins(0, 0, 0, 0);
    messageLabelLayout->setSpacing(2);

    auto *leftLine = new QFrame(messageLabel);
    leftLine->setFrameShape(QFrame::HLine);
    leftLine->setStyleSheet(QString("background-color: %1; max-height: 1px;").arg(K4Styles::Colors::BorderSelected));
    leftLine->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);

    auto *msgText = new QLabel("MESSAGE", messageLabel);
    msgText->setStyleSheet(QString("color: %1; font-size: %2px;")
                               .arg(K4Styles::Colors::BorderSelected)
                               .arg(K4Styles::Dimensions::FontSizeSmall));
    msgText->setAlignment(Qt::AlignCenter);

    auto *rightLine = new QFrame(messageLabel);
    rightLine->setFrameShape(QFrame::HLine);
    rightLine->setStyleSheet(QString("background-color: %1; max-height: 1px;").arg(K4Styles::Colors::BorderSelected));
    rightLine->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);

    messageLabelLayout->addWidget(leftLine, 1);
    messageLabelLayout->addWidget(msgText, 0);
    messageLabelLayout->addWidget(rightLine, 1);

    messageGroupLayout->addWidget(messageLabel);
    memoryRow->addWidget(messageGroup);

    // REC (dark grey like M keys, BANK sub-label)
    auto *recContainer = createMemoryButton("REC", "BANK", false);
    QPushButton *recBtn = recContainer->findChild<QPushButton *>();
    memoryRow->addWidget(recContainer);

    // STORE (lighter grey, AF REC sub-label)
    auto *storeContainer = createMemoryButton("STORE", "AF REC", true);
    QPushButton *storeBtn = storeContainer->findChild<QPushButton *>();
    memoryRow->addWidget(storeContainer);

    // RCL (lighter grey, AF PLAY sub-label)
    auto *rclContainer = createMemoryButton("RCL", "AF PLAY", true);
    QPushButton *rclBtn = rclContainer->findChild<QPushButton *>();
    memoryRow->addWidget(rclContainer);

    memoryRow->addStretch();
    centerLayout->addLayout(memoryRow);

    // Owns left+right click wiring for the M1-M4 / REC / STORE / RCL buttons.
    m_memoryButtonsController =
        new MemoryButtonsController(m_connectionController, m1Btn, m2Btn, m3Btn, m4Btn, recBtn, storeBtn, rclBtn, this);

    centerLayout->addStretch(); // Balance below
    layout->addWidget(centerWidget);

    // ===== VFO B (Right - Cyan) - Using VFOWidget =====
    m_vfoB = new VFOWidget(VFOWidget::VFO_B, parent);

    // Set Mini-Pan B passband color to green (matching VFO B theme)
    QColor vfoBPassband(K4Styles::Colors::VfoBGreen);
    vfoBPassband.setAlpha(64);
    m_vfoB->setMiniPanPassbandColor(vfoBPassband);

    // Connect VFO B click to toggle mini-pan (send CAT to enable Mini-Pan streaming)
    // Only allow mini pan B if SUB RX is on or VFOs are on the same band
    connect(m_vfoB, &VFOWidget::normalContentClicked, this, [this]() {
        // Block mini pan B if VFOs are on different bands and SUB RX is off
        // (K4 cannot provide separate Sub RX spectrum without SUB RX enabled)
        if (RadioUtils::getBandFromFrequency(m_radioState->vfoA()) !=
                RadioUtils::getBandFromFrequency(m_radioState->vfoB()) &&
            !m_radioState->subReceiverEnabled()) {
            qCDebug(qk4Main) << "Mini-Pan B blocked: VFOs on different bands and SUB RX is off";
            return;
        }
        m_vfoB->showMiniPan();
        m_radioState->setMiniPanBEnabled(true);    // Set state BEFORE sending CAT (K4 doesn't echo)
        m_connectionController->sendCAT("#MP$1;"); // Enable Mini-Pan B (Sub RX) streaming
    });
    connect(m_vfoB, &VFOWidget::miniPanClicked, this, [this]() {
        m_radioState->setMiniPanBEnabled(false);   // Set state BEFORE sending CAT
        m_connectionController->sendCAT("#MP$0;"); // Disable Mini-Pan B streaming
    });

    // Connect VFO B frequency entry - send FB command then query to refresh display
    connect(m_vfoB, &VFOWidget::frequencyEntered, this, [this](const QString &freqString) {
        // FB accepts 1-11 digits: 1-2 = MHz, 3-5 = kHz, 6+ = Hz
        m_connectionController->sendCAT(QString("FB%1;FB;").arg(freqString));
    });

    // Connect VFO B wheel tuning - same pattern as panadapter wheel tuning
    connect(m_vfoB, &VFOWidget::frequencyScrolled, this, [this](int steps) {
        if (!m_connectionController->isConnected())
            return;
        quint64 currentFreq = m_radioState->vfoB();
        int stepHz = RadioUtils::tuningStepToHz(m_radioState->tuningStepB());
        qint64 newFreq = static_cast<qint64>(currentFreq) + static_cast<qint64>(steps) * stepHz;
        if (newFreq > 0) {
            QString cmd = QString("FB%1;").arg(static_cast<quint64>(newFreq), 11, 10, QChar('0'));
            m_connectionController->sendCAT(cmd);
            m_radioState->parseCATCommand(cmd);
        }
    });

    m_vfoB->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    layout->addWidget(m_vfoB, 1);

    // Add the VFO row to main layout
    mainVLayout->addWidget(vfoRowWidget);

    // ===== Antenna Row (below VFO section) =====
    // Layout: [RX Ant A (left)] --- [TX Antenna (center)] --- [RX Ant B (right)]
    auto *antennaRow = new QHBoxLayout();
    antennaRow->setContentsMargins(8, 0, 8, 0);
    antennaRow->setSpacing(0);

    // RX Antenna A (Main) - white color, left-justified
    m_rxAntALabel = new QLabel("1:ANT1", parent);
    m_rxAntALabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_rxAntALabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                     .arg(K4Styles::Colors::TextWhite)
                                     .arg(K4Styles::Dimensions::FontSizeLarge));
    antennaRow->addWidget(m_rxAntALabel);

    antennaRow->addStretch(1); // Push TX antenna to center

    // TX Antenna - orange color, centered
    m_txAntennaLabel = new QLabel("1:ANT1", parent);
    m_txAntennaLabel->setAlignment(Qt::AlignCenter);
    m_txAntennaLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                        .arg(K4Styles::Colors::AccentAmber)
                                        .arg(K4Styles::Dimensions::FontSizeLarge));
    antennaRow->addWidget(m_txAntennaLabel);

    antennaRow->addStretch(1); // Push RX Ant B to right

    // RX Antenna B (Sub) - white color, right-justified
    m_rxAntBLabel = new QLabel("1:ANT1", parent);
    m_rxAntBLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_rxAntBLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                     .arg(K4Styles::Colors::TextWhite)
                                     .arg(K4Styles::Dimensions::FontSizeLarge));
    antennaRow->addWidget(m_rxAntBLabel);

    mainVLayout->addLayout(antennaRow);
}

void MainWindow::showRadioManager() {
    // Toggle: the globe icon opens the Server Manager and a second click closes it. The dialog is
    // shown modeless (not exec()) so the icon stays clickable while it's open.
    if (m_radioManager) {
        m_radioManager->reject(); // fires finished() → teardown below
        return;
    }

    m_radioManager = new RadioManagerDialog(this);
    connect(m_radioManager, &RadioManagerDialog::connectRequested, this, &MainWindow::connectToRadio);
    connect(m_radioManager, &RadioManagerDialog::disconnectRequested, this, [this]() {
        // TcpClient::disconnectFromHost() sends RRN; automatically
        m_connectionController->disconnectFromRadio();
    });

    // Send SL live if changed while connected (K4 does not echo SL, so update optimistically)
    connect(m_radioManager, &RadioManagerDialog::streamingLatencyChanged, this, [this](int tier) {
        if (m_connectionController->isConnected()) {
            m_connectionController->sendCAT(QString("SL%1;").arg(tier));
            m_radioState->parseCATCommand(QString("SL%1;").arg(tier));
        }
    });

    // Single teardown path for every close (Connect/Disconnect accept(), Cancel/icon reject()):
    // drop the highlight, delete the dialog, and clear the pointer so the next click reopens.
    connect(m_radioManager, &QDialog::finished, this, [this](int) {
        m_sideControlPanel->setServerManagerActive(false);
        m_radioManager->deleteLater();
        m_radioManager = nullptr;
    });

    // Set the connected host so dialog can show "Disconnect" for active connection
    if (m_connectionController->isConnected()) {
        m_radioManager->setConnectedHost(m_connectionController->currentRadio().host);
    }

    m_sideControlPanel->setServerManagerActive(true);
    m_radioManager->show();
    m_radioManager->raise();
    m_radioManager->activateWindow();
}

void MainWindow::connectToRadio(const RadioEntry &radio) {
    m_statusBarController->setTitle("Elecraft K4 - " + radio.name);
    m_connectionController->connectToRadio(radio);
}

void MainWindow::onConnectionStateChanged(TcpClient::ConnectionState state) {
    updateConnectionState(state);
}

void MainWindow::onConnectionError(const QString &error) {
    m_statusBarController->showError(error);
}

void MainWindow::onHardwareError(const QString &error) {
    if (m_notificationWidget) {
        m_notificationWidget->showMessage(error, 5000);
    }
}

void MainWindow::onRadioReady() {
    qCDebug(qk4Main) << "Successfully authenticated with K4 radio";

    // WHY we send SL here and also call parseCATCommand optimistically:
    // The K4 does NOT echo `SL` (streaming latency) responses — it silently applies the new tier
    // without ever sending back `SL<n>;`. Two consequences:
    //   (1) We must send `SL<n>;` AFTER the `RDY;` dump so our choice overrides whatever tier
    //       the radio booted with; sending before RDY would be trampled by the dump's playback.
    //   (2) Because no echo ever arrives, RadioState would never learn the new value from the
    //       normal parse path. We update it optimistically so AudioEngine frame sizing and the
    //       UI display tier match what we just requested. Tier→packet-size map verified in
    //       `memory/k4-streaming-latency.md`.
    int sl = m_connectionController->currentRadio().streamingLatency;
    m_connectionController->sendCAT(QString("SL%1;").arg(sl));
    m_radioState->parseCATCommand(QString("SL%1;").arg(sl));

    // Start audio engine via AudioController
    m_audioController->startAudio(m_sideControlPanel->volume() / 100.0f, m_sideControlPanel->subVolume() / 100.0f,
                                  RadioSettings::instance()->micGain() / 100.0f);

    // Most state is already included in the RDY; response from TcpClient.
    // Only query commands NOT included in RDY dump:
    m_connectionController->sendCAT("#DSM;");  // Display mode (LCD) - not in RDY
    m_connectionController->sendCAT("#HDSM;"); // Display mode (EXT) - not in RDY
    m_connectionController->sendCAT("#FRZ;");  // Freeze - not in RDY
    m_connectionController->sendCAT(
        "#FPS15;"); // Set display FPS to 15 on connect (12 default is too slow for large monitors)
    m_connectionController->sendCAT("#FPS;"); // Query back to confirm and update menu
    m_connectionController->sendCAT("#SCL;"); // Panadapter scale - not in RDY, needed for dB range
    m_connectionController->sendCAT("PS;");   // Remote power state - not in RDY, drives the status-bar power button
    // Note: ML and KP commands come in RDY; dump - no need to query

    // Sync element length with K4 server (sent in RDY dump as KZLnn)
    if (m_radioState->keyerSpeed() > 0) {
        int ditMs = 1200 / m_radioState->keyerSpeed();
        m_connectionController->sendCAT(QString("KZL%1;").arg(ditMs, 2, 10, QChar('0')));
    }

    // Create synthetic "Display FPS" menu item (will update from radio echo)
    m_menuController->addSyntheticDisplayFpsItem(15);

    // Startup macro is sent pre-RDY by TcpClient so the state dump reflects changes.

    // KPA connectivity is gated on K4 connectivity — see KPA1500UiController.
    m_kpa1500UiController->connectIfEnabled();

    // Auto-connect all DX cluster entries marked for auto-connect
    QString dxCall = RadioSettings::instance()->dxClusterCallsign();
    if (!dxCall.isEmpty()) {
        auto dxClusters = RadioSettings::instance()->dxClusters();
        for (int i = 0; i < dxClusters.size(); ++i) {
            if (dxClusters[i].autoConnect && !dxClusters[i].host.isEmpty()) {
                m_dxClusterController->connectCluster(i, dxClusters[i].host, dxClusters[i].port, dxCall);
            }
        }
    }
}

void MainWindow::onAuthFailed() {
    qCDebug(qk4Main) << "Authentication failed";
    m_statusBarController->showAuthFailed();
}

void MainWindow::onCatResponse(const QString &response) {

    // Parse CAT commands (may contain multiple commands separated by ;)
    QStringList commands = response.split(';', Qt::SkipEmptyParts);
    for (const QString &cmd : commands) {
        // PONG is handled by TcpClient for latency measurement — skip
        if (cmd.startsWith("PONG"))
            continue;

        m_radioState->parseCATCommand(cmd + ";");

        // Menu system routing — MenuController encapsulates the MenuModel.
        // BN / BN$ are parsed by BandNavigationController which subscribes
        // to catResponseReceived directly; not handled here.
        if (cmd.startsWith("MEDF")) {
            m_menuController->ingestMedf(cmd + ";");
        } else if (cmd.startsWith("ME")) {
            m_menuController->ingestMe(cmd + ";");
        }
    }
}

void MainWindow::updateConnectionState(TcpClient::ConnectionState state) {
    switch (state) {
    case TcpClient::Disconnected:
        m_statusBarController->showDisconnected();
        resetUiForDisconnect();
        break;
    case TcpClient::Connecting:
    case TcpClient::Authenticating:
        m_statusBarController->showConnecting();
        break;
    case TcpClient::Connected:
        m_statusBarController->showConnected();
        break;
    }
}

// Disconnect-path helper. Each owning controller's reset() absorbs the
// label writes that used to live here directly.
void MainWindow::resetUiForDisconnect() {
    m_audioController->stopAudio();
    m_spectrumController->clearDisplays();
    m_vfoA->resetToDefaults();
    m_vfoB->resetToDefaults();
    m_vfoRow->setLockA(false);
    m_vfoRow->setLockB(false);
    m_sideControlPanel->resetToDefaults();
    m_filterAWidget->resetToDefaults();
    m_filterBWidget->resetToDefaults();
    m_statusBarController->clearReadings();
    m_menuController->clearModel();
    m_kpa1500UiController->disconnectFromHost();

    m_modeLabelController->reset();
    m_antennaDisplayController->reset();
    m_txStateController->reset();
    m_vfoRowIndicatorController->reset();
    m_subDivIndicatorController->reset();
    m_ritXitController->reset();

    m_radioState->reset();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    // Clicks on VFO-A square / mode label → mode popup targeted at VFO A.
    if ((watched == m_vfoASquare || watched == m_modeALabel) && event->type() == QEvent::MouseButtonPress) {
        m_modePopupController->toggleForVfoA(m_bottomMenuBar);
        return true;
    }

    // Clicks on VFO-B square / mode label → mode popup targeted at VFO B.
    if ((watched == m_vfoBSquare || watched == m_modeBLabel) && event->type() == QEvent::MouseButtonPress) {
        m_modePopupController->toggleForVfoB(m_bottomMenuBar);
        return true;
    }

    // Panadapter resize events handled by SpectrumController's eventFilter.
    // Memory button right-clicks (REC/STORE/RCL) handled by
    // MemoryButtonsController's own eventFilter.

    // RIT / XIT label clicks + wheel routed to RitXitController.
    if (watched == m_ritLabel && event->type() == QEvent::MouseButtonPress)
        return m_ritXitController->handleRitLabelClick();
    if (watched == m_xitLabel && event->type() == QEvent::MouseButtonPress)
        return m_ritXitController->handleXitLabelClick();
    if (event->type() == QEvent::Wheel &&
        (watched == m_ritXitBox || watched == m_ritLabel || watched == m_xitLabel || watched == m_ritXitValueLabel)) {
        return m_ritXitController->handleWheel(static_cast<QWheelEvent *>(event));
    }

    // TX indicator click - toggle split on/off
    if (watched == m_txIndicator && event->type() == QEvent::MouseButtonPress) {
        m_connectionController->sendCAT("SW145;");
        return true;
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::moveEvent(QMoveEvent *event) {
    QMainWindow::moveEvent(event);
    closeAllPopups();
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
    // Handle F1-F12 for keyboard macros
    if (event->key() >= Qt::Key_F1 && event->key() <= Qt::Key_F12) {
        int fKeyNum = event->key() - Qt::Key_F1 + 1; // 1-12
        QString functionId = QString("Keyboard-F%1").arg(fKeyNum);
        m_macroController->executeMacro(functionId);
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

// WHY: toggle handlers close only popups NOT owned by PopupManager before
// delegating to PopupManager::toggleX. PopupManager::toggleX captures the
// target popup's visibility up-front, then calls its own closeOwnedPopups —
// so if closeAllPopups ran first, toggleX would see the popup as already
// hidden and always re-open it (i.e., the toggle would never close).
void MainWindow::closeNonPopupManagerPopups() {
    if (m_menuController && m_menuController->isOverlayVisible()) {
        m_menuController->closeOverlay();
        if (m_bottomMenuBar)
            m_bottomMenuBar->setMenuActive(false);
    }
    m_antennaCfgController->closeAll();
    if (m_modePopupController)
        m_modePopupController->close();
}

void MainWindow::closeAllPopups() {
    closeNonPopupManagerPopups();
    m_popupManager->closeOwnedPopups();
}

void MainWindow::toggleDisplayPopup() {
    closeNonPopupManagerPopups();
    m_popupManager->toggleDisplay();
}

void MainWindow::toggleBandPopup() {
    closeNonPopupManagerPopups();
    m_popupManager->toggleBand();
}

void MainWindow::toggleFnPopup() {
    closeNonPopupManagerPopups();
    m_popupManager->toggleFn();
}

void MainWindow::toggleMainRxPopup() {
    closeNonPopupManagerPopups();
    m_popupManager->toggleMainRx();
}

void MainWindow::toggleSubRxPopup() {
    closeNonPopupManagerPopups();
    m_popupManager->toggleSubRx();
}

void MainWindow::toggleTxPopup() {
    closeNonPopupManagerPopups();
    m_popupManager->toggleTx();
}
