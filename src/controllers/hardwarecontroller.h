#ifndef HARDWARECONTROLLER_H
#define HARDWARECONTROLLER_H

#include <QObject>
#include <functional>

#include "hardware/usbdevicelifecycle.h"
#include <QThread>

class KpodDevice;
class KpodPlusDevice;
class HalikeyDevice;
class IambicKeyer;
class SidetoneGenerator;
class RadioState;
class ConnectionController;

/**
 * @brief Owns hardware-side workers: KPOD USB knob (main thread), HaliKey CW paddle
 *        (HalikeyDevice — holds its own worker thread), IambicKeyer (HighPriority keyer thread),
 *        SidetoneGenerator (sidetone thread). Translates KPOD button presses → macroRequested
 *        for MainWindow dispatch.
 *
 * CW-keying orchestration (IambicKeyer + HaliKey paddle wiring, the V1.4 PTT demux,
 * the KPOD+ keyer-active gate, sidetone playback) lives on CwController — see
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
    // KpodPlusPage, HaliKeyPage) need direct handles to construct their config
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

    // IambicKeyer + SidetoneGenerator accessors — consumed by CwController,
    // which is constructed right after HardwareController and wires the CW
    // signal graph across these (HardwareController-owned) devices. Same
    // Rule 2 documented-exception rationale as the device accessors above.
    IambicKeyer *iambicKeyer() const { return m_iambicKeyer; }
    SidetoneGenerator *sidetoneGenerator() const { return m_sidetoneGenerator; }

    /// Stop every input producer this controller owns — HaliKey, the iambic keyer, the sidetone,
    /// KPOD and KPOD+ — and join their threads. Idempotent; the destructor calls it too.
    ///
    /// WHY this is public rather than left to the destructor, which is where it used to live
    /// entirely: CONC-001. The HaliKey worker and the sidetone thread both reach
    /// CwController::kpodPlusActive(), which reads ConnectionController. Qt destroys children in
    /// CONSTRUCTION order, and ConnectionController is constructed before HardwareController, so it
    /// was already freed while these threads were still running and still dereferencing it.
    ///
    /// The order inside is unchanged and still load-bearing: HaliKey stops paddle events first,
    /// then the keyer (which produces KZ), then the sidetone (which consumes them). Producers
    /// before consumers. What changed is only that MainWindow::closeEvent can now run it while
    /// everything it touches is still alive.
    void shutdownDevices();

private:
    /// "HaliKey V1.4" or "HaliKey MIDI", matching the Options dropdown, so a notification and the
    /// settings page can never disagree about what the operator's device is called.
    QString halikeyName() const;

    /// Apply one lifecycle event to a device and carry out what the policy decides.
    ///
    /// This is the ONLY place either device is opened, closed, or reported to the operator. Before
    /// it, that was spread across two lambdas per device plus the worker's own presence timer, and
    /// the worker's copy did not know the "Enable K-Pod" setting existed — which is USB-002.
    void applyKpod(UsbDeviceLifecycle::Event event);
    void applyKpodPlus(UsbDeviceLifecycle::Event event);

    UsbDeviceLifecycle::State m_kpodState;
    UsbDeviceLifecycle::State m_kpodPlusState;

    // Pushes the K4's keyer settings to a freshly opened KPOD+. Held rather than captured in a
    // lambda so the policy can invoke it on whichever event opens the device.
    std::function<void()> m_applyKpodPlusConfig;

public:
signals:
    // KPOD button press → MainWindow dispatches macro
    void macroRequested(const QString &functionId);

    // Hardware error (port open failure, MIDI subsystem error, etc.) → MainWindow
    // shows it on the notification overlay. Currently fed by HalikeyDevice's
    // connectionError signal; future device errors can route here too. Prefix the
    // emitted message with the device name (e.g. "HaliKey: <text>") so the user
    // can tell where it came from.
    void hardwareError(const QString &message);

private slots:
    void onKpodEncoderRotated(int ticks);
    void onKpodPollError(const QString &error);

private:
    void onKpodEncoderRotatedWithRocker(int ticks, int rockerPosition);

private:
    RadioState *m_radioState;
    ConnectionController *m_connectionController;

    // KPOD USB tuning knob
    // shutdownDevices() runs once. closeEvent calls it, and so does the destructor for the paths
    // that never reach closeEvent — a fatal error, or a window that was never shown.
    bool m_devicesShutDown = false;

    KpodDevice *m_kpodDevice;

    // KPOD+ USB keyer device (libusb, vendor-specific class)
    KpodPlusDevice *m_kpodPlusDevice;

    // HaliKey CW paddle device
    HalikeyDevice *m_halikeyDevice;

    // Iambic keyer state machine (HighPriority thread). Constructed + owned
    // here; CW signal wiring lives on CwController.
    IambicKeyer *m_iambicKeyer;
    QThread *m_keyerThread = nullptr;

    // Local sidetone generator (dedicated thread). Constructed + owned here;
    // CW playback wiring lives on CwController.
    SidetoneGenerator *m_sidetoneGenerator;
    QThread *m_sidetoneThread = nullptr;
};

#endif // HARDWARECONTROLLER_H
