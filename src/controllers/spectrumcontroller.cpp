#include "spectrumcontroller.h"
#include "connectioncontroller.h"
#include "dsp/panadapter_rhi.h"
#include "models/radiostate.h"
#include "ui/k4styles.h"
#include "ui/vfowidget.h"
#include "dxclustercontroller.h"
#include "utils/radioutils.h"

#include "ui/dxspotoverlay.h"
#include "ui/mousevfoindicator.h"
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>

// ============== SpectrumController Implementation ==============

SpectrumController::SpectrumController(ConnectionController *conn, RadioState *radioState, QObject *parent)
    : QObject(parent), m_connectionController(conn), m_radioState(radioState), m_mouseQsyMode(0) {}

SpectrumController::~SpectrumController() {
    disconnect(this);
}

QWidget *SpectrumController::spectrumContainer() const {
    return m_spectrumContainer;
}
PanadapterRhiWidget *SpectrumController::panadapterA() const {
    return m_panadapterA;
}
PanadapterRhiWidget *SpectrumController::panadapterB() const {
    return m_panadapterB;
}

void SpectrumController::setupSpectrumUI(QWidget *parentWidget, VFOWidget *vfoA, VFOWidget *vfoB) {
    m_vfoA = vfoA;
    m_vfoB = vfoB;

    // Container for spectrum displays
    m_spectrumContainer = new QWidget(parentWidget);
    m_spectrumContainer->setStyleSheet(QString("background-color: %1; border: %2px solid %3;")
                                           .arg(K4Styles::Colors::DarkBackground)
                                           .arg(K4Styles::Dimensions::SeparatorHeight)
                                           .arg(K4Styles::Colors::PanelBorder));
    m_spectrumContainer->setMinimumHeight(300);

    // Use QHBoxLayout for side-by-side panadapters (Main left, Sub right)
    // Use QBoxLayout for panadapters (horizontal side-by-side, or vertical stacked in DualAlt)
    auto *layout = new QBoxLayout(QBoxLayout::LeftToRight, m_spectrumContainer);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);

    // Main panadapter for VFO A (left side) - QRhiWidget with Metal/DirectX/Vulkan
    m_panadapterA = new PanadapterRhiWidget(m_spectrumContainer);
    m_panadapterA->setObjectName("A");
    // dB range set via setScale()/setRefLevel() from radio's #SCL/#REF values
    m_panadapterA->setSpectrumRatio(0.35f);
    m_panadapterA->setGridEnabled(true);
    // Primary VFO A uses default cyan passband
    // Secondary VFO B uses green passband
    QColor vfoBPassbandAlpha(K4Styles::Colors::VfoBGreen);
    vfoBPassbandAlpha.setAlpha(64);
    m_panadapterA->setSecondaryPassbandColor(vfoBPassbandAlpha);
    m_panadapterA->setSecondaryMarkerColor(QColor(K4Styles::Colors::VfoBGreen));
    m_panadapterA->setSecondaryVisible(true);
    layout->addWidget(m_panadapterA);

    // Vertical separator between A/B panadapters (visible only in Dual mode)
    m_spectrumSeparator = new QFrame(m_spectrumContainer);
    m_spectrumSeparator->setFrameShape(QFrame::VLine);
    m_spectrumSeparator->setFrameShadow(QFrame::Plain);
    m_spectrumSeparator->setStyleSheet(QString("color: %1;").arg(K4Styles::Colors::PanelBorder));
    m_spectrumSeparator->setFixedWidth(K4Styles::Dimensions::SeparatorHeight);
    m_spectrumSeparator->hide();
    layout->addWidget(m_spectrumSeparator);

    // Sub panadapter for VFO B (right side) - QRhiWidget with Metal/DirectX/Vulkan
    m_panadapterB = new PanadapterRhiWidget(m_spectrumContainer);
    m_panadapterB->setObjectName("B");
    // dB range set via setScale()/setRefLevel() from radio's #SCL/#REF$ values
    m_panadapterB->setSpectrumRatio(0.35f);
    m_panadapterB->setGridEnabled(true);
    // Primary VFO B uses green passband
    m_panadapterB->setPassbandColor(vfoBPassbandAlpha);
    m_panadapterB->setFrequencyMarkerColor(QColor(K4Styles::Colors::VfoBGreen));
    // Secondary VFO A uses cyan passband
    QColor vfoAPassbandAlpha(K4Styles::Colors::VfoACyan);
    vfoAPassbandAlpha.setAlpha(64);
    m_panadapterB->setSecondaryPassbandColor(vfoAPassbandAlpha);
    m_panadapterB->setSecondaryMarkerColor(QColor(K4Styles::Colors::VfoACyan));
    m_panadapterB->setSecondaryVisible(true);
    layout->addWidget(m_panadapterB);
    m_panadapterB->hide(); // Start hidden (MainOnly mode)

    // Span control buttons - overlay on panadapter (lower right, above freq labels)
    // Note: rgba used intentionally for transparent overlay effect on spectrum
    QString btnStyle = QString("QPushButton { background: rgba(0,0,0,0.6); color: %1; "
                               "border: 1px solid %2; border-radius: 4px; "
                               "font-size: %3px; font-weight: bold; min-width: 28px; min-height: 24px; }"
                               "QPushButton:hover { background: rgba(80,80,80,0.8); }")
                           .arg(K4Styles::Colors::TextWhite)
                           .arg(K4Styles::Colors::InactiveGray)
                           .arg(K4Styles::Dimensions::FontSizePopup);

    // Main panadapter (A) buttons
    m_spanDownBtn = new QPushButton("-", m_panadapterA);
    m_spanDownBtn->setStyleSheet(btnStyle);
    m_spanDownBtn->setFixedSize(K4Styles::Dimensions::ButtonHeightSmall, K4Styles::Dimensions::ButtonHeightMini);

    m_spanUpBtn = new QPushButton("+", m_panadapterA);
    m_spanUpBtn->setStyleSheet(btnStyle);
    m_spanUpBtn->setFixedSize(K4Styles::Dimensions::ButtonHeightSmall, K4Styles::Dimensions::ButtonHeightMini);

    m_centerBtn = new QPushButton("C", m_panadapterA);
    m_centerBtn->setStyleSheet(btnStyle);
    m_centerBtn->setFixedSize(K4Styles::Dimensions::ButtonHeightSmall, K4Styles::Dimensions::ButtonHeightMini);

    // Sub panadapter (B) buttons
    m_spanDownBtnB = new QPushButton("-", m_panadapterB);
    m_spanDownBtnB->setStyleSheet(btnStyle);
    m_spanDownBtnB->setFixedSize(K4Styles::Dimensions::ButtonHeightSmall, K4Styles::Dimensions::ButtonHeightMini);

    m_spanUpBtnB = new QPushButton("+", m_panadapterB);
    m_spanUpBtnB->setStyleSheet(btnStyle);
    m_spanUpBtnB->setFixedSize(K4Styles::Dimensions::ButtonHeightSmall, K4Styles::Dimensions::ButtonHeightMini);

    m_centerBtnB = new QPushButton("C", m_panadapterB);
    m_centerBtnB->setStyleSheet(btnStyle);
    m_centerBtnB->setFixedSize(K4Styles::Dimensions::ButtonHeightSmall, K4Styles::Dimensions::ButtonHeightMini);

    // VFO indicator badges - bottom-left corner of waterfall, tab shape with top-right rounded
    QString vfoIndicatorStyle = QString("QLabel { background: %1; color: black; "
                                        "font-size: %2px; font-weight: bold; "
                                        "border-top-left-radius: 0px; border-top-right-radius: %3px; "
                                        "border-bottom-left-radius: 0px; border-bottom-right-radius: 0px; }")
                                    .arg(K4Styles::Colors::OverlayBackground)
                                    .arg(K4Styles::Dimensions::FontSizeTitle)
                                    .arg(K4Styles::Dimensions::BorderRadiusLarge);

    m_vfoIndicatorA = new QLabel("A", m_panadapterA);
    m_vfoIndicatorA->setStyleSheet(vfoIndicatorStyle);
    m_vfoIndicatorA->setFixedSize(K4Styles::Dimensions::VfoIndicatorWidth, K4Styles::Dimensions::VfoIndicatorHeight);
    m_vfoIndicatorA->setAlignment(Qt::AlignCenter);

    m_vfoIndicatorB = new QLabel("B", m_panadapterB);
    m_vfoIndicatorB->setStyleSheet(vfoIndicatorStyle);
    m_vfoIndicatorB->setFixedSize(K4Styles::Dimensions::VfoIndicatorWidth, K4Styles::Dimensions::VfoIndicatorHeight);
    m_vfoIndicatorB->setAlignment(Qt::AlignCenter);

    // Mouse VFO focus indicators - shows which VFO the scroll wheel controls
    m_mouseVfoIndicatorA = new MouseVfoIndicator(m_panadapterA);
    m_mouseVfoIndicatorB = new MouseVfoIndicator(m_panadapterB);

    // Position buttons (will be repositioned in resizeEvent of panadapter)
    // Triangle layout: C centered above, - and + below (bottom-right)
    m_spanDownBtn->move(m_panadapterA->width() - 70, m_panadapterA->height() - 45);
    m_spanUpBtn->move(m_panadapterA->width() - 35, m_panadapterA->height() - 45);
    m_centerBtn->move(m_panadapterA->width() - 52, m_panadapterA->height() - 73);

    m_spanDownBtnB->move(m_panadapterB->width() - 70, m_panadapterB->height() - 45);
    m_spanUpBtnB->move(m_panadapterB->width() - 35, m_panadapterB->height() - 45);
    m_centerBtnB->move(m_panadapterB->width() - 52, m_panadapterB->height() - 73);

    // VFO indicators at bottom-left corner, flush with edges
    m_vfoIndicatorA->move(0, m_panadapterA->height() - 30);
    m_vfoIndicatorB->move(0, m_panadapterB->height() - 30);
    m_mouseVfoIndicatorA->move(K4Styles::Dimensions::VfoIndicatorWidth, m_panadapterA->height() - 44);
    m_mouseVfoIndicatorB->move(K4Styles::Dimensions::VfoIndicatorWidth, m_panadapterB->height() - 44);

    // Span adjustment for Main: K4 span steps
    connect(m_spanDownBtn, &QPushButton::clicked, this, [this]() {
        int currentSpan = m_radioState->spanHz();
        int newSpan = RadioUtils::getNextSpanDown(currentSpan); // - decreases span
        if (newSpan != currentSpan) {
            m_radioState->setSpanHz(newSpan);
            m_connectionController->sendCAT(QString("#SPN%1;").arg(newSpan));
        }
    });

    connect(m_spanUpBtn, &QPushButton::clicked, this, [this]() {
        int currentSpan = m_radioState->spanHz();
        int newSpan = RadioUtils::getNextSpanUp(currentSpan); // + increases span
        if (newSpan != currentSpan) {
            m_radioState->setSpanHz(newSpan);
            m_connectionController->sendCAT(QString("#SPN%1;").arg(newSpan));
        }
    });

    connect(m_centerBtn, &QPushButton::clicked, this, [this]() { m_connectionController->sendCAT("FC;"); });

    // Span adjustment for Sub: uses $ suffix for Sub RX commands
    connect(m_spanDownBtnB, &QPushButton::clicked, this, [this]() {
        int currentSpan = m_radioState->spanHzB();
        int newSpan = RadioUtils::getNextSpanDown(currentSpan); // - decreases span
        if (newSpan != currentSpan) {
            m_radioState->setSpanHzB(newSpan);
            m_connectionController->sendCAT(QString("#SPN$%1;").arg(newSpan));
        }
    });

    connect(m_spanUpBtnB, &QPushButton::clicked, this, [this]() {
        int currentSpan = m_radioState->spanHzB();
        int newSpan = RadioUtils::getNextSpanUp(currentSpan); // + increases span
        if (newSpan != currentSpan) {
            m_radioState->setSpanHzB(newSpan);
            m_connectionController->sendCAT(QString("#SPN$%1;").arg(newSpan));
        }
    });

    connect(m_centerBtnB, &QPushButton::clicked, this, [this]() { m_connectionController->sendCAT("FC$;"); });

    // DX Spot overlays (transparent child widgets for callsign labels)
    m_spotOverlayA = new DxSpotOverlay(m_panadapterA);
    m_spotOverlayA->show();
    m_spotOverlayB = new DxSpotOverlay(m_panadapterB);
    m_spotOverlayB->show();

    // Install event filter to reposition span buttons when panadapters resize
    m_panadapterA->installEventFilter(this);
    m_panadapterB->installEventFilter(this);

    // Debug: Connect to renderFailed signal to diagnose QRhiWidget issues
    connect(m_panadapterA, &QRhiWidget::renderFailed, this,
            []() { qCritical() << "!!! PanadapterA renderFailed() emitted - QRhi could not be obtained !!!"; });
    connect(m_panadapterB, &QRhiWidget::renderFailed, this,
            []() { qCritical() << "!!! PanadapterB renderFailed() emitted - QRhi could not be obtained !!!"; });

    // RadioState display properties → panadapter configuration
    connect(m_radioState, &RadioState::refLevelChanged, this, [this](int level) { m_panadapterA->setRefLevel(level); });
    connect(m_radioState, &RadioState::refLevelBChanged, this,
            [this](int level) { m_panadapterB->setRefLevel(level); });
    connect(m_radioState, &RadioState::scaleChanged, this, [this](int scale) {
        m_panadapterA->setScale(scale);
        m_panadapterB->setScale(scale);
    });
    connect(m_radioState, &RadioState::spanChanged, this, [this](int spanHz) { m_panadapterA->setSpan(spanHz); });
    connect(m_radioState, &RadioState::spanBChanged, this, [this](int spanHz) { m_panadapterB->setSpan(spanHz); });
    connect(m_radioState, &RadioState::waterfallHeightChanged, this, [this](int percent) {
        m_panadapterA->setWaterfallHeight(percent);
        m_panadapterB->setWaterfallHeight(percent);
        m_vfoA->setMiniPanWaterfallHeight(percent);
        m_vfoB->setMiniPanWaterfallHeight(percent);
        // Reposition DX spot overlays after spectrum/waterfall ratio changes
        if (m_spotOverlayA) {
            int specH = static_cast<int>(m_panadapterA->height() * m_panadapterA->spectrumRatio());
            m_spotOverlayA->setGeometry(0, 0, m_panadapterA->width(), specH);
        }
        if (m_spotOverlayB) {
            int specH = static_cast<int>(m_panadapterB->height() * m_panadapterB->spectrumRatio());
            m_spotOverlayB->setGeometry(0, 0, m_panadapterB->width(), specH);
        }
    });
    connect(m_radioState, &RadioState::averagingChanged, this, [this](int level) {
        m_panadapterA->setAveraging(level);
        m_panadapterB->setAveraging(level);
        m_vfoA->setMiniPanAveraging(level);
        m_vfoB->setMiniPanAveraging(level);
    });
    connect(m_radioState, &RadioState::vfoACursorChanged, this,
            [this](int mode) { m_panadapterA->setCursorVisible(mode == 1 || mode == 2); });
    connect(m_radioState, &RadioState::vfoBCursorChanged, this,
            [this](int mode) { m_panadapterB->setCursorVisible(mode == 1 || mode == 2); });
    connect(m_radioState, &RadioState::dualPanModeLcdChanged, this, [this](int mode) {
        switch (mode) {
        case 0:
            setPanadapterMode(PanadapterMode::MainOnly);
            break;
        case 1:
            setPanadapterMode(PanadapterMode::SubOnly);
            break;
        case 2: // Dual (A+B) — don't override if we're in DualAlt
            if (!m_dualAltActive)
                setPanadapterMode(PanadapterMode::Dual);
            break;
        }
    });

    // Update panadapter when frequency/mode changes
    connect(m_radioState, &RadioState::frequencyChanged, this, [this](quint64) {
        updatePanadapterPassbands();
        updateTxMarkers();
    });
    connect(m_radioState, &RadioState::modeChanged, this,
            [this](RadioState::Mode mode) { m_panadapterA->setMode(RadioState::modeToString(mode)); });
    connect(m_radioState, &RadioState::dataSubModeChanged, this,
            [this](int subMode) { m_panadapterA->setDataSubMode(subMode); });
    connect(m_radioState, &RadioState::filterBandwidthChanged, this,
            [this](int bw) { m_panadapterA->setFilterBandwidth(bw); });
    connect(m_radioState, &RadioState::ifShiftChanged, this, [this](int shift) { m_panadapterA->setIfShift(shift); });
    connect(m_radioState, &RadioState::cwPitchChanged, this, [this](int pitch) { m_panadapterA->setCwPitch(pitch); });

    // Notch filter visualization
    connect(m_radioState, &RadioState::notchChanged, this, [this]() {
        bool enabled = m_radioState->manualNotchEnabled();
        int pitch = m_radioState->manualNotchPitch();
        m_panadapterA->setNotchFilter(enabled, pitch);
        // Update mini-pan too (using forwarding method that handles lazy creation)
        m_vfoA->setMiniPanNotchFilter(enabled, pitch);
        // Update NTCH indicator in VFO processing row
        m_vfoA->setNotch(m_radioState->autoNotchEnabled(), m_radioState->manualNotchEnabled());
    });
    // Also update mini-pan mode when mode changes
    connect(m_radioState, &RadioState::modeChanged, this,
            [this](RadioState::Mode mode) { m_vfoA->setMiniPanMode(RadioState::modeToString(mode)); });
    connect(m_radioState, &RadioState::dataSubModeChanged, this,
            [this](int subMode) { m_vfoA->setMiniPanDataSubMode(subMode); });

    // Mini-pan filter passband visualization (using forwarding methods)
    connect(m_radioState, &RadioState::filterBandwidthChanged, this,
            [this](int bw) { m_vfoA->setMiniPanFilterBandwidth(bw); });
    connect(m_radioState, &RadioState::ifShiftChanged, this, [this](int shift) { m_vfoA->setMiniPanIfShift(shift); });
    connect(m_radioState, &RadioState::cwPitchChanged, this, [this](int pitch) { m_vfoA->setMiniPanCwPitch(pitch); });

    // Tuning rate indicator (VT command)
    connect(m_radioState, &RadioState::tuningStepChanged, this, [this](int step) { m_vfoA->setTuningRate(step); });
    connect(m_radioState, &RadioState::tuningStepBChanged, this, [this](int step) { m_vfoB->setTuningRate(step); });

    // Mouse control: click to tune
    connect(m_panadapterA, &PanadapterRhiWidget::frequencyClicked, this, [this](qint64 freq) {
        // Guard: only send if connected and frequency is valid
        if (!m_connectionController->isConnected() || freq <= 0)
            return;
        if (m_radioState->lockA())
            return;
        // PSK-D/FSK-D: passband centered at dial+IS, so subtract IS to place passband on click
        freq = adjustClickFreqForMode(freq, false);
        int stepHz = RadioUtils::tuningStepToHz(m_radioState->tuningStep());
        qint64 snapped = (freq / stepHz) * stepHz;
        if (snapped <= 0)
            return;
        QString cmd = QString("FA%1;").arg(snapped, 11, 10, QChar('0'));
        m_connectionController->sendCAT(cmd);
        // Request frequency back to update UI (K4 doesn't echo SET commands)
        m_connectionController->sendCAT("FA;");
        // Set scroll wheel to control VFO A
        m_scrollVfoB = false;
        m_mouseVfoIndicatorA->setActiveVfo(false);
        m_mouseVfoIndicatorB->setActiveVfo(false);
    });

    // Mouse control: drag to tune (continuous frequency change while dragging)
    // Frequency is snapped to the current tuning rate step for consistent behavior
    connect(m_panadapterA, &PanadapterRhiWidget::frequencyDragged, this, [this](qint64 freq) {
        // Guard: only send if connected and frequency is valid
        if (!m_connectionController->isConnected() || freq <= 0)
            return;
        if (m_radioState->lockA())
            return;
        freq = adjustClickFreqForMode(freq, false);
        int stepHz = RadioUtils::tuningStepToHz(m_radioState->tuningStep());
        qint64 snapped = (freq / stepHz) * stepHz;
        if (snapped <= 0)
            return;
        QString cmd = QString("FA%1;").arg(snapped, 11, 10, QChar('0'));
        m_connectionController->sendCAT(cmd);
        // Update local state immediately for responsive UI (K4 doesn't echo SET commands)
        m_radioState->parseCATCommand(cmd);
    });

    // Mouse control: scroll wheel to adjust frequency by computed step
    connect(m_panadapterA, &PanadapterRhiWidget::frequencyScrolled, this, [this](int steps) {
        if (!m_connectionController->isConnected())
            return;
        // Scroll follows the mouse VFO focus indicator (last clicked VFO)
        bool tuneB = m_scrollVfoB;
        if (tuneB ? m_radioState->lockB() : m_radioState->lockA())
            return;
        quint64 currentFreq = tuneB ? m_radioState->vfoB() : m_radioState->vfoA();
        int stepHz = RadioUtils::tuningStepToHz(tuneB ? m_radioState->tuningStepB() : m_radioState->tuningStep());
        qint64 newFreq = static_cast<qint64>(currentFreq) + static_cast<qint64>(steps) * stepHz;
        if (newFreq > 0) {
            QString vfo = tuneB ? "FB" : "FA";
            QString cmd = QString("%1%2;").arg(vfo).arg(static_cast<quint64>(newFreq), 11, 10, QChar('0'));
            m_connectionController->sendCAT(cmd);
            m_radioState->parseCATCommand(cmd);
        }
    });

    // Shift+Wheel: Adjust scale (dB range) - global setting applies to both panadapters
    connect(m_panadapterA, &PanadapterRhiWidget::scaleScrolled, this, [this](int steps) {
        if (!m_connectionController->isConnected())
            return;
        int currentScale = m_radioState->scale();
        if (currentScale < 0)
            currentScale = 75;                                    // Default if not yet received from radio
        int newScale = qBound(10, currentScale + steps * 5, 150); // 5 dB per step
        m_connectionController->sendCAT(QString("#SCL%1;").arg(newScale));
        // Optimistic update (scale is global) - updates both panadapters via signal
        m_radioState->setScale(newScale);
    });

    // Ctrl+Wheel: Adjust reference level for Main RX (blocked when auto-ref is active)
    connect(m_panadapterA, &PanadapterRhiWidget::refLevelScrolled, this, [this](int steps) {
        if (!m_connectionController->isConnected() || m_radioState->autoRefLevel())
            return;
        int currentRef = m_radioState->refLevel();
        if (currentRef < -200)
            currentRef = -110; // Default if not yet received
        int newRef = qBound(-140, currentRef + steps, 10);
        m_connectionController->sendCAT(QString("#REF%1;").arg(newRef));
        // Optimistic update
        m_panadapterA->setRefLevel(newRef);
    });

    // Right-click on panadapter A tunes VFO B (L=A R=B mode)
    connect(m_panadapterA, &PanadapterRhiWidget::frequencyRightClicked, this, [this](qint64 freq) {
        if (m_mouseQsyMode == 0) // Left Only — right-click disabled
            return;
        if (!m_connectionController->isConnected() || freq <= 0)
            return;
        if (m_radioState->lockB())
            return;
        freq = adjustClickFreqForMode(freq, true); // right-click on Pan A → VFO B
        int stepHz = RadioUtils::tuningStepToHz(m_radioState->tuningStepB());
        qint64 snapped = (freq / stepHz) * stepHz;
        if (snapped <= 0)
            return;
        QString cmd = QString("FB%1;").arg(snapped, 11, 10, QChar('0'));
        m_connectionController->sendCAT(cmd);
        m_connectionController->sendCAT("FB;");
        // Set scroll wheel to control VFO B
        m_scrollVfoB = true;
        m_mouseVfoIndicatorA->setActiveVfo(true);
        m_mouseVfoIndicatorB->setActiveVfo(true);
    });

    connect(m_panadapterA, &PanadapterRhiWidget::frequencyRightDragged, this, [this](qint64 freq) {
        if (m_mouseQsyMode == 0) // Left Only — right-drag disabled
            return;
        if (!m_connectionController->isConnected() || freq <= 0)
            return;
        if (m_radioState->lockB())
            return;
        freq = adjustClickFreqForMode(freq, true); // right-drag on Pan A → VFO B
        int stepHz = RadioUtils::tuningStepToHz(m_radioState->tuningStepB());
        qint64 snapped = (freq / stepHz) * stepHz;
        if (snapped <= 0)
            return;
        QString cmd = QString("FB%1;").arg(snapped, 11, 10, QChar('0'));
        m_connectionController->sendCAT(cmd);
        m_radioState->parseCATCommand(cmd);
    });

    // VFO B connections
    connect(m_radioState, &RadioState::frequencyBChanged, this, [this](quint64) {
        updatePanadapterPassbands();
        updateTxMarkers();
    });
    connect(m_radioState, &RadioState::modeBChanged, this,
            [this](RadioState::Mode mode) { m_panadapterB->setMode(RadioState::modeToString(mode)); });
    connect(m_radioState, &RadioState::dataSubModeBChanged, this,
            [this](int subMode) { m_panadapterB->setDataSubMode(subMode); });
    connect(m_radioState, &RadioState::filterBandwidthBChanged, this,
            [this](int bw) { m_panadapterB->setFilterBandwidth(bw); });
    connect(m_radioState, &RadioState::ifShiftBChanged, this, [this](int shift) { m_panadapterB->setIfShift(shift); });
    connect(m_radioState, &RadioState::cwPitchChanged, this, [this](int pitch) { m_panadapterB->setCwPitch(pitch); });
    connect(m_radioState, &RadioState::notchBChanged, this, [this]() {
        bool enabled = m_radioState->manualNotchEnabledB();
        int pitch = m_radioState->manualNotchPitchB();
        m_panadapterB->setNotchFilter(enabled, pitch);
    });

    // VFO B Mini-Pan connections (mode-dependent bandwidth, using forwarding methods)
    connect(m_radioState, &RadioState::modeBChanged, this,
            [this](RadioState::Mode mode) { m_vfoB->setMiniPanMode(RadioState::modeToString(mode)); });
    connect(m_radioState, &RadioState::dataSubModeBChanged, this,
            [this](int subMode) { m_vfoB->setMiniPanDataSubMode(subMode); });
    connect(m_radioState, &RadioState::filterBandwidthBChanged, this,
            [this](int bw) { m_vfoB->setMiniPanFilterBandwidth(bw); });
    connect(m_radioState, &RadioState::ifShiftBChanged, this, [this](int shift) { m_vfoB->setMiniPanIfShift(shift); });
    connect(m_radioState, &RadioState::cwPitchChanged, this, [this](int pitch) { m_vfoB->setMiniPanCwPitch(pitch); });
    connect(m_radioState, &RadioState::notchBChanged, this, [this]() {
        bool enabled = m_radioState->manualNotchEnabledB();
        int pitch = m_radioState->manualNotchPitchB();
        m_vfoB->setMiniPanNotchFilter(enabled, pitch);
        // Update NTCH indicator in VFO B processing row
        m_vfoB->setNotch(m_radioState->autoNotchEnabledB(), m_radioState->manualNotchEnabledB());
    });

    // Secondary VFO passband display: VFO B state → PanadapterA's secondary
    auto updatePanadapterASecondary = [this]() {
        m_panadapterA->setSecondaryVfo(m_radioState->vfoB(), m_radioState->filterBandwidthB(),
                                       RadioState::modeToString(m_radioState->modeB()), m_radioState->ifShiftB(),
                                       m_radioState->dataSubModeB());
    };
    connect(m_radioState, &RadioState::frequencyBChanged, this, updatePanadapterASecondary);
    connect(m_radioState, &RadioState::modeBChanged, this, updatePanadapterASecondary);
    connect(m_radioState, &RadioState::filterBandwidthBChanged, this, updatePanadapterASecondary);
    connect(m_radioState, &RadioState::ifShiftBChanged, this, updatePanadapterASecondary);

    // Secondary VFO passband display: VFO A state → PanadapterB's secondary
    auto updatePanadapterBSecondary = [this]() {
        m_panadapterB->setSecondaryVfo(m_radioState->vfoA(), m_radioState->filterBandwidth(),
                                       RadioState::modeToString(m_radioState->mode()), m_radioState->ifShift(),
                                       m_radioState->dataSubMode());
    };
    connect(m_radioState, &RadioState::frequencyChanged, this, updatePanadapterBSecondary);
    connect(m_radioState, &RadioState::modeChanged, this, updatePanadapterBSecondary);
    connect(m_radioState, &RadioState::filterBandwidthChanged, this, updatePanadapterBSecondary);
    connect(m_radioState, &RadioState::ifShiftChanged, this, updatePanadapterBSecondary);

    // Mouse control for VFO B: click to tune
    connect(m_panadapterB, &PanadapterRhiWidget::frequencyClicked, this, [this](qint64 freq) {
        // Guard: only send if connected and frequency is valid
        if (!m_connectionController->isConnected() || freq <= 0)
            return;
        // L=A R=B mode: left-click on Pan B tunes VFO A
        bool tuneA = (m_mouseQsyMode == 1);
        if (tuneA ? m_radioState->lockA() : m_radioState->lockB())
            return;
        freq = adjustClickFreqForMode(freq, !tuneA);
        QString vfo = tuneA ? "FA" : "FB";
        int stepHz = RadioUtils::tuningStepToHz(tuneA ? m_radioState->tuningStep() : m_radioState->tuningStepB());
        qint64 snapped = (freq / stepHz) * stepHz;
        if (snapped <= 0)
            return;
        QString cmd = QString("%1%2;").arg(vfo).arg(snapped, 11, 10, QChar('0'));
        m_connectionController->sendCAT(cmd);
        m_connectionController->sendCAT(vfo + ";");
        // Set scroll wheel to control the VFO that was clicked
        m_scrollVfoB = !tuneA;
        m_mouseVfoIndicatorA->setActiveVfo(!tuneA);
        m_mouseVfoIndicatorB->setActiveVfo(!tuneA);
    });

    // Mouse control for VFO B: drag to tune (continuous frequency change while dragging)
    // Frequency is snapped to the current tuning rate step for consistent behavior
    connect(m_panadapterB, &PanadapterRhiWidget::frequencyDragged, this, [this](qint64 freq) {
        // Guard: only send if connected and frequency is valid
        if (!m_connectionController->isConnected() || freq <= 0)
            return;
        // L=A R=B mode: left-drag on Pan B tunes VFO A
        bool tuneA = (m_mouseQsyMode == 1);
        if (tuneA ? m_radioState->lockA() : m_radioState->lockB())
            return;
        freq = adjustClickFreqForMode(freq, !tuneA);
        QString vfo = tuneA ? "FA" : "FB";
        int stepHz = RadioUtils::tuningStepToHz(tuneA ? m_radioState->tuningStep() : m_radioState->tuningStepB());
        qint64 snapped = (freq / stepHz) * stepHz;
        if (snapped <= 0)
            return;
        QString cmd = QString("%1%2;").arg(vfo).arg(snapped, 11, 10, QChar('0'));
        m_connectionController->sendCAT(cmd);
        m_radioState->parseCATCommand(cmd);
    });

    // Mouse control for VFO B: scroll wheel to adjust frequency by computed step
    connect(m_panadapterB, &PanadapterRhiWidget::frequencyScrolled, this, [this](int steps) {
        if (!m_connectionController->isConnected())
            return;
        // Scroll follows the mouse VFO focus indicator (last clicked VFO)
        bool tuneB = m_scrollVfoB;
        if (tuneB ? m_radioState->lockB() : m_radioState->lockA())
            return;
        quint64 currentFreq = tuneB ? m_radioState->vfoB() : m_radioState->vfoA();
        int stepHz = RadioUtils::tuningStepToHz(tuneB ? m_radioState->tuningStepB() : m_radioState->tuningStep());
        qint64 newFreq = static_cast<qint64>(currentFreq) + static_cast<qint64>(steps) * stepHz;
        if (newFreq > 0) {
            QString vfo = tuneB ? "FB" : "FA";
            QString cmd = QString("%1%2;").arg(vfo).arg(static_cast<quint64>(newFreq), 11, 10, QChar('0'));
            m_connectionController->sendCAT(cmd);
            m_radioState->parseCATCommand(cmd);
        }
    });

    // Shift+Wheel on panadapter B: Adjust scale (same as A - global setting)
    connect(m_panadapterB, &PanadapterRhiWidget::scaleScrolled, this, [this](int steps) {
        if (!m_connectionController->isConnected())
            return;
        int currentScale = m_radioState->scale();
        if (currentScale < 0)
            currentScale = 75;
        int newScale = qBound(10, currentScale + steps * 5, 150);
        m_connectionController->sendCAT(QString("#SCL%1;").arg(newScale));
        // Optimistic update (scale is global) - updates both panadapters via signal
        m_radioState->setScale(newScale);
    });

    // Ctrl+Wheel on panadapter B: Adjust reference level for Sub RX (blocked when auto-ref is active)
    connect(m_panadapterB, &PanadapterRhiWidget::refLevelScrolled, this, [this](int steps) {
        if (!m_connectionController->isConnected() || m_radioState->autoRefLevel())
            return;
        int currentRef = m_radioState->refLevelB();
        if (currentRef < -200)
            currentRef = -110;
        int newRef = qBound(-140, currentRef + steps, 10);
        m_connectionController->sendCAT(QString("#REF$%1;").arg(newRef)); // Note: #REF$ for Sub RX
        // Optimistic update
        m_panadapterB->setRefLevel(newRef);
    });

    // Right-click on panadapter B
    connect(m_panadapterB, &PanadapterRhiWidget::frequencyRightClicked, this, [this](qint64 freq) {
        if (m_mouseQsyMode == 0) // Left Only — right-click disabled
            return;
        if (!m_connectionController->isConnected() || freq <= 0)
            return;
        if (m_radioState->lockB())
            return;
        // L=A R=B mode: right-click always tunes VFO B
        freq = adjustClickFreqForMode(freq, true);
        int stepHz = RadioUtils::tuningStepToHz(m_radioState->tuningStepB());
        qint64 snapped = (freq / stepHz) * stepHz;
        if (snapped <= 0)
            return;
        QString cmd = QString("FB%1;").arg(snapped, 11, 10, QChar('0'));
        m_connectionController->sendCAT(cmd);
        m_connectionController->sendCAT("FB;");
        // Set scroll wheel to control VFO B
        m_scrollVfoB = true;
        m_mouseVfoIndicatorA->setActiveVfo(true);
        m_mouseVfoIndicatorB->setActiveVfo(true);
    });

    connect(m_panadapterB, &PanadapterRhiWidget::frequencyRightDragged, this, [this](qint64 freq) {
        if (m_mouseQsyMode == 0) // Left Only — right-drag disabled
            return;
        if (!m_connectionController->isConnected() || freq <= 0)
            return;
        if (m_radioState->lockB())
            return;
        // L=A R=B mode: right-drag always tunes VFO B
        freq = adjustClickFreqForMode(freq, true);
        int stepHz = RadioUtils::tuningStepToHz(m_radioState->tuningStepB());
        qint64 snapped = (freq / stepHz) * stepHz;
        if (snapped <= 0)
            return;
        QString cmd = QString("FB%1;").arg(snapped, 11, 10, QChar('0'));
        m_connectionController->sendCAT(cmd);
        m_radioState->parseCATCommand(cmd);
    });
}

