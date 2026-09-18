#include "transmitcontroller.h"

#include "audiocontroller.h"
#include "connectioncontroller.h"
#include "models/radiostate.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(txOwner, "tx.owner")

// TransmitOwner::AudioSource is declared without including audiocontroller.h, so the arbiter — and
// the test that links it alone — stays clear of the controller layer. These keep the two from
// drifting into a silently wrong TX source.
static_assert(static_cast<int>(TransmitOwner::AudioSource::Microphone) ==
              static_cast<int>(AudioController::TxSource::Microphone));
static_assert(static_cast<int>(TransmitOwner::AudioSource::Tci) == static_cast<int>(AudioController::TxSource::Tci));

namespace {
const char *ownerName(TransmitOwner::Owner o) {
    switch (o) {
    case TransmitOwner::Owner::None:
        return "none";
    case TransmitOwner::Owner::PttButton:
        return "PTT button";
    case TransmitOwner::Owner::Xmit:
        return "XMIT";
    case TransmitOwner::Owner::CatClient:
        return "CAT client";
    case TransmitOwner::Owner::TciClient:
        return "TCI client";
    case TransmitOwner::Owner::Radio:
        return "radio";
    }
    return "?";
}
} // namespace

TransmitController::TransmitController(RadioState *radioState, ConnectionController *connection, AudioController *audio,
                                       QObject *parent)
    : QObject(parent), m_radioState(radioState), m_connection(connection), m_audio(audio) {

    // INT-002: the radio's own transmit state was never fed back. A radio-side RX — the front
    // panel, a footswitch plugged into the K4, a fault, a tune timeout — left QK4 encoding and
    // sending mic frames, which re-key the radio. transmitStateChanged had three consumers and not
    // one of them touched PTT.
    connect(m_radioState, &RadioState::transmitStateChanged, this, [this](bool transmitting) {
        const Owner previous = m_state.owner;
        const bool was = m_state.transmitting();
        apply(TransmitOwner::radioReports(m_state, transmitting), previous, was);
    });

    // Losing the radio releases whatever was held. AUD-001 was this not happening: the gate
    // survived a disconnect, and the next mic-device change reopened the microphone and streamed
    // the room at a radio nobody had keyed.
    connect(m_connection, &ConnectionController::connectionStateChanged, this,
            [this](TcpClient::ConnectionState state) {
                if (state == TcpClient::Disconnected)
                    releaseAll();
            });
}

TransmitController::~TransmitController() {
    // CONVENTIONS Rule 11 — sever connections before anything this handler touches tears down.
    disconnect(this);
}

bool TransmitController::engage(Owner who, Route how) {
    // No radio, no transmission. Checked here rather than left to AudioController, which silently
    // drops a PTT-on when disconnected: the arbiter would otherwise believe it held a transmitter
    // that was never keyed, and the indicator would light for it (APP-005).
    if (!m_connection->isConnected()) {
        qCDebug(txOwner) << "engage refused:" << ownerName(who) << "- not connected";
        return false;
    }

    const Owner previous = m_state.owner;
    const bool was = m_state.transmitting();
    const TransmitOwner::Effects e = TransmitOwner::engage(m_state, who, how);
    if (e.refused) {
        qCInfo(txOwner) << "engage refused:" << ownerName(who) << "- held by" << ownerName(previous);
        return false;
    }
    apply(e, previous, was);
    return true;
}

void TransmitController::release(Owner who) {
    const Owner previous = m_state.owner;
    const bool was = m_state.transmitting();
    const TransmitOwner::Effects e = TransmitOwner::release(m_state, who);
    if (e.ignored && previous != Owner::None)
        qCDebug(txOwner) << "release ignored:" << ownerName(who) << "does not hold it -" << ownerName(previous)
                         << "does";
    apply(e, previous, was);
}

void TransmitController::releaseAll() {
    const Owner previous = m_state.owner;
    const bool was = m_state.transmitting();
    apply(TransmitOwner::releaseAll(m_state), previous, was);
}

void TransmitController::apply(const TransmitOwner::Effects &e, Owner previous, bool wasTransmitting) {
    // ORDER IS THE POINT OF THIS FUNCTION.
    //
    // Keying: the source must be in place before the gate opens, and TX; must reach the radio
    // before audio does. Unkeying: the gate must shut before RX; goes out, or QK4 is still
    // streaming when the radio is told to stop and the audio re-keys it; and the source must move
    // only after the gate is shut, or one audio block escapes with the gate still asserted and the
    // source already changed (see AudioController::setTxSourceAfterPtt).
    //
    // sendTx and sendRx are mutually exclusive by construction, and setGate carries one direction,
    // so this single sequence expresses both orderings without branching on which one is happening.
    if (e.setSource && e.sourceBeforeGate())
        m_audio->setTxSource(static_cast<AudioController::TxSource>(e.source));

    if (e.sendTx)
        m_connection->sendCAT(QStringLiteral("TX;"));

    if (e.setGate)
        m_audio->setPttActive(e.gateActive);

    if (e.sendRx)
        m_connection->sendCAT(QStringLiteral("RX;"));

    if (e.setSource && !e.sourceBeforeGate())
        m_audio->setTxSourceAfterPtt(static_cast<AudioController::TxSource>(e.source));

    if (m_state.owner != previous) {
        qCDebug(txOwner) << "transmitter:" << ownerName(previous) << "->" << ownerName(m_state.owner);
        emit ownerChanged(previous, m_state.owner);
    }
    if (e.transmitting != wasTransmitting)
        emit transmittingChanged(e.transmitting);
}
