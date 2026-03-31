#include "sidecontrolpanel.h"
#include "dualcontrolbutton.h"
#include "k4styles.h"
#include "monoverlay.h"
#include "baloverlay.h"
#include "../settings/radiosettings.h"
#include <QVBoxLayout>
#include <QSizePolicy>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QSpacerItem>
#include <QEvent>
#include <QIcon>
#include <QMouseEvent>

SideControlPanel::SideControlPanel(QWidget *parent) : QWidget(parent) {
    setupUi();
}

void SideControlPanel::setupUi() {
    setFixedWidth(K4Styles::Dimensions::SidePanelWidth);
    // Note: No explicit size policy - let Qt handle vertical expansion like RightSidePanel

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(K4Styles::Dimensions::PaddingSmall, K4Styles::Dimensions::PopupButtonSpacing,
                               K4Styles::Dimensions::PaddingSmall, K4Styles::Dimensions::PopupButtonSpacing);
    layout->setSpacing(4); // Default spacing between buttons in a group

    // ===== TX Function Buttons (2x3 grid) =====
    auto *txGrid = new QGridLayout();
    txGrid->setContentsMargins(0, 0, 0, 0);
    txGrid->setHorizontalSpacing(4);
    txGrid->setVerticalSpacing(8);

    // Row 0: TUNE, XMIT
    txGrid->addWidget(createTxFunctionButton("TUNE", "TUNE LP", m_tuneBtn), 0, 0);
    txGrid->addWidget(createTxFunctionButton("XMIT", "TEST", m_xmitBtn), 0, 1);

    // Row 1: ATU TUNE, VOX
    txGrid->addWidget(createTxFunctionButton("ATU\nTUNE", "ATU", m_atuTuneBtn), 1, 0);
    txGrid->addWidget(createTxFunctionButton("VOX", "QSK", m_voxBtn), 1, 1);

    // Row 2: ANT, RX ANT
    txGrid->addWidget(createTxFunctionButton("ANT", "REM ANT", m_antBtn), 2, 0);
    txGrid->addWidget(createTxFunctionButton("RX ANT", "SUB ANT", m_rxAntBtn), 2, 1);

    layout->addLayout(txGrid);

    // ===== Connect TX function button signals and install event filters =====
    // Left-click signals
    connect(m_tuneBtn, &QPushButton::clicked, this, &SideControlPanel::tuneClicked);
    connect(m_xmitBtn, &QPushButton::clicked, this, &SideControlPanel::xmitClicked);
    connect(m_atuTuneBtn, &QPushButton::clicked, this, &SideControlPanel::atuTuneClicked);
    connect(m_voxBtn, &QPushButton::clicked, this, &SideControlPanel::voxClicked);
    connect(m_antBtn, &QPushButton::clicked, this, &SideControlPanel::antClicked);
    connect(m_rxAntBtn, &QPushButton::clicked, this, &SideControlPanel::rxAntClicked);

    // Install event filters for right-click handling
    m_tuneBtn->installEventFilter(this);
    m_xmitBtn->installEventFilter(this);
    m_atuTuneBtn->installEventFilter(this);
    m_voxBtn->installEventFilter(this);
    m_antBtn->installEventFilter(this);
    m_rxAntBtn->installEventFilter(this);

    // ===== Spacing after TX buttons =====
    layout->addSpacing(16);

    // ===== Group 1: Global (CW/Power) - Orange bar =====
    m_wpmBtn = new DualControlButton(this);
    m_wpmBtn->setPrimaryLabel("WPM");
    m_wpmBtn->setPrimaryValue("--");
    m_wpmBtn->setAlternateLabel("PTCH");
    m_wpmBtn->setAlternateValue("--");
    m_wpmBtn->setContext(DualControlButton::Global);
    m_wpmBtn->setShowIndicator(true); // First button in group is active by default
    layout->addWidget(m_wpmBtn);

    m_pwrBtn = new DualControlButton(this);
    m_pwrBtn->setPrimaryLabel("PWR");
    m_pwrBtn->setPrimaryValue("--");
    m_pwrBtn->setAlternateLabel("DLY");
    m_pwrBtn->setAlternateValue("--");
    m_pwrBtn->setContext(DualControlButton::Global);
    m_pwrBtn->setShowIndicator(false); // Second button starts inactive
    layout->addWidget(m_pwrBtn);

    // ===== Spacing between groups =====
    layout->addSpacing(16);

    // ===== Group 2: Filter (BW/Shift) - Cyan bar - LINKED PAIR =====
    m_bwBtn = new DualControlButton(this);
    m_bwBtn->setPrimaryLabel("BW");
    m_bwBtn->setPrimaryValue("--");
    m_bwBtn->setAlternateLabel("HI");
    m_bwBtn->setAlternateValue("--");
    m_bwBtn->setContext(DualControlButton::MainRx);
    m_bwBtn->setShowIndicator(true); // First button in group is active
    layout->addWidget(m_bwBtn);

    m_shiftBtn = new DualControlButton(this);
    m_shiftBtn->setPrimaryLabel("SHFT");
    m_shiftBtn->setPrimaryValue("--");
    m_shiftBtn->setAlternateLabel("LO");
    m_shiftBtn->setAlternateValue("--");
    m_shiftBtn->setContext(DualControlButton::MainRx);
    m_shiftBtn->setShowIndicator(false); // Second button starts inactive
    layout->addWidget(m_shiftBtn);

    // ===== Spacing between groups =====
    layout->addSpacing(16);

    // ===== Group 3: RF/Squelch =====
    m_mainRfBtn = new DualControlButton(this);
    m_mainRfBtn->setPrimaryLabel("M.RF");
    m_mainRfBtn->setPrimaryValue("--");
    m_mainRfBtn->setAlternateLabel("M.SQL");
    m_mainRfBtn->setAlternateValue("--");
    m_mainRfBtn->setContext(DualControlButton::MainRx);
    m_mainRfBtn->setShowIndicator(true); // First button in group is active
    layout->addWidget(m_mainRfBtn);

    m_subSqlBtn = new DualControlButton(this);
    m_subSqlBtn->setPrimaryLabel("S.SQL");
    m_subSqlBtn->setPrimaryValue("--");
    m_subSqlBtn->setAlternateLabel("S.RF");
    m_subSqlBtn->setAlternateValue("--");
    m_subSqlBtn->setContext(DualControlButton::SubRx);
    m_subSqlBtn->setShowIndicator(false); // Second button starts inactive
    layout->addWidget(m_subSqlBtn);

    // ===== MON/NORM/BAL Buttons =====
    layout->addSpacing(10);

    // Wrap in container widget for proper layout sizing
    auto *swBtnContainer = new QWidget(this);
    swBtnContainer->setFixedHeight(K4Styles::Dimensions::ButtonHeightMini);
    auto *swBtnRow = new QHBoxLayout(swBtnContainer);
    swBtnRow->setContentsMargins(0, 0, 0, 0);
    swBtnRow->setSpacing(2);

    m_monBtn = new QPushButton("MON", swBtnContainer);
    m_monBtn->setStyleSheet(K4Styles::compactButton());
    m_monBtn->setFixedHeight(K4Styles::Dimensions::ButtonHeightMini);
    swBtnRow->addWidget(m_monBtn);

    m_normBtn = new QPushButton("NORM", swBtnContainer);
    m_normBtn->setStyleSheet(K4Styles::compactButton());
    m_normBtn->setFixedHeight(K4Styles::Dimensions::ButtonHeightMini);
    swBtnRow->addWidget(m_normBtn);

    m_balBtn = new QPushButton("BAL", swBtnContainer);
    m_balBtn->setStyleSheet(K4Styles::compactButton());
    m_balBtn->setFixedHeight(K4Styles::Dimensions::ButtonHeightMini);
    swBtnRow->addWidget(m_balBtn);

    layout->addWidget(swBtnContainer);

    // Create overlay widgets (initially hidden)
    m_monOverlay = new MonOverlay(this);
    m_balOverlay = new BalOverlay(this);

    // Connect MON button - toggles MON overlay
    connect(m_monBtn, &QPushButton::clicked, this, [this]() {
        emit swCommandRequested("SW128;");
        if (m_monOverlay->isVisible()) {
            m_monOverlay->hide();
        } else {
            m_balOverlay->hide(); // Close other overlay
            m_monOverlay->showOverGroup(m_wpmBtn, m_pwrBtn);
        }
    });

    // Connect NORM button - just sends command, no overlay
    connect(m_normBtn, &QPushButton::clicked, this, [this]() { emit swCommandRequested("SW129;"); });

    // Connect BAL button - sends SW130 and toggles BAL overlay
    connect(m_balBtn, &QPushButton::clicked, this, [this]() {
        emit swCommandRequested("SW130;");
        if (m_balOverlay->isVisible()) {
            m_balOverlay->hide();
        } else {
            m_monOverlay->hide(); // Close other overlay
            m_balOverlay->showOverGroup(m_mainRfBtn, m_subSqlBtn);
        }
    });

    // Connect overlay signals
    connect(m_monOverlay, &MonOverlay::levelChangeRequested, this, &SideControlPanel::monLevelChangeRequested);
    connect(m_balOverlay, &BalOverlay::balanceChangeRequested, this, &SideControlPanel::balChangeRequested);

    // ===== Volume Slider =====
    layout->addSpacing(10);

    m_volumeLabel = new QLabel("MAIN", this);
    m_volumeLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                     .arg(K4Styles::Colors::VfoACyan)
                                     .arg(K4Styles::Dimensions::FontSizeMedium));
    m_volumeLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_volumeLabel);

    m_volumeSlider = new QSlider(Qt::Horizontal, this);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(RadioSettings::instance()->volume()); // Restore from settings (default 45%)
    m_volumeSlider->setStyleSheet(
        K4Styles::sliderHorizontal(K4Styles::Colors::DarkBackground, K4Styles::Colors::VfoACyan));
    layout->addWidget(m_volumeSlider);

    connect(m_volumeSlider, &QSlider::valueChanged, this, &SideControlPanel::volumeChanged);

    // ===== Sub RX Volume Slider (VFO B) =====
    layout->addSpacing(6);

    m_subVolumeLabel = new QLabel("SUB", this);
    m_subVolumeLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                        .arg(K4Styles::Colors::VfoBGreen)
                                        .arg(K4Styles::Dimensions::FontSizeMedium));
    m_subVolumeLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_subVolumeLabel);

    m_subVolumeSlider = new QSlider(Qt::Horizontal, this);
    m_subVolumeSlider->setRange(0, 100);
    m_subVolumeSlider->setValue(RadioSettings::instance()->subVolume()); // Restore from settings (default 45%)
    m_subVolumeSlider->setStyleSheet(
        K4Styles::sliderHorizontal(K4Styles::Colors::DarkBackground, K4Styles::Colors::VfoBGreen));
    layout->addWidget(m_subVolumeSlider);

    connect(m_subVolumeSlider, &QSlider::valueChanged, this, &SideControlPanel::subVolumeChanged);

    // ===== Stretch to push status/icons to bottom =====
    layout->addStretch();

    // ===== Status Area (mirrors header data) =====
    m_timeLabel = new QLabel("00:00:00 Z", this);
    m_timeLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                   .arg(K4Styles::Colors::TextWhite)
                                   .arg(K4Styles::Dimensions::FontSizeLarge));
    layout->addWidget(m_timeLabel);

    m_powerSwrLabel = new QLabel("0.0W  1.0:1", this);
    m_powerSwrLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                       .arg(K4Styles::Colors::TextWhite)
                                       .arg(K4Styles::Dimensions::FontSizeLarge));
    layout->addWidget(m_powerSwrLabel);

    m_voltageCurrentLabel = new QLabel("--.-V  -.-A", this);
    m_voltageCurrentLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                             .arg(K4Styles::Colors::TextWhite)
                                             .arg(K4Styles::Dimensions::FontSizeLarge));
    layout->addWidget(m_voltageCurrentLabel);

    layout->addSpacing(K4Styles::Dimensions::PopupButtonSpacing);

    // ===== Icon Buttons at bottom =====
    auto *iconRow = new QHBoxLayout();
    iconRow->setContentsMargins(0, 0, 0, 0);
    iconRow->setSpacing(K4Styles::Dimensions::PopupButtonSpacing);

    m_helpBtn = createIconButton("");
    m_helpBtn->setIcon(QIcon(":/icons/help.svg"));
    m_helpBtn->setIconSize(QSize(24, 24));
    m_connectBtn = createIconButton("");
    m_connectBtn->setIcon(QIcon(":/icons/globe.svg"));
    m_connectBtn->setIconSize(QSize(24, 24));
    m_connectBtn->setToolTip("Connect to Radio");

    iconRow->addWidget(m_helpBtn);
    iconRow->addWidget(m_connectBtn);
    iconRow->addStretch();
    layout->addLayout(iconRow);

    // Connect icon button signals
    connect(m_helpBtn, &QPushButton::clicked, this, &SideControlPanel::helpClicked);
    connect(m_connectBtn, &QPushButton::clicked, this, &SideControlPanel::connectClicked);

    // ===== Connect Group 1 signals (WPM/PWR) =====
    connect(m_wpmBtn, &DualControlButton::becameActive, this, &SideControlPanel::onWpmBecameActive);
    connect(m_pwrBtn, &DualControlButton::becameActive, this, &SideControlPanel::onPwrBecameActive);
    connect(m_wpmBtn, &DualControlButton::valueScrolled, this, &SideControlPanel::onWpmScrolled);
    connect(m_pwrBtn, &DualControlButton::valueScrolled, this, &SideControlPanel::onPwrScrolled);
    connect(m_wpmBtn, &DualControlButton::swapped, this, [this]() {
        m_wpmIsPrimary = !m_wpmIsPrimary; // Track swap
    });
    connect(m_pwrBtn, &DualControlButton::swapped, this, [this]() {
        m_pwrIsPrimary = !m_pwrIsPrimary; // Track swap
    });

    // ===== Connect Group 2 signals (BW/SHFT) - LINKED PAIR =====
    connect(m_bwBtn, &DualControlButton::becameActive, this, &SideControlPanel::onBwBecameActive);
    connect(m_shiftBtn, &DualControlButton::becameActive, this, &SideControlPanel::onShiftBecameActive);
    connect(m_bwBtn, &DualControlButton::valueScrolled, this, &SideControlPanel::onBwScrolled);
    connect(m_shiftBtn, &DualControlButton::valueScrolled, this, &SideControlPanel::onShiftScrolled);
    connect(m_bwBtn, &DualControlButton::swapped, this, &SideControlPanel::onBwClicked);
    connect(m_shiftBtn, &DualControlButton::swapped, this, &SideControlPanel::onShiftClicked);

    // ===== Connect Group 3 signals (MainRf/SubSql) =====
    connect(m_mainRfBtn, &DualControlButton::becameActive, this, &SideControlPanel::onMainRfBecameActive);
    connect(m_subSqlBtn, &DualControlButton::becameActive, this, &SideControlPanel::onSubSqlBecameActive);
    connect(m_mainRfBtn, &DualControlButton::valueScrolled, this, &SideControlPanel::onMainRfScrolled);
    connect(m_subSqlBtn, &DualControlButton::valueScrolled, this, &SideControlPanel::onSubSqlScrolled);
    connect(m_mainRfBtn, &DualControlButton::swapped, this, [this]() {
        m_mainRfIsPrimary = !m_mainRfIsPrimary; // Track swap
    });
    connect(m_subSqlBtn, &DualControlButton::swapped, this, [this]() {
        m_subSqlIsPrimary = !m_subSqlIsPrimary; // Track swap
    });
}

