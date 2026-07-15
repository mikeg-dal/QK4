#include "ui/pages/tuningknobpage.h"
#include "hardware/flexcontroldevice.h"
#include "hardware/rc28device.h"
#include "settings/radiosettings.h"
#include "ui/styling/k4styles.h"
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QStringList>
#include <QVBoxLayout>
#include <QVector>

TuningKnobPage::TuningKnobPage(Rc28Device *rc28Device, FlexControlDevice *flexControlDevice, QWidget *parent)
    : QWidget(parent), m_rc28Device(rc28Device), m_flexControlDevice(flexControlDevice) {
    setStyleSheet(K4Styles::Dialog::pageBackground());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin,
                               K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin);
    layout->setSpacing(K4Styles::Dimensions::PaddingLarge);

    layout->addLayout(buildDeviceSection(QStringLiteral("Icom RC-28"), m_rc28));

    auto *line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(K4Styles::Dialog::separator());
    line->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line);

    layout->addLayout(buildDeviceSection(QStringLiteral("FlexRadio FlexControl"), m_flex));

    auto *help = new QLabel(QStringLiteral("These knobs have no rocker switch. Tap the RC-28 F1 button (or "
                                           "short-press FlexControl AUX1) to cycle what the knob tunes: "
                                           "VFO A → VFO B → RIT/XIT. Remaining buttons are assignable "
                                           "as macros in the Macros dialog."),
                            this);
    help->setStyleSheet(K4Styles::Dialog::helpText());
    help->setWordWrap(true);
    layout->addWidget(help);

    layout->addStretch();

    if (m_rc28Device) {
        connect(m_rc28Device, &Rc28Device::deviceConnected, this, &TuningKnobPage::updateStatus);
        connect(m_rc28Device, &Rc28Device::deviceDisconnected, this, &TuningKnobPage::updateStatus);
        connect(m_rc28Device, &Rc28Device::deviceInfoReady, this, &TuningKnobPage::updateStatus);
    }
    if (m_flexControlDevice) {
        connect(m_flexControlDevice, &FlexControlDevice::deviceConnected, this, &TuningKnobPage::updateStatus);
        connect(m_flexControlDevice, &FlexControlDevice::deviceDisconnected, this, &TuningKnobPage::updateStatus);
        connect(m_flexControlDevice, &FlexControlDevice::deviceInfoReady, this, &TuningKnobPage::updateStatus);
    }

    updateStatus();
}

QVBoxLayout *TuningKnobPage::buildDeviceSection(const QString &title, DeviceWidgets &w) {
    auto *section = new QVBoxLayout();
    section->setSpacing(K4Styles::Dimensions::PaddingMedium);

    auto *titleLabel = new QLabel(title, this);
    titleLabel->setStyleSheet(K4Styles::Dialog::titleLabel());
    section->addWidget(titleLabel);

    // Status row
    auto *statusRow = new QHBoxLayout();
    auto *statusCaption = new QLabel(QStringLiteral("Status:"), this);
    statusCaption->setStyleSheet(K4Styles::Dialog::formLabel());
    statusCaption->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);
    w.status = new QLabel(QStringLiteral("Not Detected"), this);
    statusRow->addWidget(statusCaption);
    statusRow->addWidget(w.status);
    statusRow->addStretch();
    section->addLayout(statusRow);

    // Device info grid
    auto *tableWidget = new QWidget(this);
    auto *grid = new QGridLayout(tableWidget);
    grid->setContentsMargins(0, K4Styles::Dimensions::PaddingSmall, 0, K4Styles::Dimensions::PaddingSmall);
    grid->setHorizontalSpacing(K4Styles::Dimensions::DialogMargin);
    grid->setVerticalSpacing(K4Styles::Dimensions::PopupButtonSpacing);

    const QString headerStyle = QString("color: %1; font-size: %2px; font-weight: bold; padding: 5px;")
                                    .arg(K4Styles::Colors::TextGray)
                                    .arg(K4Styles::Dimensions::FontSizeButton);

    const QStringList properties = {"Product Name", "Manufacturer", "Vendor ID",
                                    "Product ID",   "Connection",   "Firmware Version"};
    QVector<QLabel **> valueLabels = {&w.product, &w.manufacturer, &w.vendorId,
                                      &w.productId, &w.connection, &w.firmware};
    for (int row = 0; row < properties.size(); ++row) {
        auto *propLabel = new QLabel(properties[row], tableWidget);
        propLabel->setStyleSheet(headerStyle);
        *valueLabels[row] = new QLabel(QStringLiteral("N/A"), tableWidget);
        grid->addWidget(propLabel, row, 0, Qt::AlignLeft);
        grid->addWidget(*valueLabels[row], row, 1, Qt::AlignLeft);
    }
    grid->setColumnStretch(1, 1);
    section->addWidget(tableWidget);

    w.enable = new QCheckBox(QStringLiteral("Enable %1").arg(title), this);
    section->addWidget(w.enable);

    return section;
}

