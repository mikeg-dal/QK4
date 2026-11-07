#ifndef HARDWARECONTROLLER_H
#define HARDWARECONTROLLER_H

#include <QObject>
#include <functional>
#include <memory>

#include "hardware/usbdevicelifecycle.h"
#include <QThread>

class KpodDevice;
class KpodPlusDevice;
class HalikeyDevice;
class MidiDeviceWatcher;
class PaddleKeyer;
class SidetoneGenerator;
class SidetoneScheduler;
class SteadyKeyerClock;
class ForwardingLink;
class RadioState;
class ConnectionController;

/**
 * @brief Owns hardware-side workers: KPOD USB knob (hidapi on its own worker thread), HaliKey CW
 *        paddle (HalikeyDevice — holds its own worker thread), PaddleKeyer (real-time keyer thread)
 *        with its sidetone scheduler and clock, SidetoneGenerator (sidetone thread). Translates
 *        KPOD button presses → macroRequested (MacroController::executeMacro).
 *
 * CW-keying orchestration (PaddleKeyer + HaliKey paddle wiring, the KPOD+ keyer-active gate,
 * the keyer's link to the K4) lives on CwController — see
 * cwcontroller.h. HardwareController constructs and owns the devices + threads;
 * CwController wires their CW behavior together via injected pointers.
 */
class HardwareController : public QObject {
    Q_OBJECT

public:
    HardwareController(RadioState *radioState, ConnectionController *connController, QObject *parent = nullptr);
    ~HardwareController();

    // WHY (CONVENTIONS Rule 2 documented exception): these accessors return raw
    // pointers to owned sub-objects. OptionsDialog's per-device pages (KpodPage,
    // CwKeyerPage) need direct handles to construct their config
    // widgets, observe per-device signals, and invoke device-specific setters
    // that don't belong on a generic controller façade. Wrapping all of this in
    // HardwareController would inflate its surface area without collapsing any
    // cross-controller coupling. Same shape as ConnectionController::tcpClient().
    // If a future page only needs a single signal or property, prefer adding a
    // HardwareController-level signal or getter and remove that consumer's
    // dependency on the raw pointer.
    KpodDevice *kpodDevice() const { return m_kpodDevice; }
    KpodPlusDevice *kpodPlusDevice() const { return m_kpodPlusDevice; }
    HalikeyDevice *halikeyDevice() const { return m_halikeyDevice; }

    // Keyer + sidetone accessors — consumed by CwController, which is constructed right after
    // HardwareController and wires the CW graph across these (HardwareController-owned) objects.
    // Same Rule 2 documented-exception rationale as the device accessors above.
    PaddleKeyer *paddleKeyer() const { return m_paddleKeyer.get(); }
    ForwardingLink *keyerLink() const { return m_keyerLink.get(); }
    SidetoneGenerator *sidetoneGenerator() const { return m_sidetoneGenerator; }

    /// Stop every input producer this controller owns — HaliKey, the iambic keyer, the sidetone,
    /// KPOD and KPOD+ — and join their threads. Idempotent; the destructor calls it too.
    ///
    /// WHY public (CONC-001): MainWindow::closeEvent runs it while ConnectionController, which the
    /// HaliKey worker, the keyer and the sidetone reach, is still alive. Qt destroys children in
    /// CONSTRUCTION order, and ConnectionController is constructed before HardwareController.
    ///
    /// The order inside is load-bearing: HaliKey stops paddle events first, then the keyer (which
    /// produces KZ), then the sidetone (which consumes them). Producers before consumers.
    void shutdownDevices();
    void toggleHalikeyConnection();

private:
    /// "HaliKey V1.4" or "HaliKey MIDI", matching the Options dropdown, so a notification and the
    /// settings page can never disagree about what the operator's device is called.
    QString halikeyName() const;
    // Logs, once each, the scheduling class the real-time CW threads asked for and got. They
    // cannot log it themselves (CONVENTIONS Rule 16).
    void reportThreadScheduling();

