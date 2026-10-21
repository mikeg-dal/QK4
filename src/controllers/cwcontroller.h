#ifndef CWCONTROLLER_H
#define CWCONTROLLER_H

#include <QObject>
#include <atomic>
#include <memory>

class RadioState;
class ConnectionController;
class PaddleKeyer;
class ForwardingLink;
class SidetoneGenerator;
class HalikeyDevice;
class KpodPlusDevice;

// =============================================================================
// CwController — CW keying orchestration
// =============================================================================
//
// Purpose
// -------
// Orchestration layer that wires the CW devices together. Owns no devices and no threads;
// HardwareController constructs and owns the HaliKey, KPOD+, PaddleKeyer (keyer thread, with its
// clock, link, sidetone scheduler and timer) and SidetoneGenerator (sidetone thread). CwController
// takes them as injected pointers and wires the CW behaviour.
//
// The HaliKey keyer is PaddleKeyer, a port of K4-Companion's (docs/cw-keyer-port-plan.md). It
// sends timed KZD####/KZU####. Element timing travels in the command, so nothing on the path from
// the keyer thread to the socket can change what goes on the air. The KPOD+ runs its own keyer on
// the device and its KZ strings are forwarded untouched.
//
// Construction order
// ------------------
//     m_hardwareController = new HardwareController(rs, cc, this);
//     m_cwController = new CwController(rs, cc,
//         m_hardwareController->paddleKeyer(), m_hardwareController->keyerLink(),
//         m_hardwareController->sidetoneGenerator(), m_hardwareController->halikeyDevice(),
//         m_hardwareController->kpodPlusDevice(), this);
//
// Wiring and threading
// --------------------
// The software keyer sends through ForwardingLink into KzEgress's tagged software link. Posting
// copies into a bounded lock-free queue; strings, routing and monitor control run only on I/O.
// Source generations reject stale queued starts and preserve the release of a forwarded DOWN.
// Vail's worker sends already-keyed edges to the same I/O route without invoking PaddleKeyer.
// The KPOD+ posts its complete strings directly to the egress and bypasses software routing.
//
// The V1.4 handler and HaliKey MIDI sink run on their device threads. They read only atomic
// configuration and pass timestamped paddle events to the keyer. The Vail audio route captures
// its feedback generation before queuing to the sidetone thread, so a source change cannot revive
// old audio. Their producers stop before audio and I/O are destroyed (shutdownDevices()).
//
// Keyability follows CwGate, with PaddleKeyer additionally inhibited while Vail is selected.
// Sidetone output is open only while CW is keyable and the selected device is connected; Vail
// also requires computer sidetone. Its default buzzer setting leaves the output stream closed.
//
// Footswitch PTT: REMOVED, deliberately
// -------------------------------------
//   Neither transport keys PTT from a footswitch. On V1.4 the pedal and the dit lever both drive
//   CTS and are indistinguishable on the wire; the MIDI variant's note 31 is ignored. A footswitch
//   plugged into the K4 itself is unaffected.
//
// Verification (before a release)
// -------------------------------
//   - HaliKey V1.4 and MIDI paddle keying at 20-50 WPM, Iambic A and B; Ultimatic; straight key
//   - KPOD+ keying unchanged (paddle → on-device keyer → EP02 → K4)
//   - Sidetone audible with a constant delay; silent while the KPOD+ owns CW
//   - Hold a paddle in CW and switch to SSB mid-press: keying stops, nothing stuck
//   - Esc mid-burst: keying stops, radio unkeyed
// =============================================================================

namespace VailSidetoneRouting {
class Route;
}

class CwController : public QObject {
    Q_OBJECT

public:
    CwController(RadioState *radioState, ConnectionController *connection, PaddleKeyer *keyer, ForwardingLink *link,
                 SidetoneGenerator *sidetone, HalikeyDevice *halikey, KpodPlusDevice *kpodPlus,
                 QObject *parent = nullptr);
    ~CwController() override;

    /// The KPOD+ has taken over CW keying, or given it back. Driven by HardwareController, which
    /// owns the device lifecycle policy (USB-003). While it's up, QK4's keyer keys nothing.
    void setKpodPlusGate(bool active);

    /// The operator set the K4's remote-keying key-down delay (KZL, 0-1000 ms). Sent, then read
    /// back, so RadioState holds what the radio took. The only place QK4 writes KZL.
    void setKeyDownInitialDelay(int ms);

    /// Esc: stop QK4's own keying now. The burst ends at its next element boundary; an open
    /// straight-key KZD gets its KZU. Vail aborts immediately and waits for held inputs to release.
    void stopKeying();

private:
    void wireKeyingRoutes();
    void configureVailKeyer();
    void updateVailKeying();
    void updateVailSidetone(bool forceReset = false);
    void applySpeed();
    void applyMode();
    void applyReversal();
    void updateInhibit();
    void onPaddle(bool dah, bool asserted, double deltaTime);

    RadioState *m_radioState;
    ConnectionController *m_connection;
    PaddleKeyer *m_keyer;          // owned by HardwareController
    ForwardingLink *m_link;        // owned by HardwareController
    SidetoneGenerator *m_sidetone; // owned by HardwareController
    HalikeyDevice *m_halikey;      // owned by HardwareController
    KpodPlusDevice *m_kpodPlus;    // owned by HardwareController

    QMetaObject::Connection m_paddleConnection;
    QMetaObject::Connection m_vailSidetoneConnection;

    std::shared_ptr<std::atomic<bool>> m_vailSelected = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<std::atomic<bool>> m_kpodPlusOwnsCw = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<std::atomic<bool>> m_vailComputerSidetone = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<std::atomic<quint64>> m_vailSidetoneGeneration = std::make_shared<std::atomic<quint64>>(0);
    std::shared_ptr<std::atomic<bool>> m_vailSidetoneGate = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<VailSidetoneRouting::Route> m_vailSidetoneRoute;
    bool m_vailSidetoneAllowed = false;

    // Main thread only.
    bool m_radioReady = false;
    bool m_kpodPlusGate = false;
    bool m_sidetoneStreamWanted = false; // last value sent to SidetoneGenerator::setStreamWanted

    // true only for V1.4 serial (deviceType 0). Vail bypasses the software keyer.
    std::atomic<bool> m_cachedIsV14{true};
    // The K4's KP paddle orientation is Reverse.
    std::atomic<bool> m_reversed{false};
};

#endif // CWCONTROLLER_H