void TuningKnobPage::refresh() {
    updateStatus();
}

void TuningKnobPage::updateStatus() {
    const QString valueStyle = QString("color: %1; font-size: %2px; padding: 5px;")
                                   .arg(K4Styles::Colors::TextWhite)
                                   .arg(K4Styles::Dimensions::FontSizeButton);
    const QString naStyle = QString("color: %1; font-size: %2px; font-style: italic; padding: 5px;")
                                .arg(K4Styles::Colors::TextGray)
                                .arg(K4Styles::Dimensions::FontSizeButton);

    auto setLabel = [&](QLabel *label, const QString &value) {
        if (!label)
            return;
        const QString v = value.isEmpty() ? QStringLiteral("N/A") : value;
        label->setText(v);
        label->setStyleSheet(v == QStringLiteral("N/A") ? naStyle : valueStyle);
    };
    auto hexId = [](quint16 id) {
        return QString("%1 (0x%2)").arg(id).arg(id, 4, 16, QChar('0')).toUpper();
    };
    auto setStatus = [&](QLabel *label, bool detected) {
        label->setText(detected ? QStringLiteral("Detected") : QStringLiteral("Not Detected"));
        label->setStyleSheet(K4Styles::Dialog::statusLabel(detected ? K4Styles::Colors::StatusGreen
                                                                     : K4Styles::Colors::ErrorRed));
    };

    // --- RC-28 ---
    {
        const bool detected = m_rc28Device && m_rc28Device->isDetected();
        setStatus(m_rc28.status, detected);
        if (detected) {
            const Rc28DeviceInfo info = m_rc28Device->deviceInfo();
            setLabel(m_rc28.product, info.productName);
            setLabel(m_rc28.manufacturer, info.manufacturer);
            setLabel(m_rc28.vendorId, hexId(info.vendorId));
            setLabel(m_rc28.productId, hexId(info.productId));
            setLabel(m_rc28.connection, info.devicePath);
            setLabel(m_rc28.firmware, info.firmwareVersion);
        } else {
            for (QLabel *l : {m_rc28.product, m_rc28.manufacturer, m_rc28.vendorId, m_rc28.productId,
                              m_rc28.connection, m_rc28.firmware})
                setLabel(l, QString());
        }
        // Reflect current setting, then keep it in sync without recursing.
        m_rc28.enable->blockSignals(true);
        m_rc28.enable->setChecked(RadioSettings::instance()->rc28Enabled());
        m_rc28.enable->blockSignals(false);
        m_rc28.enable->setEnabled(detected);
        m_rc28.enable->setStyleSheet(detected ? K4Styles::Dialog::checkBox() : K4Styles::Dialog::checkBoxDisabled());
    }

    // --- FlexControl ---
    {
        const bool detected = m_flexControlDevice && m_flexControlDevice->isDetected();
        setStatus(m_flex.status, detected);
        if (detected) {
            const FlexControlDeviceInfo info = m_flexControlDevice->deviceInfo();
            setLabel(m_flex.product, info.productName);
            setLabel(m_flex.manufacturer, info.manufacturer);
            setLabel(m_flex.vendorId, hexId(info.vendorId));
            setLabel(m_flex.productId, hexId(info.productId));
            setLabel(m_flex.connection, info.portName);
            setLabel(m_flex.firmware, info.firmwareVersion);
        } else {
            for (QLabel *l : {m_flex.product, m_flex.manufacturer, m_flex.vendorId, m_flex.productId,
                              m_flex.connection, m_flex.firmware})
                setLabel(l, QString());
        }
        m_flex.enable->blockSignals(true);
        m_flex.enable->setChecked(RadioSettings::instance()->flexControlEnabled());
        m_flex.enable->blockSignals(false);
        m_flex.enable->setEnabled(detected);
        m_flex.enable->setStyleSheet(detected ? K4Styles::Dialog::checkBox() : K4Styles::Dialog::checkBoxDisabled());
    }

    // Wire the toggle handlers once (idempotent: disconnect then connect).
    m_rc28.enable->disconnect(this);
    connect(m_rc28.enable, &QCheckBox::toggled, this,
            [](bool checked) { RadioSettings::instance()->setRc28Enabled(checked); });
    m_flex.enable->disconnect(this);
    connect(m_flex.enable, &QCheckBox::toggled, this,
            [](bool checked) { RadioSettings::instance()->setFlexControlEnabled(checked); });
}