bool SpectrumController::eventFilter(QObject *watched, QEvent *event) {
    // Reposition span control buttons and VFO indicator when panadapter A resizes
    if (watched == m_panadapterA && event->type() == QEvent::Resize) {
        QResizeEvent *resizeEvent = static_cast<QResizeEvent *>(event);
        int w = resizeEvent->size().width();
        int h = resizeEvent->size().height();

        // Position buttons at lower right, above the frequency label bar (20px)
        // Triangle layout: C centered above, - and + below
        m_spanDownBtn->move(w - 70, h - 45);
        m_spanUpBtn->move(w - 35, h - 45);
        m_centerBtn->move(w - 52, h - 73);

        // VFO indicator at bottom-left corner
        m_vfoIndicatorA->move(0, h - 30);
        m_mouseVfoIndicatorA->move(K4Styles::Dimensions::VfoIndicatorWidth + 5, h - 50);

        // DX spot overlay covers the spectrum area (above waterfall)
        if (m_spotOverlayA) {
            int specHeight = static_cast<int>(h * m_panadapterA->spectrumRatio());
            m_spotOverlayA->setGeometry(0, 0, w, specHeight);
            m_spotOverlayA->raise();
        }
    }

    // Reposition span control buttons and VFO indicator when panadapter B resizes
    if (watched == m_panadapterB && event->type() == QEvent::Resize) {
        QResizeEvent *resizeEvent = static_cast<QResizeEvent *>(event);
        int w = resizeEvent->size().width();
        int h = resizeEvent->size().height();

        // Position B buttons at lower right, above the frequency label bar (20px)
        // Triangle layout: C centered above, - and + below
        m_spanDownBtnB->move(w - 70, h - 45);
        m_spanUpBtnB->move(w - 35, h - 45);
        m_centerBtnB->move(w - 52, h - 73);

        // VFO indicator at bottom-left corner
        m_vfoIndicatorB->move(0, h - 30);
        m_mouseVfoIndicatorB->move(K4Styles::Dimensions::VfoIndicatorWidth + 5, h - 50);

        // DX spot overlay covers the spectrum area (above waterfall)
        if (m_spotOverlayB) {
            int specHeight = static_cast<int>(h * m_panadapterB->spectrumRatio());
            m_spotOverlayB->setGeometry(0, 0, w, specHeight);
            m_spotOverlayB->raise();
        }
    }

    return QObject::eventFilter(watched, event);
}

