#include "mainwindow.h"
#include "utils/radioutils.h"
#include "ui/radiomanagerdialog.h"
#include "ui/sidecontrolpanel.h"
#include "ui/rightsidepanel.h"
#include "ui/kpa1500minipanel.h"
#include "ui/bottommenubar.h"
#include "ui/featuremenubar.h"
#include "ui/modepopupwidget.h"
#include "ui/menuoverlay.h"
#include "ui/bandpopupwidget.h"
#include "ui/displaypopupwidget.h"
#include "ui/buttonrowpopup.h"
#include "ui/fnpopupwidget.h"
#include "ui/rxeqpopupwidget.h"
#include "ui/macrodialog.h"
#include "ui/optionsdialog.h"
#include "ui/notificationwidget.h"
#include "ui/vforowwidget.h"
#include "ui/filterindicatorwidget.h"
#include "ui/k4styles.h"
#include "ui/antennacfgpopup.h"
#include "ui/lineoutpopup.h"
#include "ui/lineinpopup.h"
#include "ui/micinputpopup.h"
#include "ui/micconfigpopup.h"
#include "ui/voxpopup.h"
#include "ui/ssbbwpopup.h"
#include "ui/keyingweightpopup.h"
#include "ui/textdecodewindow.h"
#include "models/menumodel.h"
#include "controllers/spectrumcontroller.h"
#include "dsp/panadapter_rhi.h"
#include "dsp/minipan_rhi.h"
#include "ui/frequencydisplaywidget.h"
#include "controllers/audiocontroller.h"
#include "controllers/hardwarecontroller.h"
#include "network/kpa1500client.h"
#include "network/catserver.h"
#include "network/networkmetrics.h"
#include "ui/nethealthwidget.h"
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

Q_LOGGING_CATEGORY(qk4Main, "qk4.main")

