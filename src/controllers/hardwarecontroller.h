#ifndef HARDWARECONTROLLER_H
#define HARDWARECONTROLLER_H

#include <QObject>
#include <QThread>

class KpodDevice;
class KpodPlusDevice;
class Rc28Device;
class FlexControlDevice;
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
    Rc28Device *rc28Device() const { return m_rc28Device; }
    FlexControlDevice *flexControlDevice() const { return m_flexControlDevice; }
    HalikeyDevice *halikeyDevice() const { return m_halikeyDevice; }

    // IambicKeyer + SidetoneGenerator accessors — consumed by CwController,
    // which is constructed right after HardwareController and wires the CW
    // signal graph across these (HardwareController-owned) devices. Same
    // Rule 2 documented-exception rationale as the device accessors above.
    IambicKeyer *iambicKeyer() const { return m_iambicKeyer; }
    SidetoneGenerator *sidetoneGenerator() const { return m_sidetoneGenerator; }

    void shutdownSidetone();

signals:
    // KPOD button press → MainWindow dispatches macro
    void macroRequested(const QString &functionId);

    // Transient informational notice → MainWindow notification overlay. Used to
    // tell the user which VFO the RC-28 / FlexControl knob now tunes when its
    // mode button cycles the (software) tuning target — these devices have no
    // rocker switch, so there is no physical indication otherwise.
    void hardwareNotice(const QString &message);

    // Hardware error (port open failure, MIDI subsystem error, etc.) → MainWindow
    // shows it on the notification overlay. Currently fed by HalikeyDevice's
    // connectionError signal; future device errors can route here too. Prefix the
    // emitted message with the device name (e.g. "HaliKey: <text>") so the user
    // can tell where it came from.
    void hardwareError(const QString &message);

private slots:
    void onKpodEncoderRotated(int ticks);
    void onKpodPollError(const QString &error);
    void onKpodEnabledChanged(bool enabled);

private:
    void onKpodEncoderRotatedWithRocker(int ticks, int rockerPosition);

    // Advance a device's software tuning target (VFO A → VFO B → RIT/XIT →
    // VFO A). Substitutes for the K-POD rocker on devices that lack one.
    // Emits hardwareNotice; if `rc28` is non-null its indicator LEDs are set.
    void cycleTuningTarget(int &rocker, const QString &deviceName, Rc28Device *rc28);
    static QString tuningTargetLabel(int rocker);

private:
    RadioState *m_radioState;
    ConnectionController *m_connectionController;

    // KPOD USB tuning knob
    KpodDevice *m_kpodDevice;

    // KPOD+ USB keyer device (libusb, vendor-specific class)
    KpodPlusDevice *m_kpodPlusDevice;

    // Icom RC-28 USB tuning knob (HID). No rocker — m_rc28Rocker tracks the
    // software tuning target (2=VFO A, 0=VFO B, 1=RIT/XIT, matching the K-POD
    // rocker encoding consumed by onKpodEncoderRotatedWithRocker).
    Rc28Device *m_rc28Device = nullptr;
    int m_rc28Rocker = 2;

    // FlexRadio FlexControl USB tuning knob (serial). Same software-target model.
    FlexControlDevice *m_flexControlDevice = nullptr;
    int m_flexRocker = 2;

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