qint64 SpectrumController::adjustClickFreqForMode(qint64 freq, bool vfoB) {
    // In CW mode, the dial frequency is offset from the RF frequency by cwPitch.
    // To hear a signal at RF frequency S, the dial must be set to S - cwPitch (CW)
    // or S + cwPitch (CW-R). xToFreq returns the actual RF frequency at the clicked
    // pixel, so we apply the offset here to get the correct dial frequency.
    RadioState::Mode mode = vfoB ? m_radioState->modeB() : m_radioState->mode();
    if (mode == RadioState::CW)
        return freq - m_radioState->cwPitch();
    if (mode == RadioState::CW_R)
        return freq + m_radioState->cwPitch();
    return freq;
}

void SpectrumController::updatePanadapterPassbands() {
    // Panadapter A always shows VFO A's own passband (it's VFO A's spectrum)
    quint64 rxA = m_radioState->vfoA();
    if (m_radioState->ritEnabled()) {
        qint64 adjusted = static_cast<qint64>(rxA) + m_radioState->ritXitOffset();
        if (adjusted > 0)
            rxA = static_cast<quint64>(adjusted);
    }
    m_panadapterA->setTunedFrequency(rxA);

    // Panadapter B always shows VFO B's own passband
    quint64 rxB = m_radioState->vfoB();
    if (m_radioState->ritEnabledB()) {
        qint64 adjusted = static_cast<qint64>(rxB) + m_radioState->ritXitOffsetB();
        if (adjusted > 0)
            rxB = static_cast<quint64>(adjusted);
    }
    m_panadapterB->setTunedFrequency(rxB);

    // Update secondary VFO overlays with RIT-adjusted positions
    // VFO B overlay on panadapter A (green passband showing where VFO B is listening)
    m_panadapterA->setSecondaryVfo(rxB, m_radioState->filterBandwidthB(),
                                   RadioState::modeToString(m_radioState->modeB()), m_radioState->ifShiftB(),
                                   m_radioState->dataSubModeB());
    // VFO A overlay on panadapter B
    m_panadapterB->setSecondaryVfo(rxA, m_radioState->filterBandwidth(), RadioState::modeToString(m_radioState->mode()),
                                   m_radioState->ifShift(), m_radioState->dataSubMode());
}

