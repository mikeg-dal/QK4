#include "ui/pages/audioinputpage.h"
#include "ui/styling/k4styles.h"
#include "audio/txaudiopipeline.h"
#include "controllers/audiocontroller.h"
#include "settings/radiosettings.h"
#include "ui/pages/optionspagelayout.h"
#include <QFormLayout>
#include <QVBoxLayout>

AudioInputPage::AudioInputPage(AudioController *audioController, QWidget *parent)
    : QWidget(parent), m_audioController(audioController) {
    auto *layout = OptionsPageLayout::createPage(this);
    OptionsPageLayout::addTitle(layout, this, "Audio Input");
    auto *form = OptionsPageLayout::addForm(layout);

    m_micDeviceCombo = new QComboBox(this);
    m_micDeviceCombo->setStyleSheet(K4Styles::Dialog::comboBox());
    OptionsPageLayout::fitDeviceCombo(m_micDeviceCombo);
    populateMicDevices();
    // WHY activated, not currentIndexChanged: repopulating the list changes the current index (and
    // falls back to System Default while a pinned device is absent). Only an operator's pick may
    // move the saved device or the stream.
    connect(m_micDeviceCombo, &QComboBox::activated, this, &AudioInputPage::onMicDeviceChanged);
    OptionsPageLayout::addRow(form, this, "Microphone", m_micDeviceCombo);

    m_micGainSlider = new QSlider(Qt::Horizontal, this);
    m_micGainSlider->setRange(0, 100);
    m_micGainSlider->setValue(RadioSettings::instance()->micGain());
    m_micGainSlider->setStyleSheet(
        K4Styles::sliderHorizontal(K4Styles::Colors::DialogBorder, K4Styles::Colors::AccentAmber));
    connect(m_micGainSlider, &QSlider::valueChanged, this, &AudioInputPage::onMicGainChanged);
    m_micGainValueLabel = OptionsPageLayout::sliderValue(QString("%1%").arg(m_micGainSlider->value()), this);
    OptionsPageLayout::addRow(form, this, "Mic Gain",
                              OptionsPageLayout::sliderField(m_micGainSlider, m_micGainValueLabel, this));

    OptionsPageLayout::addHelp(
        layout, this,
        "Adjust the microphone input level. Uses an exponential curve for fine control at low levels. "
        "This affects the microphone only — audio from a TCI client has its own TX Level on the TCI Server page.");

    layout->addStretch();
}

void AudioInputPage::refresh() {
    populateMicDevices();
}

void AudioInputPage::populateMicDevices() {
    if (!m_micDeviceCombo)
        return;

    m_micDeviceCombo->clear();

    auto devices = TxAudioPipeline::availableInputDevices();
    QString savedDevice = RadioSettings::instance()->micDevice();
    int selectedIndex = 0;

    for (int i = 0; i < devices.size(); i++) {
        const auto &device = devices[i];
        m_micDeviceCombo->addItem(device.second, device.first);

        // Find the saved device
        if (device.first == savedDevice) {
            selectedIndex = i;
        }
    }

    m_micDeviceCombo->setCurrentIndex(selectedIndex);
}

void AudioInputPage::onMicDeviceChanged(int index) {
    if (!m_micDeviceCombo || index < 0)
        return;

    QString deviceId = m_micDeviceCombo->currentData().toString();
    RadioSettings::instance()->setMicDevice(deviceId);

    if (m_audioController) {
        m_audioController->setMicDevice(deviceId);
    }
}

void AudioInputPage::onMicGainChanged(int value) {
    if (m_micGainValueLabel) {
        m_micGainValueLabel->setText(QString("%1%").arg(value));
    }

    RadioSettings::instance()->setMicGain(value);

    if (m_audioController) {
        m_audioController->setMicGain(value / 100.0f);
    }
}
