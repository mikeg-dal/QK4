#include "ui/pages/datamodepage.h"
#include "ui/styling/k4styles.h"
#include "audio/audioengine.h"
#include "settings/radiosettings.h"
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>

DataModePage::DataModePage(QWidget *parent) : QWidget(parent) {
    setStyleSheet(K4Styles::Dialog::pageBackground());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin,
                               K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin);
    layout->setSpacing(K4Styles::Dimensions::PaddingLarge);

    // Title
    auto *titleLabel = new QLabel("Data Mode Audio", this);
    titleLabel->setStyleSheet(K4Styles::Dialog::titleLabel());
    layout->addWidget(titleLabel);

    // Description
    auto *descLabel =
        new QLabel("Use a separate microphone and speaker when the TX VFO is in DATA or DATA-R mode. "
                   "When split is on, the TX VFO is the sub VFO; otherwise the main VFO. "
                   "If a device is left unset, the primary Audio Input/Output device is used as a fallback.",
                   this);
    descLabel->setStyleSheet(K4Styles::Dialog::helpText());
    descLabel->setWordWrap(true);
    layout->addWidget(descLabel);

    // Separator
    auto *line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(K4Styles::Dialog::separator());
    line->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line);

    // Enable checkbox
    m_enableCheckbox = new QCheckBox("Enable data mode audio routing", this);
    m_enableCheckbox->setStyleSheet(K4Styles::Dialog::checkBox());
    m_enableCheckbox->setChecked(RadioSettings::instance()->dataModeAudioEnabled());
    connect(m_enableCheckbox, &QCheckBox::toggled, this, &DataModePage::onEnableToggled);
    layout->addWidget(m_enableCheckbox);

    layout->addSpacing(K4Styles::Dimensions::PaddingMedium);

    // === Microphone Device Selection ===
    m_micDeviceLabel = new QLabel("Data Microphone:", this);
    m_micDeviceLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    layout->addWidget(m_micDeviceLabel);

    m_micDeviceCombo = new QComboBox(this);
    m_micDeviceCombo->setStyleSheet(K4Styles::Dialog::comboBox());
    populateMicDevices();
    connect(m_micDeviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &DataModePage::onMicDeviceChanged);
    layout->addWidget(m_micDeviceCombo);

    layout->addSpacing(K4Styles::Dimensions::PaddingMedium);

    // === Microphone Gain ===
    auto *gainLayout = new QHBoxLayout();
    m_micGainLabel = new QLabel("Data Mic Gain:", this);
    m_micGainLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    m_micGainLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);
    gainLayout->addWidget(m_micGainLabel);

    m_micGainSlider = new QSlider(Qt::Horizontal, this);
    m_micGainSlider->setRange(0, 100);
    m_micGainSlider->setValue(RadioSettings::instance()->dataMicGain());
    m_micGainSlider->setStyleSheet(
        K4Styles::sliderHorizontal(K4Styles::Colors::TextDark, K4Styles::Colors::AccentAmber));
    connect(m_micGainSlider, &QSlider::valueChanged, this, &DataModePage::onMicGainChanged);
    gainLayout->addWidget(m_micGainSlider, 1);

    m_micGainValueLabel = new QLabel(QString("%1%").arg(m_micGainSlider->value()), this);
    m_micGainValueLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                           .arg(K4Styles::Colors::TextWhite)
                                           .arg(K4Styles::Dimensions::FontSizePopup));
    m_micGainValueLabel->setFixedWidth(K4Styles::Dimensions::SliderValueLabelWidth);
    m_micGainValueLabel->setAlignment(Qt::AlignRight);
    gainLayout->addWidget(m_micGainValueLabel);

    layout->addLayout(gainLayout);

    layout->addSpacing(K4Styles::Dimensions::PaddingMedium);

    // === Speaker Device Selection ===
    m_speakerDeviceLabel = new QLabel("Data Speaker:", this);
    m_speakerDeviceLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    layout->addWidget(m_speakerDeviceLabel);

    m_speakerDeviceCombo = new QComboBox(this);
    m_speakerDeviceCombo->setStyleSheet(K4Styles::Dialog::comboBox());
    populateSpeakerDevices();
    connect(m_speakerDeviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &DataModePage::onSpeakerDeviceChanged);
    layout->addWidget(m_speakerDeviceCombo);

    updateControlsEnabled(m_enableCheckbox->isChecked());

    layout->addStretch();
}