void SpectrumController::updateTxMarkers() {
    // TX VFO depends on split mode: VFO A (no split) or VFO B (split)
    // XIT offset shifts the TX frequency; RIT does not affect TX
    bool split = m_radioState->splitEnabled();
    bool bset = m_radioState->bSetEnabled();
    bool xit = m_radioState->xitEnabled();
    bool ritA = m_radioState->ritEnabled();
    bool ritB = m_radioState->ritEnabledB();
    // K4 routes XIT offset to the TX VFO's register:
    //   No split: RO (VFO A) — TX on VFO A
    //   Split:    RO$ (VFO B) — TX on VFO B
    int xitOffset = xit ? (split ? m_radioState->ritXitOffsetB() : m_radioState->ritXitOffset()) : 0;

    // TX dial frequency (before CW pitch — panadapter applies pitch offset internally)
    qint64 txVfoDial = split ? static_cast<qint64>(m_radioState->vfoB()) : static_cast<qint64>(m_radioState->vfoA());
    qint64 txFreq = txVfoDial + xitOffset;

    // Only show marker when the offset is actually non-zero (TX ≠ RX).
    // When RIT/XIT is on but offset is 0.00, TX = RX — no marker needed. Matches K4 behaviour.
    bool ritAShifted = ritA && (m_radioState->ritXitOffset() != 0);
    bool ritBShifted = ritB && (m_radioState->ritXitOffsetB() != 0);
    bool xitShifted = xit && (xitOffset != 0);

    // Panadapter A (VFO A spectrum):
    //   SPLIT on: always show — TX from VFO B, different VFO than this spectrum
    //   No split + BSET: real K4 shows no TX marker (user focused on VFO B)
    //   No split: when RIT A or XIT shifts TX != RX
    bool showTxOnA = split ? true : (bset ? false : (ritAShifted || xitShifted));
    // Panadapter B (VFO B spectrum):
    //   SPLIT + XIT: show TX marker (XIT shifts TX away from VFO B dial)
    //   SPLIT + RIT B: show (RIT shifts RX away from TX)
    //   No split + BSET: real K4 shows no TX marker (user focused on VFO B)
    //   No split: show when RIT A or XIT — TX from VFO A, different VFO than this spectrum
    bool showTxOnB = split ? (ritBShifted || xitShifted) : (bset ? false : (ritAShifted || xitShifted));

    m_panadapterA->setTxMarker(txFreq, showTxOnA);
    m_panadapterB->setTxMarker(txFreq, showTxOnB);
}

