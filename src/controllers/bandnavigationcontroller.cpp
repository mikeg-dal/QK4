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
        emit currentBandChanged(band, /*forVfoB=*/false);
    });
    connect(m_radioState, &RadioState::bandNumberBChanged, this, [this](int band) {
        refreshSelectedBand();
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
