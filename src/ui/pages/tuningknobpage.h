#ifndef TUNINGKNOBPAGE_H
#define TUNINGKNOBPAGE_H

#include <QCheckBox>
#include <QLabel>
#include <QWidget>

class Rc28Device;
class FlexControlDevice;
class QVBoxLayout;

/**
 * @brief OptionsDialog "Tuning Knobs" tab. Toggles support for the two
 *        third-party USB tuning knobs QK4 can drive alongside the K-Pod — the
 *        Icom RC-28 (HID) and the FlexRadio FlexControl (serial) — and shows
 *        probe/descriptor info for whichever is connected.
 *
 * Both devices lack the K-Pod's rocker switch, so the knob's tuning target
 * (VFO A / VFO B / RIT-XIT) is cycled with a device button (RC-28 F1 tap,
 * FlexControl AUX1 short-press). The help text explains this to the user.
 */
class TuningKnobPage : public QWidget {
    Q_OBJECT

public:
    explicit TuningKnobPage(Rc28Device *rc28Device, FlexControlDevice *flexControlDevice, QWidget *parent = nullptr);

    void refresh();

private:
    // Per-device summary widgets, grouped so the two sections stay symmetric.
    struct DeviceWidgets {
        QLabel *status = nullptr;
        QLabel *product = nullptr;
        QLabel *manufacturer = nullptr;
        QLabel *vendorId = nullptr;
        QLabel *productId = nullptr;
        QLabel *connection = nullptr; // HID path or serial port
        QLabel *firmware = nullptr;
        QCheckBox *enable = nullptr;
    };

    QVBoxLayout *buildDeviceSection(const QString &title, DeviceWidgets &w);
    void updateStatus();

    Rc28Device *m_rc28Device;
    FlexControlDevice *m_flexControlDevice;

    DeviceWidgets m_rc28;
    DeviceWidgets m_flex;
};

#endif // TUNINGKNOBPAGE_H
