#ifndef TRANSMITCONTROLLER_H
#define TRANSMITCONTROLLER_H

#include <QObject>
#include "models/transmitowner.h"

class RadioState;
class ConnectionController;
class AudioController;

/**
 * @brief The single owner of "are we transmitting".
 *
 * Every producer — the PTT button, XMIT, the Esc key, a CAT client, a TCI client, and the radio
 * itself — asks this controller instead of writing the audio gate and the indicator by hand. It
 * holds a TransmitOwner::State, and executes the TransmitOwner::Effects each decision returns.
 *
 * WHY it exists: before it, seven call sites wrote the PTT indicator and five independently wrote
 * the audio gate, only one of them put TX;/RX; on the wire, and no disconnect path released
 * anything. AUDIT.md calls that the G1 violation; AUD-001, INT-001 and INT-002 are symptoms.
 *
 * The decision itself lives in models/transmitowner.h so it can be unit-tested without an audio
 * device or a socket. This class is only the execution half: ordering, marshalling and signals.
 *
 * Threading: main thread only. Every caller is already on main — CatServer's readyRead, TciServer's
 * queued hop into TciController, the widget signals — and AudioController marshals to the audio
 * thread itself.
 */
class TransmitController : public QObject {
    Q_OBJECT

public:
    using Owner = TransmitOwner::Owner;
    using Route = TransmitOwner::Route;

    TransmitController(RadioState *radioState, ConnectionController *connection, AudioController *audio,
                       QObject *parent = nullptr);
    ~TransmitController() override;

    /// Ask to transmit. Returns false if refused — because somebody else holds the transmitter, or
    /// because there is no radio to transmit with. A refusal changes nothing, so the caller must
    /// not light an indicator on the strength of having asked (APP-005).
    bool engage(Owner who, Route how);

    /// Give it up. A release from someone who does not hold it is ignored, not obeyed: acting on it
    /// would let any client unkey the operator.
    void release(Owner who);

    /// Unconditional release — the Esc key, losing the radio, shutting down. Fails closed.
    void releaseAll();

    bool isTransmitting() const { return m_state.transmitting(); }
    Owner owner() const { return m_state.owner; }

signals:
    /// The one signal the PTT indicator follows. Reflects what is actually happening, not what was
    /// requested.
    void transmittingChanged(bool transmitting);

    /// Emitted whenever the holder changes, including to and from Owner::None. TciController uses
    /// it to tell its clients they lost the transmitter — which is why it carries the previous
    /// owner, not just the current one.
    void ownerChanged(TransmitOwner::Owner previous, TransmitOwner::Owner current);

private:
    void apply(const TransmitOwner::Effects &e, Owner previous, bool wasTransmitting);

    RadioState *m_radioState;
    ConnectionController *m_connection;
    AudioController *m_audio;

    TransmitOwner::State m_state;
};

#endif // TRANSMITCONTROLLER_H