// ===== Group Management =====

void SideControlPanel::setGroup1Active(DualControlButton *activeBtn) {
    m_wpmBtn->setShowIndicator(activeBtn == m_wpmBtn);
    m_pwrBtn->setShowIndicator(activeBtn == m_pwrBtn);
}

void SideControlPanel::setGroup2Active(DualControlButton *activeBtn) {
    m_bwBtn->setShowIndicator(activeBtn == m_bwBtn);
    m_shiftBtn->setShowIndicator(activeBtn == m_shiftBtn);
}

void SideControlPanel::setGroup3Active(DualControlButton *activeBtn) {
    m_mainRfBtn->setShowIndicator(activeBtn == m_mainRfBtn);
    m_subSqlBtn->setShowIndicator(activeBtn == m_subSqlBtn);
}

// ===== Group 1 Slots =====

void SideControlPanel::onWpmBecameActive() {
    setGroup1Active(m_wpmBtn);
}

void SideControlPanel::onPwrBecameActive() {
    setGroup1Active(m_pwrBtn);
}

void SideControlPanel::onWpmScrolled(int delta) {
    if (m_isCWMode) {
        // CW mode: WPM/PTCH
        if (m_wpmIsPrimary) {
            emit wpmChanged(delta);
        } else {
            emit pitchChanged(delta);
        }
    } else {
        // Voice mode: MIC/CMP
        if (m_wpmIsPrimary) {
            emit micGainChanged(delta);
        } else {
            emit compressionChanged(delta);
        }
    }
}

