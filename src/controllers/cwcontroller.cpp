#include "cwcontroller.h"

#include <QLoggingCategory>

// Defined in hardware/iambickeyer.cpp. The gate below belongs with the keyer trace, not in a
// category of its own — a reader following CW needs both in one stream.
Q_DECLARE_LOGGING_CATEGORY(cwKeyer)

#include "audio/sidetonegenerator.h"
#include "connectioncontroller.h"
#include "hardware/halikeydevice.h"
#include "hardware/iambickeyer.h"
#include "hardware/kpodplusdevice.h"
#include "models/radiostate.h"
#include "network/tcpclient.h"
#include "settings/radiosettings.h"
#include "utils/radioutils.h"

CwController::CwController(RadioState *radioState, ConnectionController *connection, IambicKeyer *keyer,
                           SidetoneGenerator *sidetone, HalikeyDevice *halikey, KpodPlusDevice *kpodPlus,
                           QObject *parent)
    : QObject(parent), m_radioState(radioState), m_connection(connection), m_keyer(keyer), m_sidetone(sidetone),
      m_halikey(halikey), m_kpodPlus(kpodPlus) {

    // =========================================================================
    // Initial keyer + sidetone state from RadioState
    // =========================================================================
    int initWpm = m_radioState->keyerSpeed();
    if (initWpm <= 0)
        initWpm = 20;
    QMetaObject::invokeMethod(m_keyer, "setSpeed", Qt::QueuedConnection, Q_ARG(int, initWpm));
    QMetaObject::invokeMethod(
        m_keyer, "setMode", Qt::QueuedConnection,
        Q_ARG(IambicKeyer::Mode, m_radioState->iambicMode() == 'B' ? IambicKeyer::IambicB : IambicKeyer::IambicA));
    QMetaObject::invokeMethod(m_keyer, "setReversed", Qt::QueuedConnection,
                              Q_ARG(bool, m_radioState->paddleOrientation() == 'R'));

    if (m_radioState->cwPitch() > 0) {
        QMetaObject::invokeMethod(m_sidetone, "setFrequency", Qt::QueuedConnection,
                                  Q_ARG(int, m_radioState->cwPitch()));
    }
    if (m_radioState->keyerSpeed() > 0) {
        QMetaObject::invokeMethod(m_sidetone, "setKeyerSpeed", Qt::QueuedConnection,
                                  Q_ARG(int, m_radioState->keyerSpeed()));
    }

    // =========================================================================
    // RadioState observers — keyer speed / paddle / pitch
    // =========================================================================
    connect(m_radioState, &RadioState::keyerSpeedChanged, this, [this](int wpm) {
        // WHY use invokeMethod instead of a direct call: SidetoneGenerator lives on
        // its own thread. setKeyerSpeed only writes a std::atomic<int> today (safe direct),
        // but the matching invokeMethod for m_keyer on the next line establishes the
        // cross-thread pattern — future changes to setKeyerSpeed that touch non-atomic
        // members would otherwise introduce a silent race with no call-site warning.
        QMetaObject::invokeMethod(m_sidetone, "setKeyerSpeed", Qt::QueuedConnection, Q_ARG(int, wpm));
        QMetaObject::invokeMethod(m_keyer, "setSpeed", Qt::QueuedConnection, Q_ARG(int, wpm));
        // Sync element length with K4 server — same conversion the local keyer and sidetone use.
        int ditMs = RadioUtils::ditMsForWpm(wpm);
        m_connection->sendCAT(QString("KZL%1;").arg(ditMs, 2, 10, QChar('0')));
        // K4 is the source of truth — mirror the speed onto the KPOD+ keyer.
        if (m_kpodPlus->isPolling())
            m_kpodPlus->setKeyerSpeed(wpm);
    });

    // Update the local iambic keyer mode/reversal — and the KPOD+ keyer — when
    // the K4's KP settings change. The K4 is the source of truth.
    connect(m_radioState, &RadioState::keyerPaddleChanged, this, [this](QChar iambic, QChar paddle, int /*weight*/) {
        QMetaObject::invokeMethod(
            m_keyer, "setMode", Qt::QueuedConnection,
            Q_ARG(IambicKeyer::Mode, iambic == 'B' ? IambicKeyer::IambicB : IambicKeyer::IambicA));
        QMetaObject::invokeMethod(m_keyer, "setReversed", Qt::QueuedConnection, Q_ARG(bool, paddle == 'R'));
        if (m_kpodPlus->isPolling())
            m_kpodPlus->setKeyerParams(iambic == 'B' ? 1 : 0, paddle == 'R');
    });

    connect(m_radioState, &RadioState::cwPitchChanged, this, [this](int pitchHz) {
        QMetaObject::invokeMethod(m_sidetone, "setFrequency", Qt::QueuedConnection, Q_ARG(int, pitchHz));
        // K4 is the source of truth — mirror the CW pitch onto the KPOD+ keyer.
        if (m_kpodPlus->isPolling())
            m_kpodPlus->setCwPitch(pitchHz);
    });

    // =========================================================================
    // Mode tracking + V1.4 PTT-line demux cleanup
    // =========================================================================
    // WHY read m_cachedMode instead of m_radioState->mode() on the HaliKey worker
    // thread: m_radioState->mode() reads a non-atomic subsystem field concurrently
    // with parseCATCommand()'s writes on the main thread — data race. The atomic
    // cache is updated from modeChanged via AutoConnection — both this controller
    // and RadioState live on the main thread, so AutoConnection resolves to
    // DirectConnection and the store runs synchronously alongside parseCATCommand.
    // The HaliKey worker thread reads with acquire ordering, paired with the
    // release store here.
    m_cachedMode.store(static_cast<int>(m_radioState->mode()), std::memory_order_release);
    connect(m_radioState, &RadioState::modeChanged, this, [this](RadioState::Mode mode) {
        m_cachedMode.store(static_cast<int>(mode), std::memory_order_release);

        // Release both levers on any mode change. The line handler gates them on CW, so a lever
        // held across the transition would otherwise stay set on the keyer with no further event
        // to clear it — and would still be down on the next entry into CW.
        const bool inCw = (mode == RadioState::CW || mode == RadioState::CW_R);
        if (!inCw)
            m_keyer->setPaddleState(false, false);
    });

    // Device-type fan-out: mirror for the V1.4 PTT demux below + the keyer's hold
    // gate (V1.4 serial needs the bounce gate; MIDI is firmware-debounced and WinMM
    // burst delivery would make an arrival-time gate drop real elements). RadioSettings
    // is a plain main-thread singleton; the PTT handler runs on the HaliKey worker
    // thread — same store/load pattern as m_cachedMode above. setHoldGateEnabled is a
    // plain atomic write, safe to call directly from the main thread.
    const bool initIsV14 = (RadioSettings::instance()->halikeyDeviceType() != 1);
    m_cachedIsV14.store(initIsV14, std::memory_order_release);
    m_keyer->setHoldGateEnabled(initIsV14);
    connect(RadioSettings::instance(), &RadioSettings::halikeyDeviceTypeChanged, this, [this](int type) {
        const bool isV14 = (type != 1);
        m_cachedIsV14.store(isV14, std::memory_order_release);
        m_keyer->setHoldGateEnabled(isV14);
    });

    // =========================================================================
    // Keyer → CAT commands + sidetone audio
    // =========================================================================
    //
    // Wire keyer signals (emitted on the HighPriority keyer thread) directly to TcpClient on
    // the I/O thread via queued connections. The main thread is not on this hot path.
    // The atomic gate on ConnectionController drops emissions when the KPOD+ device owns
    // the keyer; the local-iambic state machine still runs but its KZ output is suppressed.
    //
    // Order preservation: all three signals (restartAfterPause, elementStarted, characterSpace)
    // originate on the same source thread (keyer) and target the same destination thread (I/O),
    // so Qt's event queue keeps them FIFO. The on-air ordering is:
    //   restartAfterPause → elementStarted (per enterElement)
    //   characterSpace → keyingFinished     (per goIdle)
    // matching the K4 KZ protocol (KZP timing → KZ./KZ- elements → KZ ; letter marker —
    // literal 0x20 SPACE, confirmed by hexdump of live KPOD+ EP02 traffic; the PDF spec's
    // monospace rendering of "KZ_;" is a typographic artifact, not an underscore byte).
    auto *tc = m_connection->tcpClient();
    auto *cc = m_connection;
    connect(
        m_keyer, &IambicKeyer::elementStarted, tc,
        [tc, cc](bool isDit) {
            if (cc->isKpodPlusKeyerActive())
                return;
            tc->sendCAT(isDit ? QStringLiteral("KZ.;") : QStringLiteral("KZ-;"));
        },
        Qt::QueuedConnection);
    connect(
        m_keyer, &IambicKeyer::characterSpace, tc,
        [tc, cc]() {
            if (cc->isKpodPlusKeyerActive())
                return;
            // WHY space, not underscore: the Elecraft KPodKeyerInterface.pdf renders the
            // letter-space marker as "KZ_;" but a hexdump of EP02 traffic from a live KPOD+
            // device shows the actual byte is 0x20 (literal SPACE). The K4 firmware accepts
            // that form; emitting "KZ_;" with a real underscore is what the parser rejects.
            // PDF rendering artifact — the underline beneath the space in the spec's
            // monospace font reads as an underscore.
            tc->sendCAT(QStringLiteral("KZ ;"));
        },
        Qt::QueuedConnection);
    connect(
        m_keyer, &IambicKeyer::restartAfterPause, tc,
        [tc, cc](int ms) {
            if (cc->isKpodPlusKeyerActive())
                return;
            tc->sendCAT(QStringLiteral("KZP%1;").arg(ms, 4, 10, QChar('0')));
        },
        Qt::QueuedConnection);

    // Sidetone stays on its own thread. Sidetone gate uses the local helper so a hot KPOD+
    // takeover silences feedback immediately even before the next emit lands on I/O.
    connect(m_keyer, &IambicKeyer::elementStarted, m_sidetone, [this, sg = m_sidetone](bool isDit) {
        if (kpodPlusActive())
            return;
        isDit ? sg->playSingleDit() : sg->playSingleDah();
    });
    // No keyingFinished → sidetone wiring: each element is written as a complete
    // PCM block (tone + space) and always plays to completion — there is nothing
    // to stop when the keyer goes idle.

    // =========================================================================
    // HaliKey lines → keyer (ZERO-LATENCY DirectConnection)
    // =========================================================================
    // Direct on the HaliKey worker thread (invariant 1). Both levers reach the keyer in a single
    // call from a single sample, which is what stops a released squeeze from being seen
    // half-applied — the case that appended an element the operator never keyed. See
    // IambicKeyer::setPaddleState.
    //
    // Line → lever mapping differs by transport. V1.4 serial firmware reports the dit lever on CTS
    // (which `lineStateChanged` carries as `ptt`), while MIDI has a dedicated dit line. Neither
    // transport keys PTT from a footswitch any more — see the header's "Footswitch PTT: REMOVED".
    connect(
        m_halikey, &HalikeyDevice::lineStateChanged, this,
        [this](bool dit, bool dah, bool ptt) {
            const bool isV14 = m_cachedIsV14.load(std::memory_order_acquire);
            const auto mode = static_cast<RadioState::Mode>(m_cachedMode.load(std::memory_order_acquire));
            const bool inCw = (mode == RadioState::CW || mode == RadioState::CW_R);

            // WHY the KPOD+ gate is a TERM here and not an early return at the top of the handler:
            // as a term it keeps the lever output a pure function of (sample, mode, transport,
            // gate), so a
            // lever held across a gate rise is released by the very next edge instead of staying
            // latched on the keyer until the gate clears - and then emitting KZ nobody keyed.
            const bool gated = kpodPlusActive();

            // Both levers are gated on CW together. Keying the radio from a paddle in SSB/AM/FM is
            // never wanted, and letting one lever through outside CW also left its state set on the
            // keyer going back into CW.
            const bool ditLever = !gated && inCw && (isV14 ? ptt : dit);
            const bool dahLever = !gated && inCw && dah;
            m_keyer->setPaddleState(ditLever, dahLever);
        },
        Qt::DirectConnection);

    // Enable keyer when radio connects, disable on disconnect
    connect(m_connection, &ConnectionController::radioReady, this,
            [this]() { QMetaObject::invokeMethod(m_keyer, "setEnabled", Qt::QueuedConnection, Q_ARG(bool, true)); });
    connect(m_connection, &ConnectionController::connectionStateChanged, this,
            [this](TcpClient::ConnectionState state) {
                if (state == TcpClient::Disconnected) {
                    QMetaObject::invokeMethod(m_keyer, "setEnabled", Qt::QueuedConnection, Q_ARG(bool, false));
                }
            });

    // Stop keyer when HaliKey disconnects (prevents runaway keying
    // if paddle was held when disconnected — Note Off never arrives)
    connect(m_halikey, &HalikeyDevice::disconnected, this, [this]() {
        // Both levers down, explicitly: IambicKeyer::stop() is guarded on the keyer not being
        // Idle, so it leaves m_phys untouched when nothing was being sent, and a lever held at
        // unplug would still read as down on the next entry into CW.
        m_keyer->setPaddleState(false, false);
        QMetaObject::invokeMethod(m_keyer, "stop", Qt::QueuedConnection);
    });

    // =========================================================================
    // KPOD+ keyer-active gate + EP02 keyer data routing
    // =========================================================================
    // The KPOD+ owns the entire CW chain when present. The gate is set on
    // deviceInfoReady (KPOD+ detected) rather than deviceConnected (open
    // succeeded) so the ~10-100 ms open window doesn't leak paddle events to
    // the local sidetone path.
    connect(m_kpodPlus, &KpodPlusDevice::deviceConnected, this, [this]() { setKpodPlusGate(true); });
    connect(m_kpodPlus, &KpodPlusDevice::deviceDisconnected, this, [this]() { setKpodPlusGate(false); });
    connect(m_kpodPlus, &KpodPlusDevice::deviceInfoReady, this, [this]() {
        if (m_kpodPlus->isDetected())
            setKpodPlusGate(true);
    });

    // EP02 keyer data → straight to the I/O thread.
    //
    // The KPOD+ delivers complete KZ/KX strings in 32-byte transfers, zero-padded after the
    // last ';'. By targeting TcpClient as the receiver (lives on the I/O thread) with a
    // queued connection we skip the main thread entirely on this hot path. sendCATBytes
    // trims NUL padding on the I/O thread and hands off to sendCAT().
    connect(m_kpodPlus, &KpodPlusDevice::keyerDataReceived, m_connection->tcpClient(), &TcpClient::sendCATBytes,
            Qt::QueuedConnection);
}