void SpectrumController::onSpectrumData(int receiver, const QByteArray &payload, int binsOffset, int binCount,
                                        qint64 centerFreq, qint32 sampleRate, float noiseFloor) {
    // Route spectrum data to appropriate panadapter
    // receiver: 0 = Main (VFO A), 1 = Sub (VFO B)
    if (receiver == 0) {
        m_panadapterA->updateSpectrum(payload, binsOffset, binCount, centerFreq, sampleRate, noiseFloor);
    } else if (receiver == 1) {
        m_panadapterB->updateSpectrum(payload, binsOffset, binCount, centerFreq, sampleRate, noiseFloor);
    }
}

void SpectrumController::onMiniSpectrumData(int receiver, const QByteArray &payload, int binsOffset, int binCount) {
    // Extract bins here to avoid changing VFOWidget/MiniPan interface
    QByteArray bins = QByteArray::fromRawData(payload.constData() + binsOffset, binCount);
    // Route Mini-PAN data based on receiver byte (0=Main/A, 1=Sub/B)
    if (receiver == 0 && m_vfoA->isMiniPanVisible()) {
        m_vfoA->updateMiniPan(bins);
    } else if (receiver == 1 && m_vfoB->isMiniPanVisible()) {
        m_vfoB->updateMiniPan(bins);
    }
}