// ============== MainWindow Implementation ==============
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), m_radioState(new RadioState(this)), m_clockTimer(new QTimer(this)),
      m_menuModel(new MenuModel(this)), m_menuOverlay(nullptr) {
    // Connection controller owns TcpClient, I/O thread, and NetworkMetrics
    m_connectionController = new ConnectionController(m_radioState, this);

    // Audio controller owns AudioEngine, Opus codecs, audio thread, and PTT state
    m_audioController = new AudioController(m_connectionController, m_radioState, this);

    // Spectrum controller owns panadapters, span buttons, and all spectrum wiring
    m_spectrumController = new SpectrumController(m_connectionController, m_radioState, this);

    // IMPORTANT: setupUi() MUST be called BEFORE setupMenuBar()!
    // Qt 6.10.1 bug on macOS Tahoe: calling menuBar() before creating QRhiWidget
    // prevents the RHI backing store from being set up correctly, causing
    // "QRhiWidget: No QRhi" errors and blank panadapter display.
    setupUi();
    setupMenuBar();

    // Menu items are populated from MEDF responses in onCatResponse()
    // when the radio sends RDY; after connection

    // Create menu overlay (positioned over spectrum container)
    m_menuOverlay = new MenuOverlayWidget(m_menuModel, this);
    m_menuOverlay->hide();

    // Connect menu overlay signals
    connect(m_menuOverlay, &MenuOverlayWidget::menuValueChangeRequested, this, &MainWindow::onMenuValueChangeRequested);
    connect(m_menuOverlay, &MenuOverlayWidget::closed, this, [this]() {
        if (m_bottomMenuBar) {
            m_bottomMenuBar->setMenuActive(false);
        }
    });

    // Connect menu model value changes for display settings
    connect(m_menuModel, &MenuModel::menuValueChanged, this, &MainWindow::onMenuModelValueChanged);

    // Also check initial values when menu items are first loaded from MEDF
    connect(m_menuModel, &MenuModel::menuItemAdded, this, [this](int menuId) {
        const MenuItem *item = m_menuModel->getMenuItem(menuId);
        if (item && item->name == "Spectrum Amplitude Units") {
            bool useSUnits = (item->currentValue == 1);
            qCDebug(qk4Main) << "Initial spectrum amplitude units:" << (useSUnits ? "S-UNITS" : "dBm");
            if (m_spectrumController->panadapterA()) {
                m_spectrumController->panadapterA()->setAmplitudeUnits(useSUnits);
            }
            if (m_spectrumController->panadapterB()) {
                m_spectrumController->panadapterB()->setAmplitudeUnits(useSUnits);
            }
        }
        if (item && item->name == "Mouse L/R Button QSY") {
            m_mouseQsyMenuId = item->id;
            m_mouseQsyMode = item->currentValue;
            m_spectrumController->setMouseQsyMode(m_mouseQsyMode);
            qCDebug(qk4Main) << "Mouse L/R Button QSY: menuId=" << m_mouseQsyMenuId << "mode=" << m_mouseQsyMode;
        }
        if (item && item->name == "FSK Mark-Tone") {
            m_fskMarkToneMenuId = item->id;
            int toneHz = item->options[item->currentValue].toInt();
            qCDebug(qk4Main) << "FSK Mark-Tone: menuId=" << m_fskMarkToneMenuId << "tone=" << toneHz << "Hz";
            if (m_spectrumController->panadapterA())
                m_spectrumController->panadapterA()->setFskMarkTone(toneHz);
            if (m_spectrumController->panadapterB())
                m_spectrumController->panadapterB()->setFskMarkTone(toneHz);
        }
    });

    // Create band selection popup
    m_bandPopup = new BandPopupWidget(this);
    connect(m_bandPopup, &BandPopupWidget::bandSelected, this, &MainWindow::onBandSelected);
    connect(m_bandPopup, &BandPopupWidget::closed, this, [this]() {
        if (m_bottomMenuBar) {
            m_bottomMenuBar->setBandActive(false);
        }
    });

    // Create display popup
    m_displayPopup = new DisplayPopupWidget(this);
    connect(m_displayPopup, &DisplayPopupWidget::closed, this, [this]() {
        if (m_bottomMenuBar) {
            m_bottomMenuBar->setDisplayActive(false);
        }
    });
    // DisplayPopup pan mode changed -> update panadapter display
    // (K4 doesn't echo #DPM commands, so DisplayPopup notifies us directly)
    connect(m_displayPopup, &DisplayPopupWidget::dualPanModeChanged, this, [this](int mode) {
        switch (mode) {
        case 0: // A only
            m_spectrumController->setPanadapterMode(SpectrumController::PanadapterMode::MainOnly);
            break;
        case 1: // B only
            m_spectrumController->setPanadapterMode(SpectrumController::PanadapterMode::SubOnly);
            break;
        case 2: // Dual (A+B)
            m_spectrumController->setPanadapterMode(SpectrumController::PanadapterMode::Dual);
            break;
        }
    });

    // DisplayPopup CAT commands -> TcpClient
    connect(m_displayPopup, &DisplayPopupWidget::catCommandRequested, this,
            [this](const QString &cmd) { m_connectionController->sendCAT(cmd); });

    // Create Fn popup with dual-action buttons (macro system)
    m_fnPopup = new FnPopupWidget(this);
    connect(m_fnPopup, &FnPopupWidget::closed, this, [this]() {
        if (m_bottomMenuBar) {
            m_bottomMenuBar->setFnActive(false);
        }
    });
    connect(m_fnPopup, &FnPopupWidget::functionTriggered, this, &MainWindow::onFnFunctionTriggered);

    // Create macro configuration dialog (full-screen overlay)
    m_macroDialog = new MacroDialog(this);
    m_macroDialog->hide();

    // Create button row popups for MAIN RX, SUB RX, TX

    m_mainRxPopup = new ButtonRowPopup(this);
    // Set button labels: primary (white), alternate (amber if has right-click function, white otherwise)
    m_mainRxPopup->setButtonLabel(0, "ANT", "CFG", false);     // No alternate function - all white
    m_mainRxPopup->setButtonLabel(1, "RX", "EQ", false);       // No alternate function - all white
    m_mainRxPopup->setButtonLabel(2, "LINE OUT", "VFO LINK");  // Right-click toggles VFO LINK
    m_mainRxPopup->setButtonLabel(3, "AFX OFF", "OFF");        // Right-click same as left
    m_mainRxPopup->setButtonLabel(4, "AGC-S", "ON");           // Right-click toggles AGC on/off
    m_mainRxPopup->setButtonLabel(5, "APF", "OFF");            // No alternate function but shows state
    m_mainRxPopup->setButtonLabel(6, "TEXT", "DECODE", false); // No alternate function - all white
    connect(m_mainRxPopup, &ButtonRowPopup::closed, this, [this]() {
        if (m_bottomMenuBar) {
            m_bottomMenuBar->setMainRxActive(false);
        }
    });
    connect(m_mainRxPopup, &ButtonRowPopup::buttonClicked, this, &MainWindow::onMainRxButtonClicked);
    connect(m_mainRxPopup, &ButtonRowPopup::buttonRightClicked, this, &MainWindow::onMainRxButtonRightClicked);

    m_subRxPopup = new ButtonRowPopup(this);
    // Set button labels: primary (white), alternate (amber if has right-click function, white otherwise)
    m_subRxPopup->setButtonLabel(0, "ANT", "CFG", false);     // No alternate function - all white
    m_subRxPopup->setButtonLabel(1, "RX", "EQ", false);       // No alternate function - all white
    m_subRxPopup->setButtonLabel(2, "LINE OUT", "VFO LINK");  // Right-click toggles VFO LINK
    m_subRxPopup->setButtonLabel(3, "AFX OFF", "OFF");        // Right-click same as left
    m_subRxPopup->setButtonLabel(4, "AGC-S", "ON");           // Right-click toggles AGC on/off
    m_subRxPopup->setButtonLabel(5, "APF", "OFF");            // No alternate function but shows state
    m_subRxPopup->setButtonLabel(6, "TEXT", "DECODE", false); // No alternate function - all white
    connect(m_subRxPopup, &ButtonRowPopup::closed, this, [this]() {
        if (m_bottomMenuBar) {
            m_bottomMenuBar->setSubRxActive(false);
        }
    });
    connect(m_subRxPopup, &ButtonRowPopup::buttonClicked, this, &MainWindow::onSubRxButtonClicked);
    connect(m_subRxPopup, &ButtonRowPopup::buttonRightClicked, this, &MainWindow::onSubRxButtonRightClicked);

    m_txPopup = new ButtonRowPopup(this);
    m_txPopup->setButtonLabel(0, "ANT", "CFG", false);        // TX Antenna config
    m_txPopup->setButtonLabel(1, "TX", "EQ", false);          // TX Equalizer (future)
    m_txPopup->setButtonLabel(2, "LINE", "IN", false);        // LINE IN control
    m_txPopup->setButtonLabel(3, "MIC INP", "MIC CFG", true); // Mic input/config
    m_txPopup->setButtonLabel(4, "VOX GN", "ANTIVOX", true);  // VOX Gain / Anti-VOX
    m_txPopup->setButtonLabel(5, "SSB BW", "2.8k", false);    // SSB TX Bandwidth
    m_txPopup->setButtonLabel(6, "ESSB", "OFF", false);       // ESSB toggle
    connect(m_txPopup, &ButtonRowPopup::closed, this, [this]() {
        if (m_bottomMenuBar) {
            m_bottomMenuBar->setTxActive(false);
        }
    });
    connect(m_txPopup, &ButtonRowPopup::buttonClicked, this, [this](int index) {
        if (!m_connectionController->isConnected())
            return;
        switch (index) {
        case 0: // ANT CFG - show TX antenna config popup
            if (m_txAntCfgPopup && m_txPopup) {
                m_txAntCfgPopup->showAboveWidget(m_txPopup);
            }
            break;
        case 1: // TX EQ - show TX graphic equalizer popup
            if (m_txEqPopup && m_txPopup) {
                m_txEqPopup->setAllBands(m_radioState->txEqBands());
                m_txEqPopup->showAboveWidget(m_txPopup);
            }
            break;
        case 2: // LINE IN - show line in control popup
            if (m_lineInPopup && m_txPopup) {
                m_lineInPopup->setSoundCardLevel(m_radioState->lineInSoundCard());
                m_lineInPopup->setLineInJackLevel(m_radioState->lineInJack());
                m_lineInPopup->setSource(m_radioState->lineInSource());
                m_lineInPopup->showAboveWidget(m_txPopup);
            }
            break;
        case 3: // MIC INP - show mic input selection popup
            if (m_micInputPopup && m_txPopup) {
                m_micInputPopup->setCurrentInput(m_radioState->micInput());
                m_micInputPopup->showAboveWidget(m_txPopup);
            }
            break;
        case 4: // VOX GN - show VOX Gain popup
            if (m_voxPopup && m_txPopup) {
                bool isDataMode =
                    (m_radioState->mode() == RadioState::DATA || m_radioState->mode() == RadioState::DATA_R);
                m_voxPopup->setPopupMode(VoxPopupWidget::VoxGain);
                m_voxPopup->setDataMode(isDataMode);
                m_voxPopup->setValue(m_radioState->voxGainForCurrentMode());
                m_voxPopup->setVoxEnabled(m_radioState->voxForCurrentMode());
                m_voxPopup->showAboveWidget(m_txPopup);
            }
            break;
        case 5: { // Paddle toggle (CW) or SSB BW popup (voice/data)
            auto mode = m_radioState->mode();
            if (mode == RadioState::CW || mode == RadioState::CW_R) {
                // Toggle paddle orientation N↔R
                QChar curPaddle = m_radioState->paddleOrientation();
                QChar newPaddle = (curPaddle == 'R') ? QChar('N') : QChar('R');
                QChar iambic = m_radioState->iambicMode().isNull() ? QChar('A') : m_radioState->iambicMode();
                int weight = m_radioState->keyingWeight() < 0 ? 100 : m_radioState->keyingWeight();
                m_connectionController->sendCAT(
                    QString("KP%1%2%3;").arg(iambic).arg(newPaddle).arg(weight, 3, 10, QChar('0')));
                m_radioState->setPaddleOrientation(newPaddle);
            } else if (m_ssbBwPopup && m_txPopup) {
                m_ssbBwPopup->setEssbEnabled(m_radioState->essbEnabled());
                int bw = m_radioState->ssbTxBw();
                if (bw >= 24 && bw <= 45) {
                    m_ssbBwPopup->setBandwidth(bw);
                }
                m_ssbBwPopup->showAboveWidget(m_txPopup);
            }
            break;
        }
        case 6: { // Keying Weight popup (CW) or ESSB toggle (voice/data)
            auto mode = m_radioState->mode();
            if (mode == RadioState::CW || mode == RadioState::CW_R) {
                if (m_keyingWeightPopup && m_txPopup) {
                    int weight = m_radioState->keyingWeight();
                    if (weight >= 90 && weight <= 125)
                        m_keyingWeightPopup->setWeight(weight);
                    m_keyingWeightPopup->showAboveWidget(m_txPopup);
                }
            } else {
                bool newState = !m_radioState->essbEnabled();
                int bw = m_radioState->ssbTxBw();
                // Ensure bw is valid for the new mode
                // SSB: 24-28, ESSB: 30-45
                if (newState) {
                    // Switching to ESSB - use 30 if bw is outside ESSB range
                    if (bw < 30 || bw > 45)
                        bw = 30;
                } else {
                    // Switching to SSB - use 28 if bw is outside SSB range
                    if (bw < 24 || bw > 28)
                        bw = 28;
                }
                m_connectionController->sendCAT(QString("ES%1%2;").arg(newState ? 1 : 0).arg(bw, 2, 10, QChar('0')));
                // Optimistic update
                m_radioState->setEssbEnabled(newState);
                m_radioState->setSsbTxBw(bw);
                // Update button labels
                if (m_txPopup) {
                    QString bwStr = QString("%1k").arg(bw / 10.0, 0, 'f', 1);
                    m_txPopup->setButtonLabel(5, "SSB BW", bwStr, false);
                    m_txPopup->setButtonLabel(6, "ESSB", newState ? "ON" : "OFF", false);
                }
            }
            break;
        }
        default:
            break;
        }
    });

    // TX popup right-click handler for MIC CFG and ANTIVOX
    connect(m_txPopup, &ButtonRowPopup::buttonRightClicked, this, [this](int index) {
        if (!m_connectionController->isConnected())
            return;
        if (index == 4) { // ANTIVOX
            if (m_voxPopup && m_txPopup) {
                m_voxPopup->setPopupMode(VoxPopupWidget::AntiVox);
                m_voxPopup->setValue(m_radioState->antiVox());
                m_voxPopup->setVoxEnabled(m_radioState->voxForCurrentMode());
                m_voxPopup->showAboveWidget(m_txPopup);
            }
        } else if (index == 5) { // Iambic A↔B toggle (CW mode only)
            auto mode = m_radioState->mode();
            if (mode == RadioState::CW || mode == RadioState::CW_R) {
                QChar curIambic = m_radioState->iambicMode();
                QChar newIambic = (curIambic == 'B') ? QChar('A') : QChar('B');
                QChar paddle =
                    m_radioState->paddleOrientation().isNull() ? QChar('N') : m_radioState->paddleOrientation();
                int weight = m_radioState->keyingWeight() < 0 ? 100 : m_radioState->keyingWeight();
                m_connectionController->sendCAT(
                    QString("KP%1%2%3;").arg(newIambic).arg(paddle).arg(weight, 3, 10, QChar('0')));
                m_radioState->setIambicMode(newIambic);
            }
        } else if (index == 3) { // MIC CFG
            int input = m_radioState->micInput();
            // LINE IN only (input=2) has no mic config
            if (input == 2)
                return;

            // Determine if Front or Rear mic
            bool isFront = (input == 0 || input == 3); // 0=front, 3=front+line
            if (m_micConfigPopup && m_txPopup) {
                m_micConfigPopup->setMicType(isFront ? MicConfigPopupWidget::Front : MicConfigPopupWidget::Rear);
                if (isFront) {
                    m_micConfigPopup->setBias(m_radioState->micFrontBias());
                    m_micConfigPopup->setPreamp(m_radioState->micFrontPreamp());
                    m_micConfigPopup->setButtons(m_radioState->micFrontButtons());
                } else {
                    m_micConfigPopup->setBias(m_radioState->micRearBias());
                    m_micConfigPopup->setPreamp(m_radioState->micRearPreamp());
                }
                m_micConfigPopup->showAboveWidget(m_txPopup);
            }
        }
    });

    // Create RX EQ popup (Main RX - cyan theme)
    m_rxEqPopup = new RxEqPopupWidget("RX GRAPHIC EQUALIZER", K4Styles::Colors::VfoACyan, this);
    connect(m_rxEqPopup, &RxEqPopupWidget::closed, this, [this]() {
        // Close the MAIN RX button row popup when EQ popup closes
    });

    // Debounce timer for RX EQ - sends command 100ms after last slider change
    m_rxEqDebounceTimer = new QTimer(this);
    m_rxEqDebounceTimer->setSingleShot(true);
    m_rxEqDebounceTimer->setInterval(100);
    connect(m_rxEqDebounceTimer, &QTimer::timeout, this, [this]() {
        // Build RE command with all 8 bands: RE+00+00+00+00+00+00+00+00;
        QString cmd = "RE";
        for (int i = 0; i < 8; i++) {
            int value = m_radioState->rxEqBand(i);
            cmd += QString("%1%2").arg(value >= 0 ? '+' : '-').arg(qAbs(value), 2, 10, QChar('0'));
        }
        m_connectionController->sendCAT(cmd);
    });

    connect(m_rxEqPopup, &RxEqPopupWidget::bandValueChanged, this, [this](int bandIndex, int dB) {
        // Update optimistic state immediately (UI stays responsive)
        m_radioState->setRxEqBand(bandIndex, dB);
        // Restart debounce timer - will send after 100ms of no changes
        m_rxEqDebounceTimer->start();
    });
    connect(m_rxEqPopup, &RxEqPopupWidget::flatRequested, this, [this]() {
        // Reset all bands to 0 and send CAT command
        QVector<int> flat(8, 0);
        m_radioState->setRxEqBands(flat);
        m_connectionController->sendCAT("RE+00+00+00+00+00+00+00+00");
    });

    // Preset load: get preset from RadioSettings, apply to sliders, send CAT
    connect(m_rxEqPopup, &RxEqPopupWidget::presetLoadRequested, this, [this](int index) {
        EqPreset preset = RadioSettings::instance()->rxEqPreset(index);
        if (!preset.isEmpty() && preset.bands.size() == 8) {
            m_rxEqPopup->setAllBands(preset.bands);
            m_radioState->setRxEqBands(preset.bands);

            // Send CAT command
            QString cmd = "RE";
            for (int i = 0; i < 8; i++) {
                int value = preset.bands[i];
                cmd += QString("%1%2").arg(value >= 0 ? '+' : '-').arg(qAbs(value), 2, 10, QChar('0'));
            }
            m_connectionController->sendCAT(cmd);
        }
    });

    // Preset save: show name dialog, save current EQ to preset
    connect(m_rxEqPopup, &RxEqPopupWidget::presetSaveRequested, this, [this](int index) {
        EqPreset existing = RadioSettings::instance()->rxEqPreset(index);
        QString defaultName = existing.name.isEmpty() ? QString("Preset %1").arg(index + 1) : existing.name;

        // Store current EQ bands before dialog (popup may close)
        QVector<int> currentBands = m_radioState->rxEqBands();

        bool ok;
        QString name = QInputDialog::getText(this, "Save Preset", "Preset name:", QLineEdit::Normal, defaultName, &ok);

        // Re-show the EQ popup after dialog closes
        if (m_bottomMenuBar) {
            m_rxEqPopup->showAboveButton(m_bottomMenuBar->mainRxButton());
        }

        if (ok) {
            // Use default name if user cleared it
            if (name.isEmpty()) {
                name = QString("Preset %1").arg(index + 1);
            }
            EqPreset preset;
            preset.name = name;
            preset.bands = currentBands;
            RadioSettings::instance()->setRxEqPreset(index, preset);
            m_rxEqPopup->updatePresetName(index, name);
        }
    });

    // Preset clear: remove preset from RadioSettings
    connect(m_rxEqPopup, &RxEqPopupWidget::presetClearRequested, this, [this](int index) {
        RadioSettings::instance()->clearRxEqPreset(index);
        m_rxEqPopup->updatePresetName(index, "");
    });

    // Load preset names on popup creation
    for (int i = 0; i < 4; i++) {
        EqPreset preset = RadioSettings::instance()->rxEqPreset(i);
        m_rxEqPopup->updatePresetName(i, preset.name);
    }

    // Create TX EQ popup (amber theme)
    m_txEqPopup = new RxEqPopupWidget("TX GRAPHIC EQUALIZER", K4Styles::Colors::AccentAmber, this);
    connect(m_txEqPopup, &RxEqPopupWidget::closed, this, [this]() {
        // Close the TX button row popup when EQ popup closes
    });

    // Debounce timer for TX EQ - sends command 100ms after last slider change
    m_txEqDebounceTimer = new QTimer(this);
    m_txEqDebounceTimer->setSingleShot(true);
    m_txEqDebounceTimer->setInterval(100);
    connect(m_txEqDebounceTimer, &QTimer::timeout, this, [this]() {
        // Build TE command with all 8 bands: TE+00+00+00+00+00+00+00+00;
        QString cmd = "TE";
        for (int i = 0; i < 8; i++) {
            int value = m_radioState->txEqBand(i);
            cmd += QString("%1%2").arg(value >= 0 ? '+' : '-').arg(qAbs(value), 2, 10, QChar('0'));
        }
        m_connectionController->sendCAT(cmd);
    });

    connect(m_txEqPopup, &RxEqPopupWidget::bandValueChanged, this, [this](int bandIndex, int dB) {
        // Update optimistic state immediately (UI stays responsive)
        m_radioState->setTxEqBand(bandIndex, dB);
        // Restart debounce timer - will send after 100ms of no changes
        m_txEqDebounceTimer->start();
    });
    connect(m_txEqPopup, &RxEqPopupWidget::flatRequested, this, [this]() {
        // Reset all bands to 0 and send CAT command
        QVector<int> flat(8, 0);
        m_radioState->setTxEqBands(flat);
        m_connectionController->sendCAT("TE+00+00+00+00+00+00+00+00");
    });

    // TX EQ Preset load: get preset from RadioSettings, apply to sliders, send CAT
    connect(m_txEqPopup, &RxEqPopupWidget::presetLoadRequested, this, [this](int index) {
        EqPreset preset = RadioSettings::instance()->txEqPreset(index);
        if (!preset.isEmpty() && preset.bands.size() == 8) {
            m_txEqPopup->setAllBands(preset.bands);
            m_radioState->setTxEqBands(preset.bands);

            // Send CAT command
            QString cmd = "TE";
            for (int i = 0; i < 8; i++) {
                int value = preset.bands[i];
                cmd += QString("%1%2").arg(value >= 0 ? '+' : '-').arg(qAbs(value), 2, 10, QChar('0'));
            }
            m_connectionController->sendCAT(cmd);
        }
    });

    // TX EQ Preset save: show name dialog, save current EQ to preset
    connect(m_txEqPopup, &RxEqPopupWidget::presetSaveRequested, this, [this](int index) {
        EqPreset existing = RadioSettings::instance()->txEqPreset(index);
        QString defaultName = existing.name.isEmpty() ? QString("Preset %1").arg(index + 1) : existing.name;

        // Store current EQ bands before dialog (popup may close)
        QVector<int> currentBands = m_radioState->txEqBands();

        bool ok;
        QString name =
            QInputDialog::getText(this, "Save TX Preset", "Preset name:", QLineEdit::Normal, defaultName, &ok);

        // Re-show the EQ popup after dialog closes
        if (m_bottomMenuBar) {
            m_txEqPopup->showAboveButton(m_bottomMenuBar->txButton());
        }

        if (ok) {
            // Use default name if user cleared it
            if (name.isEmpty()) {
                name = QString("Preset %1").arg(index + 1);
            }
            EqPreset preset;
            preset.name = name;
            preset.bands = currentBands;
            RadioSettings::instance()->setTxEqPreset(index, preset);
            m_txEqPopup->updatePresetName(index, name);
        }
    });

    // TX EQ Preset clear: remove preset from RadioSettings
    connect(m_txEqPopup, &RxEqPopupWidget::presetClearRequested, this, [this](int index) {
        RadioSettings::instance()->clearTxEqPreset(index);
        m_txEqPopup->updatePresetName(index, "");
    });

    // Load TX EQ preset names on popup creation
    for (int i = 0; i < 4; i++) {
        EqPreset preset = RadioSettings::instance()->txEqPreset(i);
        m_txEqPopup->updatePresetName(i, preset.name);
    }

    // Create antenna configuration popups (MAIN RX, SUB RX, TX)
    m_mainRxAntCfgPopup = new AntennaCfgPopupWidget(AntennaCfgVariant::MainRx, this);
    connect(m_mainRxAntCfgPopup, &AntennaCfgPopupWidget::configChanged, this,
            [this](bool displayAll, QVector<bool> mask) {
                if (!m_connectionController->isConnected())
                    return;
                // Build ACM command: ACMzabcdefg where z=displayAll, a-g=antenna enables
                QString cmd = QString("ACM%1").arg(displayAll ? '1' : '0');
                for (int i = 0; i < 7; i++) {
                    cmd += (i < mask.size() && mask[i]) ? '1' : '0';
                }
                m_connectionController->sendCAT(cmd);
            });

    m_subRxAntCfgPopup = new AntennaCfgPopupWidget(AntennaCfgVariant::SubRx, this);
    connect(m_subRxAntCfgPopup, &AntennaCfgPopupWidget::configChanged, this,
            [this](bool displayAll, QVector<bool> mask) {
                if (!m_connectionController->isConnected())
                    return;
                // Build ACS command: ACSzabcdefg where z=displayAll, a-g=antenna enables
                QString cmd = QString("ACS%1").arg(displayAll ? '1' : '0');
                for (int i = 0; i < 7; i++) {
                    cmd += (i < mask.size() && mask[i]) ? '1' : '0';
                }
                m_connectionController->sendCAT(cmd);
            });

    m_txAntCfgPopup = new AntennaCfgPopupWidget(AntennaCfgVariant::Tx, this);
    connect(m_txAntCfgPopup, &AntennaCfgPopupWidget::configChanged, this, [this](bool displayAll, QVector<bool> mask) {
        if (!m_connectionController->isConnected())
            return;
        // Build ACT command: ACTzabc where z=displayAll, a-c=antenna enables
        QString cmd = QString("ACT%1").arg(displayAll ? '1' : '0');
        for (int i = 0; i < 3; i++) {
            cmd += (i < mask.size() && mask[i]) ? '1' : '0';
        }
        m_connectionController->sendCAT(cmd);
    });

    // Create Line Out popup (shared by MAIN RX and SUB RX)
    m_lineOutPopup = new LineOutPopupWidget(this);
    connect(m_lineOutPopup, &LineOutPopupWidget::leftLevelChanged, this, [this](int level) {
        if (!m_connectionController->isConnected())
            return;
        // Send full LO command with current state
        QString cmd = QString("LO%1%2%3;")
                          .arg(level, 3, 10, QChar('0'))
                          .arg(m_radioState->lineOutRight(), 3, 10, QChar('0'))
                          .arg(m_radioState->lineOutRightEqualsLeft() ? 1 : 0);
        m_connectionController->sendCAT(cmd);
    });
    connect(m_lineOutPopup, &LineOutPopupWidget::rightLevelChanged, this, [this](int level) {
        if (!m_connectionController->isConnected())
            return;
        QString cmd = QString("LO%1%2%3;")
                          .arg(m_radioState->lineOutLeft(), 3, 10, QChar('0'))
                          .arg(level, 3, 10, QChar('0'))
                          .arg(m_radioState->lineOutRightEqualsLeft() ? 1 : 0);
        m_connectionController->sendCAT(cmd);
    });
    connect(m_lineOutPopup, &LineOutPopupWidget::rightEqualsLeftChanged, this, [this](bool enabled) {
        if (!m_connectionController->isConnected())
            return;
        int left = m_radioState->lineOutLeft();
        int right = enabled ? left : m_radioState->lineOutRight();
        QString cmd =
            QString("LO%1%2%3;").arg(left, 3, 10, QChar('0')).arg(right, 3, 10, QChar('0')).arg(enabled ? 1 : 0);
        m_connectionController->sendCAT(cmd);
    });
    // Connect RadioState to update popup when K4 sends LO response
    connect(m_radioState, &RadioState::lineOutChanged, this, [this]() {
        if (m_lineOutPopup) {
            m_lineOutPopup->setLeftLevel(m_radioState->lineOutLeft());
            m_lineOutPopup->setRightLevel(m_radioState->lineOutRight());
            m_lineOutPopup->setRightEqualsLeft(m_radioState->lineOutRightEqualsLeft());
        }
    });

    // Create Line In popup (TX menu button index 3)
    m_lineInPopup = new LineInPopupWidget(this);
    connect(m_lineInPopup, &LineInPopupWidget::soundCardLevelChanged, this, [this](int level) {
        if (!m_connectionController->isConnected())
            return;
        m_radioState->setLineInSoundCard(level);
        QString cmd = QString("LI%1%2%3;")
                          .arg(level, 3, 10, QChar('0'))
                          .arg(m_radioState->lineInJack(), 3, 10, QChar('0'))
                          .arg(m_radioState->lineInSource());
        m_connectionController->sendCAT(cmd);
    });
    connect(m_lineInPopup, &LineInPopupWidget::lineInJackLevelChanged, this, [this](int level) {
        if (!m_connectionController->isConnected())
            return;
        m_radioState->setLineInJack(level);
        QString cmd = QString("LI%1%2%3;")
                          .arg(m_radioState->lineInSoundCard(), 3, 10, QChar('0'))
                          .arg(level, 3, 10, QChar('0'))
                          .arg(m_radioState->lineInSource());
        m_connectionController->sendCAT(cmd);
    });
    connect(m_lineInPopup, &LineInPopupWidget::sourceChanged, this, [this](int source) {
        if (!m_connectionController->isConnected())
            return;
        m_radioState->setLineInSource(source);
        QString cmd = QString("LI%1%2%3;")
                          .arg(m_radioState->lineInSoundCard(), 3, 10, QChar('0'))
                          .arg(m_radioState->lineInJack(), 3, 10, QChar('0'))
                          .arg(source);
        m_connectionController->sendCAT(cmd);
    });
    // Connect RadioState to update popup when K4 sends LI response
    connect(m_radioState, &RadioState::lineInChanged, this, [this]() {
        if (m_lineInPopup) {
            m_lineInPopup->setSoundCardLevel(m_radioState->lineInSoundCard());
            m_lineInPopup->setLineInJackLevel(m_radioState->lineInJack());
            m_lineInPopup->setSource(m_radioState->lineInSource());
        }
    });

    // Create Mic Input popup (TX menu button index 3, left-click)
    m_micInputPopup = new MicInputPopupWidget(this);
    connect(m_micInputPopup, &MicInputPopupWidget::inputChanged, this, [this](int input) {
        if (!m_connectionController->isConnected())
            return;
        m_radioState->setMicInput(input);
        m_connectionController->sendCAT(QString("MI%1;").arg(input));
    });
    // Connect RadioState to update popup when K4 sends MI response
    connect(m_radioState, &RadioState::micInputChanged, this, [this](int input) {
        if (m_micInputPopup) {
            m_micInputPopup->setCurrentInput(input);
        }
    });

    // Create Mic Config popup (TX menu button index 3, right-click)
    m_micConfigPopup = new MicConfigPopupWidget(this);
    connect(m_micConfigPopup, &MicConfigPopupWidget::biasChanged, this, [this](int bias) {
        if (!m_connectionController->isConnected())
            return;
        // Use individual SET command based on mic type
        if (m_micConfigPopup->micType() == MicConfigPopupWidget::Front) {
            m_radioState->setMicFrontBias(bias);
            m_connectionController->sendCAT(QString("MSB%1;").arg(bias));
        } else {
            m_radioState->setMicRearBias(bias);
            m_connectionController->sendCAT(QString("MSE%1;").arg(bias));
        }
    });
    connect(m_micConfigPopup, &MicConfigPopupWidget::preampChanged, this, [this](int preamp) {
        if (!m_connectionController->isConnected())
            return;
        if (m_micConfigPopup->micType() == MicConfigPopupWidget::Front) {
            m_radioState->setMicFrontPreamp(preamp);
            m_connectionController->sendCAT(QString("MSA%1;").arg(preamp));
        } else {
            m_radioState->setMicRearPreamp(preamp);
            m_connectionController->sendCAT(QString("MSD%1;").arg(preamp));
        }
    });
    connect(m_micConfigPopup, &MicConfigPopupWidget::buttonsChanged, this, [this](int buttons) {
        if (!m_connectionController->isConnected())
            return;
        // Buttons only applies to Front mic
        m_radioState->setMicFrontButtons(buttons);
        m_connectionController->sendCAT(QString("MSC%1;").arg(buttons));
    });
    // Connect RadioState to update popup when K4 sends MS response
    connect(m_radioState, &RadioState::micSetupChanged, this, [this]() {
        if (m_micConfigPopup) {
            if (m_micConfigPopup->micType() == MicConfigPopupWidget::Front) {
                m_micConfigPopup->setBias(m_radioState->micFrontBias());
                m_micConfigPopup->setPreamp(m_radioState->micFrontPreamp());
                m_micConfigPopup->setButtons(m_radioState->micFrontButtons());
            } else {
                m_micConfigPopup->setBias(m_radioState->micRearBias());
                m_micConfigPopup->setPreamp(m_radioState->micRearPreamp());
            }
        }
    });

    // Create VOX Gain / Anti-VOX popup (TX menu button index 4)
    m_voxPopup = new VoxPopupWidget(this);
    connect(m_voxPopup, &VoxPopupWidget::valueChanged, this, [this](int value) {
        if (!m_connectionController->isConnected())
            return;
        if (m_voxPopup->popupMode() == VoxPopupWidget::VoxGain) {
            // VOX Gain: VGVnnn or VGDnnn depending on mode
            bool isDataMode = (m_radioState->mode() == RadioState::DATA || m_radioState->mode() == RadioState::DATA_R);
            QString modeChar = isDataMode ? "D" : "V";
            if (isDataMode) {
                m_radioState->setVoxGainData(value);
            } else {
                m_radioState->setVoxGainVoice(value);
            }
            m_connectionController->sendCAT(QString("VG%1%2;").arg(modeChar).arg(value, 3, 10, QChar('0')));
        } else {
            // Anti-VOX: VInnn
            m_radioState->setAntiVox(value);
            m_connectionController->sendCAT(QString("VI%1;").arg(value, 3, 10, QChar('0')));
        }
    });
    connect(m_voxPopup, &VoxPopupWidget::voxToggled, this, [this](bool enabled) {
        if (!m_connectionController->isConnected())
            return;
        // VXmn where m=C/V/D, n=0/1
        RadioState::Mode mode = m_radioState->mode();
        QString modeChar;
        if (mode == RadioState::CW || mode == RadioState::CW_R) {
            modeChar = "C";
        } else if (mode == RadioState::DATA || mode == RadioState::DATA_R) {
            modeChar = "D";
        } else {
            modeChar = "V";
        }
        m_connectionController->sendCAT(QString("VX%1%2;").arg(modeChar).arg(enabled ? 1 : 0));
    });
    // Connect RadioState to update popup when K4 sends VG/VI/VX response
    connect(m_radioState, &RadioState::voxGainChanged, this, [this](int mode, int gain) {
        if (m_voxPopup && m_voxPopup->popupMode() == VoxPopupWidget::VoxGain) {
            bool isDataMode = (m_radioState->mode() == RadioState::DATA || m_radioState->mode() == RadioState::DATA_R);
            if ((mode == 1 && isDataMode) || (mode == 0 && !isDataMode)) {
                m_voxPopup->setValue(gain);
            }
        }
    });
    connect(m_radioState, &RadioState::antiVoxChanged, this, [this](int level) {
        if (m_voxPopup && m_voxPopup->popupMode() == VoxPopupWidget::AntiVox) {
            m_voxPopup->setValue(level);
        }
    });
    connect(m_radioState, &RadioState::voxChanged, this, [this](bool enabled) {
        if (m_voxPopup) {
            m_voxPopup->setVoxEnabled(m_radioState->voxForCurrentMode());
        }
    });

    // Create SSB TX Bandwidth popup (TX menu button index 5)
    m_ssbBwPopup = new SsbBwPopupWidget(this);
    connect(m_ssbBwPopup, &SsbBwPopupWidget::bandwidthChanged, this, [this](int bw) {
        if (!m_connectionController->isConnected())
            return;
        // ES command: ESnbb where n=essb mode, bb=bandwidth
        int essbMode = m_radioState->essbEnabled() ? 1 : 0;
        m_radioState->setSsbTxBw(bw);
        m_connectionController->sendCAT(QString("ES%1%2;").arg(essbMode).arg(bw, 2, 10, QChar('0')));
        // Update button label with new bandwidth (optimistic)
        if (m_txPopup) {
            QString bwStr = QString("%1k").arg(bw / 10.0, 0, 'f', 1);
            m_txPopup->setButtonLabel(5, "SSB BW", bwStr, false);
        }
    });
    // Connect RadioState to update popup and ESSB button when K4 sends ES response
    // SSB: 24-28 (2.4-2.8 kHz), ESSB: 30-45 (3.0-4.5 kHz)
    connect(m_radioState, &RadioState::essbChanged, this, [this](bool enabled, int bw) {
        if (m_ssbBwPopup) {
            m_ssbBwPopup->setEssbEnabled(enabled);
            if (bw >= 24 && bw <= 45) {
                m_ssbBwPopup->setBandwidth(bw);
            }
        }
        // Update TX popup button labels (only in non-CW modes — CW uses paddle/weight buttons)
        auto mode = m_radioState->mode();
        if (m_txPopup && mode != RadioState::CW && mode != RadioState::CW_R) {
            // Button 5: SSB BW with current bandwidth value (e.g., "2.8k" or "3.0k")
            if (bw >= 24 && bw <= 45) {
                QString bwStr = QString("%1k").arg(bw / 10.0, 0, 'f', 1);
                m_txPopup->setButtonLabel(5, "SSB BW", bwStr, false);
            }
            // Button 6: ESSB toggle with ON/OFF state
            m_txPopup->setButtonLabel(6, "ESSB", enabled ? "ON" : "OFF", false);
        }
        // Update mode labels to show USB+/LSB+ when ESSB enabled
        updateModeLabels();
    });

    // Create Keying Weight popup (TX menu button index 6 in CW mode)
    m_keyingWeightPopup = new KeyingWeightPopupWidget(this);
    connect(m_keyingWeightPopup, &KeyingWeightPopupWidget::weightChanged, this, [this](int weight) {
        if (!m_connectionController->isConnected())
            return;
        QChar iambic = m_radioState->iambicMode().isNull() ? QChar('A') : m_radioState->iambicMode();
        QChar paddle = m_radioState->paddleOrientation().isNull() ? QChar('N') : m_radioState->paddleOrientation();
        m_radioState->setKeyingWeight(weight);
        m_connectionController->sendCAT(QString("KP%1%2%3;").arg(iambic).arg(paddle).arg(weight, 3, 10, QChar('0')));
        // Update button label with new weight value (optimistic)
        if (m_txPopup) {
            QString weightStr = QString::number(weight / 100.0, 'f', 2);
            m_txPopup->setButtonLabel(6, "WEIGHT", weightStr, false);
        }
    });

    // Connect keyerPaddleChanged to update CW button labels and weight popup
    connect(m_radioState, &RadioState::keyerPaddleChanged, this, [this](QChar iambic, QChar paddle, int weight) {
        auto mode = m_radioState->mode();
        if (m_txPopup && (mode == RadioState::CW || mode == RadioState::CW_R)) {
            // Button 5: paddle orientation + iambic mode
            QString paddleStr = (paddle == 'R') ? "PDL REV" : "PDL NOR";
            QString iambicStr = QString("IAMB %1").arg(iambic);
            m_txPopup->setButtonLabel(5, paddleStr, iambicStr, true);
            // Button 6: keying weight ratio
            if (weight >= 90 && weight <= 125) {
                QString weightStr = QString::number(weight / 100.0, 'f', 2);
                m_txPopup->setButtonLabel(6, "WEIGHT", weightStr, false);
            }
        }
        // Update weight popup if visible
        if (m_keyingWeightPopup && m_keyingWeightPopup->isVisible() && weight >= 90 && weight <= 125) {
            m_keyingWeightPopup->setWeight(weight);
        }
    });

    // Create floating text decode windows (separate for MAIN RX and SUB RX)
    // Controls integrated in title bar - no separate popup needed
    m_textDecodeWindowMain = new TextDecodeWindow(TextDecodeWindow::MainRx, this);
    m_textDecodeWindowSub = new TextDecodeWindow(TextDecodeWindow::SubRx, this);

    // Helper lambda to send TD command
    auto sendTextDecodeCmd = [this](TextDecodeWindow *window, bool isMainRx) {
        if (!m_connectionController->isConnected())
            return;
        int mode = 0;
        int threshold = 0;
        if (window->isDecodeEnabled()) {
            auto opMode = window->operatingMode();
            if (opMode == TextDecodeWindow::ModeCW) {
                mode = 2 + window->wpmRange(); // 2=8-45, 3=8-60, 4=8-90
                threshold = window->autoThreshold() ? 0 : window->threshold();
            } else {
                mode = 1; // DATA/SSB mode
            }
        }
        QString cmdPrefix = isMainRx ? "TD" : "TD$";
        QString cmd = QString("%1%2%3%4;").arg(cmdPrefix).arg(mode).arg(threshold).arg(window->maxLines());
        qCDebug(qk4Main) << "Sending TD command:" << cmd;
        m_connectionController->sendCAT(cmd);
    };

    // Wire MAIN RX window signals
    connect(m_textDecodeWindowMain, &TextDecodeWindow::enabledChanged, this,
            [this, sendTextDecodeCmd](bool) { sendTextDecodeCmd(m_textDecodeWindowMain, true); });
    connect(m_textDecodeWindowMain, &TextDecodeWindow::wpmRangeChanged, this, [this, sendTextDecodeCmd](int) {
        if (m_textDecodeWindowMain->isDecodeEnabled()) {
            sendTextDecodeCmd(m_textDecodeWindowMain, true);
        }
    });
    connect(m_textDecodeWindowMain, &TextDecodeWindow::thresholdModeChanged, this, [this, sendTextDecodeCmd](bool) {
        if (m_textDecodeWindowMain->isDecodeEnabled()) {
            sendTextDecodeCmd(m_textDecodeWindowMain, true);
        }
    });
    connect(m_textDecodeWindowMain, &TextDecodeWindow::thresholdChanged, this, [this, sendTextDecodeCmd](int) {
        if (m_textDecodeWindowMain->isDecodeEnabled()) {
            sendTextDecodeCmd(m_textDecodeWindowMain, true);
        }
    });
    connect(m_textDecodeWindowMain, &TextDecodeWindow::closeRequested, this, [this, sendTextDecodeCmd]() {
        // Disable decode and hide window
        m_textDecodeWindowMain->setDecodeEnabled(false);
        sendTextDecodeCmd(m_textDecodeWindowMain, true);
        m_textDecodeWindowMain->clearText();
        m_textDecodeWindowMain->hide();
    });

    // Wire SUB RX window signals
    connect(m_textDecodeWindowSub, &TextDecodeWindow::enabledChanged, this,
            [this, sendTextDecodeCmd](bool) { sendTextDecodeCmd(m_textDecodeWindowSub, false); });
    connect(m_textDecodeWindowSub, &TextDecodeWindow::wpmRangeChanged, this, [this, sendTextDecodeCmd](int) {
        if (m_textDecodeWindowSub->isDecodeEnabled()) {
            sendTextDecodeCmd(m_textDecodeWindowSub, false);
        }
    });
    connect(m_textDecodeWindowSub, &TextDecodeWindow::thresholdModeChanged, this, [this, sendTextDecodeCmd](bool) {
        if (m_textDecodeWindowSub->isDecodeEnabled()) {
            sendTextDecodeCmd(m_textDecodeWindowSub, false);
        }
    });
    connect(m_textDecodeWindowSub, &TextDecodeWindow::thresholdChanged, this, [this, sendTextDecodeCmd](int) {
        if (m_textDecodeWindowSub->isDecodeEnabled()) {
            sendTextDecodeCmd(m_textDecodeWindowSub, false);
        }
    });
    connect(m_textDecodeWindowSub, &TextDecodeWindow::closeRequested, this, [this, sendTextDecodeCmd]() {
        // Disable decode and hide window
        m_textDecodeWindowSub->setDecodeEnabled(false);
        sendTextDecodeCmd(m_textDecodeWindowSub, false);
        m_textDecodeWindowSub->clearText();
        m_textDecodeWindowSub->hide();
    });

    // Wire data rate changes → send DR command
    connect(m_textDecodeWindowMain, &TextDecodeWindow::dataRateChanged, this, [this](int rate) {
        if (m_connectionController->isConnected()) {
            m_radioState->setDataRate(rate);
            m_connectionController->sendCAT(QString("DR%1;").arg(rate));
        }
    });
    connect(m_textDecodeWindowSub, &TextDecodeWindow::dataRateChanged, this, [this](int rate) {
        if (m_connectionController->isConnected()) {
            m_radioState->setDataRateB(rate);
            m_connectionController->sendCAT(QString("DR$%1;").arg(rate));
        }
    });

    // Sync data rate from radio echoes
    connect(m_radioState, &RadioState::dataRateChanged, this,
            [this](int rate) { m_textDecodeWindowMain->setDataRate(rate); });
    connect(m_radioState, &RadioState::dataRateBChanged, this,
            [this](int rate) { m_textDecodeWindowSub->setDataRate(rate); });

    // Connect RadioState text decode signals to sync window state
    connect(m_radioState, &RadioState::textDecodeChanged, this, [this]() {
        int mode = m_radioState->textDecodeMode();
        bool enabled = (mode > 0);
        m_textDecodeWindowMain->setDecodeEnabled(enabled);
        if (mode >= 2 && mode <= 4) {
            m_textDecodeWindowMain->setWpmRange(mode - 2);
        }
        int threshold = m_radioState->textDecodeThreshold();
        m_textDecodeWindowMain->setAutoThreshold(threshold == 0);
        if (threshold > 0) {
            m_textDecodeWindowMain->setThreshold(threshold);
        }
        m_textDecodeWindowMain->setMaxLines(m_radioState->textDecodeLines());
    });
    connect(m_radioState, &RadioState::textDecodeBChanged, this, [this]() {
        int mode = m_radioState->textDecodeModeB();
        bool enabled = (mode > 0);
        m_textDecodeWindowSub->setDecodeEnabled(enabled);
        if (mode >= 2 && mode <= 4) {
            m_textDecodeWindowSub->setWpmRange(mode - 2);
        }
        int threshold = m_radioState->textDecodeThresholdB();
        m_textDecodeWindowSub->setAutoThreshold(threshold == 0);
        if (threshold > 0) {
            m_textDecodeWindowSub->setThreshold(threshold);
        }
        m_textDecodeWindowSub->setMaxLines(m_radioState->textDecodeLinesB());
    });

    // Helper to determine text decode operating mode from radio state
    auto textDecodeMode = [](RadioState::Mode radioMode, int dataSubMode) -> TextDecodeWindow::OperatingMode {
        if (radioMode == RadioState::CW || radioMode == RadioState::CW_R)
            return TextDecodeWindow::ModeCW;
        if (radioMode == RadioState::DATA || radioMode == RadioState::DATA_R) {
            switch (dataSubMode) {
            case 1:
                return TextDecodeWindow::ModeAFSK;
            case 2:
                return TextDecodeWindow::ModeFSK;
            case 3:
                return TextDecodeWindow::ModePSK;
            default:
                return TextDecodeWindow::ModeData;
            }
        }
        if (radioMode == RadioState::LSB || radioMode == RadioState::USB)
            return TextDecodeWindow::ModeSSB;
        return TextDecodeWindow::ModeOther;
    };

    // Update text decode window mode when radio mode changes while window is open
    connect(m_radioState, &RadioState::modeChanged, this, [this, textDecodeMode](RadioState::Mode mode) {
        if (m_textDecodeWindowMain->isVisible()) {
            m_textDecodeWindowMain->setOperatingMode(textDecodeMode(mode, m_radioState->dataSubMode()));
        }
    });
    connect(m_radioState, &RadioState::modeBChanged, this, [this, textDecodeMode](RadioState::Mode mode) {
        if (m_textDecodeWindowSub->isVisible()) {
            m_textDecodeWindowSub->setOperatingMode(textDecodeMode(mode, m_radioState->dataSubModeB()));
        }
    });
    connect(m_radioState, &RadioState::dataSubModeChanged, this, [this, textDecodeMode](int subMode) {
        if (m_textDecodeWindowMain->isVisible()) {
            m_textDecodeWindowMain->setOperatingMode(textDecodeMode(m_radioState->mode(), subMode));
        }
    });
    connect(m_radioState, &RadioState::dataSubModeBChanged, this, [this, textDecodeMode](int subMode) {
        if (m_textDecodeWindowSub->isVisible()) {
            m_textDecodeWindowSub->setOperatingMode(textDecodeMode(m_radioState->modeB(), subMode));
        }
    });

    // Connect decoded text buffer to windows
    connect(m_radioState, &RadioState::textBufferReceived, this, [this](const QString &text, bool isSubRx) {
        if (isSubRx) {
            m_textDecodeWindowSub->appendText(text);
        } else {
            m_textDecodeWindowMain->appendText(text);
        }
    });

    // Create notification popup for K4 error/status messages (ERxx:)
    m_notificationWidget = new NotificationWidget(this);

    // ConnectionController signals
    connect(m_connectionController, &ConnectionController::connectionStateChanged, this,
            &MainWindow::onConnectionStateChanged);
    connect(m_connectionController, &ConnectionController::connectionError, this, &MainWindow::onConnectionError);
    connect(m_connectionController, &ConnectionController::radioReady, this, &MainWindow::onRadioReady);
    connect(m_connectionController, &ConnectionController::authFailed, this, &MainWindow::onAuthFailed);

    // Protocol CAT responses -> RadioState (via ConnectionController re-emitted signal)
    connect(m_connectionController, &ConnectionController::catResponseReceived, this, &MainWindow::onCatResponse);

    // RadioState signals -> UI updates (VFO A)
    connect(m_radioState, &RadioState::frequencyChanged, this, &MainWindow::onFrequencyChanged);
    connect(m_radioState, &RadioState::modeChanged, this, &MainWindow::onModeChanged);
    connect(m_radioState, &RadioState::modeChanged, this, [this](RadioState::Mode) {
        onVoxChanged(false); // Refresh VOX display when mode changes (VOX is mode-specific)
    });
    // Data sub-mode changes also update mode label (AFSK, FSK, PSK, DATA)
    connect(m_radioState, &RadioState::dataSubModeChanged, this, [this](int) { updateModeLabels(); });
    connect(m_radioState, &RadioState::sMeterChanged, this, &MainWindow::onSMeterChanged);
    // RX EQ state -> popup (Main and Sub RX share the same EQ)
    connect(m_radioState, &RadioState::rxEqChanged, this, [this]() {
        if (m_rxEqPopup) {
            m_rxEqPopup->setAllBands(m_radioState->rxEqBands());
        }
    });
    // TX EQ state -> popup
    connect(m_radioState, &RadioState::txEqChanged, this, [this]() {
        if (m_txEqPopup) {
            m_txEqPopup->setAllBands(m_radioState->txEqBands());
        }
    });

    // Antenna configuration signals - update popups when state changes
    connect(m_radioState, &RadioState::mainRxAntCfgChanged, this, [this]() {
        if (m_mainRxAntCfgPopup) {
            m_mainRxAntCfgPopup->setDisplayAll(m_radioState->mainRxDisplayAll());
            m_mainRxAntCfgPopup->setAntennaMask(m_radioState->mainRxAntMask());
        }
    });
    connect(m_radioState, &RadioState::subRxAntCfgChanged, this, [this]() {
        if (m_subRxAntCfgPopup) {
            m_subRxAntCfgPopup->setDisplayAll(m_radioState->subRxDisplayAll());
            m_subRxAntCfgPopup->setAntennaMask(m_radioState->subRxAntMask());
        }
    });
    connect(m_radioState, &RadioState::txAntCfgChanged, this, [this]() {
        if (m_txAntCfgPopup) {
            m_txAntCfgPopup->setDisplayAll(m_radioState->txDisplayAll());
            m_txAntCfgPopup->setAntennaMask(m_radioState->txAntMask());
        }
    });

    // RadioState signals -> UI updates (VFO B)
    connect(m_radioState, &RadioState::frequencyBChanged, this, &MainWindow::onFrequencyBChanged);
    connect(m_radioState, &RadioState::modeBChanged, this, &MainWindow::onModeBChanged);
    // Data sub-mode changes also update mode label (AFSK, FSK, PSK, DATA)
    connect(m_radioState, &RadioState::dataSubModeBChanged, this, [this](int) { updateModeLabels(); });
    connect(m_radioState, &RadioState::sMeterBChanged, this, &MainWindow::onSMeterBChanged);
    // Auto-hide mini pan B when VFOs move to different bands (and SUB RX is off)
    connect(m_radioState, &RadioState::frequencyChanged, m_spectrumController,
            &SpectrumController::checkAndHideMiniPanB);
    connect(m_radioState, &RadioState::frequencyBChanged, m_spectrumController,
            &SpectrumController::checkAndHideMiniPanB);

    // RadioState signals -> Status bar updates
    connect(m_radioState, &RadioState::rfPowerChanged, this, &MainWindow::onRfPowerChanged);
    connect(m_radioState, &RadioState::supplyVoltageChanged, this, &MainWindow::onSupplyVoltageChanged);
    connect(m_radioState, &RadioState::supplyCurrentChanged, this, &MainWindow::onSupplyCurrentChanged);
    connect(m_radioState, &RadioState::swrChanged, this, &MainWindow::onSwrChanged);

    // Display FPS (synthetic menu item)
    connect(m_radioState, &RadioState::displayFpsChanged, this, &MainWindow::onDisplayFpsChanged);

    // Error/notification messages from K4 (ERxx: format) -> show notification popup
    connect(m_radioState, &RadioState::errorNotificationReceived, this, &MainWindow::onErrorNotification);

    // TX Meter data -> update power displays and VFO multifunction meters during TX
    connect(m_radioState, &RadioState::txMeterChanged, this, [this](int alc, int comp, double fwdPower, double swr) {
        // Update status bar power label
        QString powerStr;
        if (fwdPower < 10.0) {
            powerStr = QString("%1 W").arg(fwdPower, 0, 'f', 1);
        } else {
            powerStr = QString("%1 W").arg(static_cast<int>(fwdPower));
        }
        m_powerLabel->setText(powerStr);
        // Update side panel power reading
        m_sideControlPanel->setPowerReading(fwdPower);

        // Calculate PA drain current (Id) from forward power and supply voltage
        // Formula: Id = ForwardPower / (Voltage × Efficiency)
        // K4 PA efficiency is approximately 34% (measured: 80W @ 17A @ 13.8V)
        double voltage = m_radioState->supplyVoltage();
        double paCurrent = 0.0;
        if (voltage > 0 && fwdPower > 0) {
            paCurrent = fwdPower / (voltage * 0.34);
        }

        // Update TX meters only on the active TX VFO
        // SPLIT OFF: VFO A transmits, SPLIT ON: VFO B transmits
        if (m_radioState->splitEnabled()) {
            m_vfoB->setTxMeters(alc, comp, fwdPower, swr);
            m_vfoB->setTxMeterCurrent(paCurrent);
        } else {
            m_vfoA->setTxMeters(alc, comp, fwdPower, swr);
            m_vfoA->setTxMeterCurrent(paCurrent);
        }
    });

    // TX state changes -> switch VFO meters between S-meter (RX) and Po (TX) mode
    // Also change TX indicator color to red when transmitting
    connect(m_radioState, &RadioState::transmitStateChanged, this, [this](bool transmitting) {
        // Only the active TX VFO switches to TX meter mode
        // SPLIT OFF: VFO A transmits, SPLIT ON: VFO B transmits
        // The non-TX VFO stays in S-meter mode (showing received signal)
        if (m_radioState->splitEnabled()) {
            m_vfoA->setTransmitting(false); // VFO A stays in RX mode
            m_vfoB->setTransmitting(transmitting);
        } else {
            m_vfoA->setTransmitting(transmitting);
            m_vfoB->setTransmitting(false); // VFO B stays in RX mode
        }

        // TX indicator and triangles turn red when transmitting
        QString color = transmitting ? "#FF0000" : K4Styles::Colors::AccentAmber;
        m_txIndicator->setStyleSheet(QString("color: %1; font-size: 18px; font-weight: bold;").arg(color));
        m_txTriangle->setStyleSheet(QString("color: %1; font-size: 18px;").arg(color));
        m_txTriangleB->setStyleSheet(QString("color: %1; font-size: 18px;").arg(color));

        // When XIT is active, show the actual TX frequency on the TX VFO display
        // No split: VFO A displays TX freq; Split: VFO B displays TX freq
        // On return to RX, restore the normal RX frequency display
        if (m_radioState->xitEnabled()) {
            if (m_radioState->splitEnabled()) {
                onFrequencyBChanged(m_radioState->vfoB());
            } else {
                onFrequencyChanged(m_radioState->vfoA());
            }
        }
    });

    // SUB indicator - green when sub RX enabled, grey when off
    // Also updates DIV indicator since DIV requires SUB to be on
    // Also dims VFO B frequency and mode labels when SUB RX is off
    connect(m_radioState, &RadioState::subRxEnabledChanged, this, [this](bool enabled) {
        if (enabled) {
            m_subLabel->setStyleSheet(QString("background-color: %1;"
                                              "color: black;"
                                              "font-size: 9px;"
                                              "font-weight: bold;"
                                              "border-radius: 2px;")
                                          .arg(K4Styles::Colors::StatusGreen));
            // If DIV is also on, light up the DIV indicator (handles timing when SB3 comes after DV1)
            if (m_radioState->diversityEnabled()) {
                m_divLabel->setStyleSheet(QString("background-color: %1;"
                                                  "color: black;"
                                                  "font-size: 9px;"
                                                  "font-weight: bold;"
                                                  "border-radius: 2px;")
                                              .arg(K4Styles::Colors::StatusGreen));
            }
            // Restore VFO B frequency and mode to normal white
            m_vfoB->frequencyDisplay()->setNormalColor(QColor(K4Styles::Colors::TextWhite));
            m_modeBLabel->setStyleSheet(
                QString("color: %1; font-size: 11px; font-weight: bold;").arg(K4Styles::Colors::TextWhite));
        } else {
            m_subLabel->setStyleSheet(
                QString("background-color: %1;"
                        "color: %2;"
                        "font-size: 9px;"
                        "font-weight: bold;"
                        "border-radius: 2px;")
                    .arg(K4Styles::Colors::DisabledBackground, K4Styles::Colors::LightGradientTop));
            // DIV requires SUB - turn off DIV indicator when SUB is off
            m_divLabel->setStyleSheet(
                QString("background-color: %1;"
                        "color: %2;"
                        "font-size: 9px;"
                        "font-weight: bold;"
                        "border-radius: 2px;")
                    .arg(K4Styles::Colors::DisabledBackground, K4Styles::Colors::LightGradientTop));
            // Dim VFO B frequency and mode to indicate SUB RX is off
            m_vfoB->frequencyDisplay()->setNormalColor(QColor(K4Styles::Colors::InactiveGray));
            m_modeBLabel->setStyleSheet(
                QString("color: %1; font-size: 11px; font-weight: bold;").arg(K4Styles::Colors::InactiveGray));

            // Auto-hide mini pan B if VFOs are on different bands (can't have mini pan B without SUB RX)
            m_spectrumController->checkAndHideMiniPanB();
        }
    });

    // DIV indicator - green only when BOTH diversity AND sub RX are enabled
    // (DIV requires SUB to be on - can't have DIV without SUB)
    connect(m_radioState, &RadioState::diversityChanged, this, [this](bool enabled) {
        // DIV only shows green if both diversity is enabled AND sub RX is enabled
        bool showActive = enabled && m_radioState->subReceiverEnabled();
        if (showActive) {
            m_divLabel->setStyleSheet(QString("background-color: %1;"
                                              "color: black;"
                                              "font-size: 9px;"
                                              "font-weight: bold;"
                                              "border-radius: 2px;")
                                          .arg(K4Styles::Colors::StatusGreen));
        } else {
            m_divLabel->setStyleSheet(
                QString("background-color: %1;"
                        "color: %2;"
                        "font-size: 9px;"
                        "font-weight: bold;"
                        "border-radius: 2px;")
                    .arg(K4Styles::Colors::DisabledBackground, K4Styles::Colors::LightGradientTop));
        }
    });

    // VFO Lock indicators - show lock arc on VFO A/B squares when locked
    connect(m_radioState, &RadioState::lockAChanged, this, [this](bool locked) { m_vfoRow->setLockA(locked); });
    connect(m_radioState, &RadioState::lockBChanged, this, [this](bool locked) { m_vfoRow->setLockB(locked); });

    // NOTE: KPA1500 amplifier integration groundwork is in the KPA1500 section (after m_kpa1500Client creation)

    // RadioState signals -> Side control panel updates (BW/SHFT/HI/LO)
    // Helper to update all 4 filter display values (called on BW or SHFT change)
    // When B SET is enabled, shows VFO B (Sub RX) filter values instead of VFO A
    auto updateFilterDisplay = [this]() {
        bool bSet = m_radioState->bSetEnabled();

        // Get bandwidth and shift from correct VFO
        int bwHz = bSet ? m_radioState->filterBandwidthB() : m_radioState->filterBandwidth();
        int shiftHz = bSet ? m_radioState->shiftBHz() : m_radioState->shiftHz();

        // BW/SHFT in kHz
        m_sideControlPanel->setBandwidth(bwHz / 1000.0);
        m_sideControlPanel->setShift(shiftHz / 1000.0);

        // Calculate and set HI/LO in kHz (clamp LO to 0, then derive HI from LO + BW)
        int lowHz = qMax(0, shiftHz - (bwHz / 2));
        int highHz = lowHz + bwHz;
        m_sideControlPanel->setHighCut(highHz / 1000.0);
        m_sideControlPanel->setLowCut(lowHz / 1000.0);
    };
    connect(m_radioState, &RadioState::filterBandwidthChanged, this, updateFilterDisplay);
    connect(m_radioState, &RadioState::ifShiftChanged, this, updateFilterDisplay);
    connect(m_radioState, &RadioState::filterBandwidthBChanged, this, updateFilterDisplay);
    connect(m_radioState, &RadioState::ifShiftBChanged, this, updateFilterDisplay);
    connect(m_radioState, &RadioState::bSetChanged, this, updateFilterDisplay);
    connect(m_radioState, &RadioState::keyerSpeedChanged, m_sideControlPanel, &SideControlPanel::setWpm);
    connect(m_radioState, &RadioState::cwPitchChanged, this, [this](int pitch) {
        m_sideControlPanel->setPitch(pitch / 1000.0); // Hz to kHz (500Hz = 0.50)
    });
    connect(m_radioState, &RadioState::rfPowerChanged, this,
            [this](double watts, bool) { m_sideControlPanel->setPower(watts); });
    connect(m_radioState, &RadioState::qskDelayChanged, this, [this](int delay) {
        m_sideControlPanel->setDelay(delay / 100.0); // 10ms units to seconds (20 -> 0.20)
    });
    connect(m_radioState, &RadioState::rfGainChanged, m_sideControlPanel, &SideControlPanel::setMainRfGain);
    connect(m_radioState, &RadioState::squelchChanged, m_sideControlPanel, &SideControlPanel::setMainSquelch);
    connect(m_radioState, &RadioState::rfGainBChanged, m_sideControlPanel, &SideControlPanel::setSubRfGain);
    connect(m_radioState, &RadioState::squelchBChanged, m_sideControlPanel, &SideControlPanel::setSubSquelch);
    connect(m_radioState, &RadioState::micGainChanged, m_sideControlPanel, &SideControlPanel::setMicGain);
    connect(m_radioState, &RadioState::compressionChanged, m_sideControlPanel, &SideControlPanel::setCompression);
    // Mode-dependent WPM/PTCH vs MIC/CMP display
    connect(m_radioState, &RadioState::modeChanged, this, [this](RadioState::Mode mode) {
        bool isCW = (mode == RadioState::CW || mode == RadioState::CW_R);
        m_sideControlPanel->setDisplayMode(isCW);
        // Refresh values after mode switch
        if (isCW) {
            m_sideControlPanel->setWpm(m_radioState->keyerSpeed());
            m_sideControlPanel->setPitch(m_radioState->cwPitch() / 1000.0);
        } else {
            m_sideControlPanel->setMicGain(m_radioState->micGain());
            m_sideControlPanel->setCompression(m_radioState->compression());
        }
    });

    // RadioState signals -> Center section updates
    connect(m_radioState, &RadioState::splitChanged, this, &MainWindow::onSplitChanged);
    connect(m_radioState, &RadioState::antennaChanged, this, &MainWindow::onAntennaChanged);
    connect(m_radioState, &RadioState::antennaNameChanged, this, &MainWindow::onAntennaNameChanged);
    connect(m_radioState, &RadioState::voxChanged, this, &MainWindow::onVoxChanged);
    connect(m_radioState, &RadioState::qskEnabledChanged, this, &MainWindow::onQskEnabledChanged);
    connect(m_radioState, &RadioState::testModeChanged, this, &MainWindow::onTestModeChanged);
    connect(m_radioState, &RadioState::atuModeChanged, this, &MainWindow::onAtuModeChanged);
    connect(m_radioState, &RadioState::ritXitChanged, this, [this](bool ritEnabled, bool xitEnabled, int offset) {
        if (!m_radioState->bSetEnabled()) {
            // BSET off: RIT/XIT state from VFO A
            // In split mode, XIT offset lives in RO$ (VFO B register).
            // Use RO$ when XIT is active OR when XIT was active (preserved value on toggle-off)
            int displayOffset = offset;
            if (m_radioState->splitEnabled() && !ritEnabled && m_radioState->ritXitOffsetB() != 0)
                displayOffset = m_radioState->ritXitOffsetB();
            onRitXitChanged(ritEnabled, xitEnabled, displayOffset);
        } else {
            // BSET on: RIT from VFO B (RO$); XIT from RO (no split) or RO$ (split)
            int displayOffset;
            if (xitEnabled)
                displayOffset = m_radioState->splitEnabled() ? m_radioState->ritXitOffsetB() : offset;
            else
                displayOffset = m_radioState->ritXitOffsetB();
            onRitXitChanged(m_radioState->ritEnabledB(), xitEnabled, displayOffset);
        }
    });
    connect(m_radioState, &RadioState::ritXitBChanged, this, [this](bool ritEnabled, int offset) {
        if (m_radioState->bSetEnabled()) {
            // BSET on: VFO B offset changed — update display
            onRitXitChanged(ritEnabled, m_radioState->xitEnabled(), offset);
        } else if (m_radioState->splitEnabled() && m_radioState->xitEnabled()) {
            // Split + XIT: K4 routes XIT offset to RO$ (VFO B register)
            onRitXitChanged(m_radioState->ritEnabled(), true, offset);
        }
    });
    connect(m_radioState, &RadioState::messageBankChanged, this, &MainWindow::onMessageBankChanged);

    // Filter position indicators
    connect(m_radioState, &RadioState::filterPositionChanged, this,
            [this](int pos) { m_filterAWidget->setFilterPosition(pos); });
    connect(m_radioState, &RadioState::filterPositionBChanged, this,
            [this](int pos) { m_filterBWidget->setFilterPosition(pos); });

    // Filter bandwidth and shift → FilterIndicatorWidget shape
    connect(m_radioState, &RadioState::filterBandwidthChanged, this,
            [this](int bw) { m_filterAWidget->setBandwidth(bw); });
    connect(m_radioState, &RadioState::filterBandwidthBChanged, this,
            [this](int bw) { m_filterBWidget->setBandwidth(bw); });
    connect(m_radioState, &RadioState::ifShiftChanged, this, [this](int shift) { m_filterAWidget->setShift(shift); });
    connect(m_radioState, &RadioState::ifShiftBChanged, this, [this](int shift) { m_filterBWidget->setShift(shift); });
    // Mode affects filter indicator shift center calculation
    connect(m_radioState, &RadioState::modeChanged, this,
            [this](RadioState::Mode mode) { m_filterAWidget->setMode(RadioState::modeToString(mode)); });
    connect(m_radioState, &RadioState::modeBChanged, this,
            [this](RadioState::Mode mode) { m_filterBWidget->setMode(RadioState::modeToString(mode)); });
    // DATA submode affects filter indicator shape (RTTY dual triangles)
    connect(m_radioState, &RadioState::dataSubModeChanged, this,
            [this](int subMode) { m_filterAWidget->setDataSubMode(subMode); });
    connect(m_radioState, &RadioState::dataSubModeBChanged, this,
            [this](int subMode) { m_filterBWidget->setDataSubMode(subMode); });

    // RadioState signals -> Processing state updates (AGC, PRE, ATT, NB, NR)
    connect(m_radioState, &RadioState::processingChanged, this, &MainWindow::onProcessingChanged);
    connect(m_radioState, &RadioState::processingChangedB, this, &MainWindow::onProcessingChangedB);

    // RadioState signals -> MAIN RX / SUB RX popup button label updates
    // AFX button: primary = "AFX ON/OFF", alternate = mode (DELAY/PITCH/OFF)
    connect(m_radioState, &RadioState::afxModeChanged, this, [this](int mode) {
        QString primary = (mode == 0) ? "AFX OFF" : "AFX ON";
        QString alternate;
        switch (mode) {
        case 0:
            alternate = "OFF";
            break;
        case 1:
            alternate = "DELAY";
            break;
        case 2:
            alternate = "PITCH";
            break;
        }
        if (m_mainRxPopup)
            m_mainRxPopup->setButtonLabel(3, primary, alternate);
        if (m_subRxPopup)
            m_subRxPopup->setButtonLabel(3, primary, alternate);
    });

    // AGC button: primary = speed (AGC-S/AGC-F), alternate = ON/OFF
    connect(m_radioState, &RadioState::processingChanged, this, [this]() {
        QString primary;
        QString alternate;
        switch (m_radioState->agcSpeed()) {
        case RadioState::AGC_Off:
            primary = "AGC";
            alternate = "OFF";
            break;
        case RadioState::AGC_Slow:
            primary = "AGC-S";
            alternate = "ON";
            break;
        case RadioState::AGC_Fast:
            primary = "AGC-F";
            alternate = "ON";
            break;
        }
        if (m_mainRxPopup)
            m_mainRxPopup->setButtonLabel(4, primary, alternate);
    });

    connect(m_radioState, &RadioState::processingChangedB, this, [this]() {
        QString primary;
        QString alternate;
        switch (m_radioState->agcSpeedB()) {
        case RadioState::AGC_Off:
            primary = "AGC";
            alternate = "OFF";
            break;
        case RadioState::AGC_Slow:
            primary = "AGC-S";
            alternate = "ON";
            break;
        case RadioState::AGC_Fast:
            primary = "AGC-F";
            alternate = "ON";
            break;
        }
        if (m_subRxPopup)
            m_subRxPopup->setButtonLabel(4, primary, alternate);
    });

    // APF button: Main RX APF state -> MAIN RX popup and VFO A indicator
    connect(m_radioState, &RadioState::apfChanged, this, [this](bool enabled, int width) {
        QString alternate;
        if (!enabled) {
            alternate = "OFF";
        } else {
            static const char *bwNames[] = {"30Hz", "50Hz", "150Hz"};
            alternate = bwNames[qBound(0, width, 2)];
        }
        if (m_mainRxPopup)
            m_mainRxPopup->setButtonLabel(5, "APF", alternate);
        m_vfoA->setApf(enabled, width);
    });

    // APF button: Sub RX APF state -> SUB RX popup and VFO B indicator
    connect(m_radioState, &RadioState::apfBChanged, this, [this](bool enabled, int width) {
        QString alternate;
        if (!enabled) {
            alternate = "OFF";
        } else {
            static const char *bwNames[] = {"30Hz", "50Hz", "150Hz"};
            alternate = bwNames[qBound(0, width, 2)];
        }
        if (m_subRxPopup)
            m_subRxPopup->setButtonLabel(5, "APF", alternate);
        m_vfoB->setApf(enabled, width);
    });

    // RadioState waterfall height -> mini-pans (panadapter wiring is in SpectrumController)
    connect(m_radioState, &RadioState::waterfallHeightChanged, this, [this](int percent) {
        m_vfoA->setMiniPanWaterfallHeight(percent);
        m_vfoB->setMiniPanWaterfallHeight(percent);
    });

    // RadioState display state -> DisplayPopup (for button face updates)
    // Separate LCD and EXT signals
    connect(m_radioState, &RadioState::dualPanModeLcdChanged, m_displayPopup, &DisplayPopupWidget::setDualPanModeLcd);
    connect(m_radioState, &RadioState::dualPanModeExtChanged, m_displayPopup, &DisplayPopupWidget::setDualPanModeExt);
    connect(m_radioState, &RadioState::displayModeLcdChanged, m_displayPopup, &DisplayPopupWidget::setDisplayModeLcd);
    connect(m_radioState, &RadioState::displayModeExtChanged, m_displayPopup, &DisplayPopupWidget::setDisplayModeExt);
    connect(m_radioState, &RadioState::waterfallColorChanged, m_displayPopup, &DisplayPopupWidget::setWaterfallColor);
    connect(m_radioState, &RadioState::averagingChanged, m_displayPopup, &DisplayPopupWidget::setAveraging);
    connect(m_radioState, &RadioState::averagingChanged, this, [this](int level) {
        m_vfoA->setMiniPanAveraging(level);
        m_vfoB->setMiniPanAveraging(level);
    });
    connect(m_radioState, &RadioState::peakModeChanged, m_displayPopup, &DisplayPopupWidget::setPeakMode);
    connect(m_radioState, &RadioState::fixedTuneChanged, m_displayPopup, &DisplayPopupWidget::setFixedTuneMode);
    connect(m_radioState, &RadioState::freezeChanged, m_displayPopup, &DisplayPopupWidget::setFreeze);
    connect(m_radioState, &RadioState::vfoACursorChanged, m_displayPopup, &DisplayPopupWidget::setVfoACursor);
    connect(m_radioState, &RadioState::vfoBCursorChanged, m_displayPopup, &DisplayPopupWidget::setVfoBCursor);
    connect(m_radioState, &RadioState::autoRefLevelChanged, m_displayPopup, &DisplayPopupWidget::setAutoRefLevel);
    connect(m_radioState, &RadioState::scaleChanged, m_displayPopup, &DisplayPopupWidget::setScale);
    connect(m_radioState, &RadioState::ddcNbModeChanged, m_displayPopup, &DisplayPopupWidget::setDdcNbMode);
    connect(m_radioState, &RadioState::ddcNbLevelChanged, m_displayPopup, &DisplayPopupWidget::setDdcNbLevel);
    connect(m_radioState, &RadioState::waterfallHeightChanged, m_displayPopup, &DisplayPopupWidget::setWaterfallHeight);
    connect(m_radioState, &RadioState::waterfallHeightExtChanged, m_displayPopup,
            &DisplayPopupWidget::setWaterfallHeightExt);
    // Also update span/ref values in popup
    connect(m_radioState, &RadioState::spanChanged, this, [this](int spanHz) {
        m_displayPopup->setSpanValueA(spanHz / 1000.0); // Hz to kHz
    });
    connect(m_radioState, &RadioState::spanBChanged, this, [this](int spanHz) {
        m_displayPopup->setSpanValueB(spanHz / 1000.0); // Hz to kHz
    });
    connect(m_radioState, &RadioState::refLevelChanged, m_displayPopup, &DisplayPopupWidget::setRefLevelValueA);
    connect(m_radioState, &RadioState::refLevelBChanged, m_displayPopup, &DisplayPopupWidget::setRefLevelValueB);

    // Averaging control +/- -> local only (not sent to K4 — our smoothing differs from K4's)
    connect(m_displayPopup, &DisplayPopupWidget::averagingIncrementRequested, this, [this]() {
        int current = m_radioState->averaging();
        int next = qMin(current + 1, 20);
        m_radioState->setAveraging(next);
    });
    connect(m_displayPopup, &DisplayPopupWidget::averagingDecrementRequested, this, [this]() {
        int current = m_radioState->averaging();
        int next = qMax(current - 1, 1);
        m_radioState->setAveraging(next);
    });

    // DDC NB level control +/- -> CAT commands
    connect(m_displayPopup, &DisplayPopupWidget::nbLevelIncrementRequested, this, [this]() {
        int current = m_radioState->ddcNbLevel();
        int next = qMin(current + 1, 14);
        m_connectionController->sendCAT(QString("#NBL$%1;").arg(next, 2, 10, QChar('0')));
    });
    connect(m_displayPopup, &DisplayPopupWidget::nbLevelDecrementRequested, this, [this]() {
        int current = m_radioState->ddcNbLevel();
        int next = qMax(current - 1, 0);
        m_connectionController->sendCAT(QString("#NBL$%1;").arg(next, 2, 10, QChar('0')));
    });

    // Waterfall height control +/- -> CAT commands (respects LCD/EXT selection)
    // LCD controls our app's panadapter, EXT is just for external HDMI display
    connect(m_displayPopup, &DisplayPopupWidget::waterfallHeightIncrementRequested, this, [this]() {
        bool isExt = m_displayPopup->isExtEnabled() && !m_displayPopup->isLcdEnabled();
        int current = isExt ? m_radioState->waterfallHeightExt() : m_radioState->waterfallHeight();
        int next = qMin(current + 1, 90); // 1% steps, max 90%
        QString cmd =
            isExt ? QString("#HWFH%1;").arg(next, 2, 10, QChar('0')) : QString("#WFH%1;").arg(next, 2, 10, QChar('0'));
        m_connectionController->sendCAT(cmd);
        // Optimistically update RadioState and UI (K4 may not echo this command)
        if (!isExt) {
            m_radioState->setWaterfallHeight(next);
            m_spectrumController->panadapterA()->setWaterfallHeight(next);
            m_spectrumController->panadapterB()->setWaterfallHeight(next);
            m_displayPopup->setWaterfallHeight(next);
            m_vfoA->setMiniPanWaterfallHeight(next);
            m_vfoB->setMiniPanWaterfallHeight(next);
        } else {
            m_radioState->setWaterfallHeightExt(next);
            m_displayPopup->setWaterfallHeightExt(next);
        }
    });
    connect(m_displayPopup, &DisplayPopupWidget::waterfallHeightDecrementRequested, this, [this]() {
        bool isExt = m_displayPopup->isExtEnabled() && !m_displayPopup->isLcdEnabled();
        int current = isExt ? m_radioState->waterfallHeightExt() : m_radioState->waterfallHeight();
        int next = qMax(current - 1, 10); // 1% steps, min 10%
        QString cmd =
            isExt ? QString("#HWFH%1;").arg(next, 2, 10, QChar('0')) : QString("#WFH%1;").arg(next, 2, 10, QChar('0'));
        m_connectionController->sendCAT(cmd);
        // Optimistically update RadioState and UI (K4 may not echo this command)
        if (!isExt) {
            m_radioState->setWaterfallHeight(next);
            m_spectrumController->panadapterA()->setWaterfallHeight(next);
            m_spectrumController->panadapterB()->setWaterfallHeight(next);
            m_displayPopup->setWaterfallHeight(next);
            m_vfoA->setMiniPanWaterfallHeight(next);
            m_vfoB->setMiniPanWaterfallHeight(next);
        } else {
            m_radioState->setWaterfallHeightExt(next);
            m_displayPopup->setWaterfallHeightExt(next);
        }
    });

    // Span control from display popup -> CAT commands (respects A/B selection)
    connect(m_displayPopup, &DisplayPopupWidget::spanIncrementRequested, this, [this]() {
        bool vfoA = m_displayPopup->isVfoAEnabled();
        bool vfoB = m_displayPopup->isVfoBEnabled();
        int currentSpan = (vfoB && !vfoA) ? m_radioState->spanHzB() : m_radioState->spanHz();
        int newSpan = RadioUtils::getNextSpanUp(currentSpan); // + increases span
        if (newSpan != currentSpan) {
            if (vfoA) {
                m_radioState->setSpanHz(newSpan);
                m_connectionController->sendCAT(QString("#SPN%1;").arg(newSpan));
            }
            if (vfoB) {
                m_radioState->setSpanHzB(newSpan);
                m_connectionController->sendCAT(QString("#SPN$%1;").arg(newSpan));
            }
        }
    });
    connect(m_displayPopup, &DisplayPopupWidget::spanDecrementRequested, this, [this]() {
        bool vfoA = m_displayPopup->isVfoAEnabled();
        bool vfoB = m_displayPopup->isVfoBEnabled();
        int currentSpan = (vfoB && !vfoA) ? m_radioState->spanHzB() : m_radioState->spanHz();
        int newSpan = RadioUtils::getNextSpanDown(currentSpan); // - decreases span
        if (newSpan != currentSpan) {
            if (vfoA) {
                m_radioState->setSpanHz(newSpan);
                m_connectionController->sendCAT(QString("#SPN%1;").arg(newSpan));
            }
            if (vfoB) {
                m_radioState->setSpanHzB(newSpan);
                m_connectionController->sendCAT(QString("#SPN$%1;").arg(newSpan));
            }
        }
    });

    // Scale control (GLOBAL - affects both panadapters, no A/B variants)
    connect(m_displayPopup, &DisplayPopupWidget::scaleIncrementRequested, this, [this]() {
        int currentScale = m_radioState->scale();
        if (currentScale < 0)
            currentScale = 75;                      // Default if not yet received
        int newScale = qMin(currentScale + 1, 150); // Increment by 1, max 150
        if (newScale != currentScale) {
            m_connectionController->sendCAT(QString("#SCL%1;").arg(newScale));
            // Optimistic update (scale is global, may not echo back)
            m_radioState->setScale(newScale); // Also updates panadapters via signal
        }
    });
    connect(m_displayPopup, &DisplayPopupWidget::scaleDecrementRequested, this, [this]() {
        int currentScale = m_radioState->scale();
        if (currentScale < 0)
            currentScale = 75;                     // Default if not yet received
        int newScale = qMax(currentScale - 1, 10); // Decrement by 1, min 10
        if (newScale != currentScale) {
            m_connectionController->sendCAT(QString("#SCL%1;").arg(newScale));
            // Optimistic update (scale is global, may not echo back)
            m_radioState->setScale(newScale); // Also updates panadapters via signal
        }
    });

    // Ref level control (LCD only for now, respects A/B selection)
    // #REF for Main RX, #REF$ for Sub RX - absolute values from -200 to 60
    connect(m_displayPopup, &DisplayPopupWidget::refLevelIncrementRequested, this, [this]() {
        bool vfoA = m_displayPopup->isVfoAEnabled();
        bool vfoB = m_displayPopup->isVfoBEnabled();
        if (vfoA) {
            int currentLevel = m_radioState->refLevel();
            int newLevel = qMin(currentLevel + 1, 60); // Increment by 1 dB, max 60
            if (newLevel != currentLevel) {
                m_radioState->setRefLevel(newLevel);
                m_connectionController->sendCAT(QString("#REF%1;").arg(newLevel));
            }
        }
        if (vfoB) {
            int currentLevel = m_radioState->refLevelB();
            int newLevel = qMin(currentLevel + 1, 60);
            if (newLevel != currentLevel) {
                m_radioState->setRefLevelB(newLevel);
                m_connectionController->sendCAT(QString("#REF$%1;").arg(newLevel));
            }
        }
    });
    connect(m_displayPopup, &DisplayPopupWidget::refLevelDecrementRequested, this, [this]() {
        bool vfoA = m_displayPopup->isVfoAEnabled();
        bool vfoB = m_displayPopup->isVfoBEnabled();
        if (vfoA) {
            int currentLevel = m_radioState->refLevel();
            int newLevel = qMax(currentLevel - 1, -200); // Decrement by 1 dB, min -200
            if (newLevel != currentLevel) {
                m_radioState->setRefLevel(newLevel);
                m_connectionController->sendCAT(QString("#REF%1;").arg(newLevel));
            }
        }
        if (vfoB) {
            int currentLevel = m_radioState->refLevelB();
            int newLevel = qMax(currentLevel - 1, -200);
            if (newLevel != currentLevel) {
                m_radioState->setRefLevelB(newLevel);
                m_connectionController->sendCAT(QString("#REF$%1;").arg(newLevel));
            }
        }
    });

    // Protocol spectrum data -> SpectrumController (via ConnectionController re-emitted signals)
    connect(m_connectionController, &ConnectionController::spectrumDataReceived, m_spectrumController,
            &SpectrumController::onSpectrumData);
    connect(m_connectionController, &ConnectionController::miniSpectrumDataReceived, m_spectrumController,
            &SpectrumController::onMiniSpectrumData);

    // Clock timer for date/time display
    connect(m_clockTimer, &QTimer::timeout, this, &MainWindow::updateDateTime);
    m_clockTimer->start(1000);
    updateDateTime();

    // Hardware controller owns KPOD, HaliKey, IambicKeyer, SidetoneGenerator and their threads
    m_hardwareController = new HardwareController(m_radioState, m_connectionController, this);

    // KPOD button presses → macro execution
    connect(m_hardwareController, &HardwareController::macroRequested, this, &MainWindow::executeMacro);

    // KPA1500 amplifier client
    m_kpa1500Client = new KPA1500Client(this);

    // Connect KPA1500 signals
    connect(m_kpa1500Client, &KPA1500Client::connected, this, &MainWindow::onKpa1500Connected);
    connect(m_kpa1500Client, &KPA1500Client::disconnected, this, &MainWindow::onKpa1500Disconnected);
    connect(m_kpa1500Client, &KPA1500Client::errorOccurred, this, &MainWindow::onKpa1500Error);

    // Wire KPA1500 data signals to embedded mini panel in right side panel
    auto *kpaMini = m_rightSidePanel->kpa1500Mini();
    connect(m_kpa1500Client, &KPA1500Client::powerChanged, this, [kpaMini](double fwd, double ref, double) {
        kpaMini->setForwardPower(static_cast<float>(fwd));
        kpaMini->setReflectedPower(static_cast<float>(ref));
    });
    connect(m_kpa1500Client, &KPA1500Client::swrChanged, this,
            [kpaMini](double swr) { kpaMini->setSWR(static_cast<float>(swr)); });
    connect(m_kpa1500Client, &KPA1500Client::paTemperatureChanged, this,
            [kpaMini](double tempC) { kpaMini->setTemperature(static_cast<float>(tempC)); });
    connect(m_kpa1500Client, &KPA1500Client::operatingStateChanged, this,
            [kpaMini](KPA1500Client::OperatingState state) { kpaMini->setMode(state == KPA1500Client::StateOperate); });
    connect(m_kpa1500Client, &KPA1500Client::atuInlineChanged, this,
            [kpaMini](bool inline_) { kpaMini->setAtuMode(inline_); });
    connect(m_kpa1500Client, &KPA1500Client::antennaChanged, this,
            [kpaMini](int antenna) { kpaMini->setAntenna(antenna); });
    connect(m_kpa1500Client, &KPA1500Client::faultStatusChanged, this,
            [kpaMini](KPA1500Client::FaultStatus status, const QString &) {
                kpaMini->setFault(status == KPA1500Client::FaultActive);
            });
    connect(m_kpa1500Client, &KPA1500Client::connected, this, [kpaMini]() {
        kpaMini->setConnected(true);
        kpaMini->setVisible(true);
    });
    connect(m_kpa1500Client, &KPA1500Client::disconnected, this, [kpaMini]() {
        kpaMini->setConnected(false);
        kpaMini->setVisible(false);
    });

    // Wire mini panel button signals to KPA1500 commands
    connect(kpaMini, &Kpa1500MiniPanel::modeToggled, this,
            [this](bool operate) { m_kpa1500Client->sendCommand(operate ? "^OS1;" : "^OS0;"); });
    connect(kpaMini, &Kpa1500MiniPanel::atuTuneRequested, this, [this]() { m_kpa1500Client->sendCommand("^FT;"); });
    connect(kpaMini, &Kpa1500MiniPanel::atuModeToggled, this,
            [this](bool in) { m_kpa1500Client->sendCommand(in ? "^AI1;" : "^AI0;"); });
    connect(kpaMini, &Kpa1500MiniPanel::antennaChanged, this,
            [this](int ant) { m_kpa1500Client->sendCommand(QString("^AN%1;").arg(ant)); });

    // Connect to settings for KPA1500 enable/disable and settings changes
    connect(RadioSettings::instance(), &RadioSettings::kpa1500EnabledChanged, this,
            &MainWindow::onKpa1500EnabledChanged);
    connect(RadioSettings::instance(), &RadioSettings::kpa1500SettingsChanged, this,
            &MainWindow::onKpa1500SettingsChanged);

    // KPA1500 connects when K4 connects (in onAuthenticated), not on app start

    // Initialize KPA1500 status display
    updateKpa1500Status();

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

    // resize directly instead of deferring - testing if deferred resize affects QRhi
    // QTimer::singleShot(0, this, [this]() { resize(1340, 800); });
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

    // Disconnect KPA1500 signals before child destruction to prevent
    // callbacks accessing destroyed widgets during cleanup
    if (m_kpa1500Client) {
        disconnect(m_kpa1500Client, nullptr, this, nullptr);
        m_kpa1500Client->disconnectFromHost();
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
                                                m_kpa1500Client, this);
        }
        m_optionsDialog->show();
        m_optionsDialog->raise();
        m_optionsDialog->activateWindow();
    });
    toolsMenu->addAction(optionsAction);

    // View menu
    QMenu *viewMenu = menuBar()->addMenu("&View");
    Q_UNUSED(viewMenu)

    // Help menu
    QMenu *helpMenu = menuBar()->addMenu("&Help");
    QAction *aboutAction = new QAction("&About QK4", this);
    aboutAction->setMenuRole(QAction::AboutRole); // macOS: moves to app menu
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, "About QK4",
                           QString("<h2>QK4</h2>"
                                   "<p>Version %1</p>"
                                   "<p>Remote control application for Elecraft K4 radios.</p>"
                                   "<p>Copyright &copy; 2024-2025 AI5QK</p>"
                                   "<p><a href='https://github.com/mikeg-dal/QK4'>github.com/mikeg-dal/QK4</a></p>")
                               .arg(QCoreApplication::applicationVersion()));
    });
    helpMenu->addAction(aboutAction);
}

