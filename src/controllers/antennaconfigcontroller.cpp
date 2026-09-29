#include "antennaconfigcontroller.h"

#include "connectioncontroller.h"
#include "models/radiostate.h"
#include "network/catframes.h"
#include "ui/popups/antennacfgpopup.h"

#include <QString>
#include <QVector>
#include <QWidget>

AntennaConfigController::AntennaConfigController(RadioState *radioState, ConnectionController *connection,
                                                 QWidget *parentWidget, QObject *parent)
    : QObject(parent), m_radioState(radioState), m_connection(connection),
      m_mainRxPopup(new AntennaCfgPopupWidget(AntennaCfgVariant::MainRx, parentWidget)),
      m_subRxPopup(new AntennaCfgPopupWidget(AntennaCfgVariant::SubRx, parentWidget)),
      m_txPopup(new AntennaCfgPopupWidget(AntennaCfgVariant::Tx, parentWidget)) {

    // MAIN RX popup: user-driven config edits → ACM CAT command.
    connect(m_mainRxPopup, &AntennaCfgPopupWidget::configChanged, this, [this](bool displayAll, QVector<bool> mask) {
        if (!m_connection->isConnected())
            return;
        // ACMzabcdefg;  z=displayAll, a..g=per-antenna enable
        m_connection->sendCAT(QString::fromLatin1(CatFrames::setAntennaConfig("ACM", displayAll, mask, 7)));
    });

    // SUB RX popup — same shape, ACS command.
    connect(m_subRxPopup, &AntennaCfgPopupWidget::configChanged, this, [this](bool displayAll, QVector<bool> mask) {
        if (!m_connection->isConnected())
            return;
        m_connection->sendCAT(QString::fromLatin1(CatFrames::setAntennaConfig("ACS", displayAll, mask, 7)));
    });

    // TX popup — 3 antennas (not 7), ACT command.
    connect(m_txPopup, &AntennaCfgPopupWidget::configChanged, this, [this](bool displayAll, QVector<bool> mask) {
        if (!m_connection->isConnected())
            return;
        m_connection->sendCAT(QString::fromLatin1(CatFrames::setAntennaConfig("ACT", displayAll, mask, 3)));
    });

    // RadioState → popup state updates. MainWindow's own label-update slot
    // for antennaChanged / antennaNameChanged stays in place; these
    // observers are independent.
    connect(m_radioState, &RadioState::mainRxAntCfgChanged, this, [this]() {
        m_mainRxPopup->setDisplayAll(m_radioState->mainRxDisplayAll());
        m_mainRxPopup->setAntennaMask(m_radioState->mainRxAntMask());
    });
    connect(m_radioState, &RadioState::subRxAntCfgChanged, this, [this]() {
        m_subRxPopup->setDisplayAll(m_radioState->subRxDisplayAll());
        m_subRxPopup->setAntennaMask(m_radioState->subRxAntMask());
    });
    connect(m_radioState, &RadioState::txAntCfgChanged, this, [this]() {
        m_txPopup->setDisplayAll(m_radioState->txDisplayAll());
        m_txPopup->setAntennaMask(m_radioState->txAntMask());
    });

    // When a stored antenna name changes (from ACN response), mirror it to
    // each popup's label. ACN uses 1-based indexes; popups want 0-based.
    connect(m_radioState, &RadioState::antennaNameChanged, this, [this](int index, const QString &name) {
        const int popupIndex = index - 1;
        if (popupIndex < 0)
            return;
        m_mainRxPopup->setAntennaName(popupIndex, name);
        m_subRxPopup->setAntennaName(popupIndex, name);
        m_txPopup->setAntennaName(popupIndex, name);
    });
}

AntennaConfigController::~AntennaConfigController() {
    // Architecture Rule 11 — disconnect first to prevent queued signals
    // from arriving during partial destruction.
    disconnect(this);
}

void AntennaConfigController::showMainRxPopupAbove(QWidget *trigger) {
    if (trigger)
        m_mainRxPopup->showAboveWidget(trigger);
}

void AntennaConfigController::showSubRxPopupAbove(QWidget *trigger) {
    if (trigger)
        m_subRxPopup->showAboveWidget(trigger);
}

void AntennaConfigController::showTxPopupAbove(QWidget *trigger) {
    if (trigger)
        m_txPopup->showAboveWidget(trigger);
}

void AntennaConfigController::closeAll() {
    if (m_mainRxPopup->isVisible())
        m_mainRxPopup->hidePopup();
    if (m_subRxPopup->isVisible())
        m_subRxPopup->hidePopup();
    if (m_txPopup->isVisible())
        m_txPopup->hidePopup();
}
