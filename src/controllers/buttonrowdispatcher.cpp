#include "buttonrowdispatcher.h"

#include "connectioncontroller.h"
#include "controllers/antennaconfigcontroller.h"
#include "controllers/popupmanager.h"
#include "controllers/textdecodecontroller.h"
#include "models/radiostate.h"
#include "network/catframes.h"
#include "utils/radioutils.h"

#include <QChar>
#include <QString>

ButtonRowDispatcher::ButtonRowDispatcher(RadioState *radioState, ConnectionController *connection,
                                         PopupManager *popupManager, AntennaConfigController *antennaCfg,
                                         TextDecodeController *textDecode, QObject *parent)
    : QObject(parent), m_radioState(radioState), m_connection(connection), m_popupManager(popupManager),
      m_antennaCfg(antennaCfg), m_textDecode(textDecode) {
    connect(m_popupManager, &PopupManager::mainRxButtonClicked, this, &ButtonRowDispatcher::onMainRxClicked);
    connect(m_popupManager, &PopupManager::mainRxButtonRightClicked, this, &ButtonRowDispatcher::onMainRxRightClicked);
    connect(m_popupManager, &PopupManager::subRxButtonClicked, this, &ButtonRowDispatcher::onSubRxClicked);
    connect(m_popupManager, &PopupManager::subRxButtonRightClicked, this, &ButtonRowDispatcher::onSubRxRightClicked);
    connect(m_popupManager, &PopupManager::txButtonClicked, this, &ButtonRowDispatcher::onTxClicked);
    connect(m_popupManager, &PopupManager::txButtonRightClicked, this, &ButtonRowDispatcher::onTxRightClicked);

    // Mode flip changes whether buttons 5/6 show paddle/weight (CW) or
    // SSB BW/ESSB (voice/data).
    connect(m_radioState, &RadioState::modeChanged, this, [this](RadioState::Mode) { refreshTxButtonsForMode(); });
}

void ButtonRowDispatcher::onMainRxClicked(int index) {
    if (!m_connection->isConnected())
        return;

    switch (index) {
    case 0: // ANT CFG
        m_antennaCfg->showMainRxPopupAbove(m_popupManager->anchorForLane(PopupManager::ButtonRowLane::MainRx));
        break;
    case 1: // RX EQ
        m_popupManager->showRxEqAbove(PopupManager::ButtonRowLane::MainRx);
        break;
    case 2: // LINE OUT
        m_popupManager->showLineOutAbove(PopupManager::ButtonRowLane::MainRx);
        break;
    case 3: { // AFX — cycle OFF → DELAY → PITCH → OFF
        const int nextMode = (m_radioState->afxMode() + 1) % 3;
        m_connection->sendCAT(QString("FX%1;").arg(nextMode));
        break;
    }
    case 4: { // AGC — toggle Fast ↔ Slow (skip Off)
        const RadioState::AGCSpeed current = m_radioState->agcSpeed();
        const int next = (current == RadioState::AGC_Fast) ? 1 : 2;
        m_connection->sendCAT(QString("GT%1;").arg(next));
        break;
    }
    case 5: // APF — toggle (Main RX); "N/A" outside CW, as on the K4
        if (RadioUtils::apfAvailable(static_cast<int>(m_radioState->mode())))
            m_connection->sendCAT("AP/;");
        break;
    case 6: // TEXT DECODE — open window for Main RX
        m_textDecode->showMainRx();
        break;
    }
}

void ButtonRowDispatcher::onMainRxRightClicked(int index) {
    if (!m_connection->isConnected())
        return;

    switch (index) {
    case 2: { // LINE OUT → VFO LINK toggle
        const bool linked = m_radioState->vfoLink();
        m_connection->sendCAT(QString("LN%1;").arg(linked ? 0 : 1));
        break;
    }
    case 3: // AFX — right-click matches left-click (cycle)
        onMainRxClicked(3);
        break;
    case 4: { // AGC — toggle ON/OFF (defaults to Slow on re-enable)
        const RadioState::AGCSpeed current = m_radioState->agcSpeed();
        m_connection->sendCAT(current == RadioState::AGC_Off ? "GT1;" : "GT0;");
        break;
    }
    case 5: // APF — cycle bandwidth (Main RX); "N/A" outside CW
        if (RadioUtils::apfAvailable(static_cast<int>(m_radioState->mode())))
            m_connection->sendCAT("AP+;");
        break;
    default:
        break;
    }
}