void MainWindow::setupUi() {
    setWindowTitle("QK4");
    setMinimumSize(1340, 840);
    resize(1340, 840); // Default to minimum size on launch

    // NOTE: Do NOT set WA_NativeWindow here!
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

    // Top status bar
    setupTopStatusBar(centralWidget);
    mainLayout->addWidget(m_titleLabel->parentWidget());

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
    m_featureMenuBar = new FeatureMenuBar(this);

    // Feature Menu Bar CAT commands - send appropriate command based on current feature
    connect(m_featureMenuBar, &FeatureMenuBar::toggleRequested, this, [this]() {
        bool bSet = m_radioState->bSetEnabled();
        switch (m_featureMenuBar->currentFeature()) {
        case FeatureMenuBar::Attenuator: {
            // Optimistic UI update - flip enabled state (use Sub RX state if B SET)
            bool newState = bSet ? !m_radioState->attenuatorEnabledB() : !m_radioState->attenuatorEnabled();
            m_featureMenuBar->setFeatureEnabled(newState);
            m_connectionController->sendCAT(bSet ? "RA$/;" : "RA/;");
            break;
        }
        case FeatureMenuBar::NbLevel: {
            // Toggle NB on/off
            bool curState = bSet ? m_radioState->noiseBlankerEnabledB() : m_radioState->noiseBlankerEnabled();
            m_featureMenuBar->setFeatureEnabled(!curState);
            m_connectionController->sendCAT(bSet ? "NB$/;" : "NB/;");
            break;
        }
        case FeatureMenuBar::NrAdjust: {
            bool newState = bSet ? !m_radioState->noiseReductionEnabledB() : !m_radioState->noiseReductionEnabled();
            m_featureMenuBar->setFeatureEnabled(newState);
            m_connectionController->sendCAT(bSet ? "NR$/;" : "NR/;");
            break;
        }
        case FeatureMenuBar::ManualNotch: {
            // Toggle notch on/off for correct VFO
            bool curState = bSet ? m_radioState->manualNotchEnabledB() : m_radioState->manualNotchEnabled();
            m_featureMenuBar->setFeatureEnabled(!curState);
            m_connectionController->sendCAT(bSet ? "NM$/;" : "NM/;");
            break;
        }
        }
    });
    connect(m_featureMenuBar, &FeatureMenuBar::incrementRequested, this, [this]() {
        bool bSet = m_radioState->bSetEnabled();
        switch (m_featureMenuBar->currentFeature()) {
        case FeatureMenuBar::Attenuator: {
            // Optimistic: show next step (RA+ adds 3dB, max 21)
            int curLevel = bSet ? m_radioState->attenuatorLevelB() : m_radioState->attenuatorLevel();
            int newLevel = qMin(curLevel + 3, 21);
            m_featureMenuBar->setValue(newLevel);
            m_connectionController->sendCAT(bSet ? "RA$+;" : "RA+;");
            break;
        }
        case FeatureMenuBar::NbLevel: {
            int curLevel = bSet ? m_radioState->noiseBlankerLevelB() : m_radioState->noiseBlankerLevel();
            int newLevel = qMin(curLevel + 1, 15);
            int enabled =
                bSet ? (m_radioState->noiseBlankerEnabledB() ? 1 : 0) : (m_radioState->noiseBlankerEnabled() ? 1 : 0);
            int filter = bSet ? m_radioState->noiseBlankerFilterWidthB() : m_radioState->noiseBlankerFilterWidth();
            // Optimistic state + UI update
            if (bSet) {
                m_radioState->setNoiseBlankerLevelB(newLevel);
            } else {
                m_radioState->setNoiseBlankerLevel(newLevel);
            }
            m_featureMenuBar->setValue(newLevel);
            QString prefix = bSet ? "NB$" : "NB";
            QString cmd = QString("%1%2%3%4;").arg(prefix).arg(newLevel, 2, 10, QChar('0')).arg(enabled).arg(filter);
            m_connectionController->sendCAT(cmd);
            break;
        }
        case FeatureMenuBar::NrAdjust: {
            int curLevel = bSet ? m_radioState->noiseReductionLevelB() : m_radioState->noiseReductionLevel();
            int newLevel = qMin(curLevel + 1, 10);
            int enabled = bSet ? (m_radioState->noiseReductionEnabledB() ? 1 : 0)
                               : (m_radioState->noiseReductionEnabled() ? 1 : 0);
            // Optimistic state + UI update
            if (bSet) {
                m_radioState->setNoiseReductionLevelB(newLevel);
            } else {
                m_radioState->setNoiseReductionLevel(newLevel);
            }
            m_featureMenuBar->setValue(newLevel);
            QString prefix = bSet ? "NR$" : "NR";
            QString cmd = QString("%1%2%3;").arg(prefix).arg(newLevel, 2, 10, QChar('0')).arg(enabled);
            m_connectionController->sendCAT(cmd);
            break;
        }
        case FeatureMenuBar::ManualNotch: {
            // Use correct VFO's pitch state
            int curPitch = bSet ? m_radioState->manualNotchPitchB() : m_radioState->manualNotchPitch();
            int newPitch = qMin(curPitch + 10, 5000);
            int enabled =
                bSet ? (m_radioState->manualNotchEnabledB() ? 1 : 0) : (m_radioState->manualNotchEnabled() ? 1 : 0);
            // Optimistic state + UI update
            if (bSet) {
                m_radioState->setManualNotchPitchB(newPitch);
            } else {
                m_radioState->setManualNotchPitch(newPitch);
            }
            m_featureMenuBar->setValue(newPitch);
            QString prefix = bSet ? "NM$" : "NM";
            m_connectionController->sendCAT(
                QString("%1%2%3;").arg(prefix).arg(newPitch, 4, 10, QChar('0')).arg(enabled));
            break;
        }
        }
    });
    connect(m_featureMenuBar, &FeatureMenuBar::decrementRequested, this, [this]() {
        bool bSet = m_radioState->bSetEnabled();
        switch (m_featureMenuBar->currentFeature()) {
        case FeatureMenuBar::Attenuator: {
            // Optimistic: show next step (RA- subtracts 3dB, min 0)
            int curLevel = bSet ? m_radioState->attenuatorLevelB() : m_radioState->attenuatorLevel();
            int newLevel = qMax(curLevel - 3, 0);
            m_featureMenuBar->setValue(newLevel);
            m_connectionController->sendCAT(bSet ? "RA$-;" : "RA-;");
            break;
        }
        case FeatureMenuBar::NbLevel: {
            int curLevel = bSet ? m_radioState->noiseBlankerLevelB() : m_radioState->noiseBlankerLevel();
            int newLevel = qMax(curLevel - 1, 0);
            int enabled =
                bSet ? (m_radioState->noiseBlankerEnabledB() ? 1 : 0) : (m_radioState->noiseBlankerEnabled() ? 1 : 0);
            int filter = bSet ? m_radioState->noiseBlankerFilterWidthB() : m_radioState->noiseBlankerFilterWidth();
            // Optimistic state + UI update
            if (bSet) {
                m_radioState->setNoiseBlankerLevelB(newLevel);
            } else {
                m_radioState->setNoiseBlankerLevel(newLevel);
            }
            m_featureMenuBar->setValue(newLevel);
            QString prefix = bSet ? "NB$" : "NB";
            m_connectionController->sendCAT(
                QString("%1%2%3%4;").arg(prefix).arg(newLevel, 2, 10, QChar('0')).arg(enabled).arg(filter));
            break;
        }
        case FeatureMenuBar::NrAdjust: {
            int curLevel = bSet ? m_radioState->noiseReductionLevelB() : m_radioState->noiseReductionLevel();
            int newLevel = qMax(curLevel - 1, 0);
            int enabled = bSet ? (m_radioState->noiseReductionEnabledB() ? 1 : 0)
                               : (m_radioState->noiseReductionEnabled() ? 1 : 0);
            // Optimistic state + UI update
            if (bSet) {
                m_radioState->setNoiseReductionLevelB(newLevel);
            } else {
                m_radioState->setNoiseReductionLevel(newLevel);
            }
            m_featureMenuBar->setValue(newLevel);
            QString prefix = bSet ? "NR$" : "NR";
            m_connectionController->sendCAT(
                QString("%1%2%3;").arg(prefix).arg(newLevel, 2, 10, QChar('0')).arg(enabled));
            break;
        }
        case FeatureMenuBar::ManualNotch: {
            // Use correct VFO's pitch state
            int curPitch = bSet ? m_radioState->manualNotchPitchB() : m_radioState->manualNotchPitch();
            int newPitch = qMax(curPitch - 10, 150);
            int enabled =
                bSet ? (m_radioState->manualNotchEnabledB() ? 1 : 0) : (m_radioState->manualNotchEnabled() ? 1 : 0);
            // Optimistic state + UI update
            if (bSet) {
                m_radioState->setManualNotchPitchB(newPitch);
            } else {
                m_radioState->setManualNotchPitch(newPitch);
            }
            m_featureMenuBar->setValue(newPitch);
            QString prefix = bSet ? "NM$" : "NM";
            m_connectionController->sendCAT(
                QString("%1%2%3;").arg(prefix).arg(newPitch, 4, 10, QChar('0')).arg(enabled));
            break;
        }
        }
    });
    connect(m_featureMenuBar, &FeatureMenuBar::extraButtonClicked, this, [this]() {
        // Extra button cycles NB filter: NONE(0) -> NARROW(1) -> WIDE(2) -> NONE(0)
        if (m_featureMenuBar->currentFeature() == FeatureMenuBar::NbLevel) {
            bool bSet = m_radioState->bSetEnabled();
            int curFilter = bSet ? m_radioState->noiseBlankerFilterWidthB() : m_radioState->noiseBlankerFilterWidth();
            int newFilter = (curFilter + 1) % 3;
            int level = bSet ? m_radioState->noiseBlankerLevelB() : m_radioState->noiseBlankerLevel();
            int enabled =
                bSet ? (m_radioState->noiseBlankerEnabledB() ? 1 : 0) : (m_radioState->noiseBlankerEnabled() ? 1 : 0);
            // Optimistic state + UI update
            if (bSet) {
                m_radioState->setNoiseBlankerFilterB(newFilter);
            } else {
                m_radioState->setNoiseBlankerFilter(newFilter);
            }
            m_featureMenuBar->setNbFilter(newFilter);
            QString prefix = bSet ? "NB$" : "NB";
            m_connectionController->sendCAT(
                QString("%1%2%3%4;").arg(prefix).arg(level, 2, 10, QChar('0')).arg(enabled).arg(newFilter));
        }
    });

    // Update feature menu bar from RadioState - helper lambda
    auto updateFeatureMenuBarState = [this]() {
        if (!m_featureMenuBar->isMenuVisible())
            return;
        bool bSet = m_radioState->bSetEnabled();
        switch (m_featureMenuBar->currentFeature()) {
        case FeatureMenuBar::Attenuator:
            if (bSet) {
                m_featureMenuBar->setFeatureEnabled(m_radioState->attenuatorEnabledB());
                m_featureMenuBar->setValue(m_radioState->attenuatorLevelB());
            } else {
                m_featureMenuBar->setFeatureEnabled(m_radioState->attenuatorEnabled());
                m_featureMenuBar->setValue(m_radioState->attenuatorLevel());
            }
            break;
        case FeatureMenuBar::NbLevel:
            if (bSet) {
                m_featureMenuBar->setFeatureEnabled(m_radioState->noiseBlankerEnabledB());
                m_featureMenuBar->setValue(m_radioState->noiseBlankerLevelB());
                m_featureMenuBar->setNbFilter(m_radioState->noiseBlankerFilterWidthB());
            } else {
                m_featureMenuBar->setFeatureEnabled(m_radioState->noiseBlankerEnabled());
                m_featureMenuBar->setValue(m_radioState->noiseBlankerLevel());
                m_featureMenuBar->setNbFilter(m_radioState->noiseBlankerFilterWidth());
            }
            break;
        case FeatureMenuBar::NrAdjust:
            if (bSet) {
                m_featureMenuBar->setFeatureEnabled(m_radioState->noiseReductionEnabledB());
                m_featureMenuBar->setValue(m_radioState->noiseReductionLevelB());
            } else {
                m_featureMenuBar->setFeatureEnabled(m_radioState->noiseReductionEnabled());
                m_featureMenuBar->setValue(m_radioState->noiseReductionLevel());
            }
            break;
        case FeatureMenuBar::ManualNotch:
            // Use correct VFO's notch state
            if (bSet) {
                m_featureMenuBar->setFeatureEnabled(m_radioState->manualNotchEnabledB());
                m_featureMenuBar->setValue(m_radioState->manualNotchPitchB());
            } else {
                m_featureMenuBar->setFeatureEnabled(m_radioState->manualNotchEnabled());
                m_featureMenuBar->setValue(m_radioState->manualNotchPitch());
            }
            break;
        }
    };
    // Connect both Main and Sub RX processing changes
    connect(m_radioState, &RadioState::processingChanged, this, updateFeatureMenuBarState);
    connect(m_radioState, &RadioState::processingChangedB, this, updateFeatureMenuBarState);
    connect(m_radioState, &RadioState::notchChanged, this, updateFeatureMenuBarState);
    connect(m_radioState, &RadioState::notchBChanged, this, updateFeatureMenuBarState);
    // Also update when B SET changes to refresh display with correct VFO's state
    connect(m_radioState, &RadioState::bSetChanged, this, updateFeatureMenuBarState);

    // Mode Popup Widget (popup, positioned above bottom menu bar when shown)
    m_modePopup = new ModePopupWidget(this);
    connect(m_modePopup, &ModePopupWidget::modeSelected, this, [this](const QString &catCmd) {
        // Send the command to the radio
        m_connectionController->sendCAT(catCmd);

        // Optimistically update data sub-mode (K4 doesn't echo DT SET commands)
        // Parse DT or DT$ from command like "MD6;DT1;" or "MD$6;DT$3;"
        QRegularExpression dtRegex("DT(\\$?)(\\d)");
        QRegularExpressionMatch match = dtRegex.match(catCmd);
        if (match.hasMatch()) {
            bool isSubRx = !match.captured(1).isEmpty(); // DT$ = Sub RX
            int subMode = match.captured(2).toInt();
            qCDebug(qk4Main) << "Optimistic DT update: isSubRx=" << isSubRx << "subMode=" << subMode;
            if (isSubRx) {
                m_radioState->setDataSubModeB(subMode);
            } else {
                m_radioState->setDataSubMode(subMode);
            }
        }
    });
    // Update mode popup when mode changes - use A or B based on B SET state
    connect(m_radioState, &RadioState::modeChanged, this, [this](RadioState::Mode mode) {
        if (!m_radioState->bSetEnabled()) {
            m_modePopup->setCurrentMode(static_cast<int>(mode));
        }
    });
    connect(m_radioState, &RadioState::modeBChanged, this, [this](RadioState::Mode mode) {
        if (m_radioState->bSetEnabled()) {
            m_modePopup->setCurrentMode(static_cast<int>(mode));
        }
    });
    connect(m_radioState, &RadioState::dataSubModeChanged, this, [this](int subMode) {
        if (!m_radioState->bSetEnabled()) {
            m_modePopup->setCurrentDataSubMode(subMode);
        }
    });
    connect(m_radioState, &RadioState::dataSubModeBChanged, this, [this](int subMode) {
        if (m_radioState->bSetEnabled()) {
            m_modePopup->setCurrentDataSubMode(subMode);
        }
    });
    // Update B SET state for mode popup - also refresh mode/submode/frequency display
    connect(m_radioState, &RadioState::bSetChanged, this, [this](bool enabled) {
        m_modePopup->setBSetEnabled(enabled);
        // Update displayed mode and frequency to match the new target VFO
        if (enabled) {
            m_modePopup->setFrequency(m_radioState->vfoB());
            m_modePopup->setCurrentMode(static_cast<int>(m_radioState->modeB()));
            m_modePopup->setCurrentDataSubMode(m_radioState->dataSubModeB());
        } else {
            m_modePopup->setFrequency(m_radioState->vfoA());
            m_modePopup->setCurrentMode(static_cast<int>(m_radioState->mode()));
            m_modePopup->setCurrentDataSubMode(m_radioState->dataSubMode());
        }
    });

    // B SET indicator visibility and side panel indicator color
    connect(m_radioState, &RadioState::bSetChanged, this, [this](bool enabled) {
        qCDebug(qk4Main) << "B SET changed:" << enabled;
        // Show/hide B SET indicator (hide SPLIT when B SET active)
        m_bSetLabel->setVisible(enabled);
        m_splitLabel->setVisible(!enabled);

        // Change side panel BW/SHFT indicator color (cyan=MainRx, green=SubRx)
        m_sideControlPanel->setActiveReceiver(enabled);

        // Switch RIT/XIT display to match active VFO
        if (enabled) {
            onRitXitChanged(m_radioState->ritEnabledB(), m_radioState->xitEnabled(), m_radioState->ritXitOffsetB());
        } else {
            onRitXitChanged(m_radioState->ritEnabled(), m_radioState->xitEnabled(), m_radioState->ritXitOffset());
        }
    });

    // Bottom Menu Bar
    m_bottomMenuBar = new BottomMenuBar(centralWidget);
    mainLayout->addWidget(m_bottomMenuBar);

    // Connect side control panel icon buttons
    connect(m_sideControlPanel, &SideControlPanel::connectClicked, this, &MainWindow::showRadioManager);
    connect(m_sideControlPanel, &SideControlPanel::helpClicked, this, []() {
        // TODO: Show help dialog
    });

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

    // Connect side control panel scroll signals to CAT commands
    // After sending CAT, update RadioState optimistically (radio doesn't echo these commands)
    // Group 1: WPM/PTCH (CW mode) and MIC/CMP (Voice mode)
    connect(m_sideControlPanel, &SideControlPanel::wpmChanged, this, [this](int delta) {
        int newWpm = qBound(8, m_radioState->keyerSpeed() + delta, 50);
        m_connectionController->sendCAT(QString("KS%1;").arg(newWpm, 3, 10, QChar('0')));
        m_radioState->setKeyerSpeed(newWpm);
    });
    connect(m_sideControlPanel, &SideControlPanel::pitchChanged, this, [this](int delta) {
        int currentPitch = m_radioState->cwPitch(); // In Hz
        int newPitch = qBound(300, currentPitch + (delta * 10), 990);
        m_connectionController->sendCAT(QString("CW%1;").arg(newPitch / 10, 2, 10, QChar('0')));
        m_radioState->setCwPitch(newPitch);
    });
    connect(m_sideControlPanel, &SideControlPanel::micGainChanged, this, [this](int delta) {
        int newGain = qBound(0, m_radioState->micGain() + delta, 80);
        m_connectionController->sendCAT(QString("MG%1;").arg(newGain, 3, 10, QChar('0')));
        m_radioState->setMicGain(newGain);
    });
    connect(m_sideControlPanel, &SideControlPanel::compressionChanged, this, [this](int delta) {
        int newComp = qBound(0, m_radioState->compression() + delta, 30);
        m_connectionController->sendCAT(QString("CP%1;").arg(newComp, 3, 10, QChar('0')));
        m_radioState->setCompression(newComp);
    });
    // Group 1: PWR/DLY
    // PC command uses PCnnnr; format: L=QRP (0.1-10W), H=QRO (11-110W)
    // QRP (≤10W): 0.1W increments, e.g., 10.0, 9.9, 9.8, ... 0.1
    // QRO (>10W): 1W increments, e.g., 11, 12, 13, ... 110
    connect(m_sideControlPanel, &SideControlPanel::powerChanged, this, [this](int delta) {
        double currentPower = m_radioState->rfPower();
        double newPower;

        if (currentPower <= 10.0) {
            // Currently in QRP range: 0.1W increments
            newPower = currentPower + (delta * 0.1);
            if (newPower > 10.0) {
                // Transition to QRO at 11W
                newPower = 11.0;
                int powerVal = static_cast<int>(newPower);
                m_connectionController->sendCAT(QString("PC%1H;").arg(powerVal, 3, 10, QChar('0')));
            } else {
                newPower = qBound(0.1, newPower, 10.0);
                int powerVal = static_cast<int>(qRound(newPower * 10)); // 9.9W = 099
                m_connectionController->sendCAT(QString("PC%1L;").arg(powerVal, 3, 10, QChar('0')));
            }
        } else {
            // Currently in QRO range: 1W increments
            newPower = currentPower + delta;
            if (newPower <= 10.0) {
                // Transition to QRP at 10.0W
                newPower = 10.0;
                int powerVal = static_cast<int>(qRound(newPower * 10)); // 10.0W = 100
                m_connectionController->sendCAT(QString("PC%1L;").arg(powerVal, 3, 10, QChar('0')));
            } else {
                newPower = qBound(11.0, newPower, 110.0);
                int powerVal = static_cast<int>(newPower);
                m_connectionController->sendCAT(QString("PC%1H;").arg(powerVal, 3, 10, QChar('0')));
            }
        }
        m_radioState->setRfPower(newPower);
    });
    connect(m_sideControlPanel, &SideControlPanel::delayChanged, this, [this](int delta) {
        int currentDelay = m_radioState->delayForCurrentMode();
        if (currentDelay < 0)
            currentDelay = 0;                                // Handle uninitialized
        int newDelay = qBound(0, currentDelay + delta, 255); // 0-255 = 0.00 to 2.55 seconds

        // Optimistic update - update local state immediately
        m_radioState->setDelayForCurrentMode(newDelay);

        // SD command format: SDxyzzz where x=QSK flag, y=mode (C/V/D), zzz=delay in 10ms
        // Determine mode character based on current operating mode
        QChar modeChar = 'V'; // Default to Voice
        RadioState::Mode mode = m_radioState->mode();
        if (mode == RadioState::CW || mode == RadioState::CW_R) {
            modeChar = 'C';
        } else if (mode == RadioState::DATA || mode == RadioState::DATA_R) {
            modeChar = 'D';
        }
        // x=0 means use specified delay (not full QSK)
        m_connectionController->sendCAT(QString("SD0%1%2;").arg(modeChar).arg(newDelay, 3, 10, QChar('0')));
    });
    // Group 2: BW/HI and SHFT/LO
    // BW command uses 10Hz units (divide by 10)
    connect(m_sideControlPanel, &SideControlPanel::bandwidthChanged, this, [this](int delta) {
        bool bSet = m_radioState->bSetEnabled();
        int currentBw = bSet ? m_radioState->filterBandwidthB() : m_radioState->filterBandwidth();

        // Mode-specific BW limits (Hz)
        int bwMin = 50, bwMax = 5000;
        RadioState::Mode mode = m_radioState->mode();
        if (mode == RadioState::DATA || mode == RadioState::DATA_R) {
            int subMode = bSet ? m_radioState->dataSubModeB() : m_radioState->dataSubMode();
            if (subMode == 2) { // FSK-D
                bwMin = 150;
                bwMax = 800;
            } else if (subMode == 3) { // PSK-D
                bwMax = 200;
            }
        }

        int newBw = qBound(bwMin, currentBw + (delta * 50), bwMax);
        QString cmd = bSet ? "BW$" : "BW";
        m_connectionController->sendCAT(QString("%1%2;").arg(cmd).arg(newBw / 10, 4, 10, QChar('0')));
        if (bSet) {
            m_radioState->setFilterBandwidthB(newBw);
        } else {
            m_radioState->setFilterBandwidth(newBw);
        }
    });
    connect(m_sideControlPanel, &SideControlPanel::highCutChanged, this, [this](int delta) {
        // HI adjusts upper filter edge while keeping LO fixed.
        // Both BW and IS must change. Work in decahertz (dah) to avoid rounding drift.
        // Step is 2 dah (20Hz) per scroll tick — even so IS stays on-grid.
        bool bSet = m_radioState->bSetEnabled();
        RadioState::Mode mode = m_radioState->mode();
        int bwDah = (bSet ? m_radioState->filterBandwidthB() : m_radioState->filterBandwidth()) / 10;
        int isDah = bSet ? m_radioState->ifShiftB() : m_radioState->ifShift();

        // Mode-specific BW limits (dah) and IS-locked flag
        int bwMinDah = 5, bwMaxDah = 500;
        bool isLocked = false;
        if (mode == RadioState::DATA || mode == RadioState::DATA_R) {
            int subMode = bSet ? m_radioState->dataSubModeB() : m_radioState->dataSubMode();
            if (subMode == 2) { // FSK-D: BW 150-800Hz, IS locked
                bwMinDah = 15;
                bwMaxDah = 80;
                isLocked = true;
            } else if (subMode == 3) { // PSK-D: BW 50-200Hz, IS locked
                bwMaxDah = 20;
                isLocked = true;
            }
        }

        // Compute displayed (clamped) HI/LO
        int loDah = qMax(0, isDah - bwDah / 2);
        int hiDah = loDah + bwDah;

        int newHiDah = hiDah + (delta * 2);
        if (newHiDah <= loDah)
            return;

        int newBwDah = qBound(bwMinDah, newHiDah - loDah, bwMaxDah);

        QString bwCmd = bSet ? "BW$" : "BW";
        m_connectionController->sendCAT(QString("%1%2;").arg(bwCmd).arg(newBwDah, 4, 10, QChar('0')));

        if (isLocked) {
            // IS stays fixed — only BW changes
            if (bSet)
                m_radioState->setFilterBandwidthB(newBwDah * 10);
            else
                m_radioState->setFilterBandwidth(newBwDah * 10);
        } else {
            int newIsDah =
                qBound(30, (newHiDah + loDah) / 2, (mode == RadioState::CW || mode == RadioState::CW_R) ? 200 : 300);
            QString isPrefix = bSet ? "IS$" : "IS";
            m_connectionController->sendCAT(QString("%1+%2;").arg(isPrefix).arg(newIsDah, 4, 10, QChar('0')));

            if (bSet) {
                m_radioState->setFilterBandwidthB(newBwDah * 10);
                m_radioState->setIfShiftB(newIsDah);
            } else {
                m_radioState->setFilterBandwidth(newBwDah * 10);
                m_radioState->setIfShift(newIsDah);
            }
        }
    });
    connect(m_sideControlPanel, &SideControlPanel::shiftChanged, this, [this](int delta) {
        bool bSet = m_radioState->bSetEnabled();
        RadioState::Mode mode = m_radioState->mode();

        // IS is locked in certain modes — ignore scroll
        if (mode == RadioState::AM || mode == RadioState::FM)
            return;
        if (mode == RadioState::DATA || mode == RadioState::DATA_R) {
            int subMode = bSet ? m_radioState->dataSubModeB() : m_radioState->dataSubMode();
            if (subMode == 2 || subMode == 3)
                return; // FSK-D, PSK-D: IS locked
        }

        int currentShift = bSet ? m_radioState->ifShiftB() : m_radioState->ifShift();
        int isMax = (mode == RadioState::CW || mode == RadioState::CW_R) ? 200 : 300;
        int newShift = qBound(30, currentShift + delta, isMax);
        QString prefix = bSet ? "IS$" : "IS";
        m_connectionController->sendCAT(QString("%1+%2;").arg(prefix).arg(newShift, 4, 10, QChar('0')));
        if (bSet) {
            m_radioState->setIfShiftB(newShift);
        } else {
            m_radioState->setIfShift(newShift);
        }
    });
    connect(m_sideControlPanel, &SideControlPanel::lowCutChanged, this, [this](int delta) {
        // LO adjusts lower filter edge while keeping HI fixed.
        // Both BW and IS must change. Work in decahertz (dah) to avoid rounding drift.
        // Step is 2 dah (20Hz) per scroll tick — even so IS stays on-grid.
        bool bSet = m_radioState->bSetEnabled();
        RadioState::Mode mode = m_radioState->mode();
        int bwDah = (bSet ? m_radioState->filterBandwidthB() : m_radioState->filterBandwidth()) / 10;
        int isDah = bSet ? m_radioState->ifShiftB() : m_radioState->ifShift();

        // Mode-specific BW limits (dah) and IS-locked flag
        int bwMinDah = 5, bwMaxDah = 500;
        bool isLocked = false;
        if (mode == RadioState::DATA || mode == RadioState::DATA_R) {
            int subMode = bSet ? m_radioState->dataSubModeB() : m_radioState->dataSubMode();
            if (subMode == 2) { // FSK-D: BW 150-800Hz, IS locked
                bwMinDah = 15;
                bwMaxDah = 80;
                isLocked = true;
            } else if (subMode == 3) { // PSK-D: BW 50-200Hz, IS locked
                bwMaxDah = 20;
                isLocked = true;
            }
        }

        // Compute displayed (clamped) HI/LO
        int loDah = qMax(0, isDah - bwDah / 2);
        int hiDah = loDah + bwDah;

        int newLoDah = loDah + (delta * 2);
        if (newLoDah >= hiDah)
            return;

        int newBwDah = qBound(bwMinDah, hiDah - newLoDah, bwMaxDah);

        QString bwCmd = bSet ? "BW$" : "BW";
        m_connectionController->sendCAT(QString("%1%2;").arg(bwCmd).arg(newBwDah, 4, 10, QChar('0')));

        if (isLocked) {
            // IS stays fixed — only BW changes
            if (bSet)
                m_radioState->setFilterBandwidthB(newBwDah * 10);
            else
                m_radioState->setFilterBandwidth(newBwDah * 10);
        } else {
            int newIsDah =
                qBound(30, (hiDah + newLoDah) / 2, (mode == RadioState::CW || mode == RadioState::CW_R) ? 200 : 300);
            QString isPrefix = bSet ? "IS$" : "IS";
            m_connectionController->sendCAT(QString("%1+%2;").arg(isPrefix).arg(newIsDah, 4, 10, QChar('0')));

            if (bSet) {
                m_radioState->setFilterBandwidthB(newBwDah * 10);
                m_radioState->setIfShiftB(newIsDah);
            } else {
                m_radioState->setFilterBandwidth(newBwDah * 10);
                m_radioState->setIfShift(newIsDah);
            }
        }
    });
    // Group 3: M.RF/M.SQL and S.RF/S.SQL
    // RF Gain uses RG-nn; format where nn is 00-60 (representing -0 to -60 dB attenuation)
    // Scroll up = less attenuation = decrease value, scroll down = more attenuation = increase value
    connect(m_sideControlPanel, &SideControlPanel::mainRfGainChanged, this, [this](int delta) {
        int newGain = qBound(0, m_radioState->rfGain() - delta, 60);
        m_connectionController->sendCAT(QString("RG-%1;").arg(newGain, 2, 10, QChar('0')));
        m_radioState->setRfGain(newGain);
    });
    connect(m_sideControlPanel, &SideControlPanel::mainSquelchChanged, this, [this](int delta) {
        int newSql = qBound(0, m_radioState->squelchLevel() + delta, 29);
        m_connectionController->sendCAT(QString("SQ%1;").arg(newSql, 3, 10, QChar('0')));
        m_radioState->setSquelchLevel(newSql);
    });
    connect(m_sideControlPanel, &SideControlPanel::subRfGainChanged, this, [this](int delta) {
        int newGain = qBound(0, m_radioState->rfGainB() - delta, 60);
        m_connectionController->sendCAT(QString("RG$-%1;").arg(newGain, 2, 10, QChar('0')));
        m_radioState->setRfGainB(newGain);
    });
    connect(m_sideControlPanel, &SideControlPanel::subSquelchChanged, this, [this](int delta) {
        int newSql = qBound(0, m_radioState->squelchLevelB() + delta, 29);
        m_connectionController->sendCAT(QString("SQ$%1;").arg(newSql, 3, 10, QChar('0')));
        m_radioState->setSquelchLevelB(newSql);
    });

    // Connect TX function button signals to CAT commands
    connect(m_sideControlPanel, &SideControlPanel::tuneClicked, this,
            [this]() { m_connectionController->sendCAT("SW16;"); });
    connect(m_sideControlPanel, &SideControlPanel::tuneLpClicked, this,
            [this]() { m_connectionController->sendCAT("SW131;"); });
    connect(m_sideControlPanel, &SideControlPanel::xmitClicked, this,
            [this]() { m_connectionController->sendCAT("SW30;"); });
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

    // Connect right side panel button signals to CAT commands
    // Primary (left-click) signals
    connect(m_rightSidePanel, &RightSidePanel::preClicked, this,
            [this]() { m_connectionController->sendCAT("SW61;"); });
    connect(m_rightSidePanel, &RightSidePanel::nbClicked, this, [this]() { m_connectionController->sendCAT("SW32;"); });
    connect(m_rightSidePanel, &RightSidePanel::nrClicked, this, [this]() { m_connectionController->sendCAT("SW62;"); });
    connect(m_rightSidePanel, &RightSidePanel::ntchClicked, this,
            [this]() { m_connectionController->sendCAT("SW31;"); });
    connect(m_rightSidePanel, &RightSidePanel::filClicked, this,
            [this]() { m_connectionController->sendCAT("SW33;"); });
    connect(m_rightSidePanel, &RightSidePanel::abClicked, this, [this]() { m_connectionController->sendCAT("SW41;"); });
    connect(m_rightSidePanel, &RightSidePanel::revPressed, this,
            [this]() { m_connectionController->sendCAT("SW160;"); });
    connect(m_rightSidePanel, &RightSidePanel::revReleased, this,
            [this]() { m_connectionController->sendCAT("SW161;"); });
    connect(m_rightSidePanel, &RightSidePanel::atobClicked, this,
            [this]() { m_connectionController->sendCAT("SW72;"); });
    connect(m_rightSidePanel, &RightSidePanel::spotClicked, this,
            [this]() { m_connectionController->sendCAT("SW42;"); });
    connect(m_rightSidePanel, &RightSidePanel::modeClicked, this, [this]() {
        // Toggle mode popup - if open, close it; otherwise show it
        if (m_modePopup->isVisible()) {
            m_modePopup->hidePopup();
        } else {
            // Update current state before showing - use A or B based on B SET
            bool bSet = m_radioState->bSetEnabled();
            if (bSet) {
                m_modePopup->setFrequency(m_radioState->vfoB());
                m_modePopup->setCurrentMode(static_cast<int>(m_radioState->modeB()));
                m_modePopup->setCurrentDataSubMode(m_radioState->dataSubModeB());
            } else {
                m_modePopup->setFrequency(m_radioState->vfoA());
                m_modePopup->setCurrentMode(static_cast<int>(m_radioState->mode()));
                m_modePopup->setCurrentDataSubMode(m_radioState->dataSubMode());
            }
            m_modePopup->setBSetEnabled(bSet);
            m_modePopup->showAboveWidget(m_bottomMenuBar);
        }
    });

    // Secondary (right-click) signals - these show feature menus with toggle behavior
    // If same menu is open, close it; otherwise switch to the new menu
    auto toggleFeatureMenu = [this](FeatureMenuBar::Feature feature) {
        if (m_featureMenuBar->isMenuVisible() && m_featureMenuBar->currentFeature() == feature) {
            m_featureMenuBar->hideMenu();
        } else {
            // Populate initial state from RadioState (use Sub RX state if B SET enabled)
            bool bSet = m_radioState->bSetEnabled();
            switch (feature) {
            case FeatureMenuBar::Attenuator:
                if (bSet) {
                    m_featureMenuBar->setFeatureEnabled(m_radioState->attenuatorEnabledB());
                    m_featureMenuBar->setValue(m_radioState->attenuatorLevelB());
                } else {
                    m_featureMenuBar->setFeatureEnabled(m_radioState->attenuatorEnabled());
                    m_featureMenuBar->setValue(m_radioState->attenuatorLevel());
                }
                break;
            case FeatureMenuBar::NbLevel:
                if (bSet) {
                    m_featureMenuBar->setFeatureEnabled(m_radioState->noiseBlankerEnabledB());
                    m_featureMenuBar->setValue(m_radioState->noiseBlankerLevelB());
                    m_featureMenuBar->setNbFilter(m_radioState->noiseBlankerFilterWidthB());
                } else {
                    m_featureMenuBar->setFeatureEnabled(m_radioState->noiseBlankerEnabled());
                    m_featureMenuBar->setValue(m_radioState->noiseBlankerLevel());
                    m_featureMenuBar->setNbFilter(m_radioState->noiseBlankerFilterWidth());
                }
                break;
            case FeatureMenuBar::NrAdjust:
                if (bSet) {
                    m_featureMenuBar->setFeatureEnabled(m_radioState->noiseReductionEnabledB());
                    m_featureMenuBar->setValue(m_radioState->noiseReductionLevelB());
                } else {
                    m_featureMenuBar->setFeatureEnabled(m_radioState->noiseReductionEnabled());
                    m_featureMenuBar->setValue(m_radioState->noiseReductionLevel());
                }
                break;
            case FeatureMenuBar::ManualNotch:
                // Use correct VFO's notch state
                if (bSet) {
                    m_featureMenuBar->setFeatureEnabled(m_radioState->manualNotchEnabledB());
                    m_featureMenuBar->setValue(m_radioState->manualNotchPitchB());
                } else {
                    m_featureMenuBar->setFeatureEnabled(m_radioState->manualNotchEnabled());
                    m_featureMenuBar->setValue(m_radioState->manualNotchPitch());
                }
                break;
            }
            // Show popup positioned above the bottom menu bar (like other popups)
            m_featureMenuBar->showForFeature(feature);
            m_featureMenuBar->showAboveWidget(m_bottomMenuBar);
        }
    };
    connect(m_rightSidePanel, &RightSidePanel::attnClicked, this,
            [=]() { toggleFeatureMenu(FeatureMenuBar::Attenuator); });
    connect(m_rightSidePanel, &RightSidePanel::levelClicked, this,
            [=]() { toggleFeatureMenu(FeatureMenuBar::NbLevel); });
    connect(m_rightSidePanel, &RightSidePanel::adjClicked, this,
            [=]() { toggleFeatureMenu(FeatureMenuBar::NrAdjust); });
    connect(m_rightSidePanel, &RightSidePanel::manualClicked, this,
            [=]() { toggleFeatureMenu(FeatureMenuBar::ManualNotch); });
    connect(m_rightSidePanel, &RightSidePanel::apfClicked, this, [this]() {
        // Toggle APF on/off for Main RX or Sub RX based on B SET state
        if (m_radioState->bSetEnabled()) {
            m_connectionController->sendCAT("AP$/;"); // Sub RX toggle
        } else {
            m_connectionController->sendCAT("AP/;"); // Main RX toggle
        }
    });
    connect(m_rightSidePanel, &RightSidePanel::splitClicked, this,
            [this]() { m_connectionController->sendCAT("SW145;"); });
    connect(m_rightSidePanel, &RightSidePanel::btoaClicked, this,
            [this]() { m_connectionController->sendCAT("SW147;"); });
    connect(m_rightSidePanel, &RightSidePanel::autoClicked, this,
            [this]() { m_connectionController->sendCAT("SW146;"); });
    // altClicked (MODE/ALT right-click) - send SW148 for ALT function
    connect(m_rightSidePanel, &RightSidePanel::altClicked, this,
            [this]() { m_connectionController->sendCAT("SW148;"); });

    // PF row primary (left-click) signals
    connect(m_rightSidePanel, &RightSidePanel::bsetClicked, this,
            [this]() { m_connectionController->sendCAT("SW44;"); });
    connect(m_rightSidePanel, &RightSidePanel::clrClicked, this,
            [this]() { m_connectionController->sendCAT("SW64;"); });
    connect(m_rightSidePanel, &RightSidePanel::ritClicked, this,
            [this]() { m_connectionController->sendCAT("SW54;"); });
    connect(m_rightSidePanel, &RightSidePanel::xitClicked, this,
            [this]() { m_connectionController->sendCAT("SW74;"); });

    // PF row secondary (right-click) signals
    // PF1-PF4 execute user-configured macros (or default K4 PF functions if no macro set)
    connect(m_rightSidePanel, &RightSidePanel::pf1Clicked, this, [this]() {
        MacroEntry macro = RadioSettings::instance()->macro(MacroIds::PF1);
        if (!macro.command.isEmpty()) {
            executeMacro(MacroIds::PF1);
        } else {
            m_connectionController->sendCAT("SW153;"); // Default: K4 PF1
        }
    });
    connect(m_rightSidePanel, &RightSidePanel::pf2Clicked, this, [this]() {
        MacroEntry macro = RadioSettings::instance()->macro(MacroIds::PF2);
        if (!macro.command.isEmpty()) {
            executeMacro(MacroIds::PF2);
        } else {
            m_connectionController->sendCAT("SW154;"); // Default: K4 PF2
        }
    });
    connect(m_rightSidePanel, &RightSidePanel::pf3Clicked, this, [this]() {
        MacroEntry macro = RadioSettings::instance()->macro(MacroIds::PF3);
        if (!macro.command.isEmpty()) {
            executeMacro(MacroIds::PF3);
        } else {
            m_connectionController->sendCAT("SW155;"); // Default: K4 PF3
        }
    });
    connect(m_rightSidePanel, &RightSidePanel::pf4Clicked, this, [this]() {
        MacroEntry macro = RadioSettings::instance()->macro(MacroIds::PF4);
        if (!macro.command.isEmpty()) {
            executeMacro(MacroIds::PF4);
        } else {
            m_connectionController->sendCAT("SW156;"); // Default: K4 PF4
        }
    });

    // Bottom row signals (SUB, DIVERSITY, RATE, LOCK)
    connect(m_rightSidePanel, &RightSidePanel::subClicked, this,
            [this]() { m_connectionController->sendCAT("SW83;"); });
    connect(m_rightSidePanel, &RightSidePanel::diversityClicked, this,
            [this]() { m_connectionController->sendCAT("SW152;"); });
    connect(m_rightSidePanel, &RightSidePanel::rateClicked, this, [this]() {
        // Cycle fine rates: 1 Hz → 10 Hz → 100 Hz → 1 Hz
        // B-SET aware: targets VFO B (VT$) when B SET is engaged
        bool bSet = m_radioState->bSetEnabled();
        int current = bSet ? m_radioState->tuningStepB() : m_radioState->tuningStep();
        int next = (current >= 0 && current < 2) ? current + 1 : 0;
        QString cmd = QString("%1%2;").arg(bSet ? "VT$" : "VT").arg(next);
        m_connectionController->sendCAT(cmd);
        m_radioState->parseCATCommand(cmd);
    });
    connect(m_rightSidePanel, &RightSidePanel::khzClicked, this, [this]() {
        // Set tuning step to 1 kHz (VT3)
        // B-SET aware: targets VFO B (VT$) when B SET is engaged
        bool bSet = m_radioState->bSetEnabled();
        QString cmd = bSet ? QStringLiteral("VT$3;") : QStringLiteral("VT3;");
        m_connectionController->sendCAT(cmd);
        m_radioState->parseCATCommand(cmd);
    });
    connect(m_rightSidePanel, &RightSidePanel::lockAClicked, this,
            [this]() { m_connectionController->sendCAT("SW63;"); }); // Toggle Lock A
    connect(m_rightSidePanel, &RightSidePanel::lockBClicked, this,
            [this]() { m_connectionController->sendCAT("SW151;"); }); // Toggle Lock B

    // Connect memory buttons (M1-M4, REC, STORE, RCL)
    // Primary actions (left click)
    connect(m_m1Btn, &QPushButton::clicked, this, [this]() { m_connectionController->sendCAT("SW17;"); });
    connect(m_m2Btn, &QPushButton::clicked, this, [this]() { m_connectionController->sendCAT("SW51;"); });
    connect(m_m3Btn, &QPushButton::clicked, this, [this]() { m_connectionController->sendCAT("SW18;"); });
    connect(m_m4Btn, &QPushButton::clicked, this, [this]() { m_connectionController->sendCAT("SW52;"); });
    connect(m_recBtn, &QPushButton::clicked, this, [this]() { m_connectionController->sendCAT("SW19;"); });
    connect(m_storeBtn, &QPushButton::clicked, this, [this]() { m_connectionController->sendCAT("SW20;"); });
    connect(m_rclBtn, &QPushButton::clicked, this, [this]() { m_connectionController->sendCAT("SW34;"); });

    // Install event filters for right-click (alternate actions)
    m_recBtn->installEventFilter(this);
    m_storeBtn->installEventFilter(this);
    m_rclBtn->installEventFilter(this);

    // Connect bottom menu bar signals
    connect(m_bottomMenuBar, &BottomMenuBar::menuClicked, this, &MainWindow::showMenuOverlay);
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

    // Note: audio buffer flushing on mode/filter changes was removed — AudioEngine now runs
    // on a dedicated thread with a properly sized jitter buffer, so stale audio lag no longer
    // occurs. Flushing would cause a brief audio dropout on every mode/filter switch.
}

