#include "ui/pages/kpodpage.h"
#include "ui/styling/k4styles.h"
#include "hardware/kpoddevice.h"
#include "hardware/kpodplusdevice.h"
#include "settings/radiosettings.h"
#include <QVBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QStringList>
#include <QVector>

KpodPage::KpodPage(KpodDevice *kpodDevice, KpodPlusDevice *kpodPlusDevice, QWidget *parent)
    : QWidget(parent), m_kpodDevice(kpodDevice), m_kpodPlusDevice(kpodPlusDevice) {
    setStyleSheet(K4Styles::Dialog::pageBackground());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin,
                               K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin);
    layout->setSpacing(K4Styles::Dimensions::PaddingLarge);

    // Status indicator
    auto *statusLayout = new QHBoxLayout();
    auto *statusLabel = new QLabel("Status:", this);
    statusLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    statusLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_kpodStatusLabel = new QLabel("Not Detected", this);
    statusLayout->addWidget(statusLabel);
    statusLayout->addWidget(m_kpodStatusLabel);
    statusLayout->addStretch();
    layout->addLayout(statusLayout);

    // Separator line
    auto *line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(K4Styles::Dialog::separator());
    line->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line);

    // Device Summary title
    auto *titleLabel = new QLabel("Device Summary", this);
    titleLabel->setStyleSheet(K4Styles::Dialog::titleLabel());
    layout->addWidget(titleLabel);

    // Device info table using grid layout
    auto *tableWidget = new QWidget(this);
    auto *tableLayout = new QGridLayout(tableWidget);
    tableLayout->setContentsMargins(0, K4Styles::Dimensions::PaddingMedium, 0, K4Styles::Dimensions::PaddingMedium);
    tableLayout->setHorizontalSpacing(K4Styles::Dimensions::DialogMargin);
    tableLayout->setVerticalSpacing(K4Styles::Dimensions::PopupButtonSpacing);

    // Table styling
    QString headerStyle = QString("color: %1; font-size: %2px; font-weight: bold; padding: 5px;")
                              .arg(K4Styles::Colors::TextGray)
                              .arg(K4Styles::Dimensions::FontSizeButton);

    // Create labels with property names
    QStringList properties = {"Product Name", "Manufacturer",     "Vendor ID", "Product ID",
                              "Device Type",  "Firmware Version", "Device ID"};
    QVector<QLabel **> valueLabels = {&m_kpodProductLabel,   &m_kpodManufacturerLabel, &m_kpodVendorIdLabel,
                                      &m_kpodProductIdLabel, &m_kpodDeviceTypeLabel,   &m_kpodFirmwareLabel,
                                      &m_kpodDeviceIdLabel};

    for (int row = 0; row < properties.size(); ++row) {
        auto *propLabel = new QLabel(properties[row], tableWidget);
        propLabel->setStyleSheet(headerStyle);

        *valueLabels[row] = new QLabel("N/A", tableWidget);

        tableLayout->addWidget(propLabel, row, 0, Qt::AlignLeft);
        tableLayout->addWidget(*valueLabels[row], row, 1, Qt::AlignLeft);
    }

    tableLayout->setColumnStretch(1, 1);
    layout->addWidget(tableWidget);

    // Another separator
    auto *line2 = new QFrame(this);
    line2->setFrameShape(QFrame::HLine);
    line2->setStyleSheet(K4Styles::Dialog::separator());
    line2->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line2);

    // Enable checkbox
    m_kpodEnableCheckbox = new QCheckBox("Enable K-Pod", this);
    m_kpodEnableCheckbox->setChecked(RadioSettings::instance()->kpodEnabled());

    connect(m_kpodEnableCheckbox, &QCheckBox::toggled, this,
            [](bool checked) { RadioSettings::instance()->setKpodEnabled(checked); });

    // Follow the setting, not just write it. setChecked ran once at construction and nothing ever
    // re-synced it, so the page was only correct because it happened to be the single writer. A
    // second writer - anything at all - would have left the box showing a stale value with no
    // symptom until someone toggled it. setChecked does not re-emit toggled for an unchanged value,
    // so this cannot loop.
    connect(RadioSettings::instance(), &RadioSettings::kpodEnabledChanged, this, [this](bool enabled) {
        m_kpodEnableCheckbox->setChecked(enabled);
        updateKpodStatus();
    });

    layout->addWidget(m_kpodEnableCheckbox);

    // KPOD+ keyer configuration section (hidden until KPOD+ detected)
    setupKeyerConfigSection(layout);

    // Help text
    m_kpodHelpLabel = new QLabel("Connect a K-Pod device to enable this feature.", this);
    m_kpodHelpLabel->setStyleSheet(K4Styles::Dialog::helpText());
    m_kpodHelpLabel->setWordWrap(true);
    layout->addWidget(m_kpodHelpLabel);

    layout->addStretch();

    // Connect to device signals for real-time status updates
    if (m_kpodDevice) {
        connect(m_kpodDevice, &KpodDevice::deviceConnected, this, &KpodPage::updateKpodStatus);
        connect(m_kpodDevice, &KpodDevice::deviceDisconnected, this, &KpodPage::updateKpodStatus);
    }
    if (m_kpodPlusDevice) {
        connect(m_kpodPlusDevice, &KpodPlusDevice::deviceConnected, this, &KpodPage::updateKpodStatus);
        connect(m_kpodPlusDevice, &KpodPlusDevice::deviceDisconnected, this, &KpodPage::updateKpodStatus);
    }

    // Initialize with current status
    updateKpodStatus();
}

