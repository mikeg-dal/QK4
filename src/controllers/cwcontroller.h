#ifndef CWCONTROLLER_H
#define CWCONTROLLER_H

#include <QObject>
#include <atomic>

class RadioState;
class ConnectionController;
class IambicKeyer;
class SidetoneGenerator;
class HalikeyDevice;
class KpodPlusDevice;

// =============================================================================
// CwController — CW keying orchestration
// =============================================================================
//
// Purpose
// -------
// Pure orchestration layer that wires existing devices together for the CW
// use case. Owns no devices and no threads; HardwareController retains
// construction + thread management for KPOD, KPOD+, HaliKey, IambicKeyer
// (keyer thread), SidetoneGenerator (sidetone thread). CwController takes
// those devices as injected pointers and wires the CW-specific signal
// connections that used to crowd HardwareController.
//
// Pulling this out of HardwareController shrinks HW from ~558 LOC to
// ~150 LOC and makes the seam between "what is hardware" and "what is
// CW behavior" explicit. The riskiest single architectural change in
// the plan — see "Threading invariants" below.
//
// Construction order
// ------------------
// MainWindow constructs HardwareController first (devices + threads),
// then CwController, fetching the devices via HardwareController's
// Rule 2-documented accessors:
//
//     m_hardwareController = new HardwareController(rs, cc, this);
//     m_cwController = new CwController(
//         rs, cc,
//         m_hardwareController->iambicKeyer(),
//         m_hardwareController->sidetoneGenerator(),
//         m_hardwareController->halikeyDevice(),
//         m_hardwareController->kpodPlusDevice(),
//         this);
//
// Signals owned (moved from HardwareController)
// ---------------------------------------------
//   src                                  | dst                | thread hop                | conn
//   -------------------------------------|--------------------|---------------------------|--------
//   RadioState::keyerSpeedChanged        | sidetone + keyer + | main -> sidetone thread   | invokeMethod queued
//                                        | KZL to K4 +        | main -> keyer thread      |
//                                        | KPOD+ setKeyerSpd  | main -> I/O thread (sendCAT)
//   RadioState::keyerPaddleChanged       | keyer mode/reverse | main -> keyer thread      | invokeMethod queued
//                                        | + KPOD+ setKeyer-  |                           |
//                                        | Params             |                           |
//   RadioState::cwPitchChanged           | sidetone freq +    | main -> sidetone thread   | invokeMethod queued
//                                        | KPOD+ setCwPitch   |                           |
//   RadioState::modeChanged              | m_cachedMode +     | main -> main (atomic)     | AutoConnection (Direct)
//   IambicKeyer::elementStarted          | KZ. / KZ- to K4    | keyer -> I/O thread       | QueuedConnection
//   IambicKeyer::characterSpace          | KZ space to K4     | keyer -> I/O thread       | QueuedConnection
//   IambicKeyer::restartAfterPause       | KZP%04d to K4      | keyer -> I/O thread       | QueuedConnection
//   IambicKeyer::elementStarted          | sidetone dit/dah   | keyer -> sidetone thread  | AutoConnection (Queued)
//   HalikeyDevice::lineStateChanged      | keyer              | HaliKey worker -> same    | DirectConnection
//                                        |  setPaddleState    |  (runs ON the worker)     |
//   HalikeyDevice::disconnected          | stop keyer         | main -> main              | AutoConnection
//   ConnectionController::radioReady     | keyer setEnabled t | main -> keyer thread      | invokeMethod queued
//   ConnectionController::connection-    | keyer setEnabled f | main -> keyer thread      | invokeMethod queued
//                       StateChanged     |                    |                           |
//   KpodPlusDevice::deviceConnected      | setKpodPlusKeyer-  | main -> main (atomic)     | AutoConnection
//                                        | Active(true)       |                           |
//   KpodPlusDevice::deviceDisconnected   | setKpodPlusKeyer-  | main -> main (atomic)     | AutoConnection
//                                        | Active(false)      |                           |
//   KpodPlusDevice::deviceInfoReady      | startPolling-side  | main -> main (atomic)     | AutoConnection
//                                        | preemptive set(t)  |                           |
//   KpodPlusDevice::keyerDataReceived    | TcpClient::send-   | EP02 worker -> I/O thread | QueuedConnection
//                                        | CATBytes           |                           |
//
// Gates & suppression
// -------------------
// The KPOD+ keyer-active gate (`ConnectionController::m_kpodPlusKeyerActive`,
// std::atomic<bool>) lives on ConnectionController because both CwController
// and the iambic-keyer-to-TcpClient lambdas on the I/O thread read it.
// CwController is the sole WRITER after the refactor (set on KPOD+
// deviceConnected / deviceInfoReady, cleared on deviceDisconnected); the
// iambic CAT lambdas and the HaliKey paddle handlers read with acquire
// ordering paired with CwController's release stores.
//
// When the gate is on, locally-driven CW emissions are suppressed: the
// paddle levers are withheld from the keyer, and the KZ output and sidetone
// playback of the iambic state machine (which still runs) drop. KPOD+ owns
// the CW chain when active.
//
// The gate used to be an early return at the top of the lineStateChanged
// handler; it is now a term of the lever expression, which means a lever
// held across a gate rise is released by the next edge rather than staying
// latched on the keyer until the KPOD+ goes away.
//
// State moved from HardwareController
// -----------------------------------
//   std::atomic<int> m_cachedMode
//     Mirror of RadioState::mode written from the modeChanged AutoConnection
//     (resolves to Direct since both ends live on the main thread, so the
//     store runs synchronously with parseCATCommand). Read on the HaliKey
//     worker thread by the dit/dah/PTT DirectConnection handlers with
//     acquire ordering. Avoids a data race against parseCATCommand writing
//     the non-atomic subsystem field.
//
//   std::atomic<bool> m_cachedIsV14
//     Mirror of RadioSettings::halikeyDeviceType() != 1. Stored on the main
//     thread (ctor + halikeyDeviceTypeChanged, release); read on the HaliKey
//     worker thread by the PTT DirectConnection handler (acquire). Replaces
//     a racy worker-thread read of the plain int on the settings singleton.
//
// Footswitch PTT: REMOVED, deliberately
// -------------------------------------
//   Neither transport keys PTT from a footswitch. On V1.4 the pedal and the
//   dit lever both drive CTS and are indistinguishable on the wire, and a
//   user may have wired a footswitch inline with the paddles; the MIDI
//   variant's note 31 is read and dropped. Withdrawn pending wiring
//   information from the hardware developer rather than guessing per
//   transport. If it returns it will be behind an explicit "alternate
//   wiring" setting, default off, so this code path does not exist unless
//   the operator says their hardware has it.
//
//   What went with it: CwController::pttRequested, the V1.4 CTS demux,
//   m_v14PttDestination and its CAS release, and m_lastPttState. A
//   footswitch plugged into the physical K4 is unaffected — that keys the
//   radio directly and QK4 sees it as a TX echo.
//
// Threading invariants (preserve verbatim)
// ----------------------------------------
//   1. The HaliKey LEVER handler MUST stay DirectConnection on the HaliKey
//      worker thread. Anything else adds latency to CW keying.
//   2. m_cachedMode store happens on the main thread (AutoConnection
//      from RadioState::modeChanged resolves to Direct); load happens on
//      the HaliKey worker thread with acquire ordering. m_cachedIsV14
//      follows the same rule.
//   4. IambicKeyer signals route to TcpClient on the I/O thread via
//      QueuedConnection — keyer thread is high-priority, main thread is
//      bypassed entirely. Order preservation relies on Qt's FIFO event
//      queue between a single source thread and a single dest thread.
//   5. KPOD+ EP02 keyer data routes EP02 worker -> I/O thread via
//      QueuedConnection. Main thread is bypassed on the hot path.
//   6. The KPOD+ keyer-active gate must be set on deviceInfoReady (not
//      deviceConnected) so the ~10-100 ms open window doesn't leak
//      paddle events to the local sidetone path.
//   7. Destructor MUST run disconnect(this) first per CONVENTIONS Rule 11
//      to sever signal connections before any cross-thread devices tear
//      down underneath the connections.
//   8. RESERVED. This slot held the rule that the HaliKey pedal demux must
//      run on the main thread. The footswitch path was removed (see the
//      note above the class), so there is nothing left to serialise. Kept
//      as a numbered gap so invariant 9 does not silently renumber in
//      commit history.
//   9. The KPOD+ gate is a TERM of the lever expression, never an early
//      return, so the lever output stays a pure function of (sample, mode,
//      transport, gate) and converges on the next edge. It gates levers, KZ
//      and sidetone only — the KPOD+ owns CW keying, not voice PTT. On a
//      gate rise, store the gate FIRST and then force both levers off; the
//      reverse order lets a worker edge land in between and re-set them.
//
// What stays in HardwareController
// --------------------------------
//   - KPOD USB tuning knob (encoder -> tuning CAT, button -> macro)
//   - Device construction + thread management
//   - Device-config push (applyKpodPlusConfig on KPOD+ plug-in pushes the
//     current K4 keyer state from RadioState, encode mode from RadioSettings,
//     and a fixed stuck timeout — device-arrival snapshot. Live K4 changes
//     are mirrored to the KPOD+ by CwController, above)
//   - Sidetone volume / output-device follow (audio device lifecycle,
//     not CW behavior)
//   - shutdownDevices() public entry point used by MainWindow::closeEvent,
//     which stops the HaliKey worker, the keyer and the sidetone while
//     ConnectionController is still alive (CONC-001)
//   - macroRequested / hardwareError signal forwarding
//
// Verification (mandatory before PR 17 merges)
// --------------------------------------------
//   - HaliKey V1.4 paddle keying works at multiple WPM speeds
//   - HaliKey MIDI paddle keying works
//   - KPOD+ keying works (paddle -> on-device keyer -> EP02 -> K4)
//   - Sidetone audible during keying, gated correctly when KPOD+ active
//   - Hold a paddle in CW, switch mode mid-press to SSB; keyer cleanly
//     stops, no stuck KZ
//   - WPM changes during keying: KZL syncs correctly to K4
//   - KPOD+ presence detection gates IambicKeyer correctly (no double
//     keying when KPOD+ plug-in event arrives mid-paddle)
//   - ASAN clean (debug build with QK4_TESTS_SANITIZE=ON)
//
// Stop criterion: any keying regression -> revert immediately. Don't
// try to fix forward on the CW hot path.
// =============================================================================

