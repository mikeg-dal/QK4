#include "subdivindicatorcontroller.h"

#include "controllers/spectrumcontroller.h"
#include "models/radiostate.h"
#include "ui/widgets/frequencydisplaywidget.h"
#include "ui/styling/k4styles.h"
#include "ui/widgets/vfowidget.h"

#include <QColor>
#include <QLabel>
#include <QString>

SubDivIndicatorController::SubDivIndicatorController(RadioState *radioState, SpectrumController *spectrum,
                                                     VFOWidget *vfoB, QLabel *subLabel, QLabel *divLabel,
                                                     QLabel *modeBLabel, QObject *parent)
    : QObject(parent), m_radioState(radioState), m_spectrum(spectrum), m_vfoB(vfoB), m_subLabel(subLabel),
      m_divLabel(divLabel), m_modeBLabel(modeBLabel) {
    m_subLabel->setStyleSheet(K4Styles::subDivBadge());
    m_divLabel->setStyleSheet(K4Styles::subDivBadge());
    m_modeBLabel->setStyleSheet(K4Styles::vfoBModeLabel());
    connect(m_radioState, &RadioState::subRxEnabledChanged, this, &SubDivIndicatorController::onSubRxEnabledChanged);
    connect(m_radioState, &RadioState::diversityChanged, this, &SubDivIndicatorController::onDiversityChanged);
}

void SubDivIndicatorController::onSubRxEnabledChanged(bool enabled) {
    setSubLabelActive(enabled);
    // WHY: DIV requires SUB (K4 constraint). When SUB turns on while DIV is
    // already set, also activate the DIV badge — handles the case where SB3
    // arrives after DV1. When SUB turns off, DIV forces off.
    if (enabled && m_radioState->diversityEnabled())
        setDivLabelActive(true);
    else if (!enabled)
        setDivLabelActive(false);

    setVfoBDimmed(!enabled);

    // SUB RX off: hide mini pan B if the VFOs are on different bands.
    if (!enabled)
        m_spectrum->checkAndHideMiniPanB();
}

void SubDivIndicatorController::onDiversityChanged(bool enabled) {
    // DIV badge is green only when BOTH diversity AND sub RX are enabled.
    setDivLabelActive(enabled && m_radioState->subReceiverEnabled());
}

void SubDivIndicatorController::setSubLabelActive(bool active) {
    K4Styles::setSelected(m_subLabel, active);
}

void SubDivIndicatorController::setDivLabelActive(bool active) {
    K4Styles::setSelected(m_divLabel, active);
}

void SubDivIndicatorController::setVfoBDimmed(bool dimmed) {
    const char *color = dimmed ? K4Styles::Colors::TextSecondary : K4Styles::Colors::TextPrimary;
    m_vfoB->frequencyDisplay()->setNormalColor(QColor(color));
    K4Styles::setStyleFlag(m_modeBLabel, K4Styles::DimmedProperty, dimmed);
}

void SubDivIndicatorController::reset() {
    setSubLabelActive(false);
    setDivLabelActive(false);
    setVfoBDimmed(true);
}