void SpectrumController::checkAndHideMiniPanB() {
    // Auto-hide mini pan B if SUB RX is off and VFOs are on different bands
    int bandA = RadioUtils::getBandFromFrequency(m_radioState->vfoA());
    int bandB = RadioUtils::getBandFromFrequency(m_radioState->vfoB());
    bool differentBands = (bandA != bandB);

    if (!m_radioState->subReceiverEnabled() && differentBands) {
        if (m_radioState->miniPanBEnabled()) {
            m_radioState->setMiniPanBEnabled(false);
            m_connectionController->sendCAT("#MP$0;"); // Disable Mini-Pan B streaming
        }
        if (m_vfoB->isMiniPanVisible()) {
            m_vfoB->showNormal();
        }
    }
}

void SpectrumController::setPanadapterMode(PanadapterMode mode) {
    m_dualAltActive = (mode == PanadapterMode::DualAlt);

    // Get the box layout to switch direction for DualAlt (vertical stacking)
    auto *boxLayout = qobject_cast<QBoxLayout *>(m_spectrumContainer->layout());

    switch (mode) {
    case PanadapterMode::MainOnly:
        m_panadapterA->show();
        m_spectrumSeparator->hide();
        m_panadapterB->hide();
        if (boxLayout)
            boxLayout->setDirection(QBoxLayout::LeftToRight);
        break;
    case PanadapterMode::Dual:
        m_panadapterA->show();
        m_spectrumSeparator->show();
        m_panadapterB->show();
        if (boxLayout)
            boxLayout->setDirection(QBoxLayout::LeftToRight);
        m_spectrumSeparator->setFrameShape(QFrame::VLine);
        m_spectrumSeparator->setFixedWidth(K4Styles::Dimensions::SeparatorHeight);
        m_spectrumSeparator->setMaximumHeight(QWIDGETSIZE_MAX);
        break;
    case PanadapterMode::SubOnly:
        m_panadapterA->hide();
        m_spectrumSeparator->hide();
        m_panadapterB->show();
        if (boxLayout)
            boxLayout->setDirection(QBoxLayout::LeftToRight);
        break;
    case PanadapterMode::DualAlt:
        m_panadapterA->show();
        m_spectrumSeparator->show();
        m_panadapterB->show();
        if (boxLayout)
            boxLayout->setDirection(QBoxLayout::TopToBottom);
        m_spectrumSeparator->setFrameShape(QFrame::HLine);
        m_spectrumSeparator->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
        m_spectrumSeparator->setMaximumWidth(QWIDGETSIZE_MAX);
        break;
    }
}

