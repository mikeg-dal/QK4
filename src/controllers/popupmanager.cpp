#include "popupmanager.h"

#include "connectioncontroller.h"
#include "models/radiostate.h"
#include "spectrumcontroller.h"
#include "ui/popups/bandpopupwidget.h"
#include "ui/widgets/bottommenubar.h"
#include "ui/popups/buttonrowpopup.h"
#include "ui/popups/displaypopupwidget.h"
#include "ui/popups/fnpopupwidget.h"
#include "ui/dialogs/macrodialog.h"
#include "ui/widgets/vfowidget.h"
#include "utils/radioutils.h"

#include "settings/radiosettings.h"
#include "ui/popups/keyingweightpopup.h"
#include "ui/popups/lineinpopup.h"
#include "ui/popups/lineoutpopup.h"
#include "ui/popups/micconfigpopup.h"
#include "ui/popups/micinputpopup.h"
#include "ui/popups/rxeqpopupwidget.h"
#include "ui/popups/softwarelistpopup.h"
#include "ui/popups/ssbbwpopup.h"
#include "ui/popups/voxpopup.h"
#include "ui/styling/k4constants.h"
#include <QInputDialog>
#include <QLineEdit>
#include <QTimer>
#include <QVector>
#include <QPoint>
#include <QString>
#include <QWidget>

PopupManager::PopupManager(RadioState *radioState, ConnectionController *connection, SpectrumController *spectrum,
                           VFOWidget *vfoA, VFOWidget *vfoB, QWidget *parentWidget, QObject *parent)
    : QObject(parent), m_radioState(radioState), m_connection(connection), m_spectrum(spectrum), m_vfoA(vfoA),
      m_vfoB(vfoB), m_parentWidget(parentWidget), m_bandPopup(new BandPopupWidget(parentWidget)),
      m_displayPopup(new DisplayPopupWidget(parentWidget)), m_fnPopup(new FnPopupWidget(parentWidget)),
      m_macroDialog(new MacroDialog(parentWidget)), m_mainRxRow(new ButtonRowPopup(parentWidget)),
      m_subRxRow(new ButtonRowPopup(parentWidget)), m_txRow(new ButtonRowPopup(parentWidget)),
      m_rxEqPopup(new RxEqPopupWidget("RX GRAPHIC EQUALIZER", K4Styles::Colors::VfoACyan, parentWidget)),
      m_txEqPopup(new RxEqPopupWidget("TX GRAPHIC EQUALIZER", K4Styles::Colors::AccentAmber, parentWidget)),
      m_rxEqDebounce(new QTimer(this)), m_txEqDebounce(new QTimer(this)),
      m_lineOutPopup(new LineOutPopupWidget(parentWidget)), m_lineInPopup(new LineInPopupWidget(parentWidget)),
      m_micInputPopup(new MicInputPopupWidget(parentWidget)), m_micConfigPopup(new MicConfigPopupWidget(parentWidget)),
      m_voxPopup(new VoxPopupWidget(parentWidget)), m_ssbBwPopup(new SsbBwPopupWidget(parentWidget)),
      m_keyingWeightPopup(new KeyingWeightPopupWidget(parentWidget)),
      m_softwareListPopup(new SoftwareListPopupWidget(parentWidget)) {

    m_macroDialog->hide();

    // --- Button-row popups (Main RX, Sub RX, TX) ---
    // Initial labels. Alternate color: amber when the right-click has its
    // own function, white when primary and alternate are both informational.
    m_mainRxRow->setButtonLabel(0, "ANT", "CFG", false);
    m_mainRxRow->setButtonLabel(1, "RX", "EQ", false);
    m_mainRxRow->setButtonLabel(2, "LINE OUT", "VFO LINK");
    m_mainRxRow->setButtonLabel(3, "AFX OFF", "OFF");
    m_mainRxRow->setButtonLabel(4, "AGC-S", "ON");
    m_mainRxRow->setButtonLabel(5, "APF", "OFF");
    m_mainRxRow->setButtonLabel(6, "TEXT", "DECODE", false);
    connect(m_mainRxRow, &ButtonRowPopup::closed, this, [this]() {
        if (m_bottomMenuBar)
            m_bottomMenuBar->setMainRxActive(false);
    });
    connect(m_mainRxRow, &ButtonRowPopup::buttonClicked, this, &PopupManager::mainRxButtonClicked);
    connect(m_mainRxRow, &ButtonRowPopup::buttonRightClicked, this, &PopupManager::mainRxButtonRightClicked);

    m_subRxRow->setButtonLabel(0, "ANT", "CFG", false);
    m_subRxRow->setButtonLabel(1, "RX", "EQ", false);
    m_subRxRow->setButtonLabel(2, "LINE OUT", "VFO LINK");
    m_subRxRow->setButtonLabel(3, "AFX OFF", "OFF");
    m_subRxRow->setButtonLabel(4, "AGC-S", "ON");
    m_subRxRow->setButtonLabel(5, "APF", "OFF");
    m_subRxRow->setButtonLabel(6, "TEXT", "DECODE", false);
    connect(m_subRxRow, &ButtonRowPopup::closed, this, [this]() {
        if (m_bottomMenuBar)
            m_bottomMenuBar->setSubRxActive(false);
    });
    connect(m_subRxRow, &ButtonRowPopup::buttonClicked, this, &PopupManager::subRxButtonClicked);
    connect(m_subRxRow, &ButtonRowPopup::buttonRightClicked, this, &PopupManager::subRxButtonRightClicked);

    m_txRow->setButtonLabel(0, "ANT", "CFG", false);
    m_txRow->setButtonLabel(1, "TX", "EQ", false);
    m_txRow->setButtonLabel(2, "LINE", "IN", false);
    m_txRow->setButtonLabel(3, "MIC INP", "MIC CFG", true);
    m_txRow->setButtonLabel(4, "VOX GN", "ANTIVOX", true);
    m_txRow->setButtonLabel(5, "SSB BW", "2.8k", false);
    m_txRow->setButtonLabel(6, "ESSB", "OFF", false);
    connect(m_txRow, &ButtonRowPopup::closed, this, [this]() {
        if (m_bottomMenuBar)
            m_bottomMenuBar->setTxActive(false);
    });
    connect(m_txRow, &ButtonRowPopup::buttonClicked, this, &PopupManager::txButtonClicked);
    connect(m_txRow, &ButtonRowPopup::buttonRightClicked, this, &PopupManager::txButtonRightClicked);

    // --- Band popup ---
    connect(m_bandPopup, &BandPopupWidget::bandSelected, this, &PopupManager::bandSelected);
    connect(m_bandPopup, &BandPopupWidget::closed, this, [this]() {
        if (m_bottomMenuBar)
            m_bottomMenuBar->setBandActive(false);
    });

    // --- Display popup ---
    wireDisplayPopup();

    // --- RadioState → DisplayPopup state sync ---
    // These used to live in MainWindow::setupRadioStateWiring; they
    // logically belong with the popup that consumes them.
    connect(m_radioState, &RadioState::dualPanModeLcdChanged, m_displayPopup, &DisplayPopupWidget::setDualPanModeLcd);
    connect(m_radioState, &RadioState::dualPanModeExtChanged, m_displayPopup, &DisplayPopupWidget::setDualPanModeExt);
    connect(m_radioState, &RadioState::displayModeLcdChanged, m_displayPopup, &DisplayPopupWidget::setDisplayModeLcd);
    connect(m_radioState, &RadioState::displayModeExtChanged, m_displayPopup, &DisplayPopupWidget::setDisplayModeExt);
    connect(m_radioState, &RadioState::waterfallColorChanged, m_displayPopup, &DisplayPopupWidget::setWaterfallColor);
    connect(m_radioState, &RadioState::averagingChanged, m_displayPopup, &DisplayPopupWidget::setAveraging);
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
    connect(m_radioState, &RadioState::spanChanged, this,
            [this](int spanHz) { m_displayPopup->setSpanValueA(spanHz / 1000.0); });
    connect(m_radioState, &RadioState::spanBChanged, this,
            [this](int spanHz) { m_displayPopup->setSpanValueB(spanHz / 1000.0); });
    connect(m_radioState, &RadioState::refLevelChanged, m_displayPopup, &DisplayPopupWidget::setRefLevelValueA);
    connect(m_radioState, &RadioState::refLevelBChanged, m_displayPopup, &DisplayPopupWidget::setRefLevelValueB);

    // --- Fn popup ---
    connect(m_fnPopup, &FnPopupWidget::closed, this, [this]() {
        if (m_bottomMenuBar)
            m_bottomMenuBar->setFnActive(false);
    });
    connect(m_fnPopup, &FnPopupWidget::functionTriggered, this, &PopupManager::macroFunctionTriggered);

    wireEqPopups();
    wireLinePopups();
    wireMicPopups();
    wireVoxAndSsbPopups();
    wireKeyingWeightPopup();
    wireRxRowButtonLabels();

    // RadioState EQ echoes → refresh popup bands.
    connect(m_radioState, &RadioState::rxEqChanged, this,
            [this]() { m_rxEqPopup->setAllBands(m_radioState->rxEqBands()); });
    connect(m_radioState, &RadioState::txEqChanged, this,
            [this]() { m_txEqPopup->setAllBands(m_radioState->txEqBands()); });
}