void KpodPage::setupKeyerConfigSection(QVBoxLayout *parentLayout) {
    m_keyerConfigWidget = new QWidget(this);
    auto *keyerLayout = new QVBoxLayout(m_keyerConfigWidget);
    keyerLayout->setContentsMargins(0, 0, 0, 0);
    keyerLayout->setSpacing(K4Styles::Dimensions::PaddingMedium);

    // Section title
    auto *keyerTitle = new QLabel("KPOD+ Configuration", m_keyerConfigWidget);
    keyerTitle->setStyleSheet(K4Styles::Dialog::titleLabel());
    keyerLayout->addWidget(keyerTitle);

    // Grid for the keyer controls
    auto *gridWidget = new QWidget(m_keyerConfigWidget);
    auto *grid = new QGridLayout(gridWidget);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(K4Styles::Dimensions::DialogMargin);
    grid->setVerticalSpacing(K4Styles::Dimensions::PopupButtonSpacing);

    QString labelStyle = QString("color: %1; font-size: %2px; font-weight: bold; padding: 5px;")
                             .arg(K4Styles::Colors::TextGray)
                             .arg(K4Styles::Dimensions::FontSizeButton);

    auto *settings = RadioSettings::instance();

    // Encode Mode: Element (KZ) / ASCII (KX). This is the only KPOD+ keyer
    // setting still configured here — keyer speed, CW pitch, iambic mode and
    // paddle orientation now mirror the connected K4 (see CwController) and
    // have no manual control. Encode mode has no K4 equivalent.
    auto *encodeLabel = new QLabel("Encode Mode", gridWidget);
    encodeLabel->setStyleSheet(labelStyle);
    m_encodeModeCombo = new QComboBox(gridWidget);
    m_encodeModeCombo->addItem("Element (KZ)", 0);
    m_encodeModeCombo->addItem("ASCII (KX)", 1);
    m_encodeModeCombo->setCurrentIndex(settings->kpodPlusEncodeMode());
    grid->addWidget(encodeLabel, 0, 0, Qt::AlignLeft);
    grid->addWidget(m_encodeModeCombo, 0, 1, Qt::AlignLeft);

    grid->setColumnStretch(1, 1);
    keyerLayout->addWidget(gridWidget);

    connect(m_encodeModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        RadioSettings::instance()->setKpodPlusEncodeMode(index);
        if (m_kpodPlusDevice && m_kpodPlusDevice->isPolling()) {
            m_kpodPlusDevice->setEncodeMode(index);
        }
    });

    // Separator before keyer section
    auto *line3 = new QFrame(this);
    line3->setFrameShape(QFrame::HLine);
    line3->setStyleSheet(K4Styles::Dialog::separator());
    line3->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    parentLayout->addWidget(line3);
    parentLayout->addWidget(m_keyerConfigWidget);

    // Initially hidden
    line3->setVisible(false);
    m_keyerConfigWidget->setVisible(false);
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

    // Styling
    QString valueStyle = QString("color: %1; font-size: %2px; padding: 5px;")
                             .arg(K4Styles::Colors::TextWhite)
                             .arg(K4Styles::Dimensions::FontSizeButton);
    QString notDetectedStyle = QString("color: %1; font-size: %2px; font-style: italic; padding: 5px;")
                                   .arg(K4Styles::Colors::TextGray)
                                   .arg(K4Styles::Dimensions::FontSizeButton);

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
    QString statusColor = anyDetected ? K4Styles::Colors::StatusGreen : K4Styles::Colors::ErrorRed;
    m_kpodStatusLabel->setText(statusText);
    m_kpodStatusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(statusColor));

    // Prefer KPOD+ info if both are detected, otherwise show KPOD
    auto setLabel = [&](QLabel *label, const QString &value) {
        QString displayValue = value.isEmpty() ? "N/A" : value;
        label->setText(displayValue);
        label->setStyleSheet(displayValue == "N/A" ? notDetectedStyle : valueStyle);
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

    // The checkbox is a PREFERENCE and stays settable whether or not a device is plugged in.
    //
    // It used to be greyed out on !anyDetected, which meant the only way to arrive with K-Pod
    // switched off was to plug a device in, uncheck it, and unplug again. The setting persists
    // across restarts but could only be CHANGED while the hardware was present - and if you wanted
    // it off because the device misbehaves, you had to attach the misbehaving device to say so.
    // The status row above already says whether anything is connected, so disabling the control
    // added nothing and removed capability.
    m_kpodEnableCheckbox->setStyleSheet(K4Styles::Dialog::checkBox());

    // Show/hide keyer configuration section based on KPOD+ detection
    if (m_keyerConfigWidget) {
        m_keyerConfigWidget->setVisible(kpodPlusDetected);
        // Also show the separator above it
        QWidget *sep = m_keyerConfigWidget->parentWidget();
        if (sep) {
            // Find the separator line just before keyer config
            int idx = static_cast<QVBoxLayout *>(layout())->indexOf(m_keyerConfigWidget);
            if (idx > 0) {
                QLayoutItem *item = layout()->itemAt(idx - 1);
                if (item && item->widget()) {
                    item->widget()->setVisible(kpodPlusDetected);
                }
            }
        }
    }

    // Help text. DETECTED, ENABLED and RUNNING are three different things, and this used to conflate
    // them: "KPOD+ keyer is active" was shown whenever a KPOD+ was merely plugged in, including
    // with the box unchecked and the device never opened. That is the same conflation as USB-003,
    // in the UI rather than the CW gate, and it is what told an operator the keyer had taken over
    // when it had not.
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