void SideControlPanel::onPwrScrolled(int delta) {
    if (m_pwrIsPrimary) {
        emit powerChanged(delta);
    } else {
        emit delayChanged(delta);
    }
}

// ===== Group 2 Slots (BW/SHFT - LINKED PAIR) =====

void SideControlPanel::onBwBecameActive() {
    setGroup2Active(m_bwBtn);
}

void SideControlPanel::onShiftBecameActive() {
    setGroup2Active(m_shiftBtn);
}

void SideControlPanel::onBwScrolled(int delta) {
    if (m_bwIsPrimary) {
        emit bandwidthChanged(delta);
    } else {
        emit highCutChanged(delta);
    }
}

void SideControlPanel::onShiftScrolled(int delta) {
    if (m_shiftIsPrimary) {
        emit shiftChanged(delta);
    } else {
        emit lowCutChanged(delta);
    }
}

void SideControlPanel::onBwClicked() {
    // BW and SHFT are linked - when one swaps, the other swaps too
    m_bwIsPrimary = !m_bwIsPrimary;
    m_shiftIsPrimary = !m_shiftIsPrimary;
    m_shiftBtn->swapFunctions(); // Also swap the shift button
}

void SideControlPanel::onShiftClicked() {
    // BW and SHFT are linked - when one swaps, the other swaps too
    m_shiftIsPrimary = !m_shiftIsPrimary;
    m_bwIsPrimary = !m_bwIsPrimary;
    m_bwBtn->swapFunctions(); // Also swap the BW button
}