void SpectrumController::setMouseQsyMode(int mode) {
    m_mouseQsyMode = mode;
}

void SpectrumController::setDxClusterController(DxClusterController *controller) {
    m_dxClusterController = controller;
    if (!controller)
        return;

    // Update overlays when spots change
    connect(controller, &DxClusterController::spotsUpdated, this, &SpectrumController::updateSpotOverlays);

    // Update overlays when frequency/span changes (panadapter center shifts)
    connect(m_radioState, &RadioState::frequencyChanged, this, &SpectrumController::updateSpotOverlays);
    connect(m_radioState, &RadioState::frequencyBChanged, this, &SpectrumController::updateSpotOverlays);
    connect(m_radioState, &RadioState::spanChanged, this, &SpectrumController::updateSpotOverlays);
    connect(m_radioState, &RadioState::spanBChanged, this, &SpectrumController::updateSpotOverlays);

    // Click-to-tune from spot overlay
    if (m_spotOverlayA) {
        connect(m_spotOverlayA, &DxSpotOverlay::spotClicked, this, [this](qint64 freq) {
            qint64 adjusted = adjustClickFreqForMode(freq, false);
            m_connectionController->sendCAT(QString("FA%1;").arg(adjusted, 11, 10, QChar('0')));
        });
    }
    if (m_spotOverlayB) {
        connect(m_spotOverlayB, &DxSpotOverlay::spotClicked, this, [this](qint64 freq) {
            qint64 adjusted = adjustClickFreqForMode(freq, true);
            m_connectionController->sendCAT(QString("FB%1;").arg(adjusted, 11, 10, QChar('0')));
        });
    }
}

