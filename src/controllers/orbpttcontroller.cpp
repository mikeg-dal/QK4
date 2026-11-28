#include "orbpttcontroller.h"

#include "connectioncontroller.h"
#include "hardware/halikeydevice.h"
#include "hardware/halikeydevicetype.h"
#include "models/radiostate.h"
#include "settings/radiosettings.h"
#include "transmitcontroller.h"

OrbPttController::OrbPttController(RadioState *radioState, ConnectionController *connection, HalikeyDevice *halikey,
                                   TransmitController *transmit, QObject *parent)
    : QObject(parent), m_radioState(radioState), m_halikey(halikey), m_transmit(transmit) {
    connect(halikey, &HalikeyDevice::pttJackChanged, this,
            [this](bool pressed) { apply(m_gate.jack(pressed, allowed())); });

    // A closed port reports no release.
    connect(halikey, &HalikeyDevice::disconnected, this, [this]() { apply(m_gate.jack(false, false)); });

    // TransmitController releases everything itself on a disconnect; this keeps the gate in step.
    connect(connection, &ConnectionController::connectionStateChanged, this, [this](TcpClient::ConnectionState state) {
        if (state != TcpClient::Connected)
            apply(m_gate.jack(false, false));
    });

    connect(radioState, &RadioState::modeChanged, this, &OrbPttController::recheck);
    connect(radioState, &RadioState::modeBChanged, this, &OrbPttController::recheck);
    connect(radioState, &RadioState::splitChanged, this, &OrbPttController::recheck);
    connect(RadioSettings::instance(), &RadioSettings::halikeyOrbPttChanged, this, &OrbPttController::recheck);
    connect(RadioSettings::instance(), &RadioSettings::halikeyDeviceTypeChanged, this, &OrbPttController::recheck);
    recheck();
}

void OrbPttController::recheck() {
    const bool isAllowed = allowed();
    if (m_halikey)
        m_halikey->setPttJackWanted(isAllowed);
    apply(m_gate.allowedChanged(isAllowed));
}

bool OrbPttController::allowed() const {
    OrbPtt::Inputs in;
    in.enabled = RadioSettings::instance()->halikeyOrbPtt();
    in.orbSelected = RadioSettings::instance()->halikeyDeviceType() == HaliKeyType::kOrb;
    in.modeA = m_radioState->mode();
    in.modeB = m_radioState->modeB();
    in.split = m_radioState->splitEnabled();
    return OrbPtt::allowed(in);
}

void OrbPttController::apply(OrbPtt::Action action) {
    // engage() refuses when disconnected or another local source holds the transmitter, and
    // release() ignores a caller that does not hold it, so neither needs checking here.
    if (action == OrbPtt::Action::Engage)
        m_transmit->engage(TransmitOwner::Owner::OrbPtt, TransmitOwner::Route::StreamedFromHere);
    else if (action == OrbPtt::Action::Release)
        m_transmit->release(TransmitOwner::Owner::OrbPtt);
}