// ===== Group 3 Slots =====

void SideControlPanel::onMainRfBecameActive() {
    setGroup3Active(m_mainRfBtn);
}

void SideControlPanel::onSubSqlBecameActive() {
    setGroup3Active(m_subSqlBtn);
}

void SideControlPanel::onMainRfScrolled(int delta) {
    if (m_mainRfIsPrimary) {
        emit mainRfGainChanged(delta);
    } else {
        emit mainSquelchChanged(delta);
    }
}

void SideControlPanel::onSubSqlScrolled(int delta) {
    if (m_subSqlIsPrimary) {
        emit subSquelchChanged(delta);
    } else {
        emit subRfGainChanged(delta);
    }
}

// ===== Mode Switching =====

void SideControlPanel::setDisplayMode(bool isCWMode) {
    if (m_isCWMode == isCWMode)
        return;
    m_isCWMode = isCWMode;

    if (isCWMode) {
        m_wpmBtn->setPrimaryLabel("WPM");
        m_wpmBtn->setAlternateLabel("PTCH");
    } else {
        m_wpmBtn->setPrimaryLabel("MIC");
        m_wpmBtn->setAlternateLabel("CMP");
    }
    // Reset to show primary value
    m_wpmIsPrimary = true;
    m_wpmBtn->setPrimaryValue("--");
    m_wpmBtn->setAlternateValue("--");
}