void SpectrumController::updateSpotOverlays() {
    if (!m_dxClusterController)
        return;

    // Update panadapter A overlay
    if (m_spotOverlayA && m_panadapterA) {
        qint64 center = m_panadapterA->centerFreq();
        int span = m_panadapterA->span();
        if (center > 0 && span > 0) {
            qint64 startFreq = center - span / 2;
            qint64 endFreq = center + span / 2;
            auto spots = m_dxClusterController->spotsForFrequencyRange(startFreq, endFreq);
            m_spotOverlayA->setFrequencyRange(center, span);
            m_spotOverlayA->setSpots(spots);
            if (!spots.isEmpty())
                qDebug() << "[DxSpot] Overlay A:" << spots.size() << "spots in range" << startFreq << "-" << endFreq
                         << "overlay size:" << m_spotOverlayA->size();
        }
    }

    // Update panadapter B overlay
    if (m_spotOverlayB && m_panadapterB && m_panadapterB->isVisible()) {
        qint64 center = m_panadapterB->centerFreq();
        int span = m_panadapterB->span();
        if (center > 0 && span > 0) {
            qint64 startFreq = center - span / 2;
            qint64 endFreq = center + span / 2;
            auto spots = m_dxClusterController->spotsForFrequencyRange(startFreq, endFreq);
            m_spotOverlayB->setFrequencyRange(center, span);
            m_spotOverlayB->setSpots(spots);
        }
    }
}
