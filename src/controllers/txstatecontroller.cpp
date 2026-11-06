#include "txstatecontroller.h"

#include "controllers/statusbarcontroller.h"
#include "controllers/vfofrequencycontroller.h"
#include "models/radiostate.h"
#include "ui/styling/k4styles.h"
#include "ui/widgets/sidecontrolpanel.h"
#include "ui/widgets/vfowidget.h"

#include <QLabel>
#include <QString>

TxStateController::TxStateController(RadioState *radioState, SideControlPanel *sideControlPanel,
                                     VfoFrequencyController *vfoFrequencyController, VFOWidget *vfoA, VFOWidget *vfoB,
                                     QLabel *txIndicator, QLabel *txTriangle, QLabel *txTriangleB, QObject *parent)
    : QObject(parent), m_radioState(radioState), m_sideControlPanel(sideControlPanel),
      m_vfoFrequencyController(vfoFrequencyController), m_vfoA(vfoA), m_vfoB(vfoB), m_txIndicator(txIndicator),
      m_txTriangle(txTriangle), m_txTriangleB(txTriangleB) {
    connect(m_radioState, &RadioState::transmitStateChanged, this, &TxStateController::onTransmitStateChanged);
    connect(m_radioState, &RadioState::txMeterChanged, this, &TxStateController::onTxMeterChanged);
    // paDrainCurrentChanged fires from BOTH paths in RxTxMeterState:
    //   - handleSIRF (every ~0.3 Hz) — ground truth from K4's PM field. Used to (re)calibrate
    //     the radio's actual efficiency at the current power/band.
    //   - handleTM   (every ~10 Hz)  — live derivation: fwdPower / (V × calibrated_efficiency).
    //     Gives a smooth Id reading at TM frame rate while staying accurate to the K4 panel.
    // The route below picks up both, so the meter updates at TM rate but always with
    // SIRF-anchored values.
    connect(m_radioState, &RadioState::paDrainCurrentChanged, this, [this](double amps) {
        if (m_radioState->splitEnabled()) {
            m_vfoB->setTxMeterCurrent(amps);
        } else {
            m_vfoA->setTxMeterCurrent(amps);
        }
    });
}

void TxStateController::onTransmitStateChanged(bool transmitting) {
    // Only the active TX VFO flips its meter to TX mode; the other VFO
    // stays on S-meter so you can see incoming signal on the non-TX side.
    if (m_radioState->splitEnabled()) {
        m_vfoA->setTransmitting(false);
        m_vfoB->setTransmitting(transmitting);
    } else {
        m_vfoA->setTransmitting(transmitting);
        m_vfoB->setTransmitting(false);
    }

    K4Styles::setSelected(m_txIndicator, transmitting);
    K4Styles::setSelected(m_txTriangle, transmitting);
    K4Styles::setSelected(m_txTriangleB, transmitting);

    // When XIT is on, rendered VFO frequency = dial + XIT offset; force a
    // refresh on the TX VFO so the display matches the actual TX freq
    // when transmitting, and restores to the RX freq on return to RX.
    if (m_radioState->xitEnabled()) {
        if (m_radioState->splitEnabled())
            m_vfoFrequencyController->refreshVfoB();
        else
            m_vfoFrequencyController->refreshVfoA();
    }
}

void TxStateController::onTxMeterChanged(int alc, int comp, double fwdPower, double swr) {
    m_sideControlPanel->setPowerReading(fwdPower);

    const double paCurrent = m_radioState->paDrainCurrent();

    // Route meter tuple + current to the active TX VFO only.
    if (m_radioState->splitEnabled()) {
        m_vfoB->setTxMeters(alc, comp, fwdPower, swr);
        m_vfoB->setTxMeterCurrent(paCurrent);
    } else {
        m_vfoA->setTxMeters(alc, comp, fwdPower, swr);
        m_vfoA->setTxMeterCurrent(paCurrent);
    }
}

void TxStateController::reset() {
    // RX-state defaults: left triangle points at A, right triangle empty.
    m_txTriangle->setText(QStringLiteral("◀"));
    m_txTriangleB->setText(QString());
}