// ===== Value Setters =====

void SideControlPanel::setWpm(int wpm) {
    if (!m_isCWMode)
        return; // Only set in CW mode
    if (m_wpmIsPrimary) {
        m_wpmBtn->setPrimaryValue(QString::number(wpm));
    } else {
        m_wpmBtn->setAlternateValue(QString::number(wpm));
    }
}

void SideControlPanel::setPitch(double pitch) {
    if (!m_isCWMode)
        return; // Only set in CW mode
    QString pitchStr = QString::number(pitch, 'f', 2);
    if (!m_wpmIsPrimary) {
        m_wpmBtn->setPrimaryValue(pitchStr);
    } else {
        m_wpmBtn->setAlternateValue(pitchStr);
    }
}

void SideControlPanel::setMicGain(int gain) {
    if (m_isCWMode)
        return; // Only set in Voice mode
    if (m_wpmIsPrimary) {
        m_wpmBtn->setPrimaryValue(QString::number(gain));
    } else {
        m_wpmBtn->setAlternateValue(QString::number(gain));
    }
}

void SideControlPanel::setCompression(int comp) {
    if (m_isCWMode)
        return; // Only set in Voice mode
    if (!m_wpmIsPrimary) {
        m_wpmBtn->setPrimaryValue(QString::number(comp));
    } else {
        m_wpmBtn->setAlternateValue(QString::number(comp));
    }
}

