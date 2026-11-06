#include "antennadisplaycontroller.h"

#include "bandnavigationcontroller.h"
#include "models/radiostate.h"
#include "utils/radioutils.h"

#include <QLabel>
#include <QString>

namespace {

// Format an RX antenna display from an AR (Main) or AR$ (Sub) value. commandPrefix names the
// command in the unknown-value fallback. K4 AR values (from official K4 protocol documentation):
//   0 = Disconnected (all RX RF sources disconnected)
//   1 = EXT. XVTR IN / RX ANT IN2 (external transverter jack)
//   2 = RX USES TX ANT (follows TX antenna selection) — show resolved value
//   3 = INT. XVTR IN (internal transverter)
//   4 = RX ANT IN1 (receive antenna jack)
//   5 = ATU RX ANT1 (TX antenna 1 via ATU)
//   6 = ATU RX ANT2 (TX antenna 2 via ATU)
//   7 = ATU RX ANT3 (TX antenna 3 via ATU)
QString formatRxAntenna(const char *commandPrefix, int arValue, int txAnt, RadioState *radioState) {
    switch (arValue) {
    case 0:
        return QStringLiteral("OFF");
    case 1:
        return QString("RX2:%1").arg(radioState->antennaName(5));
    case 2: // Resolve to current TX antenna (matches K4 front-panel behavior).
        return QString("%1:%2").arg(txAnt).arg(radioState->antennaName(txAnt));
    case 3:
        return QStringLiteral("INT XVTR");
    case 4:
        return QString("RX1:%1").arg(radioState->antennaName(4));
    case 5:
        return QString("1:%1").arg(radioState->antennaName(1));
    case 6:
        return QString("2:%1").arg(radioState->antennaName(2));
    case 7:
        return QString("3:%1").arg(radioState->antennaName(3));
    default:
        return QString("%1%2").arg(QLatin1String(commandPrefix)).arg(arValue);
    }
}

} // namespace

AntennaDisplayController::AntennaDisplayController(RadioState *radioState, BandNavigationController *bandNav,
                                                   QLabel *txAntennaLabel, QLabel *rxAntALabel, QLabel *rxAntBLabel,
                                                   QObject *parent)
    : QObject(parent), m_radioState(radioState), m_bandNav(bandNav), m_txAntennaLabel(txAntennaLabel),
      m_rxAntALabel(rxAntALabel), m_rxAntBLabel(rxAntBLabel) {
    connect(m_radioState, &RadioState::antennaChanged, this, &AntennaDisplayController::onAntennaChanged);
    // Antenna name changes don't include AR/AR$ values; re-read RadioState and reformat.
    connect(m_radioState, &RadioState::antennaNameChanged, this, [this](int, const QString &) { refreshLabels(); });
    // On XVTR bands the K4 stops applying AN/AR semantics and the UI shows
    // "XV IN" / "XV OUT" instead. Refresh on every band change so we swap
    // labels when entering or leaving XVTR.
    if (m_bandNav) {
        connect(m_bandNav, &BandNavigationController::currentBandChanged, this, [this](int, bool) { refreshLabels(); });
    }
}

void AntennaDisplayController::onAntennaChanged(int txAnt, int rxAntMain, int rxAntSub) {
    // K4 AN and AR: "This command does not pertain to transverter antennas" (K4
    // reference rev D12): on XVTR bands the TX antenna is XV OUT and any RX path is XV IN.
    // We relabel here rather than via state because the K4 doesn't emit a
    // distinct antenna value — it just stops applying AN/AR.
    const bool xvtrMain = m_bandNav && RadioUtils::isXvtrBandNumber(m_bandNav->currentBand(/*forVfoB=*/false));
    const bool xvtrSub = m_bandNav && RadioUtils::isXvtrBandNumber(m_bandNav->currentBand(/*forVfoB=*/true));

    if (xvtrMain) {
        m_txAntennaLabel->setText(QStringLiteral("XV OUT"));
        m_rxAntALabel->setText(QStringLiteral("XV IN"));
    } else {
        m_txAntennaLabel->setText(QString("%1:%2").arg(txAnt).arg(m_radioState->antennaName(txAnt)));
        m_rxAntALabel->setText(formatRxAntenna("AR", rxAntMain, txAnt, m_radioState));
    }
    if (xvtrSub) {
        m_rxAntBLabel->setText(QStringLiteral("XV IN"));
    } else {
        m_rxAntBLabel->setText(formatRxAntenna("AR$", rxAntSub, txAnt, m_radioState));
    }
}

void AntennaDisplayController::refreshLabels() {
    onAntennaChanged(m_radioState->txAntenna(), m_radioState->rxAntennaMain(), m_radioState->rxAntennaSub());
}

void AntennaDisplayController::reset() {
    m_txAntennaLabel->setText(QString());
    m_rxAntALabel->setText(QString());
    m_rxAntBLabel->setText(QString());
}