void MainWindow::setupTopStatusBar(QWidget *parent) {
    auto *statusBar = new QWidget(parent);
    statusBar->setFixedHeight(K4Styles::Dimensions::ButtonHeightSmall);
    statusBar->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::DarkBackground));

    auto *layout = new QHBoxLayout(statusBar);
    layout->setContentsMargins(8, 2, 8, 2);
    layout->setSpacing(20);

    // Elecraft K4 title
    m_titleLabel = new QLabel("Elecraft K4", statusBar);
    m_titleLabel->setStyleSheet(
        QString("color: %1; font-weight: bold; font-size: 14px;").arg(K4Styles::Colors::TextWhite));
    layout->addWidget(m_titleLabel);

    // Date/Time
    m_dateTimeLabel = new QLabel("--/-- --:--:-- Z", statusBar);
    m_dateTimeLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(K4Styles::Colors::TextGray));
    layout->addWidget(m_dateTimeLabel);

    layout->addStretch();

    // Power
    m_powerLabel = new QLabel("--- W", statusBar);
    m_powerLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(K4Styles::Colors::AccentAmber));
    layout->addWidget(m_powerLabel);

    // SWR
    m_swrLabel = new QLabel("-.-:1", statusBar);
    m_swrLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(K4Styles::Colors::AccentAmber));
    layout->addWidget(m_swrLabel);

    // Voltage
    m_voltageLabel = new QLabel("--.- V", statusBar);
    m_voltageLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(K4Styles::Colors::AccentAmber));
    layout->addWidget(m_voltageLabel);

    // Current
    m_currentLabel = new QLabel("-.- A", statusBar);
    m_currentLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(K4Styles::Colors::AccentAmber));
    layout->addWidget(m_currentLabel);

    layout->addStretch();

    // KPA1500 status (to left of K4 status)
    m_kpa1500StatusLabel = new QLabel("", statusBar);
    m_kpa1500StatusLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(K4Styles::Colors::InactiveGray));
    m_kpa1500StatusLabel->hide(); // Hidden when not enabled
    layout->addWidget(m_kpa1500StatusLabel);

    // Network health bar
    // K4 Connection status
    m_connectionStatusLabel = new QLabel("K4", statusBar);
    m_connectionStatusLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(K4Styles::Colors::InactiveGray));
    layout->addWidget(m_connectionStatusLabel);

    // Network health signal bars
    m_netHealthWidget = new NetHealthWidget(m_connectionController->networkMetrics(), statusBar);
    layout->addWidget(m_netHealthWidget);
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
            QString cmd = QString("FA%1;").arg(static_cast<quint64>(newFreq));
            m_connectionController->sendCAT(cmd);
            m_radioState->parseCATCommand(cmd);
        }
    });

    // Set Mini-Pan A passband color to cyan (matching VFO A theme)
    QColor vfoAPassband(K4Styles::Colors::VfoACyan);
    vfoAPassband.setAlpha(64);
    m_vfoA->setMiniPanPassbandColor(vfoAPassband);

    layout->addWidget(m_vfoA, 1, Qt::AlignTop);

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

    // SPLIT, B SET, and MSG Bank labels live in VfoRowWidget (positioned under TX)
    m_splitLabel = m_vfoRow->splitLabel();
    m_bSetLabel = m_vfoRow->bSetLabel();
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
    m_ritLabel->setStyleSheet(QString("color: %1; font-size: 10px; border: none;").arg(K4Styles::Colors::InactiveGray));
    m_ritLabel->setCursor(Qt::PointingHandCursor);
    m_ritLabel->installEventFilter(this);
    ritXitLabelsRow->addWidget(m_ritLabel);

    m_xitLabel = new QLabel("XIT", m_ritXitBox);
    m_xitLabel->setStyleSheet(QString("color: %1; font-size: 10px; border: none;").arg(K4Styles::Colors::InactiveGray));
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

    // VFO A filter indicator (cyan #00BFFF to match VFO A square/slider)
    m_filterAWidget = new FilterIndicatorWidget(centerWidget);
    m_filterAWidget->setShapeColor(QColor(0x00, 0xBF, 0xFF), QColor(0x00, 0xBF, 0xFF)); // Cyan solid

    // VFO B filter indicator (green #00FF00 to match VFO B square/slider)
    m_filterBWidget = new FilterIndicatorWidget(centerWidget);
    m_filterBWidget->setShapeColor(QColor(0x00, 0xFF, 0x00), QColor(0x00, 0xFF, 0x00)); // Green solid

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
    m_voxLabel->setStyleSheet(
        QString("color: %1; font-size: 11px; font-weight: bold;").arg(K4Styles::Colors::TextGray));
    indicatorLayout->addWidget(m_voxLabel);

    // ATU indicator (orange when AUTO, grey when off)
    m_atuLabel = new QLabel("ATU", indicatorContainer);
    m_atuLabel->setAlignment(Qt::AlignCenter);
    m_atuLabel->setStyleSheet(
        QString("color: %1; font-size: 11px; font-weight: bold;").arg(K4Styles::Colors::TextGray));
    indicatorLayout->addWidget(m_atuLabel);

    // QSK indicator - white when on, grey when off
    m_qskLabel = new QLabel("QSK", indicatorContainer);
    m_qskLabel->setAlignment(Qt::AlignCenter);
    m_qskLabel->setStyleSheet(
        QString("color: %1; font-size: 11px; font-weight: bold;").arg(K4Styles::Colors::TextGray));
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

    m_m1Btn = createSimpleButton("M1");
    m1m4Row->addWidget(m_m1Btn);

    m_m2Btn = createSimpleButton("M2");
    m1m4Row->addWidget(m_m2Btn);

    m_m3Btn = createSimpleButton("M3");
    m1m4Row->addWidget(m_m3Btn);

    m_m4Btn = createSimpleButton("M4");
    m1m4Row->addWidget(m_m4Btn);

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
    m_recBtn = recContainer->findChild<QPushButton *>();
    memoryRow->addWidget(recContainer);

    // STORE (lighter grey, AF REC sub-label)
    auto *storeContainer = createMemoryButton("STORE", "AF REC", true);
    m_storeBtn = storeContainer->findChild<QPushButton *>();
    memoryRow->addWidget(storeContainer);

    // RCL (lighter grey, AF PLAY sub-label)
    auto *rclContainer = createMemoryButton("RCL", "AF PLAY", true);
    m_rclBtn = rclContainer->findChild<QPushButton *>();
    memoryRow->addWidget(rclContainer);

    memoryRow->addStretch();
    centerLayout->addLayout(memoryRow);

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
            QString cmd = QString("FB%1;").arg(static_cast<quint64>(newFreq));
            m_connectionController->sendCAT(cmd);
            m_radioState->parseCATCommand(cmd);
        }
    });

    layout->addWidget(m_vfoB, 1, Qt::AlignTop);

    // Add the VFO row to main layout
    mainVLayout->addWidget(vfoRowWidget);

    // NOTE: TX meters are now integrated into VFOWidgets as multifunction S/Po meters
    // (see VFOWidget::m_txMeter - displays S-meter when RX, Po when TX)

    // ===== Antenna Row (below VFO section) =====
    // Layout: [RX Ant A (left)] --- [TX Antenna (center)] --- [RX Ant B (right)]
    auto *antennaRow = new QHBoxLayout();
    antennaRow->setContentsMargins(8, 0, 8, 0);
    antennaRow->setSpacing(0);

    // RX Antenna A (Main) - white color, left-justified
    m_rxAntALabel = new QLabel("1:ANT1", parent);
    m_rxAntALabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_rxAntALabel->setStyleSheet(
        QString("color: %1; font-size: 11px; font-weight: bold;").arg(K4Styles::Colors::TextWhite));
    antennaRow->addWidget(m_rxAntALabel);

    antennaRow->addStretch(1); // Push TX antenna to center

    // TX Antenna - orange color, centered
    m_txAntennaLabel = new QLabel("1:ANT1", parent);
    m_txAntennaLabel->setAlignment(Qt::AlignCenter);
    m_txAntennaLabel->setStyleSheet(
        QString("color: %1; font-size: 11px; font-weight: bold;").arg(K4Styles::Colors::AccentAmber));
    antennaRow->addWidget(m_txAntennaLabel);

    antennaRow->addStretch(1); // Push RX Ant B to right

    // RX Antenna B (Sub) - white color, right-justified
    m_rxAntBLabel = new QLabel("1:ANT1", parent);
    m_rxAntBLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_rxAntBLabel->setStyleSheet(
        QString("color: %1; font-size: 11px; font-weight: bold;").arg(K4Styles::Colors::TextWhite));
    antennaRow->addWidget(m_rxAntBLabel);

    mainVLayout->addLayout(antennaRow);
}