PopupManager::~PopupManager() {
    disconnect(this);
}

void PopupManager::setBottomMenuBar(BottomMenuBar *bottomMenuBar) {
    m_bottomMenuBar = bottomMenuBar;
}

void PopupManager::setVfos(VFOWidget *vfoA, VFOWidget *vfoB) {
    m_vfoA = vfoA;
    m_vfoB = vfoB;
}

void PopupManager::toggleBand() {
    if (!m_bottomMenuBar)
        return;
    const bool wasVisible = m_bandPopup->isVisibleOrJustHidden();
    closeOwnedPopups();
    if (!wasVisible) {
        m_bandPopup->showAboveButton(m_bottomMenuBar->bandButton());
        m_bottomMenuBar->setBandActive(true);
    }
}

void PopupManager::toggleDisplay() {
    if (!m_bottomMenuBar)
        return;
    const bool wasVisible = m_displayPopup->isVisibleOrJustHidden();
    closeOwnedPopups();
    if (!wasVisible) {
        m_displayPopup->showAboveButton(m_bottomMenuBar->displayButton());
        m_bottomMenuBar->setDisplayActive(true);
    }
}

void PopupManager::toggleFn() {
    if (!m_bottomMenuBar)
        return;
    const bool wasVisible = m_fnPopup->isVisibleOrJustHidden();
    closeOwnedPopups();
    if (!wasVisible) {
        m_fnPopup->showAboveButton(m_bottomMenuBar->fnButton());
        m_bottomMenuBar->setFnActive(true);
    }
}

void PopupManager::openMacroDialog() {
    closeOwnedPopups();

    // Size the macro dialog to fill the spectrum container (same overlay
    // positioning the menu overlay uses).
    if (m_spectrum->spectrumContainer()) {
        const QPoint pos = m_spectrum->spectrumContainer()->mapTo(m_parentWidget, QPoint(0, 0));
        m_macroDialog->setGeometry(pos.x(), pos.y(), m_spectrum->spectrumContainer()->width(),
                                   m_spectrum->spectrumContainer()->height());
    }
    m_macroDialog->show();
    m_macroDialog->raise();
    m_macroDialog->setFocus();
}

