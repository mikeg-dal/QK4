#include "ui/pages/audiooutputpage.h"
#include "ui/styling/k4styles.h"
#include "audio/audioengine.h"
#include "controllers/audiocontroller.h"
#include "settings/radiosettings.h"
#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>

AudioOutputPage::AudioOutputPage(AudioController *audioController, QWidget *parent)
    : QWidget(parent), m_audioController(audioController) {
    setStyleSheet(K4Styles::Dialog::pageBackground());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin,
                               K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin);
    layout->setSpacing(K4Styles::Dimensions::PaddingLarge);

    // Title
    auto *titleLabel = new QLabel("Audio Output", this);
    titleLabel->setStyleSheet(K4Styles::Dialog::titleLabel());
    layout->addWidget(titleLabel);

    // Separator line
    auto *line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(K4Styles::Dialog::separator());
    line->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line);

    // === Speaker Device Selection ===
    auto *deviceLabel = new QLabel("Speaker:", this);
    deviceLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    layout->addWidget(deviceLabel);

    m_speakerDeviceCombo = new QComboBox(this);
    m_speakerDeviceCombo->setStyleSheet(K4Styles::Dialog::comboBox());
    populateSpeakerDevices();
    connect(m_speakerDeviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &AudioOutputPage::onSpeakerDeviceChanged);
    layout->addWidget(m_speakerDeviceCombo);

    auto *deviceHelpLabel = new QLabel("Select the audio output device for radio receive audio. "
                                       "Volume is controlled by the MAIN and SUB sliders on the side panel.",
                                       this);
    deviceHelpLabel->setStyleSheet(K4Styles::Dialog::helpText());
    deviceHelpLabel->setWordWrap(true);
    layout->addWidget(deviceHelpLabel);

    layout->addSpacing(K4Styles::Dimensions::PaddingLarge);

    m_monoMixCheckBox = new QCheckBox("Force mono mix when Sub RX is on", this);
    m_monoMixCheckBox->setStyleSheet(K4Styles::Dialog::checkBox());
    m_monoMixCheckBox->setChecked(RadioSettings::instance()->monoMixEnabled());
    connect(m_monoMixCheckBox, &QCheckBox::toggled, this, &AudioOutputPage::onMonoMixChanged);
    layout->addWidget(m_monoMixCheckBox);

    auto *monoMixHelpLabel = new QLabel("Plays Main RX and Sub RX together instead of separating Main RX to the left and Sub RX to the right.",
                                 this);
    monoMixHelpLabel->setStyleSheet(K4Styles::Dialog::helpText());
    monoMixHelpLabel->setWordWrap(true);
    layout->addWidget(monoMixHelpLabel);

    layout->addStretch();
}

void AudioOutputPage::refresh() {
    populateSpeakerDevices();
}

void AudioOutputPage::populateSpeakerDevices() {
    if (!m_speakerDeviceCombo)
        return;

    m_speakerDeviceCombo->clear();

    auto devices = AudioEngine::availableOutputDevices();
    QString savedDevice = RadioSettings::instance()->speakerDevice();
    int selectedIndex = 0;

    for (int i = 0; i < devices.size(); i++) {
        const auto &device = devices[i];
        m_speakerDeviceCombo->addItem(device.second, device.first);

        // Find the saved device
        if (device.first == savedDevice) {
            selectedIndex = i;
        }
    }

    m_speakerDeviceCombo->setCurrentIndex(selectedIndex);
}

void AudioOutputPage::onSpeakerDeviceChanged(int index) {
    if (!m_speakerDeviceCombo || index < 0)
        return;

    QString deviceId = m_speakerDeviceCombo->currentData().toString();
    RadioSettings::instance()->setSpeakerDevice(deviceId);

    if (m_audioController) {
        m_audioController->setOutputDevice(deviceId);
    }
}

void AudioOutputPage::onMonoMixChanged(bool enabled) {
    RadioSettings::instance()->setMonoMixEnabled(enabled);

    if (m_audioController) {
        m_audioController->setMonoMixEnabled(enabled);
    }
}