void MainWindow::updateDateTime() {
    QDateTime now = QDateTime::currentDateTimeUtc();
    m_dateTimeLabel->setText(now.toString("M-dd / HH:mm:ss") + " Z");
    m_sideControlPanel->setTime(now.toString("HH:mm:ss") + " Z");
}

QString MainWindow::formatFrequency(quint64 freq) {
    QString freqStr = QString::number(freq);
    while (freqStr.length() < 8) {
        freqStr.prepend('0');
    }

    // Insert dots: XX.XXX.XXX
    QString formatted;
    int len = freqStr.length();
    for (int i = 0; i < len; i++) {
        formatted.append(freqStr[i]);
        int posFromEnd = len - i - 1;
        if (posFromEnd > 0 && posFromEnd % 3 == 0) {
            formatted.append('.');
        }
    }

    // Remove leading zero for frequencies < 10 MHz (40m-160m)
    if (formatted.startsWith('0')) {
        formatted = formatted.mid(1);
    }
    return formatted;
}

void MainWindow::showRadioManager() {
    RadioManagerDialog dialog(this);
    connect(&dialog, &RadioManagerDialog::connectRequested, this, &MainWindow::connectToRadio);
    connect(&dialog, &RadioManagerDialog::disconnectRequested, this, [this]() {
        // TcpClient::disconnectFromHost() sends RRN; automatically
        m_connectionController->disconnectFromRadio();
    });

    // Set the connected host so dialog can show "Disconnect" for active connection
    if (m_connectionController->isConnected()) {
        dialog.setConnectedHost(m_connectionController->currentRadio().host);
    }

    dialog.exec();
}

