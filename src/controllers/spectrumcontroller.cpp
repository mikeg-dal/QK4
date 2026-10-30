#include "spectrumcontroller.h"
#include "logging/logcategories.h"
#include "connectioncontroller.h"
#include "dsp/panadapter_rhi.h"
#include "dsp/spectrumgeometry.h"
#include "models/radiostate.h"
#include "ui/styling/k4styles.h"
#include "ui/widgets/vfowidget.h"
#include "dxclustercontroller.h"
#include "settings/radiosettings.h"
#include "utils/bandplan.h"
#include "utils/radioutils.h"

#include "ui/overlays/dxspotoverlay.h"
#include "ui/overlays/mousevfoindicator.h"
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLoggingCategory>
#include <QPushButton>
#include <QResizeEvent>
#include <limits>

// ============== SpectrumController Implementation ==============

SpectrumController::SpectrumController(ConnectionController *conn, RadioState *radioState, QObject *parent)
    : QObject(parent), m_connectionController(conn), m_radioState(radioState), m_mouseQsyMode(0) {}

QWidget *SpectrumController::spectrumContainer() const {
    return m_spectrumContainer;
}
PanadapterRhiWidget *SpectrumController::panadapterA() const {
    return m_panA.pad;
}
PanadapterRhiWidget *SpectrumController::panadapterB() const {
    return m_panB.pad;
}

void SpectrumController::clearDisplays() {
    for (PanUi *ui : {&m_panA, &m_panB}) {
        if (ui->pad)
            ui->pad->clear();
        // clear() drops the band plan, so the next frequency must push it even within the same band.
        ui->lastPlanBand = -2;
        ui->lastRegion = -2;
    }
    // WHY: RadioState::reset() clears the mini-pan flags without emitting, so a mini-pan left open
    // here would survive a reconnect whose RDY dump reports #MP0 (false → false, no signal).
    if (m_vfoA)
        m_vfoA->showNormal();
    if (m_vfoB)
        m_vfoB->showNormal();
}

void SpectrumController::setAmplitudeUnits(bool useSUnits) {
    for (PanUi *ui : {&m_panA, &m_panB})
        if (ui->pad)
            ui->pad->setAmplitudeUnits(useSUnits);
}

void SpectrumController::setFskMarkTone(int toneHz) {
    for (PanUi *ui : {&m_panA, &m_panB})
        if (ui->pad)
            ui->pad->setFskMarkTone(toneHz);
}

