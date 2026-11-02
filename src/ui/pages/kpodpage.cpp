#include "ui/pages/kpodpage.h"
#include "ui/styling/k4styles.h"
#include "hardware/kpoddevice.h"
#include "hardware/kpodplusdevice.h"
#include "settings/radiosettings.h"
#include "ui/pages/optionspagelayout.h"
#include <QFormLayout>
#include <QVBoxLayout>
#include <QStringList>
#include <QVector>

KpodPage::KpodPage(KpodDevice *kpodDevice, KpodPlusDevice *kpodPlusDevice, QWidget *parent)
    : QWidget(parent), m_kpodDevice(kpodDevice), m_kpodPlusDevice(kpodPlusDevice) {
    auto *layout = OptionsPageLayout::createPage(this);
    OptionsPageLayout::addTitle(layout, this, "K-Pod");
    m_form = OptionsPageLayout::addForm(layout);

    m_kpodStatusLabel = new QLabel("Not Detected", this);
    m_kpodStatusLabel->setStyleSheet(K4Styles::Dialog::statusLabelByState());
    OptionsPageLayout::addRow(m_form, this, "Status", m_kpodStatusLabel);

    m_kpodEnableCheckbox = new QCheckBox("Enable K-Pod", this);
    // The checkbox is a PREFERENCE and stays settable whether or not a device is plugged in.
    // Switching off a misbehaving device must not require plugging it in.
    m_kpodEnableCheckbox->setStyleSheet(K4Styles::Dialog::checkBox());
    m_kpodEnableCheckbox->setChecked(RadioSettings::instance()->kpodEnabled());
    connect(m_kpodEnableCheckbox, &QCheckBox::toggled, this,
            [](bool checked) { RadioSettings::instance()->setKpodEnabled(checked); });

    // Follow the setting so another writer cannot leave the box stale; setChecked does not re-emit
    // toggled for an unchanged value, so this cannot loop.
    connect(RadioSettings::instance(), &RadioSettings::kpodEnabledChanged, this, [this](bool enabled) {
        m_kpodEnableCheckbox->setChecked(enabled);
        updateKpodStatus();
    });

    m_form->addRow(QString(), m_kpodEnableCheckbox);

    m_kpodHelpLabel = OptionsPageLayout::addFieldHelp(m_form, this, "Connect a K-Pod device to enable this feature.");

    OptionsPageLayout::addSection(m_form, this, "Device Summary");
    const QStringList properties = {"Product Name", "Manufacturer",     "Vendor ID", "Product ID",
                                    "Device Type",  "Firmware Version", "Device ID"};
    const QVector<QLabel **> valueLabels = {&m_kpodProductLabel,   &m_kpodManufacturerLabel, &m_kpodVendorIdLabel,
                                            &m_kpodProductIdLabel, &m_kpodDeviceTypeLabel,   &m_kpodFirmwareLabel,
                                            &m_kpodDeviceIdLabel};
    for (int row = 0; row < properties.size(); ++row) {
        *valueLabels[row] = new QLabel("N/A", this);
        (*valueLabels[row])->setStyleSheet(K4Styles::Dialog::valueLabelByState());
        OptionsPageLayout::addRow(m_form, this, properties[row], *valueLabels[row]);
    }

    // KPOD+ keyer configuration section (hidden until KPOD+ detected)
    setupKeyerConfigSection(m_form);

    layout->addStretch();

    if (m_kpodDevice) {
        connect(m_kpodDevice, &KpodDevice::deviceConnected, this, &KpodPage::updateKpodStatus);
        connect(m_kpodDevice, &KpodDevice::deviceDisconnected, this, &KpodPage::updateKpodStatus);
    }
    if (m_kpodPlusDevice) {
        connect(m_kpodPlusDevice, &KpodPlusDevice::deviceConnected, this, &KpodPage::updateKpodStatus);
        connect(m_kpodPlusDevice, &KpodPlusDevice::deviceDisconnected, this, &KpodPage::updateKpodStatus);
    }

    updateKpodStatus();
}

void KpodPage::setupKeyerConfigSection(QFormLayout *form) {
    m_keyerTitle = OptionsPageLayout::addSection(form, this, "KPOD+ Configuration");

    // Encode Mode: Element (KZ) / ASCII (KX). It has no K4 equivalent; the other keyer settings
    // mirror the K4.
    m_encodeModeCombo = new QComboBox(this);
    m_encodeModeCombo->setStyleSheet(K4Styles::Dialog::comboBox());
    m_encodeModeCombo->addItem("Element (KZ)", 0);
    m_encodeModeCombo->addItem("ASCII (KX)", 1);
    m_encodeModeCombo->setCurrentIndex(RadioSettings::instance()->kpodPlusEncodeMode());
    OptionsPageLayout::addRow(form, this, "Encode Mode", m_encodeModeCombo);
    connect(m_encodeModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        RadioSettings::instance()->setKpodPlusEncodeMode(index);
        if (m_kpodPlusDevice && m_kpodPlusDevice->isPolling()) {
            m_kpodPlusDevice->setEncodeMode(index);
        }
    });

    form->setRowVisible(m_keyerTitle, false);
    form->setRowVisible(m_encodeModeCombo, false);
}