void SideControlPanel::setPower(double power) {
    // Show decimal for QRP (≤10W), whole number for QRO (>10W)
    QString powerStr = (power <= 10.0) ? QString::number(power, 'f', 1) : QString::number(static_cast<int>(power));
    if (m_pwrIsPrimary) {
        m_pwrBtn->setPrimaryValue(powerStr);
    } else {
        m_pwrBtn->setAlternateValue(powerStr);
    }
}

void SideControlPanel::setDelay(double delay) {
    QString delayStr = QString::number(delay, 'f', 2);
    if (!m_pwrIsPrimary) {
        m_pwrBtn->setPrimaryValue(delayStr);
    } else {
        m_pwrBtn->setAlternateValue(delayStr);
    }
}

void SideControlPanel::setBandwidth(double bw) {
    QString bwStr = QString::number(bw, 'f', 2);
    if (m_bwIsPrimary) {
        m_bwBtn->setPrimaryValue(bwStr);
    } else {
        m_bwBtn->setAlternateValue(bwStr);
    }
}

void SideControlPanel::setHighCut(double hi) {
    QString hiStr = QString::number(hi, 'f', 2);
    if (!m_bwIsPrimary) {
        m_bwBtn->setPrimaryValue(hiStr);
    } else {
        m_bwBtn->setAlternateValue(hiStr);
    }
}

void SideControlPanel::setShift(double shift) {
    QString shiftStr = QString::number(shift, 'f', 2);
    if (m_shiftIsPrimary) {
        m_shiftBtn->setPrimaryValue(shiftStr);
    } else {
        m_shiftBtn->setAlternateValue(shiftStr);
    }
}

void SideControlPanel::setLowCut(double lo) {
    QString loStr = QString::number(lo, 'f', 2);
    if (!m_shiftIsPrimary) {
        m_shiftBtn->setPrimaryValue(loStr);
    } else {
        m_shiftBtn->setAlternateValue(loStr);
    }
}

void SideControlPanel::setMainRfGain(int gain) {
    QString value = gain > 0 ? QString("-%1").arg(gain) : "0";
    if (m_mainRfIsPrimary) {
        m_mainRfBtn->setPrimaryValue(value);
    } else {
        m_mainRfBtn->setAlternateValue(value);
    }
}

void SideControlPanel::setMainSquelch(int sql) {
    if (!m_mainRfIsPrimary) {
        m_mainRfBtn->setPrimaryValue(QString::number(sql));
    } else {
        m_mainRfBtn->setAlternateValue(QString::number(sql));
    }
}

void SideControlPanel::setSubSquelch(int sql) {
    if (m_subSqlIsPrimary) {
        m_subSqlBtn->setPrimaryValue(QString::number(sql));
    } else {
        m_subSqlBtn->setAlternateValue(QString::number(sql));
    }
}

void SideControlPanel::setSubRfGain(int gain) {
    QString value = gain > 0 ? QString("-%1").arg(gain) : "0";
    if (!m_subSqlIsPrimary) {
        m_subSqlBtn->setPrimaryValue(value);
    } else {
        m_subSqlBtn->setAlternateValue(value);
    }
}

void SideControlPanel::setTime(const QString &time) {
    m_timeLabel->setText(time);
}

void SideControlPanel::setPowerReading(double watts) {
    QString current = m_powerSwrLabel->text();
    int spaceIdx = current.indexOf(' ');
    QString swrPart = (spaceIdx > 0) ? current.mid(spaceIdx) : " 1.0:1";
    m_powerSwrLabel->setText(QString("%1W%2").arg(watts, 0, 'f', 1).arg(swrPart));
}