void PopupManager::openSoftwareList() {
    if (!m_bottomMenuBar)
        return;
    m_softwareListPopup->setVersions(m_radioState->firmwareVersions());
    m_softwareListPopup->showAboveButton(m_bottomMenuBar->fnButton());
}

void PopupManager::closeOwnedPopups() {
    if (m_bandPopup->isVisible()) {
        m_bandPopup->hidePopup();
        if (m_bottomMenuBar)
            m_bottomMenuBar->setBandActive(false);
    }
    if (m_displayPopup->isVisible()) {
        m_displayPopup->hidePopup();
        if (m_bottomMenuBar)
            m_bottomMenuBar->setDisplayActive(false);
    }
    if (m_fnPopup->isVisible()) {
        m_fnPopup->hidePopup();
        if (m_bottomMenuBar)
            m_bottomMenuBar->setFnActive(false);
    }
    if (m_mainRxRow->isVisible()) {
        m_mainRxRow->hidePopup();
        if (m_bottomMenuBar)
            m_bottomMenuBar->setMainRxActive(false);
    }
    if (m_subRxRow->isVisible()) {
        m_subRxRow->hidePopup();
        if (m_bottomMenuBar)
            m_bottomMenuBar->setSubRxActive(false);
    }
    if (m_txRow->isVisible()) {
        m_txRow->hidePopup();
        if (m_bottomMenuBar)
            m_bottomMenuBar->setTxActive(false);
    }
    if (m_rxEqPopup->isVisible())
        m_rxEqPopup->hidePopup();
    if (m_txEqPopup->isVisible())
        m_txEqPopup->hidePopup();
    if (m_lineOutPopup->isVisible())
        m_lineOutPopup->hidePopup();
    if (m_lineInPopup->isVisible())
        m_lineInPopup->hidePopup();
    if (m_micInputPopup->isVisible())
        m_micInputPopup->hidePopup();
    if (m_micConfigPopup->isVisible())
        m_micConfigPopup->hidePopup();
    if (m_voxPopup->isVisible())
        m_voxPopup->hidePopup();
    if (m_ssbBwPopup->isVisible())
        m_ssbBwPopup->hidePopup();
    if (m_keyingWeightPopup->isVisible())
        m_keyingWeightPopup->hidePopup();
    if (m_softwareListPopup->isVisible())
        m_softwareListPopup->hidePopup();
}

void PopupManager::toggleMainRx() {
    if (!m_bottomMenuBar)
        return;
    const bool wasVisible = m_mainRxRow->isVisibleOrJustHidden();
    closeOwnedPopups();
    if (!wasVisible) {
        m_mainRxRow->showAboveButton(m_bottomMenuBar->mainRxButton());
        m_bottomMenuBar->setMainRxActive(true);
    }
}

void PopupManager::toggleSubRx() {
    if (!m_bottomMenuBar)
        return;
    const bool wasVisible = m_subRxRow->isVisibleOrJustHidden();
    closeOwnedPopups();
    if (!wasVisible) {
        m_subRxRow->showAboveButton(m_bottomMenuBar->subRxButton());
        m_bottomMenuBar->setSubRxActive(true);
    }
}

void PopupManager::toggleTx() {
    if (!m_bottomMenuBar)
        return;
    const bool wasVisible = m_txRow->isVisibleOrJustHidden();
    closeOwnedPopups();
    if (!wasVisible) {
        m_txRow->showAboveButton(m_bottomMenuBar->txButton());
        m_bottomMenuBar->setTxActive(true);
    }
}

QWidget *PopupManager::anchorForLane(ButtonRowLane lane) const {
    switch (lane) {
    case ButtonRowLane::MainRx:
        return m_mainRxRow;
    case ButtonRowLane::SubRx:
        return m_subRxRow;
    case ButtonRowLane::Tx:
        return m_txRow;
    }
    return nullptr;
}

void PopupManager::setMainRxButtonLabel(int index, const QString &primary, const QString &alternate,
                                        bool alternateIsAmber) {
    m_mainRxRow->setButtonLabel(index, primary, alternate, alternateIsAmber);
}

void PopupManager::setSubRxButtonLabel(int index, const QString &primary, const QString &alternate,
                                       bool alternateIsAmber) {
    m_subRxRow->setButtonLabel(index, primary, alternate, alternateIsAmber);
}

void PopupManager::setTxButtonLabel(int index, const QString &primary, const QString &alternate,
                                    bool alternateIsAmber) {
    m_txRow->setButtonLabel(index, primary, alternate, alternateIsAmber);
}

void PopupManager::hideMacroDialog() {
    m_macroDialog->hide();
}

int PopupManager::bandNumberForName(const QString &bandName) const {
    return m_bandPopup->getBandNumber(bandName);
}

void PopupManager::setSelectedBandByNumber(int bandNum) {
    m_bandPopup->setSelectedBandByNumber(bandNum);
}