void KpodPage::refresh() {
    updateKpodStatus();
}

void KpodPage::updateKpodStatus() {
    if (!m_kpodStatusLabel)
        return;

    // Check both KPOD and KPOD+ — they can coexist
    bool kpodDetected = m_kpodDevice && m_kpodDevice->isDetected();
    bool kpodPlusDetected = m_kpodPlusDevice && m_kpodPlusDevice->isDetected();
    bool anyDetected = kpodDetected || kpodPlusDetected;

    // Update status label — show which device is detected
    QString statusText;
    if (kpodPlusDetected && kpodDetected) {
        statusText = "KPOD + KPOD+ Detected";
    } else if (kpodPlusDetected) {
        statusText = "KPOD+ Detected";
    } else if (kpodDetected) {
        statusText = "Detected";
    } else {
        statusText = "Not Detected";
    }
    m_kpodStatusLabel->setText(statusText);
    K4Styles::setStyleState(m_kpodStatusLabel, K4Styles::StateProperty,
                            anyDetected ? QString(K4Styles::Dialog::StatusOk) : QString());

    // Prefer KPOD+ info if both are detected, otherwise show KPOD
    auto setLabel = [&](QLabel *label, const QString &value) {
        QString displayValue = value.isEmpty() ? "N/A" : value;
        label->setText(displayValue);
        K4Styles::setStyleFlag(label, K4Styles::DimmedProperty, displayValue == "N/A");
    };

    if (kpodPlusDetected) {
        KpodPlusDeviceInfo info = m_kpodPlusDevice->deviceInfo();
        setLabel(m_kpodProductLabel, info.productName);
        setLabel(m_kpodManufacturerLabel, info.manufacturer);
        setLabel(m_kpodVendorIdLabel,
                 QString("%1 (0x%2)").arg(info.vendorId).arg(info.vendorId, 4, 16, QChar('0')).toUpper());
        setLabel(m_kpodProductIdLabel,
                 QString("%1 (0x%2)").arg(info.productId).arg(info.productId, 4, 16, QChar('0')).toUpper());
        setLabel(m_kpodDeviceTypeLabel, "USB Vendor-Specific (Keyer)");
        setLabel(m_kpodFirmwareLabel, info.firmwareVersion);
        setLabel(m_kpodDeviceIdLabel, info.deviceId);
    } else if (kpodDetected) {
        KpodDeviceInfo info = m_kpodDevice->deviceInfo();
        setLabel(m_kpodProductLabel, info.productName);
        setLabel(m_kpodManufacturerLabel, info.manufacturer);
        setLabel(m_kpodVendorIdLabel,
                 QString("%1 (0x%2)").arg(info.vendorId).arg(info.vendorId, 4, 16, QChar('0')).toUpper());
        setLabel(m_kpodProductIdLabel,
                 QString("%1 (0x%2)").arg(info.productId).arg(info.productId, 4, 16, QChar('0')).toUpper());
        setLabel(m_kpodDeviceTypeLabel, "USB HID (Human Interface Device)");
        setLabel(m_kpodFirmwareLabel, info.firmwareVersion);
        setLabel(m_kpodDeviceIdLabel, info.deviceId);
    } else {
        setLabel(m_kpodProductLabel, "");
        setLabel(m_kpodManufacturerLabel, "");
        setLabel(m_kpodVendorIdLabel, "");
        setLabel(m_kpodProductIdLabel, "");
        setLabel(m_kpodDeviceTypeLabel, "");
        setLabel(m_kpodFirmwareLabel, "");
        setLabel(m_kpodDeviceIdLabel, "");
    }

    if (m_form) {
        m_form->setRowVisible(m_keyerTitle, kpodPlusDetected);
        m_form->setRowVisible(m_encodeModeCombo, kpodPlusDetected);
    }

    // Help text: detected, enabled and running are distinct; never report the KPOD+ keyer active
    // unless it is enabled.
    const bool enabled = RadioSettings::instance()->kpodEnabled();
    if (!anyDetected) {
        m_kpodHelpLabel->setText(enabled ? "No K-Pod connected. It will be used as soon as one is plugged in."
                                         : "No K-Pod connected, and K-Pod support is switched off.");
    } else if (!enabled) {
        m_kpodHelpLabel->setText(kpodPlusDetected
                                     ? "KPOD+ connected but switched off. QK4's own keyer is handling CW."
                                     : "K-Pod connected but switched off. The knob and buttons do nothing.");
    } else if (kpodPlusDetected) {
        m_kpodHelpLabel->setText("KPOD+ keyer is active. Paddle, keyer, and sidetone are handled by the device.");
    } else {
        m_kpodHelpLabel->setText("K-Pod is active. The VFO knob and buttons control the radio.");
    }
}