void SideControlPanel::setSwr(double swr) {
    QString current = m_powerSwrLabel->text();
    int wIdx = current.indexOf('W');
    QString powerPart = (wIdx > 0) ? current.left(wIdx + 1) : "0.0W";
    m_powerSwrLabel->setText(QString("%1 %2:1").arg(powerPart).arg(swr, 0, 'f', 1));
}

void SideControlPanel::setActiveReceiver(bool isSubRx) {
    // Update context colors for filter buttons based on active receiver
    DualControlButton::Context ctx = isSubRx ? DualControlButton::SubRx : DualControlButton::MainRx;
    m_bwBtn->setContext(ctx);
    m_shiftBtn->setContext(ctx);
}

void SideControlPanel::setVoltage(double volts) {
    QString current = m_voltageCurrentLabel->text();
    int vIdx = current.indexOf('V');
    QString currentPart = (vIdx > 0) ? current.mid(vIdx + 1).trimmed() : "-.-A";
    m_voltageCurrentLabel->setText(QString("%1V  %2").arg(volts, 0, 'f', 1).arg(currentPart));
}

void SideControlPanel::setCurrent(double amps) {
    QString current = m_voltageCurrentLabel->text();
    int vIdx = current.indexOf('V');
    QString voltsPart = (vIdx > 0) ? current.left(vIdx + 1) : "--.-V";
    m_voltageCurrentLabel->setText(QString("%1  %2A").arg(voltsPart).arg(amps, 0, 'f', 1));
}

QPushButton *SideControlPanel::createIconButton(const QString &text) {
    auto *btn = new QPushButton(text, this);
    btn->setFixedSize(K4Styles::Dimensions::CompactButtonSize, K4Styles::Dimensions::CompactButtonSize);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(K4Styles::sidePanelButton());
    return btn;
}

QWidget *SideControlPanel::createTxFunctionButton(const QString &mainText, const QString &subText,
                                                  QPushButton *&btnOut) {
    // Container widget for button + sub-text label
    auto *container = new QWidget(this);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 2, 0, 2);
    layout->setSpacing(5);

    // Button - scaled down from bottom menu bar style
    auto *btn = new QPushButton(mainText, container);
    btn->setFixedHeight(K4Styles::Dimensions::ButtonHeightSmall);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(K4Styles::sidePanelButtonLight());
    btnOut = btn;
    layout->addWidget(btn);

    // Sub-text label (orange) - add top margin to prevent overlap with button
    auto *subLabel = new QLabel(subText, container);
    subLabel->setStyleSheet(QString("color: %1; font-size: 8px; margin-top: 4px;").arg(K4Styles::Colors::AccentAmber));
    subLabel->setAlignment(Qt::AlignCenter);
    subLabel->setFixedHeight(K4Styles::Dimensions::PopupContentMargin);
    layout->addWidget(subLabel);

    return container;
}

int SideControlPanel::volume() const {
    return m_volumeSlider ? m_volumeSlider->value() : 100;
}

int SideControlPanel::subVolume() const {
    return m_subVolumeSlider ? m_subVolumeSlider->value() : 100;
}

void SideControlPanel::updateMonitorLevel(int mode, int level) {
    // Only update if this is the current mode
    if (m_monOverlay && m_monOverlay->mode() == mode) {
        m_monOverlay->setValue(level);
    }
}

void SideControlPanel::updateMonitorMode(int mode) {
    if (m_monOverlay) {
        m_monOverlay->setMode(mode);
    }
}

void SideControlPanel::updateBalance(int mode, int offset) {
    if (m_balOverlay) {
        m_balOverlay->setBalance(mode, offset);
    }
}

bool SideControlPanel::eventFilter(QObject *watched, QEvent *event) {
    // Handle right-click on TX function buttons
    if (event->type() == QEvent::MouseButtonPress) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::RightButton) {
            if (watched == m_tuneBtn) {
                emit tuneLpClicked();
                return true;
            } else if (watched == m_xmitBtn) {
                emit testClicked();
                return true;
            } else if (watched == m_atuTuneBtn) {
                emit atuClicked();
                return true;
            } else if (watched == m_voxBtn) {
                emit qskClicked();
                return true;
            } else if (watched == m_rxAntBtn) {
                emit subAntClicked();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}
