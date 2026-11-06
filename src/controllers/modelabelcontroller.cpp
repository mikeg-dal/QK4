#include "modelabelcontroller.h"

#include "models/radiostate.h"

#include <QLabel>
#include <QString>

ModeLabelController::ModeLabelController(RadioState *radioState, QLabel *modeALabel, QLabel *modeBLabel,
                                         QObject *parent)
    : QObject(parent), m_radioState(radioState), m_modeALabel(modeALabel), m_modeBLabel(modeBLabel) {
    connect(m_radioState, &RadioState::modeChanged, this, [this](RadioState::Mode) { refresh(); });
    connect(m_radioState, &RadioState::modeBChanged, this, [this](RadioState::Mode) { refresh(); });
    connect(m_radioState, &RadioState::dataSubModeChanged, this, [this](int) { refresh(); });
    connect(m_radioState, &RadioState::dataSubModeBChanged, this, [this](int) { refresh(); });
    // WHY: ESSB state toggles the "+" suffix on USB/LSB labels. Observing
    // essbChanged keeps the mode labels in sync with both user-initiated
    // ESSB toggles and K4-echoed state without MainWindow needing to
    // forward anything.
    connect(m_radioState, &RadioState::essbChanged, this, [this](bool, int) { refresh(); });
}

void ModeLabelController::refresh() {
    // VFO A: mode string with "+" appended for SSB when ESSB is on.
    QString modeA = m_radioState->modeStringFull();
    const RadioState::Mode mode = m_radioState->mode();
    if (m_radioState->essbEnabled() && (mode == RadioState::USB || mode == RadioState::LSB))
        modeA += "+";
    m_modeALabel->setText(modeA);

    // VFO B: same formatting rule applied to the B-side mode.
    QString modeB = m_radioState->modeStringFullB();
    const RadioState::Mode modeVfoB = m_radioState->modeB();
    if (m_radioState->essbEnabled() && (modeVfoB == RadioState::USB || modeVfoB == RadioState::LSB))
        modeB += "+";
    m_modeBLabel->setText(modeB);
}

void ModeLabelController::reset() {
    // Clears text only. The disabled-style for VFO B's mode label is applied
    // by SubDivIndicatorController::reset() (via setVfoBDimmed), which also
    // handles the matching VFO B frequency color.
    m_modeALabel->setText(QString());
    m_modeBLabel->setText(QString());
}