class CwController : public QObject {
    Q_OBJECT

public:
    CwController(RadioState *radioState, ConnectionController *connection, IambicKeyer *keyer,
                 SidetoneGenerator *sidetone, HalikeyDevice *halikey, KpodPlusDevice *kpodPlus,
                 QObject *parent = nullptr);
    ~CwController() override;

    // No signals: pttRequested was the only one, and it went with the footswitch path.

private:
    // Set or clear the KPOD+ keyer-active gate, releasing both levers when it rises. Every writer
    // of the gate goes through here so the store-then-release order cannot drift between them.
    void setKpodPlusGate(bool active);

    // True when the KPOD+ device owns the CW path — reads the shared
    // atomic gate on ConnectionController with acquire ordering. While
    // set, all locally-driven KZ output + sidetone playback is suppressed.
    bool kpodPlusActive() const;

    RadioState *m_radioState;
    ConnectionController *m_connection;
    IambicKeyer *m_keyer;          // owned by HardwareController
    SidetoneGenerator *m_sidetone; // owned by HardwareController
    HalikeyDevice *m_halikey;      // owned by HardwareController
    KpodPlusDevice *m_kpodPlus;    // owned by HardwareController

    // See "State moved from HardwareController" above for invariants.
    std::atomic<int> m_cachedMode{0};

    // true = V1.4 serial (deviceType 0), false = MIDI (deviceType 1).
    // Main-thread release store, HaliKey-worker acquire load — see doc block.
    std::atomic<bool> m_cachedIsV14{true};
};

#endif // CWCONTROLLER_H
