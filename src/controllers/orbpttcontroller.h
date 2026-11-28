#ifndef ORBPTTCONTROLLER_H
#define ORBPTTCONTROLLER_H

#include "hardware/orbptt.h"

#include <QObject>
#include <QPointer>

class RadioState;
class ConnectionController;
class HalikeyDevice;
class TransmitController;

// =============================================================================
// OrbPttController — the ORB's PTT/KEY jack as push-to-talk
// =============================================================================
//
// Wires the jack to TransmitController as Owner::OrbPtt, streaming QK4's Audio Input exactly as
// the on-screen PTT button does. The rule is OrbPtt::Gate and OrbPtt::allowed (hardware/orbptt.h);
// this class only feeds it.
//
// Off by default, and ORB only: on the ORB the jack has a modem line to itself (CTS), which no
// paddle uses. HaliKey footswitch PTT stays withdrawn (see cwcontroller.h).
//
// Threading: main thread only. Switch changes, mode changes and the device going away all arrive
// here in order, so a release cannot overtake its press. HalikeyDevice drops events from a closed
// port.
//
// Released by: the switch opening, the HaliKey port closing (unplug, read failure, Disconnect,
// device type change, quit), the option going off, a change away from a voice mode, and losing the
// radio. Esc releases through TransmitController; a switch still held does not key again.
// =============================================================================

class OrbPttController : public QObject {
    Q_OBJECT

public:
    OrbPttController(RadioState *radioState, ConnectionController *connection, HalikeyDevice *halikey,
                     TransmitController *transmit, QObject *parent = nullptr);

private:
    bool allowed() const;
    void recheck(); // after a mode, split or setting change
    void apply(OrbPtt::Action action);

    RadioState *m_radioState;
    // WHY a QPointer: owned by HardwareController, which is destroyed before this object.
    QPointer<HalikeyDevice> m_halikey;
    TransmitController *m_transmit;
    OrbPtt::Gate m_gate;
};

#endif // ORBPTTCONTROLLER_H
