#include "sidecontroldisplaycontroller.h"

#include "models/radiostate.h"
#include "ui/widgets/sidecontrolpanel.h"

#include <QtMath>

SideControlDisplayController::SideControlDisplayController(RadioState *radioState, SideControlPanel *panel,
                                                           QObject *parent)
    : QObject(parent), m_radioState(radioState), m_panel(panel) {

    // === BW / SHIFT / HI / LO → side panel (BSET-aware) ===
    connect(m_radioState, &RadioState::filterBandwidthChanged, this,
            &SideControlDisplayController::refreshFilterDisplay);
    connect(m_radioState, &RadioState::ifShiftChanged, this, &SideControlDisplayController::refreshFilterDisplay);
    connect(m_radioState, &RadioState::filterBandwidthBChanged, this,
            &SideControlDisplayController::refreshFilterDisplay);
    connect(m_radioState, &RadioState::ifShiftBChanged, this, &SideControlDisplayController::refreshFilterDisplay);
    connect(m_radioState, &RadioState::bSetChanged, this, &SideControlDisplayController::refreshFilterDisplay);
    // B-SET also flips the active-receiver indicator color on the BW/SHFT
    // panel (cyan for Main RX, green for Sub RX) — match which VFO the
    // filter knobs are currently driving.
    connect(m_radioState, &RadioState::bSetChanged, m_panel, &SideControlPanel::setActiveReceiver);

    // === Knob values → side panel (direct observation where possible) ===
    connect(m_radioState, &RadioState::keyerSpeedChanged, m_panel, &SideControlPanel::setWpm);
    connect(m_radioState, &RadioState::cwPitchChanged, this,
            [this](int pitch) { m_panel->setPitch(pitch / 1000.0); }); // Hz → kHz
    connect(m_radioState, &RadioState::rfPowerChanged, this,
            [this](double value, LevelsState::PowerRange range) { m_panel->setPower(value, range); });
    connect(m_radioState, &RadioState::qskDelayChanged, this,
            [this](int delay) { m_panel->setDelay(delay / 100.0); }); // 10 ms → seconds
    connect(m_radioState, &RadioState::rfGainChanged, m_panel, &SideControlPanel::setMainRfGain);
    connect(m_radioState, &RadioState::squelchChanged, m_panel, &SideControlPanel::setMainSquelch);
    connect(m_radioState, &RadioState::rfGainBChanged, m_panel, &SideControlPanel::setSubRfGain);
    connect(m_radioState, &RadioState::squelchBChanged, m_panel, &SideControlPanel::setSubSquelch);
    connect(m_radioState, &RadioState::micGainChanged, m_panel, &SideControlPanel::setMicGain);
    connect(m_radioState, &RadioState::compressionChanged, m_panel, &SideControlPanel::setCompression);

    // === Mode-dependent display swap (CW: WPM/PTCH vs Voice: MIC/CMP) ===
    connect(m_radioState, &RadioState::modeChanged, this, [this](RadioState::Mode) { onModeChanged(); });
}

void SideControlDisplayController::refreshFilterDisplay() {
    // WHY: when BSET is on, the side-panel BW/SHFT numbers track VFO B
    // (sub RX filter) rather than VFO A — matches which VFO the filter
    // knobs are currently adjusting.
    const bool bSet = m_radioState->bSetEnabled();
    const int bwHz = bSet ? m_radioState->filterBandwidthB() : m_radioState->filterBandwidth();
    const int shiftHz = bSet ? m_radioState->shiftBHz() : m_radioState->shiftHz();

    m_panel->setBandwidth(bwHz / 1000.0);
    m_panel->setShift(shiftHz / 1000.0);

    // HI/LO cutoff derived from BW + shift; clamp LO to 0 Hz.
    const int lowHz = qMax(0, shiftHz - (bwHz / 2));
    const int highHz = lowHz + bwHz;
    m_panel->setHighCut(highHz / 1000.0);
    m_panel->setLowCut(lowHz / 1000.0);
}

void SideControlDisplayController::onModeChanged() {
    const RadioState::Mode mode = m_radioState->mode();
    const bool isCW = (mode == RadioState::CW || mode == RadioState::CW_R);
    m_panel->setDisplayMode(isCW);
    // Refresh the displayed values for the newly-visible knob pair.
    if (isCW) {
        m_panel->setWpm(m_radioState->keyerSpeed());
        m_panel->setPitch(m_radioState->cwPitch() / 1000.0);
    } else {
        m_panel->setMicGain(m_radioState->micGain());
        m_panel->setCompression(m_radioState->compression());
    }
}