void PopupManager::wireDisplayPopup() {
    connect(m_displayPopup, &DisplayPopupWidget::closed, this, [this]() {
        if (m_bottomMenuBar)
            m_bottomMenuBar->setDisplayActive(false);
    });

    // DisplayPopup pan mode changed → panadapter display mode.
    // (K4 doesn't echo #DPM commands, so DisplayPopup notifies us directly.)
    connect(m_displayPopup, &DisplayPopupWidget::dualPanModeChanged, this, [this](int mode) {
        switch (mode) {
        case 0:
            m_spectrum->setPanadapterMode(SpectrumController::PanadapterMode::MainOnly);
            break;
        case 1:
            m_spectrum->setPanadapterMode(SpectrumController::PanadapterMode::SubOnly);
            break;
        case 2:
            m_spectrum->setPanadapterMode(SpectrumController::PanadapterMode::Dual);
            break;
        }
    });

    // DisplayPopup-generated raw CAT commands → connection controller.
    connect(m_displayPopup, &DisplayPopupWidget::catCommandRequested, this,
            [this](const QString &cmd) { m_connection->sendCAT(cmd); });
    connect(m_displayPopup, &DisplayPopupWidget::cursorModeChanged, this, [this](bool vfoB, int mode) {
        if (vfoB)
            m_radioState->setVfoBCursor(mode);
        else
            m_radioState->setVfoACursor(mode);
    });

    // Averaging control +/- → local only (our smoothing differs from K4's).
    connect(m_displayPopup, &DisplayPopupWidget::averagingIncrementRequested, this, [this]() {
        const int next = qMin(m_radioState->averaging() + 1, 20);
        m_radioState->setAveraging(next);
    });
    connect(m_displayPopup, &DisplayPopupWidget::averagingDecrementRequested, this, [this]() {
        const int next = qMax(m_radioState->averaging() - 1, 1);
        m_radioState->setAveraging(next);
    });

    // DDC NB level control +/- → CAT.
    connect(m_displayPopup, &DisplayPopupWidget::nbLevelIncrementRequested, this, [this]() {
        const int next = qMin(m_radioState->ddcNbLevel() + 1, 14);
        m_connection->sendCAT(QString("#NBL$%1;").arg(next, 2, 10, QChar('0')));
    });
    connect(m_displayPopup, &DisplayPopupWidget::nbLevelDecrementRequested, this, [this]() {
        const int next = qMax(m_radioState->ddcNbLevel() - 1, 0);
        m_connection->sendCAT(QString("#NBL$%1;").arg(next, 2, 10, QChar('0')));
    });

    // Waterfall height +/- (LCD or EXT depending on popup selection).
    auto adjustWaterfallHeight = [this](int delta) {
        const bool isExt = m_displayPopup->isExtEnabled() && !m_displayPopup->isLcdEnabled();
        const int current = isExt ? m_radioState->waterfallHeightExt() : m_radioState->waterfallHeight();
        const int next = qBound(10, current + delta, 90);
        const QString cmd =
            isExt ? QString("#HWFH%1;").arg(next, 2, 10, QChar('0')) : QString("#WFH%1;").arg(next, 2, 10, QChar('0'));
        m_connection->sendCAT(cmd);
        // Optimistic updates — K4 may not echo this command.
        if (!isExt) {
            m_radioState->setWaterfallHeight(next);
            m_spectrum->setWaterfallHeight(next);
            m_displayPopup->setWaterfallHeight(next);
            m_vfoA->setMiniPanWaterfallHeight(next);
            m_vfoB->setMiniPanWaterfallHeight(next);
        } else {
            m_radioState->setWaterfallHeightExt(next);
            m_displayPopup->setWaterfallHeightExt(next);
        }
    };
    connect(m_displayPopup, &DisplayPopupWidget::waterfallHeightIncrementRequested, this,
            [adjustWaterfallHeight]() { adjustWaterfallHeight(+1); });
    connect(m_displayPopup, &DisplayPopupWidget::waterfallHeightDecrementRequested, this,
            [adjustWaterfallHeight]() { adjustWaterfallHeight(-1); });

    // Span +/- (per-VFO selection).
    auto adjustSpan = [this](bool increment) {
        const bool vfoA = m_displayPopup->isVfoAEnabled();
        const bool vfoB = m_displayPopup->isVfoBEnabled();
        const int currentSpan = (vfoB && !vfoA) ? m_radioState->spanHzB() : m_radioState->spanHz();
        const int newSpan =
            increment ? RadioUtils::getNextSpanUp(currentSpan) : RadioUtils::getNextSpanDown(currentSpan);
        if (newSpan == currentSpan)
            return;
        if (vfoA) {
            m_radioState->setSpanHz(newSpan);
            m_connection->sendCAT(QString("#SPN%1;").arg(newSpan));
        }
        if (vfoB) {
            m_radioState->setSpanHzB(newSpan);
            m_connection->sendCAT(QString("#SPN$%1;").arg(newSpan));
        }
    };
    connect(m_displayPopup, &DisplayPopupWidget::spanIncrementRequested, this, [adjustSpan]() { adjustSpan(true); });
    connect(m_displayPopup, &DisplayPopupWidget::spanDecrementRequested, this, [adjustSpan]() { adjustSpan(false); });

    // Scale +/- (global — no A/B variants).
    auto adjustScale = [this](int delta) {
        int currentScale = m_radioState->scale();
        if (currentScale < 0)
            currentScale = 75; // Default if not yet received
        const int newScale = qBound(10, currentScale + delta, 150);
        if (newScale == currentScale)
            return;
        m_connection->sendCAT(QString("#SCL%1;").arg(newScale));
        // Scale is global and may not echo back; update optimistically.
        m_radioState->setScale(newScale);
    };
    connect(m_displayPopup, &DisplayPopupWidget::scaleIncrementRequested, this, [adjustScale]() { adjustScale(+1); });
    connect(m_displayPopup, &DisplayPopupWidget::scaleDecrementRequested, this, [adjustScale]() { adjustScale(-1); });

    // Ref level +/- (per-VFO, 1 dB step, range -200..60).
    auto adjustRefLevel = [this](int delta) {
        const bool vfoA = m_displayPopup->isVfoAEnabled();
        const bool vfoB = m_displayPopup->isVfoBEnabled();
        if (vfoA) {
            const int newLevel = qBound(-200, m_radioState->refLevel() + delta, 60);
            if (newLevel != m_radioState->refLevel()) {
                m_radioState->setRefLevel(newLevel);
                m_connection->sendCAT(QString("#REF%1;").arg(newLevel));
            }
        }
        if (vfoB) {
            const int newLevel = qBound(-200, m_radioState->refLevelB() + delta, 60);
            if (newLevel != m_radioState->refLevelB()) {
                m_radioState->setRefLevelB(newLevel);
                m_connection->sendCAT(QString("#REF$%1;").arg(newLevel));
            }
        }
    };
    connect(m_displayPopup, &DisplayPopupWidget::refLevelIncrementRequested, this,
            [adjustRefLevel]() { adjustRefLevel(+1); });
    connect(m_displayPopup, &DisplayPopupWidget::refLevelDecrementRequested, this,
            [adjustRefLevel]() { adjustRefLevel(-1); });
}