CwController::~CwController() {
    // CONVENTIONS Rule 11.
    //
    // This used to claim it ran "before HardwareController tears down the devices these handlers
    // reference". It does not: HardwareController is constructed first, and Qt destroys children in
    // construction order, so it is already gone by the time this runs. That belief is part of what
    // made CONC-001 look safe.
    //
    // What actually protects these handlers is MainWindow::closeEvent calling
    // HardwareController::shutdownDevices(), which stops the HaliKey worker, the keyer and the
    // sidetone while ConnectionController - which kpodPlusActive() dereferences - is still alive.
    // The disconnect below is the second line of defence, not the first.
    disconnect(this);
}

void CwController::setKpodPlusGate(bool active) {
    // The single most useful line in a CW bench log: it says who is generating the elements. While
    // the gate is up QK4's own keyer still runs but its KZ output and sidetone are suppressed, so a
    // log without this cannot distinguish "the KPOD+ is keying correctly" from "both are keying and
    // one of them is inaudible".
    qCInfo(cwKeyer) << "KPOD+ keyer gate"
                    << (active ? "UP - the KPOD+ owns CW; local KZ and sidetone suppressed"
                               : "DOWN - QK4's own keyer owns CW again");
    // Order is load-bearing. The release store has to be visible to the HaliKey worker's acquire
    // load BEFORE the levers are forced down, or an edge landing between the two lines recomputes
    // them with the gate still clear and sets them straight back.
    m_connection->setKpodPlusKeyerActive(active);
    if (active) {
        // A lever held when the KPOD+ takes over stays down on the keyer otherwise, and surfaces
        // as KZ nobody keyed once the KPOD+ is unplugged again. Unconditional rather than
        // edge-detected: the store is idempotent and cannot produce an element with the gate
        // already set, and an edge check here is one more thing to get wrong.
        m_keyer->setPaddleState(false, false);
    }
}

bool CwController::kpodPlusActive() const {
    return m_connection->isKpodPlusKeyerActive();
}
