#ifndef KPODPAGE_H
#define KPODPAGE_H

#include <QWidget>
#include <QCheckBox>
#include <QLabel>
#include <QComboBox>

class QFormLayout;
class KpodDevice;
class KpodPlusDevice;

/**
 * @brief OptionsDialog "K-Pod" tab. Toggles KPOD USB tuning-knob integration and shows
 *        probe/descriptor info (vendor id, product id, firmware) from the connected device.
 *        When a KPOD+ is detected, also shows its Encode Mode.
 */
class KpodPage : public QWidget {
    Q_OBJECT

public:
    explicit KpodPage(KpodDevice *kpodDevice, KpodPlusDevice *kpodPlusDevice, QWidget *parent = nullptr);

    void refresh();

private:
    void updateKpodStatus();
    void setupKeyerConfigSection(QFormLayout *form);

    KpodDevice *m_kpodDevice;
    KpodPlusDevice *m_kpodPlusDevice;

    QCheckBox *m_kpodEnableCheckbox = nullptr;
    QLabel *m_kpodStatusLabel = nullptr;
    QLabel *m_kpodProductLabel = nullptr;
    QLabel *m_kpodManufacturerLabel = nullptr;
    QLabel *m_kpodVendorIdLabel = nullptr;
    QLabel *m_kpodProductIdLabel = nullptr;
    QLabel *m_kpodDeviceTypeLabel = nullptr;
    QLabel *m_kpodFirmwareLabel = nullptr;
    QLabel *m_kpodDeviceIdLabel = nullptr;
    QLabel *m_kpodHelpLabel = nullptr;

    // KPOD+ Encode Mode, the one KPOD+ setting with no K4 equivalent; the rest mirror the K4 (CwController).
    QFormLayout *m_form = nullptr;
    QLabel *m_keyerTitle = nullptr; // section header; its rows are shown only while a KPOD+ is detected
    QComboBox *m_encodeModeCombo = nullptr;
};

#endif // KPODPAGE_H