void ButtonRowDispatcher::onSubRxClicked(int index) {
    if (!m_connection->isConnected())
        return;

    switch (index) {
    case 0: // ANT CFG (Sub RX)
        m_antennaCfg->showSubRxPopupAbove(m_popupManager->anchorForLane(PopupManager::ButtonRowLane::SubRx));
        break;
    case 1: // RX EQ — shared popup positioned above the Sub RX row
        m_popupManager->showRxEqAbove(PopupManager::ButtonRowLane::SubRx);
        break;
    case 2: // LINE OUT
        m_popupManager->showLineOutAbove(PopupManager::ButtonRowLane::SubRx);
        break;
    case 3: { // AFX — same FX command as Main RX (global setting)
        const int nextMode = (m_radioState->afxMode() + 1) % 3;
        m_connection->sendCAT(QString("FX%1;").arg(nextMode));
        break;
    }
    case 4: { // AGC Sub — toggle Fast ↔ Slow
        const RadioState::AGCSpeed current = m_radioState->agcSpeedB();
        const int next = (current == RadioState::AGC_Fast) ? 1 : 2;
        m_connection->sendCAT(QString("GT$%1;").arg(next));
        break;
    }
    case 5: // APF — toggle (Sub RX); "N/A" outside CW
        if (RadioUtils::apfAvailable(static_cast<int>(m_radioState->modeB())))
            m_connection->sendCAT("AP$/;");
        break;
    case 6: // TEXT DECODE — open window for Sub RX
        m_textDecode->showSubRx();
        break;
    }
}

void ButtonRowDispatcher::onSubRxRightClicked(int index) {
    if (!m_connection->isConnected())
        return;

    switch (index) {
    case 2: { // LINE OUT → VFO LINK toggle (same as Main RX)
        const bool linked = m_radioState->vfoLink();
        m_connection->sendCAT(QString("LN%1;").arg(linked ? 0 : 1));
        break;
    }
    case 3: // AFX — cycle (matches left-click)
        onSubRxClicked(3);
        break;
    case 4: { // AGC Sub — toggle ON/OFF
        const RadioState::AGCSpeed current = m_radioState->agcSpeedB();
        m_connection->sendCAT(current == RadioState::AGC_Off ? "GT$1;" : "GT$0;");
        break;
    }
    case 5: // APF — cycle bandwidth (Sub RX); "N/A" outside CW
        if (RadioUtils::apfAvailable(static_cast<int>(m_radioState->modeB())))
            m_connection->sendCAT("AP$+;");
        break;
    default:
        break;
    }
}

void ButtonRowDispatcher::onTxClicked(int index) {
    if (!m_connection->isConnected())
        return;
    switch (index) {
    case 0: // ANT CFG
        m_antennaCfg->showTxPopupAbove(m_popupManager->anchorForLane(PopupManager::ButtonRowLane::Tx));
        break;
    case 1: // TX EQ
        m_popupManager->showTxEqAbove(PopupManager::ButtonRowLane::Tx);
        break;
    case 2: // LINE IN
        m_popupManager->showLineInAbove(PopupManager::ButtonRowLane::Tx);
        break;
    case 3: // MIC INPUT
        m_popupManager->showMicInputAbove(PopupManager::ButtonRowLane::Tx);
        break;
    case 4: // VOX GAIN
        m_popupManager->showVoxGainAbove(PopupManager::ButtonRowLane::Tx);
        break;
    case 5: { // CW: paddle toggle N↔R. Voice/data: SSB BW popup.
        const auto mode = m_radioState->mode();
        if (mode == RadioState::CW || mode == RadioState::CW_R) {
            const QChar curPaddle = m_radioState->paddleOrientation();
            const QChar newPaddle = (curPaddle == 'R') ? QChar('N') : QChar('R');
            m_connection->sendCAT(QString::fromLatin1(
                CatFrames::keyerPaddle(m_radioState->iambicMode(), newPaddle, m_radioState->keyingWeight())));
            m_radioState->setPaddleOrientation(newPaddle);
        } else {
            m_popupManager->showSsbBwAbove(PopupManager::ButtonRowLane::Tx);
        }
        break;
    }
    case 6: { // CW: keying weight popup. Voice/data: ESSB toggle.
        const auto mode = m_radioState->mode();
        if (mode == RadioState::CW || mode == RadioState::CW_R) {
            m_popupManager->showKeyingWeightAbove(PopupManager::ButtonRowLane::Tx);
        } else {
            const bool newState = !m_radioState->essbEnabled();
            int bw = m_radioState->ssbTxBw();
            // WHY: ESSB and SSB have non-overlapping BW ranges — SSB 24-28,
            // ESSB 30-45. Switching mode with an out-of-range BW would reject
            // the K4's ES command, so snap to the nearest valid default.
            if (newState) {
                if (bw < AudioEffectsState::EssbTxBwMin || bw > AudioEffectsState::EssbTxBwMax)
                    bw = AudioEffectsState::EssbTxBwMin;
            } else {
                if (bw < AudioEffectsState::SsbTxBwMin || bw > AudioEffectsState::SsbTxBwMax)
                    bw = AudioEffectsState::SsbTxBwMax;
            }
            m_connection->sendCAT(QString("ES%1%2;").arg(newState ? 1 : 0).arg(bw, 2, 10, QChar('0')));
            // Optimistic — ES SET is echoed, but also immediately re-paint labels.
            m_radioState->setEssbEnabled(newState);
            m_radioState->setSsbTxBw(bw);
            if (m_popupManager->anchorForLane(PopupManager::ButtonRowLane::Tx)) {
                m_popupManager->setTxButtonLabel(5, "SSB BW", RadioUtils::txBandwidthLabel(bw), false);
                m_popupManager->setTxButtonLabel(6, "ESSB", newState ? "ON" : "OFF", false);
            }
        }
        break;
    }
    default:
        break;
    }
}