void DataModePage::refresh() {
    populateMicDevices();
    populateSpeakerDevices();
}

void DataModePage::populateMicDevices() {
    if (!m_micDeviceCombo)
        return;

    QSignalBlocker blocker(m_micDeviceCombo);
    m_micDeviceCombo->clear();
    m_micDeviceCombo->addItem("(use primary microphone)", QString());

    auto devices = AudioEngine::availableInputDevices();
    QString savedDevice = RadioSettings::instance()->dataMicDevice();
    int selectedIndex = 0;

    for (int i = 0; i < devices.size(); i++) {
        const auto &device = devices[i];
        m_micDeviceCombo->addItem(device.second, device.first);
        if (device.first == savedDevice) {
            selectedIndex = i + 1; // +1 because index 0 is the "(use primary)" entry
        }
    }

    m_micDeviceCombo->setCurrentIndex(selectedIndex);
}

void DataModePage::populateSpeakerDevices() {
    if (!m_speakerDeviceCombo)
        return;

    QSignalBlocker blocker(m_speakerDeviceCombo);
    m_speakerDeviceCombo->clear();
    m_speakerDeviceCombo->addItem("(use primary speaker)", QString());

    auto devices = AudioEngine::availableOutputDevices();
    QString savedDevice = RadioSettings::instance()->dataSpeakerDevice();
    int selectedIndex = 0;

    for (int i = 0; i < devices.size(); i++) {
        const auto &device = devices[i];
        m_speakerDeviceCombo->addItem(device.second, device.first);
        if (device.first == savedDevice) {
            selectedIndex = i + 1;
        }
    }

    m_speakerDeviceCombo->setCurrentIndex(selectedIndex);
}

void DataModePage::onEnableToggled(bool checked) {
    RadioSettings::instance()->setDataModeAudioEnabled(checked);
    updateControlsEnabled(checked);
}

void DataModePage::onMicDeviceChanged(int index) {
    if (!m_micDeviceCombo || index < 0)
        return;
    QString deviceId = m_micDeviceCombo->currentData().toString();
    RadioSettings::instance()->setDataMicDevice(deviceId);
}

void DataModePage::onMicGainChanged(int value) {
    if (m_micGainValueLabel)
        m_micGainValueLabel->setText(QString("%1%").arg(value));
    RadioSettings::instance()->setDataMicGain(value);
}

void DataModePage::onSpeakerDeviceChanged(int index) {
    if (!m_speakerDeviceCombo || index < 0)
        return;
    QString deviceId = m_speakerDeviceCombo->currentData().toString();
    RadioSettings::instance()->setDataSpeakerDevice(deviceId);
}

void DataModePage::updateControlsEnabled(bool enabled) {
    if (m_micDeviceCombo)
        m_micDeviceCombo->setEnabled(enabled);
    if (m_micGainSlider)
        m_micGainSlider->setEnabled(enabled);
    if (m_micDeviceLabel)
        m_micDeviceLabel->setEnabled(enabled);
    if (m_micGainLabel)
        m_micGainLabel->setEnabled(enabled);
    if (m_micGainValueLabel)
        m_micGainValueLabel->setEnabled(enabled);
    if (m_speakerDeviceCombo)
        m_speakerDeviceCombo->setEnabled(enabled);
    if (m_speakerDeviceLabel)
        m_speakerDeviceLabel->setEnabled(enabled);
}