void SpectrumController::setWaterfallHeight(int percent) {
    for (PanUi *ui : {&m_panA, &m_panB})
        if (ui->pad)
            ui->pad->setWaterfallHeight(percent);
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
    auto *layout = new QHBoxLayout(m_spectrumContainer);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);

    // Main panadapter for VFO A (left side) - QRhiWidget with Metal/DirectX/Vulkan
    m_panA.pad = new PanadapterRhiWidget(m_spectrumContainer);
    m_panA.pad->setObjectName("A");
    // dB range and spectrum/waterfall split follow the radio's #SCL/#REF/#WFH (wired below)
    m_panA.pad->setGridEnabled(true);
    // Primary VFO A uses default cyan passband
    // Secondary VFO B uses green passband
    QColor vfoBPassbandAlpha(K4Styles::Colors::VfoBGreen);
    vfoBPassbandAlpha.setAlpha(K4Styles::Colors::Alpha::PassbandFill);
    m_panA.pad->setSecondaryPassbandColor(vfoBPassbandAlpha);
    m_panA.pad->setSecondaryMarkerColor(QColor(K4Styles::Colors::VfoBGreen));
    m_panA.pad->setSecondaryVisible(true);
    layout->addWidget(m_panA.pad);

    // Vertical separator between A/B panadapters (visible only in Dual mode)
    m_spectrumSeparator = new QFrame(m_spectrumContainer);
    m_spectrumSeparator->setFrameShape(QFrame::VLine);
    m_spectrumSeparator->setFrameShadow(QFrame::Plain);
    m_spectrumSeparator->setStyleSheet(QString("color: %1;").arg(K4Styles::Colors::PanelBorder));
    m_spectrumSeparator->setFixedWidth(K4Styles::Dimensions::SeparatorHeight);
    m_spectrumSeparator->hide();
    layout->addWidget(m_spectrumSeparator);

    // Sub panadapter for VFO B (right side) - QRhiWidget with Metal/DirectX/Vulkan
    m_panB.pad = new PanadapterRhiWidget(m_spectrumContainer);
    m_panB.pad->setObjectName("B");
    // dB range and spectrum/waterfall split follow the radio's #SCL/#REF$/#WFH (wired below)
    m_panB.pad->setGridEnabled(true);
    // Primary VFO B uses green passband
    m_panB.pad->setPassbandColor(vfoBPassbandAlpha);
    m_panB.pad->setFrequencyMarkerColor(QColor(K4Styles::Colors::VfoBGreen));
    // Secondary VFO A uses cyan passband
    QColor vfoAPassbandAlpha(K4Styles::Colors::VfoACyan);
    vfoAPassbandAlpha.setAlpha(K4Styles::Colors::Alpha::PassbandFill);
    m_panB.pad->setSecondaryPassbandColor(vfoAPassbandAlpha);
    m_panB.pad->setSecondaryMarkerColor(QColor(K4Styles::Colors::VfoACyan));
    m_panB.pad->setSecondaryVisible(true);
    layout->addWidget(m_panB.pad);
    m_panB.pad->hide(); // Start hidden (MainOnly mode)

    // Span control buttons - overlay on panadapter (lower right, above freq labels)
    const QString btnStyle =
        QString("QPushButton { background: %4; color: %1; "
                "border: 1px solid %2; border-radius: 4px; "
                "font-size: %3px; font-weight: bold; min-width: 28px; min-height: 24px; }"
                "QPushButton:hover { background: %5; }")
            .arg(K4Styles::Colors::TextPrimary)
            .arg(K4Styles::Colors::MutedGraphic)
            .arg(K4Styles::Dimensions::FontSizeHeading)
            .arg(K4Styles::cssRgba(K4Styles::Colors::Black, K4Styles::Colors::Alpha::SpanButtonScrim))
            .arg(K4Styles::cssRgba(K4Styles::Colors::GradientTop, K4Styles::Colors::Alpha::SpanButtonHover));

    // VFO indicator badges - bottom-left corner of waterfall, tab shape with top-right rounded
    const QString vfoIndicatorStyle = QString("QLabel { background: %1; color: %4; "
                                              "font-size: %2px; font-weight: bold; "
                                              "border-top-left-radius: 0px; border-top-right-radius: %3px; "
                                              "border-bottom-left-radius: 0px; border-bottom-right-radius: 0px; }")
                                          .arg(K4Styles::Colors::OverlayBackground)
                                          .arg(K4Styles::Dimensions::FontSizeIndicator)
                                          .arg(K4Styles::Dimensions::BorderRadiusLarge)
                                          .arg(K4Styles::Colors::TextOnAccent);

    createPanOverlays(m_panA, btnStyle, vfoIndicatorStyle);
    createPanOverlays(m_panB, btnStyle, vfoIndicatorStyle);
    m_panA.vfoIndicator->setText("A");
    m_panB.vfoIndicator->setText("B");

    // Apply persisted spot label font size, and live-update both overlays when the user
    // changes it in Options. Reuse the existing dxClusterSettingsChanged() signal —
    // setFontPixelSize() is a no-op when the value hasn't moved, so callsign/age changes
    // that also fire this signal don't cause unnecessary repaints.
    const int spotFontSize = RadioSettings::instance()->dxClusterSpotFontSize();
    m_panA.spotOverlay->setFontPixelSize(spotFontSize);
    m_panB.spotOverlay->setFontPixelSize(spotFontSize);
    connect(RadioSettings::instance(), &RadioSettings::dxClusterSettingsChanged, this, [this]() {
        const int fs = RadioSettings::instance()->dxClusterSpotFontSize();
        m_panA.spotOverlay->setFontPixelSize(fs);
        m_panB.spotOverlay->setFontPixelSize(fs);
    });

    wirePan(false);
    wirePan(true);

    // RadioState display properties → panadapter configuration
    connect(m_radioState, &RadioState::refLevelChanged, this, [this](int level) { m_panA.pad->setRefLevel(level); });
    connect(m_radioState, &RadioState::refLevelBChanged, this, [this](int level) { m_panB.pad->setRefLevel(level); });
    connect(m_radioState, &RadioState::scaleChanged, this, [this](int scale) {
        m_panA.pad->setScale(scale);
        m_panB.pad->setScale(scale);
    });
    connect(m_radioState, &RadioState::spanChanged, this, [this](int spanHz) { m_panA.pad->setSpan(spanHz); });
    connect(m_radioState, &RadioState::spanBChanged, this, [this](int spanHz) { m_panB.pad->setSpan(spanHz); });
    connect(m_radioState, &RadioState::waterfallHeightChanged, this, [this](int percent) {
        m_panA.pad->setWaterfallHeight(percent);
        m_panB.pad->setWaterfallHeight(percent);
        m_vfoA->setMiniPanWaterfallHeight(percent);
        m_vfoB->setMiniPanWaterfallHeight(percent);
        // The spot overlays cover the spectrum area, which the ratio just moved.
        layoutPanOverlays(m_panA, m_panA.pad->size());
        layoutPanOverlays(m_panB, m_panB.pad->size());
    });
    // WHY: the K4 pushes #MP / #MP$ whenever the mini-pan is toggled on the radio itself, and
    // reports -1 (treated as off) when it withdraws one. The VFO view follows the radio's state
    // so a front-panel toggle opens/closes ours too; our own clicks go through the same path.
    connect(m_radioState, &RadioState::miniPanAEnabledChanged, this, [this](bool enabled) {
        if (enabled)
            m_vfoA->showMiniPan();
        else
            m_vfoA->showNormal();
    });
    connect(m_radioState, &RadioState::miniPanBEnabledChanged, this, [this](bool enabled) {
        if (enabled)
            m_vfoB->showMiniPan();
        else
            m_vfoB->showNormal();
    });
    connect(m_radioState, &RadioState::averagingChanged, this, [this](int level) {
        m_panA.pad->setAveraging(level);
        m_panB.pad->setAveraging(level);
        m_vfoA->setMiniPanAveraging(level);
        m_vfoB->setMiniPanAveraging(level);
    });
    // VFO A/B cursor visibility drives BOTH panadapters: each pad shows the
    // owning VFO's cursor as its primary and the other VFO's as a secondary
    // overlay. CURS A must therefore toggle pad A's primary AND pad B's
    // secondary; CURS B the reverse. Without the secondary wiring, a CURS
    // toggle has no visible effect whenever the owning pad is hidden (e.g.
    // CURS B in DISPLAY-A-only mode used to toggle only the hidden pad B).
    connect(m_radioState, &RadioState::vfoACursorChanged, this, [this](int mode) {
        const bool visible = (mode == 1 || mode == 2);
        m_panA.pad->setCursorVisible(visible);
        m_panB.pad->setSecondaryVisible(visible);
    });
    connect(m_radioState, &RadioState::vfoBCursorChanged, this, [this](int mode) {
        const bool visible = (mode == 1 || mode == 2);
        m_panB.pad->setCursorVisible(visible);
        m_panA.pad->setSecondaryVisible(visible);
    });
    connect(m_radioState, &RadioState::dualPanModeLcdChanged, this, [this](int mode) {
        switch (mode) {
        case 0:
            setPanadapterMode(PanadapterMode::MainOnly);
            break;
        case 1:
            setPanadapterMode(PanadapterMode::SubOnly);
            break;
        case 2:
            setPanadapterMode(PanadapterMode::Dual);
            break;
        }
    });

    // Update panadapter when frequency/mode changes
    connect(m_radioState, &RadioState::frequencyChanged, this, [this](quint64) {
        updatePanadapterPassbands();
        updateTxMarkers();
        updateBandPlan(false);
    });
    connect(m_radioState, &RadioState::modeChanged, this,
            [this](RadioState::Mode mode) { m_panA.pad->setMode(RadioState::modeToString(mode)); });
    connect(m_radioState, &RadioState::dataSubModeChanged, this,
            [this](int subMode) { m_panA.pad->setDataSubMode(subMode); });
    connect(m_radioState, &RadioState::filterBandwidthChanged, this,
            [this](int bw) { m_panA.pad->setFilterBandwidth(bw); });
    connect(m_radioState, &RadioState::ifShiftChanged, this, [this](int shift) { m_panA.pad->setIfShift(shift); });
    connect(m_radioState, &RadioState::cwPitchChanged, this, [this](int pitch) { m_panA.pad->setCwPitch(pitch); });

    // Notch filter visualization
    connect(m_radioState, &RadioState::notchChanged, this, [this]() {
        bool enabled = m_radioState->manualNotchEnabled();
        int pitch = m_radioState->manualNotchPitch();
        m_panA.pad->setNotchFilter(enabled, pitch);
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

    // VFO B connections
    connect(m_radioState, &RadioState::frequencyBChanged, this, [this](quint64) {
        updatePanadapterPassbands();
        updateTxMarkers();
        updateBandPlan(true);
    });
    connect(m_radioState, &RadioState::modeBChanged, this,
            [this](RadioState::Mode mode) { m_panB.pad->setMode(RadioState::modeToString(mode)); });
    connect(m_radioState, &RadioState::dataSubModeBChanged, this,
            [this](int subMode) { m_panB.pad->setDataSubMode(subMode); });
    connect(m_radioState, &RadioState::filterBandwidthBChanged, this,
            [this](int bw) { m_panB.pad->setFilterBandwidth(bw); });
    connect(m_radioState, &RadioState::ifShiftBChanged, this, [this](int shift) { m_panB.pad->setIfShift(shift); });
    connect(m_radioState, &RadioState::cwPitchChanged, this, [this](int pitch) { m_panB.pad->setCwPitch(pitch); });
    connect(m_radioState, &RadioState::notchBChanged, this, [this]() {
        bool enabled = m_radioState->manualNotchEnabledB();
        int pitch = m_radioState->manualNotchPitchB();
        m_panB.pad->setNotchFilter(enabled, pitch);
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

    // WHY the secondary (other VFO's) passband is only ever set by updatePanadapterPassbands():
    // it applies the other receiver's RIT offset. Separate lambdas that pushed the raw dial ran
    // after it on a frequency change and overwrote the RIT-adjusted position. Frequency changes
    // already reach it above; mode, sub-mode, bandwidth and IF shift changes go through it here.
    connect(m_radioState, &RadioState::modeChanged, this, &SpectrumController::updatePanadapterPassbands);
    connect(m_radioState, &RadioState::dataSubModeChanged, this, &SpectrumController::updatePanadapterPassbands);
    connect(m_radioState, &RadioState::filterBandwidthChanged, this, &SpectrumController::updatePanadapterPassbands);
    connect(m_radioState, &RadioState::ifShiftChanged, this, &SpectrumController::updatePanadapterPassbands);
    connect(m_radioState, &RadioState::modeBChanged, this, &SpectrumController::updatePanadapterPassbands);
    connect(m_radioState, &RadioState::dataSubModeBChanged, this, &SpectrumController::updatePanadapterPassbands);
    connect(m_radioState, &RadioState::filterBandwidthBChanged, this, &SpectrumController::updatePanadapterPassbands);
    connect(m_radioState, &RadioState::ifShiftBChanged, this, &SpectrumController::updatePanadapterPassbands);

    // Band-plan overlay: region drives the segments; the toggle drives visibility. Both come
    // from the RadioSettings singleton (written by the Station options page), so the controller
    // listens directly — no coupling to the page.
    RadioSettings *settings = RadioSettings::instance();
    connect(settings, &RadioSettings::iaruRegionChanged, this, [this](int) {
        updateBandPlan(false);
        updateBandPlan(true);
    });
    connect(settings, &RadioSettings::bandPlanOverlayEnabledChanged, this, [this](bool enabled) {
        m_panA.pad->setBandPlanVisible(enabled);
        m_panB.pad->setBandPlanVisible(enabled);
    });
    const bool overlayOn = settings->bandPlanOverlayEnabled();
    m_panA.pad->setBandPlanVisible(overlayOn);
    m_panB.pad->setBandPlanVisible(overlayOn);
    updateBandPlan(false);
    updateBandPlan(true);
}

void SpectrumController::createPanOverlays(PanUi &ui, const QString &buttonStyle, const QString &indicatorStyle) {
    // Creation order is stacking order: buttons, VFO badge, mouse-VFO indicator, then the spot
    // overlay (which updateSpotOverlays() lowers beneath the rest).
    auto makeButton = [&](const char *text) {
        auto *btn = new QPushButton(text, ui.pad);
        btn->setStyleSheet(buttonStyle);
        btn->setFixedSize(K4Styles::Dimensions::ButtonHeightSmall, K4Styles::Dimensions::ButtonHeightMini);
        return btn;
    };
    ui.zoomOutBtn = makeButton("-");
    ui.zoomInBtn = makeButton("+");
    ui.centerBtn = makeButton("C");

    ui.vfoIndicator = new QLabel(ui.pad);
    ui.vfoIndicator->setStyleSheet(indicatorStyle);
    ui.vfoIndicator->setFixedSize(K4Styles::Dimensions::VfoIndicatorWidth, K4Styles::Dimensions::VfoIndicatorHeight);
    ui.vfoIndicator->setAlignment(Qt::AlignCenter);

    // Mouse VFO focus indicator - shows which VFO the scroll wheel controls
    ui.mouseVfoIndicator = new MouseVfoIndicator(ui.pad);

    // DX Spot overlay (transparent child widget for callsign labels)
    ui.spotOverlay = new DxSpotOverlay(ui.pad);
    ui.spotOverlay->setLeftReservedPx(ui.pad->amplitudeAxisWidthPx());
    ui.spotOverlay->show();

    layoutPanOverlays(ui, ui.pad->size());

    // Repositions the overlays on resize and refreshes spots when the pad is shown.
    ui.pad->installEventFilter(this);
}

void SpectrumController::wirePan(bool vfoB) {
    PanUi &ui = pan(vfoB);
    PanadapterRhiWidget *pad = ui.pad;

    connect(ui.zoomOutBtn, &QPushButton::clicked, this, [this, vfoB]() { zoomSpan(vfoB, false); });
    connect(ui.zoomInBtn, &QPushButton::clicked, this, [this, vfoB]() { zoomSpan(vfoB, true); });
    connect(ui.centerBtn, &QPushButton::clicked, this,
            [this, vfoB]() { m_connectionController->sendCAT(vfoB ? "FC$;" : "FC;"); });

    // WHY: cluster spots report DIAL frequencies (the spotter's dial reading), so we tune our
    // dial to that exact freq — no cwPitch math. Click-tune from the panadapter spectrum is
    // different: that click x maps to an RF position via xToFreq, which is why it needs the
    // pitch correction. Spot freq is already a dial freq, so applying it would mistune by ~pitch.
    // Left-clicking a spot tunes the pad's own VFO.
    connect(ui.spotOverlay, &DxSpotOverlay::spotClicked, this, [this, vfoB](qint64 freq) { tuneToSpot(vfoB, freq); });
    // Right-clicking a spot tunes VFO B to its exact dial frequency — mirrors the panadapter's
    // "right-click → VFO B" click-tune convention, but snapped to the spot instead of the mouse-x.
    // Disabled in the K4's "Left Only" mouse-QSY mode (m_mouseQsyMode == 0), same gate as the
    // panadapter right-click handlers.
    connect(ui.spotOverlay, &DxSpotOverlay::spotRightClicked, this, [this](qint64 freq) {
        if (m_mouseQsyMode != 0)
            tuneToSpot(true, freq);
    });

    // Left click/drag tunes the pad's own VFO, except that in L=A R=B mode (m_mouseQsyMode == 1)
    // a left click on Pan B tunes VFO A.
    auto leftTarget = [this, vfoB]() { return vfoB && m_mouseQsyMode != 1; };
    connect(pad, &PanadapterRhiWidget::frequencyClicked, this,
            [this, leftTarget](qint64 freq) { tuneFromPan(leftTarget(), freq, false); });
    connect(pad, &PanadapterRhiWidget::frequencyDragged, this,
            [this, leftTarget](qint64 freq) { tuneFromPan(leftTarget(), freq, true); });
    // Right click/drag always tunes VFO B; disabled in "Left Only" mode.
    connect(pad, &PanadapterRhiWidget::frequencyRightClicked, this, [this](qint64 freq) {
        if (m_mouseQsyMode != 0)
            tuneFromPan(true, freq, false);
    });
    connect(pad, &PanadapterRhiWidget::frequencyRightDragged, this, [this](qint64 freq) {
        if (m_mouseQsyMode != 0)
            tuneFromPan(true, freq, true);
    });

    connect(pad, &PanadapterRhiWidget::frequencyScrolled, this, &SpectrumController::scrollTune);
    // Shift+Wheel: scale is global, so both pads adjust the same value.
    connect(pad, &PanadapterRhiWidget::scaleScrolled, this, &SpectrumController::adjustScaleFromWheel);
    // Ctrl+Wheel: the pad's own receiver's reference level.
    connect(pad, &PanadapterRhiWidget::refLevelScrolled, this,
            [this, vfoB](int steps) { adjustRefLevelFromWheel(vfoB, steps); });

    // Debug: diagnose QRhiWidget issues
    connect(pad, &QRhiWidget::renderFailed, this, [vfoB]() {
        qCCritical(lcSpectrum) << (vfoB ? "PanadapterB" : "PanadapterA")
                               << "renderFailed() — QRhi could not be obtained";
    });
}

void SpectrumController::layoutPanOverlays(PanUi &ui, const QSize &size) {
    const int w = size.width();
    const int h = size.height();

    // Span buttons at lower right, above the frequency label bar (20px)
    // Triangle layout: C centered above, - and + below
    ui.zoomOutBtn->move(w - 70, h - 45);
    ui.zoomInBtn->move(w - 35, h - 45);
    ui.centerBtn->move(w - 52, h - 73);

    // VFO indicator at bottom-left corner, flush with edges
    ui.vfoIndicator->move(0, h - 30);
    ui.mouseVfoIndicator->move(K4Styles::Dimensions::VfoIndicatorWidth + 5, h - 50);

    // DX spot overlay covers the spectrum area (above waterfall)
    const int specHeight = static_cast<int>(h * ui.pad->spectrumRatio());
    ui.spotOverlay->setGeometry(0, 0, w, specHeight);
    ui.spotOverlay->lower(); // keep below the (mouse-transparent) band-plan strip — see updateSpotOverlay()
}

void SpectrumController::setScrollVfo(bool vfoB) {
    m_scrollVfoB = vfoB;
    m_panA.mouseVfoIndicator->setActiveVfo(vfoB);
    m_panB.mouseVfoIndicator->setActiveVfo(vfoB);
}

void SpectrumController::sendVfoFrequency(bool vfoB, qint64 freq, bool dragging) {
    if (vfoB ? m_radioState->lockB() : m_radioState->lockA())
        return;
    const QString vfo = vfoB ? "FB" : "FA";
    const QString cmd = QString("%1%2;").arg(vfo).arg(freq, 11, 10, QChar('0'));
    m_connectionController->sendCAT(cmd);
    if (dragging) {
        // Update local state immediately for responsive UI (K4 doesn't echo SET commands)
        m_radioState->parseCATCommand(cmd);
        return;
    }
    // Request frequency back to update UI (K4 doesn't echo SET commands)
    m_connectionController->sendCAT(vfo + ";");
    // A click selects that VFO as the working VFO — route the scroll wheel to it.
    setScrollVfo(vfoB);
}

void SpectrumController::tuneFromPan(bool vfoB, qint64 freq, bool dragging) {
    // Guard: only send if connected and frequency is valid
    if (!m_connectionController->isConnected() || freq <= 0)
        return;
    // CW/CW-R: the click is an RF position; convert it to the dial frequency for that VFO.
    freq = adjustClickFreqForMode(freq, vfoB);
    // Frequency is snapped to the current tuning rate step for consistent behavior
    const int stepHz = RadioUtils::tuningStepToHz(vfoB ? m_radioState->tuningStepB() : m_radioState->tuningStep());
    const qint64 snapped = RadioUtils::snapFreqToStep(freq, stepHz);
    if (snapped <= 0)
        return;
    sendVfoFrequency(vfoB, snapped, dragging);
}

void SpectrumController::tuneToSpot(bool vfoB, qint64 freq) {
    if (!m_connectionController->isConnected() || freq <= 0)
        return;
    sendVfoFrequency(vfoB, freq, false);
}

void SpectrumController::scrollTune(int steps) {
    if (!m_connectionController->isConnected())
        return;
    // Scroll follows the mouse VFO focus indicator (last clicked VFO)
    const bool tuneB = m_scrollVfoB;
    if (tuneB ? m_radioState->lockB() : m_radioState->lockA())
        return;
    const quint64 currentFreq = tuneB ? m_radioState->vfoB() : m_radioState->vfoA();
    const int stepHz = RadioUtils::tuningStepToHz(tuneB ? m_radioState->tuningStepB() : m_radioState->tuningStep());
    const qint64 newFreq =
        RadioUtils::snapFreqToStep(static_cast<qint64>(currentFreq), stepHz) + static_cast<qint64>(steps) * stepHz;
    if (newFreq > 0) {
        const QString vfo = tuneB ? "FB" : "FA";
        const QString cmd = QString("%1%2;").arg(vfo).arg(static_cast<quint64>(newFreq), 11, 10, QChar('0'));
        m_connectionController->sendCAT(cmd);
        m_radioState->parseCATCommand(cmd);
    }
}

void SpectrumController::zoomSpan(bool vfoB, bool zoomIn) {
    // K4 span steps; "-" zooms out (wider span), "+" zooms in (narrower). Sub uses the $ suffix.
    const int currentSpan = vfoB ? m_radioState->spanHzB() : m_radioState->spanHz();
    const int newSpan = RadioUtils::spanAfterZoom(currentSpan, zoomIn);
    if (newSpan == currentSpan)
        return;
    if (vfoB) {
        m_radioState->setSpanHzB(newSpan);
        m_connectionController->sendCAT(QString("#SPN$%1;").arg(newSpan));
    } else {
        m_radioState->setSpanHz(newSpan);
        m_connectionController->sendCAT(QString("#SPN%1;").arg(newSpan));
    }
}

void SpectrumController::adjustScaleFromWheel(int steps) {
    if (!m_connectionController->isConnected())
        return;
    int currentScale = m_radioState->scale();
    if (currentScale < 0)
        currentScale = SpectrumDisplayLimits::ScaleDefault; // not yet received from radio
    const int newScale = qBound(SpectrumDisplayLimits::ScaleMin, currentScale + steps * 5,
                                SpectrumDisplayLimits::ScaleMax); // 5 dB per step
    m_connectionController->sendCAT(QString("#SCL%1;").arg(newScale));
    // Optimistic update (scale is global) - updates both panadapters via signal
    m_radioState->setScale(newScale);
}

void SpectrumController::adjustRefLevelFromWheel(bool vfoB, int steps) {
    // Blocked while AutoRef is active: see docs/k4-protocol-quirks.md §13.
    if (!m_connectionController->isConnected() || m_radioState->autoRefLevel())
        return;
    int currentRef = vfoB ? m_radioState->refLevelB() : m_radioState->refLevel();
    if (currentRef < SpectrumDisplayLimits::RefLevelMin)
        currentRef = SpectrumDisplayLimits::RefLevelDefault; // not yet received
    const int newRef =
        qBound(SpectrumDisplayLimits::RefLevelMin, currentRef + steps, SpectrumDisplayLimits::RefLevelMax);
    m_connectionController->sendCAT(QString(vfoB ? "#REF$%1;" : "#REF%1;").arg(newRef));
    // Optimistic update
    pan(vfoB).pad->setRefLevel(newRef);
}

void SpectrumController::updateBandPlan(bool vfoB) {
    PanUi &ui = pan(vfoB);
    const int region = RadioSettings::instance()->iaruRegion();
    const qint64 freq = static_cast<qint64>(vfoB ? m_radioState->vfoB() : m_radioState->vfoA());
    const int planBand = BandPlan::planBandIndex(region, freq);
    if (planBand == ui.lastPlanBand && region == ui.lastRegion)
        return; // same plan band + region → segments unchanged; the overlay re-maps on its own
    ui.lastPlanBand = planBand;
    ui.lastRegion = region;
    ui.pad->setBandPlan(BandPlan::bandName(freq), BandPlan::segmentsForBand(region, freq),
                        BandPlan::markersForBand(region, freq));
}

bool SpectrumController::eventFilter(QObject *watched, QEvent *event) {
    for (bool vfoB : {false, true}) {
        PanUi &ui = pan(vfoB);
        if (watched != ui.pad)
            continue;
        if (event->type() == QEvent::Resize)
            layoutPanOverlays(ui, static_cast<QResizeEvent *>(event)->size());
        else if (event->type() == QEvent::Show)
            // WHY: spot updates skip a hidden pad, so one that becomes visible would otherwise keep
            // the labels from before it was hidden until the next spot or frequency change.
            updateSpotOverlay(vfoB);
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
    m_panA.pad->setTunedFrequency(rxA);

    // Panadapter B always shows VFO B's own passband
    quint64 rxB = m_radioState->vfoB();
    if (m_radioState->ritEnabledB()) {
        qint64 adjusted = static_cast<qint64>(rxB) + m_radioState->ritXitOffsetB();
        if (adjusted > 0)
            rxB = static_cast<quint64>(adjusted);
    }
    m_panB.pad->setTunedFrequency(rxB);

    // Update secondary VFO overlays with RIT-adjusted positions
    // VFO B overlay on panadapter A (green passband showing where VFO B is listening)
    m_panA.pad->setSecondaryVfo(rxB, m_radioState->filterBandwidthB(), RadioState::modeToString(m_radioState->modeB()),
                                m_radioState->ifShiftB(), m_radioState->dataSubModeB());
    // VFO A overlay on panadapter B
    m_panB.pad->setSecondaryVfo(rxA, m_radioState->filterBandwidth(), RadioState::modeToString(m_radioState->mode()),
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

    m_panA.pad->setTxMarker(txFreq, showTxOnA);
    m_panB.pad->setTxMarker(txFreq, showTxOnB);
}

void SpectrumController::onSpectrumData(int receiver, const QByteArray &payload, int binsOffset, int binCount,
                                        qint64 centerFreq, qint32 sampleRate, float noiseFloor) {
    // Route spectrum data to appropriate panadapter
    // receiver: 0 = Main (VFO A), 1 = Sub (VFO B)
    if (receiver == 0) {
        m_panA.pad->updateSpectrum(payload, binsOffset, binCount, centerFreq, sampleRate, noiseFloor);
    } else if (receiver == 1) {
        m_panB.pad->updateSpectrum(payload, binsOffset, binCount, centerFreq, sampleRate, noiseFloor);
    }
}

void SpectrumController::onMiniSpectrumData(int receiver, const QByteArray &payload, int binsOffset, int binCount) {
    // binsOffset and binCount come off the wire; never view bytes past the payload.
    if (SpectrumGeometry::usableBinCount(payload.size(), binsOffset, binCount, std::numeric_limits<int>::max()) == 0)
        return;
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
    switch (mode) {
    case PanadapterMode::MainOnly:
        m_panA.pad->show();
        m_spectrumSeparator->hide();
        m_panB.pad->hide();
        break;
    case PanadapterMode::Dual:
        m_panA.pad->show();
        m_spectrumSeparator->show();
        m_panB.pad->show();
        break;
    case PanadapterMode::SubOnly:
        m_panA.pad->hide();
        m_spectrumSeparator->hide();
        m_panB.pad->show();
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

    // Click-to-tune is wired in setupSpectrumUI() (wirePan) right after the overlays are created.
    // It cannot be done here because mainwindow may call setDxClusterController() before
    // the overlays exist.
}

void SpectrumController::updateSpotOverlays() {
    updateSpotOverlay(false);
    updateSpotOverlay(true);
}

void SpectrumController::updateSpotOverlay(bool vfoB) {
    PanUi &ui = pan(vfoB);
    if (!m_dxClusterController || !ui.spotOverlay || !ui.pad || !ui.pad->isVisible())
        return;

    // WHY: cluster spot freqs are dial freqs, so use raw centerFreq (matches the bottom
    // dial-frequency labels, which are positioned the same way). Don't use the IF-shifted
    // display centre — that would offset spot labels relative to where the user's dial is.
    const qint64 center = ui.pad->centerFreq();
    const int span = ui.pad->span();
    if (center > 0 && span > 0) {
        const qint64 startFreq = center - span / 2;
        const qint64 endFreq = center + span / 2;
        const auto spots = m_dxClusterController->spotsForFrequencyRange(startFreq, endFreq);
        ui.spotOverlay->setFrequencyRange(center, span);
        ui.spotOverlay->setSpots(spots);
        if (!spots.isEmpty())
            qCDebug(lcSpectrum) << "Overlay" << (vfoB ? "B:" : "A:") << spots.size() << "spots in range" << startFreq
                                << "-" << endFreq << "overlay size:" << ui.spotOverlay->size();
    } else {
        // WHY: still propagate clears when freq/span aren't valid yet (e.g., K4 not streaming).
        // Otherwise stale labels from a prior layout remain on screen after spots are cleared.
        ui.spotOverlay->setSpots({});
    }
    // WHY: keep the spot overlay at the BOTTOM of the panadapter's child stack so the band-plan
    // strip (and the dBm/freq scale overlays) stay permanently above it. Those overlays are all
    // WA_TransparentForMouseEvents, so the spot overlay — the only non-transparent layer in the
    // spectrum area — still receives label clicks even from underneath, and a child always
    // composes above its parent's RHI surface. Using raise() here instead made the overlay fight
    // the band strip's per-frame raise(), so callsigns behind the strip flickered in front of it.
    ui.spotOverlay->lower();
}
