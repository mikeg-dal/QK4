#include "ritxitcontroller.h"

#include "connectioncontroller.h"
#include "controllers/spectrumcontroller.h"
#include "models/radiostate.h"
#include "ui/styling/k4styles.h"

#include <QLabel>
#include <QString>
#include <QWheelEvent>

RitXitController::RitXitController(RadioState *radioState, ConnectionController *connection,
                                   SpectrumController *spectrum, QLabel *ritLabel, QLabel *xitLabel,
                                   QLabel *ritXitValueLabel, QObject *parent)
    : QObject(parent), m_radioState(radioState), m_connection(connection), m_spectrum(spectrum), m_ritLabel(ritLabel),
      m_xitLabel(xitLabel), m_ritXitValueLabel(ritXitValueLabel) {
    reset();

    // WHY: when BSET is off, RIT state reflects VFO A; when on, VFO B. In
    // split mode the K4 routes XIT offset to the VFO B register (RO$) even
    // without BSET, so we sometimes need to display RO$ while the rest of
    // the state comes from VFO A. See docs/k4-protocol-quirks.md §2.
    connect(m_radioState, &RadioState::ritXitChanged, this, [this](bool ritEnabled, bool xitEnabled, int offset) {
        if (!m_radioState->bSetEnabled()) {
            int displayOffset = offset;
            if (m_radioState->splitEnabled() && !ritEnabled && m_radioState->ritXitOffsetB() != 0)
                displayOffset = m_radioState->ritXitOffsetB();
            applyDisplay(ritEnabled, xitEnabled, displayOffset);
        } else {
            int displayOffset;
            if (xitEnabled)
                displayOffset = m_radioState->splitEnabled() ? m_radioState->ritXitOffsetB() : offset;
            else
                displayOffset = m_radioState->ritXitOffsetB();
            applyDisplay(m_radioState->ritEnabledB(), xitEnabled, displayOffset);
        }
    });
    connect(m_radioState, &RadioState::ritXitBChanged, this, [this](bool ritEnabled, int offset) {
        if (m_radioState->bSetEnabled()) {
            applyDisplay(ritEnabled, m_radioState->xitEnabled(), offset);
        } else if (m_radioState->splitEnabled() && m_radioState->xitEnabled()) {
            // Split + XIT: K4 routes XIT offset to RO$.
            applyDisplay(m_radioState->ritEnabled(), true, offset);
        }
    });
    // BSET toggle flips which VFO's RIT/XIT state drives the display.
    connect(m_radioState, &RadioState::bSetChanged, this, [this](bool enabled) {
        if (enabled)
            applyDisplay(m_radioState->ritEnabledB(), m_radioState->xitEnabled(), m_radioState->ritXitOffsetB());
        else
            applyDisplay(m_radioState->ritEnabled(), m_radioState->xitEnabled(), m_radioState->ritXitOffset());
    });
}

bool RitXitController::handleRitLabelClick() {
    // WHY: SW54 is the BSET-aware RIT toggle — on BSET off it toggles RT,
    // on BSET on it toggles RT$. RT/; only toggles RT. The K4 doesn't echo
    // RT$/RO$ for SW54 so query them explicitly when BSET is active.
    const bool bSet = m_radioState->bSetEnabled();
    m_connection->sendCAT(bSet ? "SW54;" : "RT/;");
    if (bSet) {
        m_connection->sendCAT("RT$;");
        m_connection->sendCAT("RO$;");
    }
    return true;
}

bool RitXitController::handleXitLabelClick() {
    m_connection->sendCAT("XT/;");
    return true;
}

bool RitXitController::handleWheel(QWheelEvent *event) {
    const int steps = m_wheelAccumulator.accumulate(event);
    if (steps == 0)
        return true;

    // WHY: RU/RD is BSET + XIT aware on the K4 side.
    //   - XIT active: K4 routes RU/RD to the TX VFO's offset register
    //     (RO when no split, RO$ when split), so plain RU/RD works either way.
    //   - BSET + RIT-only: K4 would adjust RO (VFO A) — we want RO$ instead,
    //     so force RU$/RD$.
    const bool bSet = m_radioState->bSetEnabled();
    const bool adjustB = bSet && !m_radioState->xitEnabled();
    const QString upCmd = adjustB ? "RU$;" : "RU;";
    const QString downCmd = adjustB ? "RD$;" : "RD;";
    for (int i = 0; i < qAbs(steps); ++i)
        m_connection->sendCAT(steps > 0 ? upCmd : downCmd);
    return true;
}

void RitXitController::applyLabels(bool ritEnabled, bool xitEnabled, int offset) {
    K4Styles::setSelected(m_ritLabel, ritEnabled);
    K4Styles::setSelected(m_xitLabel, xitEnabled);

    // Offset rendered in kHz with explicit + sign on non-negative values.
    const double offsetKHz = offset / 1000.0;
    const QString sign = (offset >= 0) ? "+" : "";
    m_ritXitValueLabel->setText(QString("%1%2").arg(sign).arg(offsetKHz, 0, 'f', 2));

    K4Styles::setSelected(m_ritXitValueLabel, ritEnabled || xitEnabled);
}

void RitXitController::applyDisplay(bool ritEnabled, bool xitEnabled, int offset) {
    applyLabels(ritEnabled, xitEnabled, offset);

    // RIT offset shifts the rendered VFO frequency; VfoFrequencyController re-renders it.
    emit displayRefreshRequested();

    // Panadapter passband + TX marker positions depend on the RIT state.
    m_spectrum->updatePanadapterPassbands();
    m_spectrum->updateTxMarkers();
}

void RitXitController::reset() {
    // The disconnected look is the inactive look applyDisplay() draws, so a
    // reconnect starts from the same styling as a fresh launch.
    applyLabels(false, false, 0);
}
