#ifndef CWCONTROLLER_H
#define CWCONTROLLER_H

#include <QObject>
#include <atomic>
#include <QPointer>

class QTimer;
class QMessageBox;
class QWidget;
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
// Wiring
// ------
//   src                                  | dst                         | thread
//   -------------------------------------|-----------------------------|-----------------------------
//   PaddleKeyer → ForwardingLink         | TcpClient's KzEgress        | keyer → I/O (lock-free post)
//   HalikeyDevice::paddleChanged (V1.4)  | PaddleKeyer::paddleEvent    | runs ON the HaliKey worker
//   HalikeyDevice MIDI sink (MIDI)       | PaddleKeyer::midiMessage    | runs ON RtMidi's thread
//   RadioState keyer speed/KP/pitch/ML/KZL| keyer + sidetone + KPOD+    | main
//   RadioState::modeChanged, radioReady,  | PaddleKeyer::setInhibited   | main
//     disconnect, KPOD+ gate             |                             |
//   RadioSettings halikey/*              | keyer mode, straight key,   | main
//                                        | dit memory, monitor mute    |
//   KpodPlusDevice EP02 reader           | TcpClient's KzEgress        | EP02 worker → I/O (lock-free)
//
// Inhibit
// -------
// The keyer keys nothing unless the radio is ready, the mode is CW or CW-R, and the KPOD+ does not
// own CW, and an open HaliKey has been approved and released (CwGate::keyerInhibited). The sidetone's output stream is
// open only while, in addition, a HaliKey port is open (CwGate::sidetoneStreamWanted), so nothing is pumped while idle
// (#92). Raising the inhibit releases whatever is keyed (an open straight-key KZD gets its KZU) and drops paddle events
// at the door. The keyer reads its paddles from time-stamped history, so a single release is all a gate needs.
//
// Threading invariants
// --------------------
//   1. The V1.4 line handler runs DirectConnection on the HaliKey worker, and the MIDI sink on
//      RtMidi's thread. Both stamp the edge on arrival; a queued hop would be timing error.
//   2. m_cachedIsV14 and m_reversed are stored on the main thread (release) and loaded on those
//      device threads (acquire).
//   3. The keyer's commands reach TcpClient through ForwardingLink → KzEgress, a lock-free queue
//      the I/O thread drains in order. FIFO per producer, so KZD/KZU stay in order. The keyer
//      thread is the only producer: a release, inhibit or close from the main thread posts a
//      request to the keyer's queue and never sends KZ or ML; the monitor restore is the keyer's
//      own deadline.
//   4. KPOD+ EP02 keyer data goes from the EP02 worker to the same KzEgress.
//   5. The destructor cuts the V1.4 line handler first (CONVENTIONS Rule 11). That stops new
//      calls only; an edge already in flight on the worker is ruled out by shutdownDevices().
//   6. The link is never gated: a KZU closing an open KZD must always reach the K4. Suppression
//      is the inhibit's job, upstream of the keyer.
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
    /// straight-key KZD gets its KZU.
    void stopKeying();
    void requestKeyingEnable();
    QString keyingStatus() const { return m_keyingStatus; }

signals:
    void keyingStatusChanged(const QString &status);

private:
    QWidget *keyingDialogParent() const;
    void resetKeyingSafety();
    void checkKeyingSafety();
    void closeKeyingDialog();
    void setKeyingStatus(const QString &status);
    void waitForRelease();
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

    // The V1.4 line handler (invariant 1). Kept so the destructor can cut it: it runs on the HaliKey
    // worker, and ~QObject would drop it only after this object's members are gone.
    QMetaObject::Connection m_paddleConnection;

    // Main thread only.
    QTimer *m_safetyTimer = nullptr;
    QPointer<QMessageBox> m_keyingDialog;
    QWidget *m_dialogParent = nullptr;
    bool m_prompted = false;
    bool m_waitingForRelease = false;
    QString m_keyingStatus = QStringLiteral("Keying disabled");
    bool m_radioReady = false;
    bool m_kpodPlusGate = false;
    bool m_sidetoneStreamWanted = false; // last value sent to SidetoneGenerator::setStreamWanted

    // true = V1.4 serial (deviceType 0), false = MIDI (deviceType 1). See invariant 2.
    std::atomic<bool> m_cachedIsV14{true};
    // The K4's KP paddle orientation is Reverse. See invariant 2.
    std::atomic<bool> m_reversed{false};
};

#endif // CWCONTROLLER_H