    /// Apply one lifecycle event to a device and carry out what the policy decides.
    ///
    /// This is the ONLY place either device is opened, closed, or reported to the operator
    /// (USB-002).
    void applyKpod(UsbDeviceLifecycle::Event event);
    void applyKpodPlus(UsbDeviceLifecycle::Event event);

    UsbDeviceLifecycle::State m_kpodState;
    UsbDeviceLifecycle::State m_kpodPlusState;

    // Pushes the K4's keyer settings to a freshly opened KPOD+. Held rather than captured in a
    // lambda so the policy can invoke it on whichever event opens the device.
    std::function<void()> m_applyKpodPlusConfig;

public:
signals:
    // KPOD button press → MacroController::executeMacro
    void macroRequested(const QString &functionId);

    /// The KPOD+ is taking over CW keying, or giving it back.
    ///
    /// Emitted from the lifecycle policy's decision to open or close, which is deliberately AHEAD
    /// of the device actually opening — the ~10-100 ms claim window would otherwise leak paddle
    /// events into QK4's own sidetone path. Ownership, not detection: a KPOD+ that is plugged in
    /// but switched off never raises this (USB-003).
    void kpodPlusOwnsCw(bool owns);

    // Device notice for the notification overlay: HaliKey connectionError and the KPOD / KPOD+
    // lifecycle messages. Prefix the emitted message with the device name (e.g. "HaliKey: <text>")
    // so the user can tell where it came from.
    void hardwareError(const QString &message);

    // A MIDI device was plugged in, removed or changed (macOS; never emitted elsewhere, where every
    // MIDI enumeration is already current). The HaliKey page rescans its list on it.
    void midiDevicesChanged();
    void halikeyConnectionChanged(bool connected);

private slots:
    void onKpodEncoderRotated(int ticks);
    void onKpodPollError(const QString &error);

private:
    void onKpodEncoderRotatedWithRocker(int ticks, int rockerPosition);

private:
    RadioState *m_radioState;
    ConnectionController *m_connectionController;

    // shutdownDevices() runs once. closeEvent calls it, and so does the destructor for the paths
    // that never reach closeEvent — a fatal error, or a window that was never shown.
    bool m_devicesShutDown = false;

    // KPOD USB tuning knob
    KpodDevice *m_kpodDevice;

    // KPOD+ USB keyer device (libusb, vendor-specific class)
    KpodPlusDevice *m_kpodPlusDevice;

    // HaliKey CW paddle device
    HalikeyDevice *m_halikeyDevice;

    // Keeps macOS's MIDI device list current (see mididevicewatcher.h). Created first, so it owns
    // the process's first CoreMIDI client.
    MidiDeviceWatcher *m_midiWatcher = nullptr;

    // The HaliKey keyer (port of K4-Companion's) and the seams it runs on. Declaration order is
    // construction order: the keyer refers to the clock, link and scheduler, so they come
    // first and outlive it. The keyer's thread runs PaddleKeyer::run() until close().
    std::unique_ptr<SteadyKeyerClock> m_keyerClock;
    std::unique_ptr<ForwardingLink> m_keyerLink;
    std::unique_ptr<SidetoneScheduler> m_sidetoneScheduler;
    std::unique_ptr<PaddleKeyer> m_paddleKeyer;
    QThread *m_keyerThread = nullptr;
    // The keyer thread missed its shutdown deadline: it and everything above are leaked, not
    // destroyed while it may still be using them.
    bool m_keyerDisowned = false;

    // Local sidetone generator (dedicated thread). Constructed + owned here;
    // CW playback wiring lives on CwController.
    SidetoneGenerator *m_sidetoneGenerator;
    QThread *m_sidetoneThread = nullptr;
    QMetaObject::Connection m_sidetoneDeleteConnection; // sidetone thread finished -> deleteLater
};

#endif // HARDWARECONTROLLER_H