void MainWindow::connectToRadio(const RadioEntry &radio) {
    m_titleLabel->setText("Elecraft K4 - " + radio.name);
    m_connectionController->connectToRadio(radio);
}

void MainWindow::onConnectionStateChanged(TcpClient::ConnectionState state) {
    updateConnectionState(state);
}

void MainWindow::onConnectionError(const QString &error) {
    m_connectionStatusLabel->setText("Error: " + error);
    m_connectionStatusLabel->setStyleSheet(
        QString("color: %1; font-size: 12px; font-weight: bold;").arg(K4Styles::Colors::TxRed));
}

void MainWindow::onRadioReady() {
    qCDebug(qk4Main) << "Successfully authenticated with K4 radio";

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
    m_connectionController->sendCAT("#FPS;");  // Query back to confirm and update menu
    m_connectionController->sendCAT("#SCL;");  // Panadapter scale - not in RDY, needed for dB range
    m_connectionController->sendCAT("SIRC1;"); // Enable 1-second client stats updates
    // Note: ML and KP commands come in RDY; dump - no need to query

    // Sync element length with K4 server (sent in RDY dump as KZLnn)
    if (m_radioState->keyerSpeed() > 0) {
        int ditMs = 1200 / m_radioState->keyerSpeed();
        m_connectionController->sendCAT(QString("KZL%1;").arg(ditMs, 2, 10, QChar('0')));
    }

    // Create synthetic "Display FPS" menu item (will update from radio echo)
    m_menuModel->addSyntheticDisplayFpsItem(15);

    // Startup macro is sent pre-RDY by TcpClient so the state dump reflects changes.

    // Connect KPA1500 if enabled and configured
    if (RadioSettings::instance()->kpa1500Enabled() && !RadioSettings::instance()->kpa1500Host().isEmpty()) {
        m_kpa1500Client->connectToHost(RadioSettings::instance()->kpa1500Host(),
                                       RadioSettings::instance()->kpa1500Port());
    }
}

void MainWindow::onAuthFailed() {
    qCDebug(qk4Main) << "Authentication failed";
    m_connectionStatusLabel->setText("Auth Failed");
    m_connectionStatusLabel->setStyleSheet(
        QString("color: %1; font-size: 12px; font-weight: bold;").arg(K4Styles::Colors::TxRed));
}

void MainWindow::onCatResponse(const QString &response) {

    // Parse CAT commands (may contain multiple commands separated by ;)
    QStringList commands = response.split(';', Qt::SkipEmptyParts);
    for (const QString &cmd : commands) {
        // PONG is handled by TcpClient for latency measurement — skip
        if (cmd.startsWith("PONG"))
            continue;

        m_radioState->parseCATCommand(cmd + ";");

        // Parse MEDF (menu definitions) from RDY response
        if (cmd.startsWith("MEDF")) {
            m_menuModel->parseMEDF(cmd + ";");
        }
        // Route ME (menu value) commands to MenuModel for real-time updates
        else if (cmd.startsWith("ME")) {
            m_menuModel->parseME(cmd + ";");
        }
        // Parse BN$ (Band Number) response for VFO B (Sub RX)
        else if (cmd.startsWith("BN$")) {
            // VFO B band number: BN$nn where nn is 00-10 or 16-25
            bool ok;
            int bandNum = cmd.mid(3, 2).toInt(&ok);
            if (ok) {
                updateBandSelectionB(bandNum);
            }
        }
        // Parse BN (Band Number) response for VFO A
        else if (cmd.startsWith("BN")) {
            // VFO A band number: BNnn where nn is 00-10 or 16-25
            bool ok;
            int bandNum = cmd.mid(2, 2).toInt(&ok);
            if (ok) {
                updateBandSelection(bandNum);
            }
        }
    }
}

void MainWindow::onFrequencyChanged(quint64 freq) {
    // When transmitting with XIT (no split): show TX frequency (dial + XIT offset)
    // When receiving with RIT: show RX frequency (dial + RIT offset)
    if (m_radioState->isTransmitting() && m_radioState->xitEnabled() && !m_radioState->splitEnabled()) {
        qint64 txFreq = static_cast<qint64>(freq) + m_radioState->ritXitOffset();
        if (txFreq > 0)
            freq = static_cast<quint64>(txFreq);
    } else if (m_radioState->ritEnabled()) {
        qint64 rxFreq = static_cast<qint64>(freq) + m_radioState->ritXitOffset();
        if (rxFreq > 0)
            freq = static_cast<quint64>(rxFreq);
    }
    m_vfoA->setFrequency(formatFrequency(freq));
}

void MainWindow::onFrequencyBChanged(quint64 freq) {
    // When transmitting with XIT (split): show TX frequency (dial + XIT offset)
    // When receiving with RIT B: show RX frequency (dial + RIT B offset)
    if (m_radioState->isTransmitting() && m_radioState->xitEnabled() && m_radioState->splitEnabled()) {
        qint64 txFreq = static_cast<qint64>(freq) + m_radioState->ritXitOffsetB();
        if (txFreq > 0)
            freq = static_cast<quint64>(txFreq);
    } else if (m_radioState->ritEnabledB()) {
        qint64 rxFreq = static_cast<qint64>(freq) + m_radioState->ritXitOffsetB();
        if (rxFreq > 0)
            freq = static_cast<quint64>(rxFreq);
    }
    m_vfoB->setFrequency(formatFrequency(freq));
}

void MainWindow::onModeChanged(RadioState::Mode mode) {
    // Use full mode string which includes data sub-mode (AFSK, FSK, PSK, DATA)
    // Also adds "+" suffix for USB/LSB when ESSB is enabled
    updateModeLabels();

    // Swap TX popup buttons 5/6 between CW keyer controls and SSB BW/ESSB
    if (m_txPopup) {
        if (mode == RadioState::CW || mode == RadioState::CW_R) {
            // CW mode: swap to paddle/iambic and keying weight buttons
            QChar iambic = m_radioState->iambicMode();
            QChar paddle = m_radioState->paddleOrientation();
            int weight = m_radioState->keyingWeight();
            if (!iambic.isNull() && !paddle.isNull()) {
                QString paddleStr = (paddle == 'R') ? "PDL REV" : "PDL NOR";
                QString iambicStr = QString("IAMB %1").arg(iambic);
                m_txPopup->setButtonLabel(5, paddleStr, iambicStr, true);
            } else {
                // KP state not yet received — show defaults
                m_txPopup->setButtonLabel(5, "PDL NOR", "IAMB A", true);
            }
            if (weight >= 90 && weight <= 125) {
                QString weightStr = QString::number(weight / 100.0, 'f', 2);
                m_txPopup->setButtonLabel(6, "WEIGHT", weightStr, false);
            } else {
                m_txPopup->setButtonLabel(6, "WEIGHT", "1.00", false);
            }
        } else {
            // Voice/data mode: restore SSB BW and ESSB buttons
            int bw = m_radioState->ssbTxBw();
            if (bw >= 24 && bw <= 45) {
                QString bwStr = QString("%1k").arg(bw / 10.0, 0, 'f', 1);
                m_txPopup->setButtonLabel(5, "SSB BW", bwStr, false);
            } else {
                m_txPopup->setButtonLabel(5, "SSB BW", "2.8k", false);
            }
            m_txPopup->setButtonLabel(6, "ESSB", m_radioState->essbEnabled() ? "ON" : "OFF", false);
        }
    }
}