void ButtonRowDispatcher::onTxRightClicked(int index) {
    if (!m_connection->isConnected())
        return;
    if (index == 4) { // ANTIVOX popup
        m_popupManager->showAntiVoxAbove(PopupManager::ButtonRowLane::Tx);
    } else if (index == 5) { // CW only: iambic A↔B toggle
        const auto mode = m_radioState->mode();
        if (mode == RadioState::CW || mode == RadioState::CW_R) {
            const QChar curIambic = m_radioState->iambicMode();
            const QChar newIambic = (curIambic == 'B') ? QChar('A') : QChar('B');
            m_connection->sendCAT(QString::fromLatin1(
                CatFrames::keyerPaddle(newIambic, m_radioState->paddleOrientation(), m_radioState->keyingWeight())));
            m_radioState->setIambicMode(newIambic);
        }
    } else if (index == 3) { // MIC CFG — skips LINE IN (input==2)
        if (m_radioState->micInput() == 2)
            return;
        m_popupManager->showMicConfigAbove(PopupManager::ButtonRowLane::Tx);
    }
}

void ButtonRowDispatcher::refreshTxButtonsForMode() {
    // TX popup's anchor is nullptr until setBottomMenuBar is injected
    // and the popup is first constructed; skip early refreshes.
    if (!m_popupManager->anchorForLane(PopupManager::ButtonRowLane::Tx))
        return;

    const RadioState::Mode mode = m_radioState->mode();
    if (mode == RadioState::CW || mode == RadioState::CW_R) {
        // CW mode: buttons 5/6 show paddle orientation + iambic mode, and keying weight.
        const QChar iambic = m_radioState->iambicMode();
        const QChar paddle = m_radioState->paddleOrientation();
        const int weight = m_radioState->keyingWeight();
        if (!iambic.isNull() && !paddle.isNull()) {
            const QString paddleStr = (paddle == 'R') ? "PDL REV" : "PDL NOR";
            const QString iambicStr = QString("IAMB %1").arg(iambic);
            m_popupManager->setTxButtonLabel(5, paddleStr, iambicStr, true);
        } else {
            // KP state not yet received from the K4 — render defaults.
            m_popupManager->setTxButtonLabel(5, "PDL NOR", "IAMB A", true);
        }
        if (weight >= 90 && weight <= 125) {
            const QString weightStr = QString::number(weight / 100.0, 'f', 2);
            m_popupManager->setTxButtonLabel(6, "WEIGHT", weightStr, false);
        } else {
            m_popupManager->setTxButtonLabel(6, "WEIGHT", "1.00", false);
        }
    } else {
        // Voice / data mode: buttons 5/6 show SSB BW and ESSB toggle.
        const int bw = m_radioState->ssbTxBw();
        const bool known = bw >= AudioEffectsState::SsbTxBwMin && bw <= AudioEffectsState::EssbTxBwMax;
        m_popupManager->setTxButtonLabel(
            5, "SSB BW", RadioUtils::txBandwidthLabel(known ? bw : AudioEffectsState::SsbTxBwMax), false);
        m_popupManager->setTxButtonLabel(6, "ESSB", m_radioState->essbEnabled() ? "ON" : "OFF", false);
    }
}
