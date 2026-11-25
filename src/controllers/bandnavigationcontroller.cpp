#include "bandnavigationcontroller.h"
#include "logging/logcategories.h"

#include "connectioncontroller.h"
#include "controllers/popupmanager.h"
#include "models/radiostate.h"

#include <QChar>
#include <QLoggingCategory>
#include <QString>

BandNavigationController::BandNavigationController(RadioState *radioState, ConnectionController *connection,
                                                   PopupManager *popupManager, QObject *parent)
    : QObject(parent), m_radioState(radioState), m_connection(connection), m_popupManager(popupManager) {
    connect(m_popupManager, &PopupManager::bandSelected, this, &BandNavigationController::onBandSelected);
    connect(m_radioState, &RadioState::bandNumberChanged, this, [this](int band) {
        refreshSelectedBand();
        requestTxPower();
        emit currentBandChanged(band, /*forVfoB=*/false);
    });
    connect(m_radioState, &RadioState::bandNumberBChanged, this, [this](int band) {
        refreshSelectedBand();
        // In split the TX VFO is B, so B's band decides which stored power applies.
        requestTxPower();
        emit currentBandChanged(band, /*forVfoB=*/true);
    });
    // BSet switches which VFO the popup targets, so the indicator follows it.
    connect(m_radioState, &RadioState::bSetChanged, this, &BandNavigationController::refreshSelectedBand);
}

int BandNavigationController::currentBand(bool forVfoB) const {
    return forVfoB ? m_radioState->bandNumberB() : m_radioState->bandNumber();
}

void BandNavigationController::refreshSelectedBand() {
    const int band = currentBand(/*forVfoB=*/m_radioState->bSetEnabled());
    if (band >= 0)
        m_popupManager->setSelectedBandByNumber(band);
}

void BandNavigationController::requestTxPower() {
    // WHY ask at all: the K4 stores TX power per band and never volunteers it when the band moves.
    // Measured on R41.FT1 (2026-11-25, tools/probe_xvtr_power.py): entering and leaving a
    // transverter band pushes BN / XV / BN$ / XV$ and no PC frame in either direction.
    //
    // WHY the RDY dump is not enough: a dump captured while the radio sat on XVTR5 carried exactly
    // one power frame, `PC000L;` — the HF range — while `PC;` asked at that same moment answered
    // `PC010X;`. So reconnecting onto a transverter band leaves QK4 showing watts where the radio
    // shows milliwatts, which is the bug this fixes. The dump's PC also arrives long before its BN,
    // so even a correct dump value could not be interpreted band-first.
    //
    // WHY PCX: D12 documents `PCX;` as always answering in the K4 format `PCnnnr`, even where the
    // K3 legacy format is in effect — which a user's startup macro could turn on with `K40;`.
    if (!m_connection->isConnected())
        return;
    m_connection->sendCAT(QStringLiteral("PCX;"));
}

void BandNavigationController::onBandSelected(const QString &bandName) {
    qCDebug(lcUiBandNav) << "Band selected:" << bandName;

    const int newBandNum = m_popupManager->bandNumberForName(bandName);

    // GEN and MEM are special pseudo-bands — no BN command is sent.
    if (newBandNum < 0) {
        qCDebug(lcUiBandNav) << "Special mode selected (GEN/MEM) — no BN command";
        return;
    }

    if (!m_connection->isConnected())
        return;

    const bool bSetEnabled = m_radioState->bSetEnabled();
    const int currentBand = this->currentBand(bSetEnabled);
    const QString cmdPrefix = bSetEnabled ? "BN$" : "BN";

    if (newBandNum == currentBand) {
        // Tap on the already-selected band → K4 band-stack step.
        const QString bandStackCmd = bSetEnabled ? "BN$^;" : "BN^;";
        qCDebug(lcUiBandNav) << "Same band — invoking band stack with" << bandStackCmd;
        m_connection->sendCAT(bandStackCmd);
    } else {
        const QString cmd = QString("%1%2;").arg(cmdPrefix).arg(newBandNum, 2, 10, QChar('0'));
        qCDebug(lcUiBandNav) << "Changing band:" << cmd;
        m_connection->sendCAT(cmd);
    }
    // Ask for the band so RadioState's BN / BN$ follows the change even if the radio does not echo it.
    m_connection->sendCAT(bSetEnabled ? "BN$;" : "BN;");
}