void MainWindow::onModeBChanged(RadioState::Mode mode) {
    Q_UNUSED(mode)
    // Use full mode string which includes data sub-mode (AFSK, FSK, PSK, DATA)
    updateModeLabels();
}

void MainWindow::updateModeLabels() {
    // VFO A mode label
    QString modeA = m_radioState->modeStringFull();
    RadioState::Mode mode = m_radioState->mode();
    if (m_radioState->essbEnabled() && (mode == RadioState::USB || mode == RadioState::LSB)) {
        modeA += "+";
    }
    m_modeALabel->setText(modeA);

    // VFO B mode label
    QString modeB = m_radioState->modeStringFullB();
    RadioState::Mode modeVfoB = m_radioState->modeB();
    if (m_radioState->essbEnabled() && (modeVfoB == RadioState::USB || modeVfoB == RadioState::LSB)) {
        modeB += "+";
    }
    m_modeBLabel->setText(modeB);
}

void MainWindow::onSMeterChanged(double value) {
    m_vfoA->setSMeterValue(value);
}

void MainWindow::onSMeterBChanged(double value) {
    m_vfoB->setSMeterValue(value);
}

void MainWindow::updateConnectionState(TcpClient::ConnectionState state) {
    switch (state) {
    case TcpClient::Disconnected:
        m_connectionStatusLabel->setText("K4");
        m_connectionStatusLabel->setStyleSheet(
            QString("color: %1; font-size: 12px;").arg(K4Styles::Colors::InactiveGray));
        m_titleLabel->setText("Elecraft K4");
        // Stop audio engine to prevent accessing invalid data
        m_audioController->stopAudio();

        // Clear all UI state to avoid showing stale data
        // Clear spectrum displays
        m_spectrumController->panadapterA()->clear();
        m_spectrumController->panadapterB()->clear();

        // Clear mini-pan displays
        if (m_vfoA->miniPan())
            m_vfoA->miniPan()->clear();
        if (m_vfoB->miniPan())
            m_vfoB->miniPan()->clear();

        // Reset VFO displays and embedded meters
        m_vfoA->setFrequency(0);
        m_vfoA->setSMeterValue(0);
        m_vfoA->setTransmitting(false);
        m_vfoA->setTxMeters(0, 0, 0, 1.0);
        m_vfoB->setFrequency(0);
        m_vfoB->setSMeterValue(0);
        m_vfoB->setTransmitting(false);
        m_vfoB->setTxMeters(0, 0, 0, 1.0);

        // Reset model state so all change-guards fire on reconnect
        m_radioState->reset();

        // --- Reset all remaining UI to clean default state ---

        // Mode labels
        m_modeALabel->setText("");
        m_modeBLabel->setText("");

        // Antenna labels
        m_txAntennaLabel->setText("");
        m_rxAntALabel->setText("");
        m_rxAntBLabel->setText("");

        // Split
        m_splitLabel->setText("SPLIT OFF");
        m_splitLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(K4Styles::Colors::AccentAmber));

        // TX indicators (default: left triangle, amber)
        m_txTriangle->setText("◀");
        m_txTriangleB->setText("");

        // B SET
        m_bSetLabel->setVisible(false);

        // SUB/DIV (disabled state)
        m_subLabel->setStyleSheet(
            QString("background-color: %1; color: %2; font-size: 9px; font-weight: bold; border-radius: 2px;")
                .arg(K4Styles::Colors::DisabledBackground, K4Styles::Colors::LightGradientTop));
        m_divLabel->setStyleSheet(
            QString("background-color: %1; color: %2; font-size: 9px; font-weight: bold; border-radius: 2px;")
                .arg(K4Styles::Colors::DisabledBackground, K4Styles::Colors::LightGradientTop));

        // Dim VFO B (SUB off state)
        m_vfoB->frequencyDisplay()->setNormalColor(QColor(K4Styles::Colors::InactiveGray));
        m_modeBLabel->setStyleSheet(
            QString("color: %1; font-size: 11px; font-weight: bold;").arg(K4Styles::Colors::InactiveGray));

        // Message bank
        m_msgBankLabel->setText("MSG: I");
        m_msgBankLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(K4Styles::Colors::AccentAmber));

        // RIT/XIT (disabled state)
        m_ritLabel->setStyleSheet(
            QString("color: %1; font-size: 11px; font-weight: bold;").arg(K4Styles::Colors::InactiveGray));
        m_xitLabel->setStyleSheet(
            QString("color: %1; font-size: 11px; font-weight: bold;").arg(K4Styles::Colors::InactiveGray));
        m_ritXitValueLabel->setText("+0.00");
        m_ritXitValueLabel->setStyleSheet(
            QString("color: %1; font-size: 14px; font-weight: bold;").arg(K4Styles::Colors::InactiveGray));

        // ATU (grey/inactive)
        m_atuLabel->setStyleSheet(
            QString("color: %1; font-size: 11px; font-weight: bold;").arg(K4Styles::Colors::TextGray));

        // VOX / QSK (grey/inactive)
        m_voxLabel->setStyleSheet(
            QString("color: %1; font-size: 11px; font-weight: bold;").arg(K4Styles::Colors::TextGray));
        m_qskLabel->setStyleSheet(
            QString("color: %1; font-size: 11px; font-weight: bold;").arg(K4Styles::Colors::TextGray));

        // TEST (hidden)
        m_vfoRow->setTestVisible(false);

        // VFO indicators (AGC, PRE, ATT, NB, NR, Notch, APF, Tuning Rate)
        m_vfoA->setAGC("AGC");
        m_vfoA->setPreamp(false, 0);
        m_vfoA->setAtt(false, 0);
        m_vfoA->setNB(false);
        m_vfoA->setNR(false);
        m_vfoA->setNotch(false, false);
        m_vfoA->setApf(false, 0);
        m_vfoA->setTuningRate(0);

        m_vfoB->setAGC("AGC");
        m_vfoB->setPreamp(false, 0);
        m_vfoB->setAtt(false, 0);
        m_vfoB->setNB(false);
        m_vfoB->setNR(false);
        m_vfoB->setNotch(false, false);
        m_vfoB->setApf(false, 0);
        m_vfoB->setTuningRate(0);

        // VFO locks (both unlocked)
        m_vfoRow->setLockA(false);
        m_vfoRow->setLockB(false);

        // Side control panel values
        m_sideControlPanel->setBandwidth(0);
        m_sideControlPanel->setShift(0);
        m_sideControlPanel->setHighCut(0);
        m_sideControlPanel->setLowCut(0);
        m_sideControlPanel->setPower(0);
        m_sideControlPanel->setDelay(0);
        m_sideControlPanel->setWpm(0);
        m_sideControlPanel->setPitch(0);
        m_sideControlPanel->setMicGain(0);
        m_sideControlPanel->setCompression(0);
        m_sideControlPanel->setMainRfGain(0);
        m_sideControlPanel->setMainSquelch(0);
        m_sideControlPanel->setSubRfGain(0);
        m_sideControlPanel->setSubSquelch(0);

        // Status bar values
        m_powerLabel->setText("--- W");
        m_swrLabel->setText("-.-:1");
        m_voltageLabel->setText("--.- V");
        m_currentLabel->setText("-.- A");
        m_sideControlPanel->setPowerReading(0);
        m_sideControlPanel->setSwr(1.0);
        m_sideControlPanel->setVoltage(0);
        m_sideControlPanel->setCurrent(0);

        // Filter indicator widgets
        m_filterAWidget->setBandwidth(0);
        m_filterAWidget->setShift(50);
        m_filterAWidget->setFilterPosition(1);
        m_filterAWidget->setMode("");
        m_filterBWidget->setBandwidth(0);
        m_filterBWidget->setShift(50);
        m_filterBWidget->setFilterPosition(1);
        m_filterBWidget->setMode("");
        m_filterAWidget->setDataSubMode(0);
        m_filterBWidget->setDataSubMode(0);

        // VFO mini-pan overlays (reset mode/filter state)
        m_vfoA->setMiniPanMode("USB");
        m_vfoA->setMiniPanFilterBandwidth(2400);
        m_vfoA->setMiniPanIfShift(50);
        m_vfoA->setMiniPanCwPitch(600);
        m_vfoA->setMiniPanNotchFilter(false, 0);
        m_vfoB->setMiniPanMode("USB");
        m_vfoB->setMiniPanFilterBandwidth(2400);
        m_vfoB->setMiniPanIfShift(50);
        m_vfoB->setMiniPanCwPitch(600);
        m_vfoB->setMiniPanNotchFilter(false, 0);

        // Clear menu model
        m_menuModel->clear();

        // Disconnect KPA1500 when K4 disconnects
        if (m_kpa1500Client->isConnected()) {
            m_kpa1500Client->disconnectFromHost();
        }

        break;

    case TcpClient::Connecting:
        m_connectionStatusLabel->setText("K4");
        m_connectionStatusLabel->setStyleSheet(
            QString("color: %1; font-size: 12px; font-weight: bold;").arg(K4Styles::Colors::AccentAmber));
        break;

    case TcpClient::Authenticating:
        m_connectionStatusLabel->setText("K4");
        m_connectionStatusLabel->setStyleSheet(
            QString("color: %1; font-size: 12px; font-weight: bold;").arg(K4Styles::Colors::AccentAmber));
        break;

    case TcpClient::Connected:
        m_connectionStatusLabel->setText("K4");
        m_connectionStatusLabel->setStyleSheet(
            QString("color: %1; font-size: 12px; font-weight: bold;").arg(K4Styles::Colors::StatusGreen));
        break;
    }
}

void MainWindow::onRfPowerChanged(double watts, bool isQrp) {
    Q_UNUSED(watts)
    // Propagate QRP mode to TX meter widgets so they use the correct scale
    m_vfoA->setTxMeterQrp(isQrp);
    m_vfoB->setTxMeterQrp(isQrp);
}

void MainWindow::onSupplyVoltageChanged(double volts) {
    m_voltageLabel->setText(QString("%1 V").arg(volts, 0, 'f', 1));
    m_sideControlPanel->setVoltage(volts);
}

void MainWindow::onSupplyCurrentChanged(double amps) {
    m_currentLabel->setText(QString("%1 A").arg(amps, 0, 'f', 1));
    m_sideControlPanel->setCurrent(amps);
}

void MainWindow::onSwrChanged(double swr) {
    m_swrLabel->setText(QString("%1:1").arg(swr, 0, 'f', 1));
    m_sideControlPanel->setSwr(swr);
}

void MainWindow::onDisplayFpsChanged(int fps) {
    // Update synthetic menu item value with whatever the radio reports
    m_menuModel->updateValue(MenuModel::SYNTHETIC_DISPLAY_FPS_ID, fps);
}

void MainWindow::onSplitChanged(bool enabled) {
    if (enabled) {
        m_splitLabel->setText("SPLIT ON");
        m_splitLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(K4Styles::Colors::AccentAmber));
        // When split is on, TX goes to VFO B - clear left triangle, show right triangle
        m_txTriangle->setText("");
        m_txTriangleB->setText("▶");
    } else {
        m_splitLabel->setText("SPLIT OFF");
        m_splitLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(K4Styles::Colors::AccentAmber));
        // When split is off, TX stays on VFO A - show left triangle, clear right triangle
        m_txTriangle->setText("◀");
        m_txTriangleB->setText("");
    }
    // Split changes which VFO transmits — update TX markers
    m_spectrumController->updateTxMarkers();
}

void MainWindow::onAntennaChanged(int txAnt, int rxAntMain, int rxAntSub) {
    // Format Main RX antenna display based on AR command value
    // K4 AR command values (from official K4 protocol documentation):
    // 0 = Disconnected (all RX RF sources disconnected)
    // 1 = EXT. XVTR IN / RX ANT IN2 (external transverter jack)
    // 2 = RX USES TX ANT (follows TX antenna selection) - show resolved value
    // 3 = INT. XVTR IN (internal transverter)
    // 4 = RX ANT IN1 (receive antenna jack)
    // 5 = ATU RX ANT1 (TX antenna 1 via ATU)
    // 6 = ATU RX ANT2 (TX antenna 2 via ATU)
    // 7 = ATU RX ANT3 (TX antenna 3 via ATU)
    auto formatMainRxAntenna = [this, txAnt](int arValue) -> QString {
        switch (arValue) {
        case 0: // Disconnected
            return "OFF";
        case 1: // EXT. XVTR IN / RX ANT IN2
            return QString("RX2:%1").arg(m_radioState->antennaName(5));
        case 2: // RX USES TX ANT - show resolved value like K4 front panel
            return QString("%1:%2").arg(txAnt).arg(m_radioState->antennaName(txAnt));
        case 3: // INT. XVTR IN
            return "INT XVTR";
        case 4: // RX ANT IN1
            return QString("RX1:%1").arg(m_radioState->antennaName(4));
        case 5: // ATU RX ANT1
            return QString("1:%1").arg(m_radioState->antennaName(1));
        case 6: // ATU RX ANT2
            return QString("2:%1").arg(m_radioState->antennaName(2));
        case 7: // ATU RX ANT3
            return QString("3:%1").arg(m_radioState->antennaName(3));
        default:
            return QString("AR%1").arg(arValue);
        }
    };

    // Format Sub RX antenna display based on AR$ command value
    // K4 AR$ command values (from official K4 protocol documentation):
    // 0 = Disconnected (all RX RF sources disconnected)
    // 1 = EXT. XVTR IN / RX ANT IN2 (external transverter jack)
    // 2 = RX USES TX ANT (follows TX antenna selection) - show resolved value
    // 3 = INT. XVTR IN (internal transverter)
    // 4 = RX ANT IN1 (receive antenna jack)
    // 5 = ATU RX ANT1 (TX antenna 1 via ATU)
    // 6 = ATU RX ANT2 (TX antenna 2 via ATU)
    // 7 = ATU RX ANT3 (TX antenna 3 via ATU)
    auto formatSubRxAntenna = [this, txAnt](int arValue) -> QString {
        switch (arValue) {
        case 0: // Disconnected
            return "OFF";
        case 1: // EXT. XVTR IN / RX ANT IN2
            return QString("RX2:%1").arg(m_radioState->antennaName(5));
        case 2: // RX USES TX ANT - show resolved value like K4 front panel
            return QString("%1:%2").arg(txAnt).arg(m_radioState->antennaName(txAnt));
        case 3: // INT. XVTR IN
            return "INT XVTR";
        case 4: // RX ANT IN1
            return QString("RX1:%1").arg(m_radioState->antennaName(4));
        case 5: // ATU RX ANT1
            return QString("1:%1").arg(m_radioState->antennaName(1));
        case 6: // ATU RX ANT2
            return QString("2:%1").arg(m_radioState->antennaName(2));
        case 7: // ATU RX ANT3
            return QString("3:%1").arg(m_radioState->antennaName(3));
        default:
            return QString("AR$%1").arg(arValue);
        }
    };

    // TX antenna (AN command) - always 1-3, format as "N:name"
    m_txAntennaLabel->setText(QString("%1:%2").arg(txAnt).arg(m_radioState->antennaName(txAnt)));

    // RX antennas - Main (AR) and Sub (AR$) have different value mappings
    m_rxAntALabel->setText(formatMainRxAntenna(rxAntMain));
    m_rxAntBLabel->setText(formatSubRxAntenna(rxAntSub));
}

void MainWindow::onAntennaNameChanged(int index, const QString &name) {
    // Refresh antenna displays when a name changes
    onAntennaChanged(m_radioState->txAntenna(), m_radioState->rxAntennaMain(), m_radioState->rxAntennaSub());

    // Update antenna config popups with custom names (ANT1-3 only)
    // Note: index is 1-based from ACN command (ACN1, ACN2, ACN3)
    // Popup labels are 0-based (0=ANT1, 1=ANT2, 2=ANT3)
    if (index >= 1 && index <= 3) {
        int popupIndex = index - 1; // Convert to 0-based
        if (m_mainRxAntCfgPopup)
            m_mainRxAntCfgPopup->setAntennaName(popupIndex, name);
        if (m_subRxAntCfgPopup)
            m_subRxAntCfgPopup->setAntennaName(popupIndex, name);
        if (m_txAntCfgPopup)
            m_txAntCfgPopup->setAntennaName(popupIndex, name);
    }
}

void MainWindow::onVoxChanged(bool enabled) {
    Q_UNUSED(enabled)
    // Use mode-specific VOX state (CW modes use VXC, Voice modes use VXV, Data modes use VXD)
    bool voxOn = m_radioState->voxForCurrentMode();
    if (voxOn) {
        m_voxLabel->setStyleSheet(
            QString("color: %1; font-size: 11px; font-weight: bold;").arg(K4Styles::Colors::AccentAmber));
    } else {
        m_voxLabel->setStyleSheet(
            QString("color: %1; font-size: 11px; font-weight: bold;").arg(K4Styles::Colors::TextGray));
    }
}

void MainWindow::onQskEnabledChanged(bool enabled) {
    // QSK indicator: white when enabled, grey when disabled
    if (enabled) {
        m_qskLabel->setStyleSheet(
            QString("color: %1; font-size: 11px; font-weight: bold;").arg(K4Styles::Colors::TextWhite));
    } else {
        m_qskLabel->setStyleSheet(
            QString("color: %1; font-size: 11px; font-weight: bold;").arg(K4Styles::Colors::TextGray));
    }
}

void MainWindow::onTestModeChanged(bool enabled) {
    // TEST indicator: visible in red when test mode is on
    m_vfoRow->setTestVisible(enabled);
}

void MainWindow::onAtuModeChanged(int mode) {
    // ATU indicator: orange when AUTO mode (2), grey otherwise
    if (mode == 2) {
        m_atuLabel->setStyleSheet(
            QString("color: %1; font-size: 11px; font-weight: bold;").arg(K4Styles::Colors::AccentAmber));
    } else {
        m_atuLabel->setStyleSheet(
            QString("color: %1; font-size: 11px; font-weight: bold;").arg(K4Styles::Colors::TextGray));
    }
}

void MainWindow::onRitXitChanged(bool ritEnabled, bool xitEnabled, int offset) {
    // Update RIT label
    if (ritEnabled) {
        m_ritLabel->setStyleSheet(
            QString("color: %1; font-size: 10px; font-weight: bold; border: none;").arg(K4Styles::Colors::TextWhite));
    } else {
        m_ritLabel->setStyleSheet(
            QString("color: %1; font-size: 10px; border: none;").arg(K4Styles::Colors::InactiveGray));
    }

    // Update XIT label
    if (xitEnabled) {
        m_xitLabel->setStyleSheet(
            QString("color: %1; font-size: 10px; font-weight: bold; border: none;").arg(K4Styles::Colors::TextWhite));
    } else {
        m_xitLabel->setStyleSheet(
            QString("color: %1; font-size: 10px; border: none;").arg(K4Styles::Colors::InactiveGray));
    }

    // Update offset value (in kHz)
    // Value is white if RIT or XIT is on, grey if both are off
    double offsetKHz = offset / 1000.0;
    QString sign = (offset >= 0) ? "+" : "";
    m_ritXitValueLabel->setText(QString("%1%2").arg(sign).arg(offsetKHz, 0, 'f', 2));

    QString valueColor = (ritEnabled || xitEnabled) ? K4Styles::Colors::TextWhite : K4Styles::Colors::InactiveGray;
    m_ritXitValueLabel->setStyleSheet(
        QString("color: %1; font-size: 14px; font-weight: bold; border: none; padding: 0 11px;").arg(valueColor));

    // Refresh frequency displays and panadapter passband — RIT offset affects receive frequency
    onFrequencyChanged(m_radioState->vfoA());
    onFrequencyBChanged(m_radioState->vfoB());

    // Update panadapter passband positions (tuned frequency includes RIT offset when active)
    // When BSET is on, panadapter A shows VFO B's passband position (matching the UI switch)
    m_spectrumController->updatePanadapterPassbands();

    // Update TX marker — shows where we'll transmit when RIT/XIT splits TX from RX
    m_spectrumController->updateTxMarkers();
}

void MainWindow::onMessageBankChanged(int bank) {
    if (bank == 1) {
        m_msgBankLabel->setText("MSG: I");
    } else {
        m_msgBankLabel->setText("MSG: II");
    }
}

void MainWindow::onProcessingChanged() {
    // AGC
    QString agcText;
    switch (m_radioState->agcSpeed()) {
    case RadioState::AGC_Off:
        agcText = "AGC";
        break;
    case RadioState::AGC_Slow:
        agcText = "AGC-S";
        break;
    case RadioState::AGC_Fast:
        agcText = "AGC-F";
        break;
    }
    m_vfoA->setAGC(agcText);

    // Preamp (level 0-3)
    m_vfoA->setPreamp(m_radioState->preampEnabled() && m_radioState->preamp() > 0, m_radioState->preamp());

    // Attenuator (level 0-21 dB)
    m_vfoA->setAtt(m_radioState->attenuatorEnabled() && m_radioState->attenuatorLevel() > 0,
                   m_radioState->attenuatorLevel());

    // Noise Blanker
    m_vfoA->setNB(m_radioState->noiseBlankerEnabled());

    // Noise Reduction
    m_vfoA->setNR(m_radioState->noiseReductionEnabled());
}