void PopupManager::wireEqPopups() {
    // Create RX EQ popup (Main RX - cyan theme)
    connect(m_rxEqPopup, &RxEqPopupWidget::closed, this, [this]() {
        // Close the MAIN RX button row popup when EQ popup closes
    });

    // Debounce timer for RX EQ - sends command 100ms after last slider change
    m_rxEqDebounce->setSingleShot(true);
    m_rxEqDebounce->setInterval(100);
    connect(m_rxEqDebounce, &QTimer::timeout, this,
            [this]() { m_connection->sendCAT(RadioUtils::buildEqCommand("RE", m_radioState->rxEqBands())); });

    connect(m_rxEqPopup, &RxEqPopupWidget::bandValueChanged, this, [this](int bandIndex, int dB) {
        // Update optimistic state immediately (UI stays responsive)
        m_radioState->setRxEqBand(bandIndex, dB);
        // Restart debounce timer - will send after 100ms of no changes
        m_rxEqDebounce->start();
    });
    connect(m_rxEqPopup, &RxEqPopupWidget::flatRequested, this, [this]() {
        // Reset all bands to 0 and send CAT command
        QVector<int> flat(8, 0);
        m_radioState->setRxEqBands(flat);
        m_connection->sendCAT(RadioUtils::buildEqCommand("RE", flat));
    });

    // Preset load: get preset from RadioSettings, apply to sliders, send CAT
    connect(m_rxEqPopup, &RxEqPopupWidget::presetLoadRequested, this, [this](int index) {
        EqPreset preset = RadioSettings::instance()->rxEqPreset(index);
        if (!preset.isEmpty() && preset.bands.size() == 8) {
            m_rxEqPopup->setAllBands(preset.bands);
            m_radioState->setRxEqBands(preset.bands);

            m_connection->sendCAT(RadioUtils::buildEqCommand("RE", preset.bands));
        }
    });

    // Preset save: show name dialog, save current EQ to preset
    connect(m_rxEqPopup, &RxEqPopupWidget::presetSaveRequested, this, [this](int index) {
        EqPreset existing = RadioSettings::instance()->rxEqPreset(index);
        QString defaultName = existing.name.isEmpty() ? QString("Preset %1").arg(index + 1) : existing.name;

        // Store current EQ bands before dialog (popup may close)
        QVector<int> currentBands = m_radioState->rxEqBands();

        bool ok;
        QString name =
            QInputDialog::getText(m_parentWidget, "Save Preset", "Preset name:", QLineEdit::Normal, defaultName, &ok);

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
    connect(m_txEqPopup, &RxEqPopupWidget::closed, this, [this]() {
        // Close the TX button row popup when EQ popup closes
    });

    // Debounce timer for TX EQ - sends command 100ms after last slider change
    m_txEqDebounce->setSingleShot(true);
    m_txEqDebounce->setInterval(100);
    connect(m_txEqDebounce, &QTimer::timeout, this,
            [this]() { m_connection->sendCAT(RadioUtils::buildEqCommand("TE", m_radioState->txEqBands())); });

    connect(m_txEqPopup, &RxEqPopupWidget::bandValueChanged, this, [this](int bandIndex, int dB) {
        // Update optimistic state immediately (UI stays responsive)
        m_radioState->setTxEqBand(bandIndex, dB);
        // Restart debounce timer - will send after 100ms of no changes
        m_txEqDebounce->start();
    });
    connect(m_txEqPopup, &RxEqPopupWidget::flatRequested, this, [this]() {
        // Reset all bands to 0 and send CAT command
        QVector<int> flat(8, 0);
        m_radioState->setTxEqBands(flat);
        m_connection->sendCAT(RadioUtils::buildEqCommand("TE", flat));
    });

    // TX EQ Preset load: get preset from RadioSettings, apply to sliders, send CAT
    connect(m_txEqPopup, &RxEqPopupWidget::presetLoadRequested, this, [this](int index) {
        EqPreset preset = RadioSettings::instance()->txEqPreset(index);
        if (!preset.isEmpty() && preset.bands.size() == 8) {
            m_txEqPopup->setAllBands(preset.bands);
            m_radioState->setTxEqBands(preset.bands);

            m_connection->sendCAT(RadioUtils::buildEqCommand("TE", preset.bands));
        }
    });

    // TX EQ Preset save: show name dialog, save current EQ to preset
    connect(m_txEqPopup, &RxEqPopupWidget::presetSaveRequested, this, [this](int index) {
        EqPreset existing = RadioSettings::instance()->txEqPreset(index);
        QString defaultName = existing.name.isEmpty() ? QString("Preset %1").arg(index + 1) : existing.name;

        // Store current EQ bands before dialog (popup may close)
        QVector<int> currentBands = m_radioState->txEqBands();

        bool ok;
        QString name = QInputDialog::getText(m_parentWidget, "Save TX Preset", "Preset name:", QLineEdit::Normal,
                                             defaultName, &ok);

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
}

void PopupManager::wireLinePopups() {
    // Create Line Out popup (shared by MAIN RX and SUB RX)
    connect(m_lineOutPopup, &LineOutPopupWidget::leftLevelChanged, this, [this](int level) {
        if (!m_connection->isConnected())
            return;
        // Send full LO command with current state
        QString cmd = QString("LO%1%2%3;")
                          .arg(level, 3, 10, QChar('0'))
                          .arg(m_radioState->lineOutRight(), 3, 10, QChar('0'))
                          .arg(m_radioState->lineOutRightEqualsLeft() ? 1 : 0);
        m_connection->sendCAT(cmd);
    });
    connect(m_lineOutPopup, &LineOutPopupWidget::rightLevelChanged, this, [this](int level) {
        if (!m_connection->isConnected())
            return;
        QString cmd = QString("LO%1%2%3;")
                          .arg(m_radioState->lineOutLeft(), 3, 10, QChar('0'))
                          .arg(level, 3, 10, QChar('0'))
                          .arg(m_radioState->lineOutRightEqualsLeft() ? 1 : 0);
        m_connection->sendCAT(cmd);
    });
    connect(m_lineOutPopup, &LineOutPopupWidget::rightEqualsLeftChanged, this, [this](bool enabled) {
        if (!m_connection->isConnected())
            return;
        int left = m_radioState->lineOutLeft();
        int right = enabled ? left : m_radioState->lineOutRight();
        QString cmd =
            QString("LO%1%2%3;").arg(left, 3, 10, QChar('0')).arg(right, 3, 10, QChar('0')).arg(enabled ? 1 : 0);
        m_connection->sendCAT(cmd);
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
    connect(m_lineInPopup, &LineInPopupWidget::soundCardLevelChanged, this, [this](int level) {
        if (!m_connection->isConnected())
            return;
        m_radioState->setLineInSoundCard(level);
        QString cmd = QString("LI%1%2%3;")
                          .arg(level, 3, 10, QChar('0'))
                          .arg(m_radioState->lineInJack(), 3, 10, QChar('0'))
                          .arg(m_radioState->lineInSource());
        m_connection->sendCAT(cmd);
    });
    connect(m_lineInPopup, &LineInPopupWidget::lineInJackLevelChanged, this, [this](int level) {
        if (!m_connection->isConnected())
            return;
        m_radioState->setLineInJack(level);
        QString cmd = QString("LI%1%2%3;")
                          .arg(m_radioState->lineInSoundCard(), 3, 10, QChar('0'))
                          .arg(level, 3, 10, QChar('0'))
                          .arg(m_radioState->lineInSource());
        m_connection->sendCAT(cmd);
    });
    connect(m_lineInPopup, &LineInPopupWidget::sourceChanged, this, [this](int source) {
        if (!m_connection->isConnected())
            return;
        m_radioState->setLineInSource(source);
        QString cmd = QString("LI%1%2%3;")
                          .arg(m_radioState->lineInSoundCard(), 3, 10, QChar('0'))
                          .arg(m_radioState->lineInJack(), 3, 10, QChar('0'))
                          .arg(source);
        m_connection->sendCAT(cmd);
    });
    // Connect RadioState to update popup when K4 sends LI response
    connect(m_radioState, &RadioState::lineInChanged, this, [this]() {
        if (m_lineInPopup) {
            m_lineInPopup->setSoundCardLevel(m_radioState->lineInSoundCard());
            m_lineInPopup->setLineInJackLevel(m_radioState->lineInJack());
            m_lineInPopup->setSource(m_radioState->lineInSource());
        }
    });
}

void PopupManager::wireMicPopups() {
    // Create Mic Input popup (TX menu button index 3, left-click)
    connect(m_micInputPopup, &MicInputPopupWidget::inputChanged, this, [this](int input) {
        if (!m_connection->isConnected())
            return;
        m_radioState->setMicInput(input);
        m_connection->sendCAT(QString("MI%1;").arg(input));
    });
    // Connect RadioState to update popup when K4 sends MI response
    connect(m_radioState, &RadioState::micInputChanged, this, [this](int input) {
        if (m_micInputPopup) {
            m_micInputPopup->setCurrentInput(input);
        }
    });

    // Create Mic Config popup (TX menu button index 3, right-click)
    connect(m_micConfigPopup, &MicConfigPopupWidget::biasChanged, this, [this](int bias) {
        if (!m_connection->isConnected())
            return;
        // Use individual SET command based on mic type
        if (m_micConfigPopup->micType() == MicConfigPopupWidget::Front) {
            m_radioState->setMicFrontBias(bias);
            m_connection->sendCAT(QString("MSB%1;").arg(bias));
        } else {
            m_radioState->setMicRearBias(bias);
            m_connection->sendCAT(QString("MSE%1;").arg(bias));
        }
    });
    connect(m_micConfigPopup, &MicConfigPopupWidget::preampChanged, this, [this](int preamp) {
        if (!m_connection->isConnected())
            return;
        if (m_micConfigPopup->micType() == MicConfigPopupWidget::Front) {
            m_radioState->setMicFrontPreamp(preamp);
            m_connection->sendCAT(QString("MSA%1;").arg(preamp));
        } else {
            m_radioState->setMicRearPreamp(preamp);
            m_connection->sendCAT(QString("MSD%1;").arg(preamp));
        }
    });
    connect(m_micConfigPopup, &MicConfigPopupWidget::buttonsChanged, this, [this](int buttons) {
        if (!m_connection->isConnected())
            return;
        // Buttons only applies to Front mic
        m_radioState->setMicFrontButtons(buttons);
        m_connection->sendCAT(QString("MSC%1;").arg(buttons));
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
}

void PopupManager::wireVoxAndSsbPopups() {
    // Create VOX Gain / Anti-VOX popup (TX menu button index 4)
    connect(m_voxPopup, &VoxPopupWidget::valueChanged, this, [this](int value) {
        if (!m_connection->isConnected())
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
            m_connection->sendCAT(QString("VG%1%2;").arg(modeChar).arg(value, 3, 10, QChar('0')));
        } else {
            // Anti-VOX: VInnn
            m_radioState->setAntiVox(value);
            m_connection->sendCAT(QString("VI%1;").arg(value, 3, 10, QChar('0')));
        }
    });
    connect(m_voxPopup, &VoxPopupWidget::voxToggled, this, [this](bool enabled) {
        if (!m_connection->isConnected())
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
        m_connection->sendCAT(QString("VX%1%2;").arg(modeChar).arg(enabled ? 1 : 0));
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
    connect(m_ssbBwPopup, &SsbBwPopupWidget::bandwidthChanged, this, [this](int bw) {
        if (!m_connection->isConnected())
            return;
        // ES command: ESnbb where n=essb mode, bb=bandwidth
        int essbMode = m_radioState->essbEnabled() ? 1 : 0;
        m_radioState->setSsbTxBw(bw);
        m_connection->sendCAT(QString("ES%1%2;").arg(essbMode).arg(bw, 2, 10, QChar('0')));
        // Update button label with new bandwidth (optimistic)
        if (m_txRow) {
            QString bwStr = QString("%1k").arg(bw / 10.0, 0, 'f', 1);
            m_txRow->setButtonLabel(5, "SSB BW", bwStr, false);
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
        if (m_txRow && mode != RadioState::CW && mode != RadioState::CW_R) {
            // Button 5: SSB BW with current bandwidth value (e.g., "2.8k" or "3.0k")
            if (bw >= 24 && bw <= 45) {
                QString bwStr = QString("%1k").arg(bw / 10.0, 0, 'f', 1);
                m_txRow->setButtonLabel(5, "SSB BW", bwStr, false);
            }
            // Button 6: ESSB toggle with ON/OFF state
            m_txRow->setButtonLabel(6, "ESSB", enabled ? "ON" : "OFF", false);
        }
        // Mode label "+"/"-" suffix refresh is handled by ModeLabelController
        // observing RadioState::essbChanged directly — no forwarding needed here.
    });
}

void PopupManager::wireKeyingWeightPopup() {
    // Create Keying Weight popup (TX menu button index 6 in CW mode)
    connect(m_keyingWeightPopup, &KeyingWeightPopupWidget::weightChanged, this, [this](int weight) {
        if (!m_connection->isConnected())
            return;
        QChar iambic = m_radioState->iambicMode().isNull() ? QChar('A') : m_radioState->iambicMode();
        QChar paddle = m_radioState->paddleOrientation().isNull() ? QChar('N') : m_radioState->paddleOrientation();
        m_radioState->setKeyingWeight(weight);
        m_connection->sendCAT(QString("KP%1%2%3;").arg(iambic).arg(paddle).arg(weight, 3, 10, QChar('0')));
        // Update button label with new weight value (optimistic)
        if (m_txRow) {
            QString weightStr = QString::number(weight / 100.0, 'f', 2);
            m_txRow->setButtonLabel(6, "WEIGHT", weightStr, false);
        }
    });

    // Connect keyerPaddleChanged to update CW button labels and weight popup
    connect(m_radioState, &RadioState::keyerPaddleChanged, this, [this](QChar iambic, QChar paddle, int weight) {
        auto mode = m_radioState->mode();
        if (m_txRow && (mode == RadioState::CW || mode == RadioState::CW_R)) {
            // Button 5: paddle orientation + iambic mode
            QString paddleStr = (paddle == 'R') ? "PDL REV" : "PDL NOR";
            QString iambicStr = QString("IAMB %1").arg(iambic);
            m_txRow->setButtonLabel(5, paddleStr, iambicStr, true);
            // Button 6: keying weight ratio
            if (weight >= 90 && weight <= 125) {
                QString weightStr = QString::number(weight / 100.0, 'f', 2);
                m_txRow->setButtonLabel(6, "WEIGHT", weightStr, false);
            }
        }
        // Update weight popup if visible
        if (m_keyingWeightPopup && m_keyingWeightPopup->isVisible() && weight >= 90 && weight <= 125) {
            m_keyingWeightPopup->setWeight(weight);
        }
    });
}

void PopupManager::showRxEqAbove(ButtonRowLane lane) {
    QWidget *anchor = anchorForLane(lane);
    m_rxEqPopup->setAllBands(m_radioState->rxEqBands());
    if (anchor)
        m_rxEqPopup->showAboveWidget(anchor);
}

void PopupManager::showTxEqAbove(ButtonRowLane lane) {
    QWidget *anchor = anchorForLane(lane);
    m_txEqPopup->setAllBands(m_radioState->txEqBands());
    if (anchor)
        m_txEqPopup->showAboveWidget(anchor);
}

void PopupManager::showLineOutAbove(ButtonRowLane lane) {
    QWidget *anchor = anchorForLane(lane);
    m_lineOutPopup->setLeftLevel(m_radioState->lineOutLeft());
    m_lineOutPopup->setRightLevel(m_radioState->lineOutRight());
    m_lineOutPopup->setRightEqualsLeft(m_radioState->lineOutRightEqualsLeft());
    if (anchor)
        m_lineOutPopup->showAboveWidget(anchor);
}

void PopupManager::showLineInAbove(ButtonRowLane lane) {
    QWidget *anchor = anchorForLane(lane);
    m_lineInPopup->setSoundCardLevel(m_radioState->lineInSoundCard());
    m_lineInPopup->setLineInJackLevel(m_radioState->lineInJack());
    m_lineInPopup->setSource(m_radioState->lineInSource());
    if (anchor)
        m_lineInPopup->showAboveWidget(anchor);
}

void PopupManager::showMicInputAbove(ButtonRowLane lane) {
    QWidget *anchor = anchorForLane(lane);
    m_micInputPopup->setCurrentInput(m_radioState->micInput());
    if (anchor)
        m_micInputPopup->showAboveWidget(anchor);
}

void PopupManager::showMicConfigAbove(ButtonRowLane lane) {
    QWidget *anchor = anchorForLane(lane);
    const int input = m_radioState->micInput();
    if (input == 2) // LINE IN only — no mic config
        return;
    const bool isFront = (input == 0 || input == 3); // 0=front, 3=front+line
    m_micConfigPopup->setMicType(isFront ? MicConfigPopupWidget::Front : MicConfigPopupWidget::Rear);
    if (isFront) {
        m_micConfigPopup->setBias(m_radioState->micFrontBias());
        m_micConfigPopup->setPreamp(m_radioState->micFrontPreamp());
        m_micConfigPopup->setButtons(m_radioState->micFrontButtons());
    } else {
        m_micConfigPopup->setBias(m_radioState->micRearBias());
        m_micConfigPopup->setPreamp(m_radioState->micRearPreamp());
    }
    if (anchor)
        m_micConfigPopup->showAboveWidget(anchor);
}

void PopupManager::showVoxGainAbove(ButtonRowLane lane) {
    QWidget *anchor = anchorForLane(lane);
    const bool isDataMode = (m_radioState->mode() == RadioState::DATA || m_radioState->mode() == RadioState::DATA_R);
    m_voxPopup->setPopupMode(VoxPopupWidget::VoxGain);
    m_voxPopup->setDataMode(isDataMode);
    m_voxPopup->setValue(m_radioState->voxGainForCurrentMode());
    m_voxPopup->setVoxEnabled(m_radioState->voxForCurrentMode());
    if (anchor)
        m_voxPopup->showAboveWidget(anchor);
}

void PopupManager::showAntiVoxAbove(ButtonRowLane lane) {
    QWidget *anchor = anchorForLane(lane);
    m_voxPopup->setPopupMode(VoxPopupWidget::AntiVox);
    m_voxPopup->setValue(m_radioState->antiVox());
    m_voxPopup->setVoxEnabled(m_radioState->voxForCurrentMode());
    if (anchor)
        m_voxPopup->showAboveWidget(anchor);
}

void PopupManager::showSsbBwAbove(ButtonRowLane lane) {
    QWidget *anchor = anchorForLane(lane);
    m_ssbBwPopup->setEssbEnabled(m_radioState->essbEnabled());
    const int bw = m_radioState->ssbTxBw();
    if (bw >= 24 && bw <= 45)
        m_ssbBwPopup->setBandwidth(bw);
    if (anchor)
        m_ssbBwPopup->showAboveWidget(anchor);
}

void PopupManager::showKeyingWeightAbove(ButtonRowLane lane) {
    QWidget *anchor = anchorForLane(lane);
    const int weight = m_radioState->keyingWeight();
    if (weight >= 90 && weight <= 125)
        m_keyingWeightPopup->setWeight(weight);
    if (anchor)
        m_keyingWeightPopup->showAboveWidget(anchor);
}

void PopupManager::wireRxRowButtonLabels() {
    // AFX (button 3, Main + Sub rows share same global state).
    connect(m_radioState, &RadioState::afxModeChanged, this, [this](int mode) {
        const QString primary = (mode == 0) ? "AFX OFF" : "AFX ON";
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
        if (m_mainRxRow)
            m_mainRxRow->setButtonLabel(3, primary, alternate);
        if (m_subRxRow)
            m_subRxRow->setButtonLabel(3, primary, alternate);
    });

    // AGC (button 4, per-VFO — Main uses GT, Sub uses GT$).
    auto agcLabel = [](RadioState::AGCSpeed speed) -> std::pair<QString, QString> {
        switch (speed) {
        case RadioState::AGC_Off:
            return {"AGC", "OFF"};
        case RadioState::AGC_Slow:
            return {"AGC-S", "ON"};
        case RadioState::AGC_Fast:
            return {"AGC-F", "ON"};
        }
        return {"AGC", "OFF"};
    };
    connect(m_radioState, &RadioState::processingChanged, this, [this, agcLabel]() {
        if (!m_mainRxRow)
            return;
        const auto [primary, alternate] = agcLabel(m_radioState->agcSpeed());
        m_mainRxRow->setButtonLabel(4, primary, alternate);
    });
    connect(m_radioState, &RadioState::processingChangedB, this, [this, agcLabel]() {
        if (!m_subRxRow)
            return;
        const auto [primary, alternate] = agcLabel(m_radioState->agcSpeedB());
        m_subRxRow->setButtonLabel(4, primary, alternate);
    });

    // APF (button 5, per-VFO). Also forwards to the VFO APF indicator —
    // same state drives both.
    auto apfLabel = [](bool enabled, int width) -> QString {
        if (!enabled)
            return "OFF";
        static const char *bwNames[] = {"30Hz", "50Hz", "150Hz"};
        return bwNames[qBound(0, width, 2)];
    };
    connect(m_radioState, &RadioState::apfChanged, this, [this, apfLabel](bool enabled, int width) {
        if (m_mainRxRow)
            m_mainRxRow->setButtonLabel(5, "APF", apfLabel(enabled, width));
        if (m_vfoA)
            m_vfoA->setApf(enabled, width);
    });
    connect(m_radioState, &RadioState::apfBChanged, this, [this, apfLabel](bool enabled, int width) {
        if (m_subRxRow)
            m_subRxRow->setButtonLabel(5, "APF", apfLabel(enabled, width));
        if (m_vfoB)
            m_vfoB->setApf(enabled, width);
    });
}