void MainWindow::onProcessingChangedB() {
    // AGC
    QString agcText;
    switch (m_radioState->agcSpeedB()) {
    case RadioState::AGC_Off:
        agcText = "AGC";
        break;
    case RadioState::AGC_Slow:
        agcText = "AGC-S";
        break;
    case RadioState::AGC_Fast:
        agcText = "AGC-F";
        break;
    }
    m_vfoB->setAGC(agcText);

    // Preamp (level 0-3)
    m_vfoB->setPreamp(m_radioState->preampEnabledB() && m_radioState->preampB() > 0, m_radioState->preampB());

    // Attenuator (level 0-21 dB)
    m_vfoB->setAtt(m_radioState->attenuatorEnabledB() && m_radioState->attenuatorLevelB() > 0,
                   m_radioState->attenuatorLevelB());

    // Noise Blanker
    m_vfoB->setNB(m_radioState->noiseBlankerEnabledB());

    // Noise Reduction
    m_vfoB->setNR(m_radioState->noiseReductionEnabledB());
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    // Handle clicks on VFO A square/mode label -> open mode popup for VFO A
    if ((watched == m_vfoASquare || watched == m_modeALabel) && event->type() == QEvent::MouseButtonPress) {
        // Toggle popup - close if open, otherwise show for VFO A
        if (m_modePopup->isVisible()) {
            m_modePopup->hidePopup();
        } else {
            m_modePopup->setFrequency(m_radioState->vfoA());
            m_modePopup->setCurrentMode(static_cast<int>(m_radioState->mode()));
            m_modePopup->setCurrentDataSubMode(m_radioState->dataSubMode());
            m_modePopup->setBSetEnabled(false); // Commands target VFO A
            m_modePopup->showAboveWidget(m_bottomMenuBar);
        }
        return true;
    }

    // Handle clicks on VFO B square/mode label -> open mode popup for VFO B
    if ((watched == m_vfoBSquare || watched == m_modeBLabel) && event->type() == QEvent::MouseButtonPress) {
        // Toggle popup - close if open, otherwise show for VFO B
        if (m_modePopup->isVisible()) {
            m_modePopup->hidePopup();
        } else {
            m_modePopup->setFrequency(m_radioState->vfoB());
            m_modePopup->setCurrentMode(static_cast<int>(m_radioState->modeB()));
            m_modePopup->setCurrentDataSubMode(m_radioState->dataSubModeB());
            m_modePopup->setBSetEnabled(true); // Commands target VFO B (MD$, DT$)
            m_modePopup->showAboveWidget(m_bottomMenuBar);
        }
        return true;
    }

    // Panadapter resize events handled by SpectrumController's eventFilter

    // Handle right-click on memory buttons (alternate actions)
    if (event->type() == QEvent::MouseButtonPress) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::RightButton) {
            if (watched == m_recBtn) {
                m_connectionController->sendCAT("SW137;"); // BANK
                return true;
            } else if (watched == m_storeBtn) {
                m_connectionController->sendCAT("SW138;"); // AF REC
                return true;
            } else if (watched == m_rclBtn) {
                m_connectionController->sendCAT("SW139;"); // AF PLAY
                return true;
            }
        }
    }

    // RIT label click - toggle RIT on/off (SW54 routes correctly when BSET targets VFO B)
    if (watched == m_ritLabel && event->type() == QEvent::MouseButtonPress) {
        bool bSet = m_radioState->bSetEnabled();
        m_connectionController->sendCAT(bSet ? "SW54;" : "RT/;");
        // K4 doesn't echo RT$/RO$ for SW54 — query VFO B RIT state
        if (bSet) {
            m_connectionController->sendCAT("RT$;");
            m_connectionController->sendCAT("RO$;");
        }
        return true;
    }

    // XIT label click - toggle XIT on/off
    if (watched == m_xitLabel && event->type() == QEvent::MouseButtonPress) {
        m_connectionController->sendCAT("XT/;");
        return true;
    }

    // TX indicator click - toggle split on/off
    if (watched == m_txIndicator && event->type() == QEvent::MouseButtonPress) {
        m_connectionController->sendCAT("SW145;");
        return true;
    }

    // Mouse wheel on RIT/XIT box (or its child widgets) - adjust offset using RU/RD commands
    // K4 routes RU;/RD; based on active mode: RIT → RO (VFO A), XIT → RO$ (VFO B)
    // BSET + RIT: use RU$/RD$ to force VFO B's RIT offset
    if (event->type() == QEvent::Wheel &&
        (watched == m_ritXitBox || watched == m_ritLabel || watched == m_xitLabel || watched == m_ritXitValueLabel)) {
        auto *wheelEvent = static_cast<QWheelEvent *>(event);
        int steps = m_ritWheelAccumulator.accumulate(wheelEvent);
        if (steps != 0) {
            bool bSet = m_radioState->bSetEnabled();
            bool adjustB = bSet && !m_radioState->xitEnabled();
            QString upCmd = adjustB ? "RU$;" : "RU;";
            QString downCmd = adjustB ? "RD$;" : "RD;";
            for (int i = 0; i < qAbs(steps); ++i)
                m_connectionController->sendCAT(steps > 0 ? upCmd : downCmd);
        }
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
        executeMacro(functionId);
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::showMenuOverlay() {
    // Close display popup if visible
    if (m_displayPopup && m_displayPopup->isVisible()) {
        m_displayPopup->hidePopup();
    }

    // Toggle menu overlay visibility
    if (m_spectrumController->spectrumContainer() && m_menuOverlay) {
        if (m_menuOverlay->isVisible()) {
            // Hide the overlay
            m_menuOverlay->hide();
            if (m_bottomMenuBar) {
                m_bottomMenuBar->setMenuActive(false);
            }
        } else {
            // Show the overlay
            QPoint pos = m_spectrumController->spectrumContainer()->mapTo(this, QPoint(0, 0));
            m_menuOverlay->setGeometry(pos.x(), pos.y(), m_spectrumController->spectrumContainer()->width(),
                                       m_spectrumController->spectrumContainer()->height());
            m_menuOverlay->show();
            m_menuOverlay->raise();

            // Set MENU button to active (inverse colors)
            if (m_bottomMenuBar) {
                m_bottomMenuBar->setMenuActive(true);
            }
        }
    }
}

void MainWindow::onMenuValueChangeRequested(int menuId, const QString &action) {
    // Handle synthetic menu items (negative IDs)
    if (menuId == MenuModel::SYNTHETIC_DISPLAY_FPS_ID) {
        MenuItem *item = m_menuModel->getMenuItem(menuId);
        if (!item)
            return;

        int newValue = item->currentValue;
        if (action == "+") {
            newValue = qMin(item->currentValue + 1, 30);
        } else if (action == "-") {
            newValue = qMax(item->currentValue - 1, 12);
        }

        // Update menu model
        m_menuModel->updateValue(menuId, newValue);

        // Send #FPS command (not ME command)
        if (m_connectionController->isConnected()) {
            qCDebug(qk4Main) << "Display FPS change:" << QString("#FPS%1;").arg(newValue);
            m_connectionController->sendCAT(QString("#FPS%1;").arg(newValue));
        }

        // Update stored preference
        m_connectionController->setDisplayFps(newValue);
        return;
    }

    // Build and send ME command for real K4 menu items
    // action: "+" = increment, "-" = decrement, "/" = toggle
    QString cmd = QString("ME%1.%2;").arg(menuId, 4, 10, QChar('0')).arg(action);
    qCDebug(qk4Main) << "Menu value change:" << cmd;

    // For prototype: update local model immediately (optimistic update)
    MenuItem *item = m_menuModel->getMenuItem(menuId);
    if (item) {
        int newValue = item->currentValue;
        if (action == "+") {
            newValue = qMin(item->currentValue + item->step, item->maxValue);
        } else if (action == "-") {
            newValue = qMax(item->currentValue - item->step, item->minValue);
        } else if (action == "/") {
            // Toggle binary
            newValue = (item->currentValue == 0) ? 1 : 0;
        }
        m_menuModel->updateValue(menuId, newValue);
    }

    // When connected to radio, send the command
    if (m_connectionController->isConnected()) {
        m_connectionController->sendCAT(cmd);
    }
}

void MainWindow::onMenuModelValueChanged(int menuId, int newValue) {
    // Check if this is the "Spectrum Amplitude Units" menu item
    const MenuItem *item = m_menuModel->getMenuItem(menuId);
    if (item && item->name == "Spectrum Amplitude Units") {
        // 0 = dBm, 1 = S-UNITS
        bool useSUnits = (newValue == 1);
        qCDebug(qk4Main) << "Spectrum amplitude units changed:" << (useSUnits ? "S-UNITS" : "dBm");

        m_spectrumController->panadapterA()->setAmplitudeUnits(useSUnits);
        m_spectrumController->panadapterB()->setAmplitudeUnits(useSUnits);
    }

    // Track "Mouse L/R Button QSY" setting changes (from radio or menu overlay)
    if (menuId == m_mouseQsyMenuId) {
        m_mouseQsyMode = newValue;
        m_spectrumController->setMouseQsyMode(newValue);
        qCDebug(qk4Main) << "Mouse L/R Button QSY changed to:" << m_mouseQsyMode;
    }

    // Track "FSK Mark-Tone" setting changes
    if (menuId == m_fskMarkToneMenuId) {
        auto *item = m_menuModel->getMenuItem(menuId);
        if (item && newValue >= 0 && newValue < item->options.size()) {
            int toneHz = item->options[newValue].toInt();
            qCDebug(qk4Main) << "FSK Mark-Tone changed to:" << toneHz << "Hz";
            m_spectrumController->panadapterA()->setFskMarkTone(toneHz);
            m_spectrumController->panadapterB()->setFskMarkTone(toneHz);
        }
    }
}

void MainWindow::toggleDisplayPopup() {
    bool wasVisible = m_displayPopup && m_displayPopup->isVisible();
    closeAllPopups();

    if (!wasVisible && m_displayPopup && m_bottomMenuBar) {
        m_displayPopup->showAboveButton(m_bottomMenuBar->displayButton());
        m_bottomMenuBar->setDisplayActive(true);
    }
}

void MainWindow::closeAllPopups() {
    // Close menu overlay
    if (m_menuOverlay && m_menuOverlay->isVisible()) {
        m_menuOverlay->hide();
        if (m_bottomMenuBar) {
            m_bottomMenuBar->setMenuActive(false);
        }
    }

    // Close band popup
    if (m_bandPopup && m_bandPopup->isVisible()) {
        m_bandPopup->hidePopup();
        if (m_bottomMenuBar) {
            m_bottomMenuBar->setBandActive(false);
        }
    }

    // Close display popup
    if (m_displayPopup && m_displayPopup->isVisible()) {
        m_displayPopup->hidePopup();
        if (m_bottomMenuBar) {
            m_bottomMenuBar->setDisplayActive(false);
        }
    }

    // Close Fn popup
    if (m_fnPopup && m_fnPopup->isVisible()) {
        m_fnPopup->hidePopup();
        if (m_bottomMenuBar) {
            m_bottomMenuBar->setFnActive(false);
        }
    }

    // Close Main RX popup
    if (m_mainRxPopup && m_mainRxPopup->isVisible()) {
        m_mainRxPopup->hidePopup();
        if (m_bottomMenuBar) {
            m_bottomMenuBar->setMainRxActive(false);
        }
    }

    // Close Sub RX popup
    if (m_subRxPopup && m_subRxPopup->isVisible()) {
        m_subRxPopup->hidePopup();
        if (m_bottomMenuBar) {
            m_bottomMenuBar->setSubRxActive(false);
        }
    }

    // Close TX popup
    if (m_txPopup && m_txPopup->isVisible()) {
        m_txPopup->hidePopup();
        if (m_bottomMenuBar) {
            m_bottomMenuBar->setTxActive(false);
        }
    }

    // Close secondary popups (opened from RX/TX button rows)
    if (m_rxEqPopup && m_rxEqPopup->isVisible())
        m_rxEqPopup->hidePopup();
    if (m_txEqPopup && m_txEqPopup->isVisible())
        m_txEqPopup->hidePopup();
    if (m_mainRxAntCfgPopup && m_mainRxAntCfgPopup->isVisible())
        m_mainRxAntCfgPopup->hidePopup();
    if (m_subRxAntCfgPopup && m_subRxAntCfgPopup->isVisible())
        m_subRxAntCfgPopup->hidePopup();
    if (m_txAntCfgPopup && m_txAntCfgPopup->isVisible())
        m_txAntCfgPopup->hidePopup();
    if (m_modePopup && m_modePopup->isVisible())
        m_modePopup->hidePopup();
}

void MainWindow::toggleBandPopup() {
    bool wasVisible = m_bandPopup && m_bandPopup->isVisible();
    closeAllPopups();

    if (!wasVisible && m_bandPopup && m_bottomMenuBar) {
        m_bandPopup->showAboveButton(m_bottomMenuBar->bandButton());
        m_bottomMenuBar->setBandActive(true);
    }
}

void MainWindow::toggleFnPopup() {
    bool wasVisible = m_fnPopup && m_fnPopup->isVisible();
    closeAllPopups();

    if (!wasVisible && m_fnPopup && m_bottomMenuBar) {
        m_fnPopup->showAboveButton(m_bottomMenuBar->fnButton());
        m_bottomMenuBar->setFnActive(true);
    }
}

void MainWindow::toggleMainRxPopup() {
    bool wasVisible = m_mainRxPopup && m_mainRxPopup->isVisible();
    closeAllPopups();

    if (!wasVisible && m_mainRxPopup && m_bottomMenuBar) {
        m_mainRxPopup->showAboveButton(m_bottomMenuBar->mainRxButton());
        m_bottomMenuBar->setMainRxActive(true);
    }
}

void MainWindow::toggleSubRxPopup() {
    bool wasVisible = m_subRxPopup && m_subRxPopup->isVisible();
    closeAllPopups();

    if (!wasVisible && m_subRxPopup && m_bottomMenuBar) {
        m_subRxPopup->showAboveButton(m_bottomMenuBar->subRxButton());
        m_bottomMenuBar->setSubRxActive(true);
    }
}

void MainWindow::toggleTxPopup() {
    bool wasVisible = m_txPopup && m_txPopup->isVisible();
    closeAllPopups();

    if (!wasVisible && m_txPopup && m_bottomMenuBar) {
        m_txPopup->showAboveButton(m_bottomMenuBar->txButton());
        m_bottomMenuBar->setTxActive(true);
    }
}

void MainWindow::onBandSelected(const QString &bandName) {
    qCDebug(qk4Main) << "Band selected:" << bandName;

    // Get band number from name
    int newBandNum = m_bandPopup->getBandNumber(bandName);

    // GEN and MEM are special modes, not band changes (-1 returned)
    if (newBandNum < 0) {
        qCDebug(qk4Main) << "Special mode selected (GEN/MEM) - no BN command";
        return;
    }

    if (m_connectionController->isConnected()) {
        // Check if BSET is enabled - target VFO B (Sub RX) instead of VFO A
        bool bSetEnabled = m_radioState->bSetEnabled();
        int currentBand = bSetEnabled ? m_currentBandNumB : m_currentBandNum;
        QString cmdPrefix = bSetEnabled ? "BN$" : "BN";

        if (newBandNum == currentBand) {
            // Same band tapped - invoke band stack
            QString bandStackCmd = bSetEnabled ? "BN$^;" : "BN^;";
            qCDebug(qk4Main) << "Same band - invoking band stack with" << bandStackCmd;
            m_connectionController->sendCAT(bandStackCmd);
        } else {
            // Different band selected - change band
            QString cmd = QString("%1%2;").arg(cmdPrefix).arg(newBandNum, 2, 10, QChar('0'));
            qCDebug(qk4Main) << "Changing band:" << cmd;
            m_connectionController->sendCAT(cmd);
        }
        // Request current band to update UI
        QString queryCmd = bSetEnabled ? "BN$;" : "BN;";
        m_connectionController->sendCAT(queryCmd);
    }
}

void MainWindow::updateBandSelection(int bandNum) {
    m_currentBandNum = bandNum;

    // Update the band popup to show the current band as selected (only when not in BSET mode)
    if (m_bandPopup && !m_radioState->bSetEnabled()) {
        m_bandPopup->setSelectedBandByNumber(bandNum);
    }
}

void MainWindow::updateBandSelectionB(int bandNum) {
    m_currentBandNumB = bandNum;

    // Update the band popup to show the current band as selected (only when in BSET mode)
    if (m_bandPopup && m_radioState->bSetEnabled()) {
        m_bandPopup->setSelectedBandByNumber(bandNum);
    }
}

// ============== K4 Error/Notification Slots ==============

void MainWindow::onErrorNotification(int errorCode, const QString &message) {
    Q_UNUSED(errorCode)
    // Show the notification message in a centered popup
    // The message contains the text after "ERxx:" (e.g., "KPA1500 Status: operate.")
    if (m_notificationWidget) {
        m_notificationWidget->showMessage(message, 2000);
    }
}

// ============== KPA1500 Amplifier Slots ==============

void MainWindow::onKpa1500Connected() {
    qCDebug(qk4Main) << "KPA1500: Connected to amplifier";
    // Start polling with configured interval
    int pollInterval = RadioSettings::instance()->kpa1500PollInterval();
    m_kpa1500Client->startPolling(pollInterval);
    updateKpa1500Status();
}

void MainWindow::onKpa1500Disconnected() {
    qCDebug(qk4Main) << "KPA1500: Disconnected from amplifier";
    updateKpa1500Status();
}

void MainWindow::onKpa1500Error(const QString &error) {
    qWarning() << "KPA1500: Error -" << error;
}

void MainWindow::onKpa1500EnabledChanged(bool enabled) {
    if (enabled) {
        // Connect if host is configured
        QString host = RadioSettings::instance()->kpa1500Host();
        if (!host.isEmpty()) {
            m_kpa1500Client->connectToHost(host, RadioSettings::instance()->kpa1500Port());
        }
    } else {
        m_kpa1500Client->disconnectFromHost();
    }
    updateKpa1500Status();
}

void MainWindow::onKpa1500SettingsChanged() {
    // Reconnect with new settings if enabled
    if (RadioSettings::instance()->kpa1500Enabled()) {
        m_kpa1500Client->disconnectFromHost();
        QString host = RadioSettings::instance()->kpa1500Host();
        if (!host.isEmpty()) {
            m_kpa1500Client->connectToHost(host, RadioSettings::instance()->kpa1500Port());
        }
    }
    updateKpa1500Status();
}

void MainWindow::updateKpa1500Status() {
    bool enabled = RadioSettings::instance()->kpa1500Enabled();
    bool connected = m_kpa1500Client && m_kpa1500Client->isConnected();

    if (!enabled) {
        m_kpa1500StatusLabel->hide();
    } else {
        m_kpa1500StatusLabel->show();
        if (connected) {
            m_kpa1500StatusLabel->setText("KPA1500");
            m_kpa1500StatusLabel->setStyleSheet(
                QString("color: %1; font-size: 12px; font-weight: bold;").arg(K4Styles::Colors::StatusGreen));
        } else {
            m_kpa1500StatusLabel->setText("KPA1500");
            m_kpa1500StatusLabel->setStyleSheet(
                QString("color: %1; font-size: 12px;").arg(K4Styles::Colors::InactiveGray));
        }
    }
}

// ============== Fn Popup / Macro Slots ==============

void MainWindow::onFnFunctionTriggered(const QString &functionId) {
    qCDebug(qk4Main) << "Fn function triggered:" << functionId;

    // Handle built-in functions
    if (functionId == MacroIds::ScrnCap) {
        // SS0; triggers K4 screenshot (saved to internal SD card)
        if (m_connectionController->isConnected()) {
            m_connectionController->sendCAT("SS0;");
            qCDebug(qk4Main) << "Screenshot captured (SS0;)";
        }
    } else if (functionId == MacroIds::Macros) {
        openMacroDialog();
    } else if (functionId == MacroIds::SwList) {
        // TODO: Show software list
        qCDebug(qk4Main) << "Software list - not yet implemented";
    } else if (functionId == MacroIds::Update) {
        // TODO: Check for updates
        qCDebug(qk4Main) << "Update check - not yet implemented";
    } else if (functionId == MacroIds::DxList) {
        // TODO: Show DX list
        qCDebug(qk4Main) << "DX list - not yet implemented";
    } else {
        // User-configurable macro - execute CAT command
        executeMacro(functionId);
    }
}

void MainWindow::executeMacro(const QString &functionId) {
    MacroEntry macro = RadioSettings::instance()->macro(functionId);
    if (!macro.command.isEmpty()) {
        qCDebug(qk4Main) << "Executing macro" << functionId << ":" << macro.command;
        if (m_connectionController->isConnected()) {
            m_connectionController->sendCAT(macro.command);
        }
    } else {
        qCDebug(qk4Main) << "No macro configured for" << functionId;
    }
}

void MainWindow::openMacroDialog() {
    if (m_macroDialog) {
        // Close any open popups first
        closeAllPopups();

        // Size to fill the spectrum container (same as menu overlay)
        if (m_spectrumController->spectrumContainer()) {
            QPoint pos = m_spectrumController->spectrumContainer()->mapTo(this, QPoint(0, 0));
            m_macroDialog->setGeometry(pos.x(), pos.y(), m_spectrumController->spectrumContainer()->width(),
                                       m_spectrumController->spectrumContainer()->height());
        }

        m_macroDialog->show();
        m_macroDialog->raise();
        m_macroDialog->setFocus();
    }
}

// ============== MAIN RX / SUB RX Popup Slots ==============

void MainWindow::onMainRxButtonClicked(int index) {
    if (!m_connectionController->isConnected())
        return;

    switch (index) {
    case 0: // ANT CFG - show Main RX antenna config popup
        if (m_mainRxAntCfgPopup && m_mainRxPopup) {
            m_mainRxAntCfgPopup->showAboveWidget(m_mainRxPopup);
        }
        break;
    case 1: // RX EQ - show graphic equalizer popup
        if (m_rxEqPopup && m_mainRxPopup) {
            // Show EQ popup above the MAIN RX popup (keep both visible)
            m_rxEqPopup->showAboveWidget(m_mainRxPopup);
        }
        break;
    case 2: // LINE OUT - show Line Out popup
        if (m_lineOutPopup && m_mainRxPopup) {
            m_lineOutPopup->showAboveWidget(m_mainRxPopup);
        }
        break;
    case 3: // AFX - cycle OFF → DELAY → PITCH → OFF
    {
        int nextMode = (m_radioState->afxMode() + 1) % 3;
        m_connectionController->sendCAT(QString("FX%1;").arg(nextMode));
        break;
    }
    case 4: // AGC - toggle Fast ↔ Slow
    {
        RadioState::AGCSpeed current = m_radioState->agcSpeed();
        // Toggle between Fast (2) and Slow (1), skip Off
        int next = (current == RadioState::AGC_Fast) ? 1 : 2;
        m_connectionController->sendCAT(QString("GT%1;").arg(next));
        break;
    }
    case 5: // APF - toggle on/off (Main RX)
        m_connectionController->sendCAT("AP/;");
        break;
    case 6: // TEXT DECODE - open window directly for Main RX
        if (m_textDecodeWindowMain) {
            // Set operating mode based on current radio mode + data submode
            RadioState::Mode radioMode = m_radioState->mode();
            if (radioMode == RadioState::CW || radioMode == RadioState::CW_R) {
                m_textDecodeWindowMain->setOperatingMode(TextDecodeWindow::ModeCW);
            } else if (radioMode == RadioState::DATA || radioMode == RadioState::DATA_R) {
                int subMode = m_radioState->dataSubMode();
                switch (subMode) {
                case 1:
                    m_textDecodeWindowMain->setOperatingMode(TextDecodeWindow::ModeAFSK);
                    break;
                case 2:
                    m_textDecodeWindowMain->setOperatingMode(TextDecodeWindow::ModeFSK);
                    break;
                case 3:
                    m_textDecodeWindowMain->setOperatingMode(TextDecodeWindow::ModePSK);
                    break;
                default:
                    m_textDecodeWindowMain->setOperatingMode(TextDecodeWindow::ModeData);
                    break;
                }
                // Sync current data rate from radio
                int dr = m_radioState->dataRate();
                if (dr >= 0)
                    m_textDecodeWindowMain->setDataRate(dr);
            } else if (radioMode == RadioState::LSB || radioMode == RadioState::USB) {
                m_textDecodeWindowMain->setOperatingMode(TextDecodeWindow::ModeSSB);
            } else {
                m_textDecodeWindowMain->setOperatingMode(TextDecodeWindow::ModeOther);
            }
            // Show window and enable decode
            m_textDecodeWindowMain->show();
            if (!m_textDecodeWindowMain->isDecodeEnabled()) {
                m_textDecodeWindowMain->setDecodeEnabled(true);
                emit m_textDecodeWindowMain->enabledChanged(true);
            }
        }
        break;
    }
}

void MainWindow::onMainRxButtonRightClicked(int index) {
    if (!m_connectionController->isConnected())
        return;

    switch (index) {
    case 2: // LINE OUT → VFO LINK toggle
    {
        bool linked = m_radioState->vfoLink();
        m_connectionController->sendCAT(QString("LN%1;").arg(linked ? 0 : 1));
        break;
    }
    case 3: // AFX - same as left-click (cycle)
        onMainRxButtonClicked(3);
        break;
    case 4: // AGC - toggle ON/OFF
    {
        RadioState::AGCSpeed current = m_radioState->agcSpeed();
        if (current == RadioState::AGC_Off) {
            // Turn on (default to Slow)
            m_connectionController->sendCAT("GT1;");
        } else {
            // Turn off
            m_connectionController->sendCAT("GT0;");
        }
        break;
    }
    case 5: // APF - cycle bandwidth (Main RX)
        m_connectionController->sendCAT("AP+;");
        break;
    default:
        break;
    }
}

void MainWindow::onSubRxButtonClicked(int index) {
    if (!m_connectionController->isConnected())
        return;

    switch (index) {
    case 0: // ANT CFG - show Sub RX antenna config popup
        if (m_subRxAntCfgPopup && m_subRxPopup) {
            m_subRxAntCfgPopup->showAboveWidget(m_subRxPopup);
        }
        break;
    case 1: // RX EQ - show graphic equalizer popup (shares same EQ as Main RX)
        if (m_rxEqPopup && m_subRxPopup) {
            m_rxEqPopup->showAboveWidget(m_subRxPopup);
        }
        break;
    case 2: // LINE OUT - show Line Out popup
        if (m_lineOutPopup && m_subRxPopup) {
            m_lineOutPopup->showAboveWidget(m_subRxPopup);
        }
        break;
    case 3: // AFX - cycle (same command, affects audio)
    {
        int nextMode = (m_radioState->afxMode() + 1) % 3;
        m_connectionController->sendCAT(QString("FX%1;").arg(nextMode));
        break;
    }
    case 4: // AGC Sub - toggle Fast ↔ Slow
    {
        RadioState::AGCSpeed current = m_radioState->agcSpeedB();
        int next = (current == RadioState::AGC_Fast) ? 1 : 2;
        m_connectionController->sendCAT(QString("GT$%1;").arg(next));
        break;
    }
    case 5: // APF - toggle on/off (Sub RX)
        m_connectionController->sendCAT("AP$/;");
        break;
    case 6: // TEXT DECODE - open window directly for Sub RX
        if (m_textDecodeWindowSub) {
            // Set operating mode based on Sub RX mode + data submode
            RadioState::Mode radioMode = m_radioState->modeB();
            if (radioMode == RadioState::CW || radioMode == RadioState::CW_R) {
                m_textDecodeWindowSub->setOperatingMode(TextDecodeWindow::ModeCW);
            } else if (radioMode == RadioState::DATA || radioMode == RadioState::DATA_R) {
                int subMode = m_radioState->dataSubModeB();
                switch (subMode) {
                case 1:
                    m_textDecodeWindowSub->setOperatingMode(TextDecodeWindow::ModeAFSK);
                    break;
                case 2:
                    m_textDecodeWindowSub->setOperatingMode(TextDecodeWindow::ModeFSK);
                    break;
                case 3:
                    m_textDecodeWindowSub->setOperatingMode(TextDecodeWindow::ModePSK);
                    break;
                default:
                    m_textDecodeWindowSub->setOperatingMode(TextDecodeWindow::ModeData);
                    break;
                }
                // Sync current data rate from radio
                int dr = m_radioState->dataRateB();
                if (dr >= 0)
                    m_textDecodeWindowSub->setDataRate(dr);
            } else if (radioMode == RadioState::LSB || radioMode == RadioState::USB) {
                m_textDecodeWindowSub->setOperatingMode(TextDecodeWindow::ModeSSB);
            } else {
                m_textDecodeWindowSub->setOperatingMode(TextDecodeWindow::ModeOther);
            }
            // Show window and enable decode
            m_textDecodeWindowSub->show();
            if (!m_textDecodeWindowSub->isDecodeEnabled()) {
                m_textDecodeWindowSub->setDecodeEnabled(true);
                emit m_textDecodeWindowSub->enabledChanged(true);
            }
        }
        break;
    }
}

void MainWindow::onSubRxButtonRightClicked(int index) {
    if (!m_connectionController->isConnected())
        return;

    switch (index) {
    case 2: // LINE OUT → VFO LINK toggle
    {
        bool linked = m_radioState->vfoLink();
        m_connectionController->sendCAT(QString("LN%1;").arg(linked ? 0 : 1));
        break;
    }
    case 3: // AFX - same as left-click (cycle)
        onSubRxButtonClicked(3);
        break;
    case 4: // AGC Sub - toggle ON/OFF
    {
        RadioState::AGCSpeed current = m_radioState->agcSpeedB();
        if (current == RadioState::AGC_Off) {
            m_connectionController->sendCAT("GT$1;");
        } else {
            m_connectionController->sendCAT("GT$0;");
        }
        break;
    }
    case 5: // APF - cycle bandwidth (Sub RX)
        m_connectionController->sendCAT("AP$+;");
        break;
    default:
        break;
    }
}
