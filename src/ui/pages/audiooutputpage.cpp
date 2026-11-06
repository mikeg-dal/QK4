#include "ui/pages/audiooutputpage.h"
#include "ui/styling/k4styles.h"
#include "audio/audioengine.h"
#include "audio/rxoutputstage.h"
#include "controllers/audiocontroller.h"
#include "settings/radiosettings.h"
#include "ui/pages/optionspagelayout.h"
#include <QFormLayout>
#include <QVBoxLayout>

AudioOutputPage::AudioOutputPage(AudioController *audioController, QWidget *parent)
    : QWidget(parent), m_audioController(audioController) {
    auto *layout = OptionsPageLayout::createPage(this);
    OptionsPageLayout::addTitle(layout, this, "Audio Output");
    auto *form = OptionsPageLayout::addForm(layout);

    m_speakerDeviceCombo = new QComboBox(this);
    m_speakerDeviceCombo->setStyleSheet(K4Styles::Dialog::comboBox());
    OptionsPageLayout::fitDeviceCombo(m_speakerDeviceCombo);
    populateSpeakerDevices();
    // WHY activated, not currentIndexChanged: repopulating the list changes the current index (and
    // falls back to System Default while a pinned device is absent). Only an operator's pick may
    // move the saved device or the stream.
    connect(m_speakerDeviceCombo, &QComboBox::activated, this, &AudioOutputPage::onSpeakerDeviceChanged);
    OptionsPageLayout::addRow(form, this, "Speaker", m_speakerDeviceCombo);
    OptionsPageLayout::addFieldHelp(form, this,
                                    "Select the audio output device for radio receive audio. Volume is controlled by "
                                    "the MAIN and SUB sliders on the side panel.");

    m_rxBoostCombo = new QComboBox(this);
    m_rxBoostCombo->setStyleSheet(K4Styles::Dialog::comboBox());
    for (int db = 0; db <= RxOutputStage::kMaxBoostDb; db += RxOutputStage::kBoostStepDb) {
        m_rxBoostCombo->addItem(db == 0 ? QStringLiteral("Off (0 dB)") : QStringLiteral("+%1 dB").arg(db), db);
    }
    m_rxBoostCombo->setCurrentIndex(m_rxBoostCombo->findData(RadioSettings::instance()->rxAudioBoostDb()));
    connect(m_rxBoostCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &AudioOutputPage::onRxBoostChanged);
    OptionsPageLayout::addRow(form, this, "RX Audio Boost", m_rxBoostCombo);
    OptionsPageLayout::addFieldHelp(form, this,
                                    "Raises receive audio above what the MAIN and SUB sliders reach at 100%.");

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

void AudioOutputPage::onRxBoostChanged(int index) {
    if (!m_rxBoostCombo || index < 0)
        return;
    const int db = m_rxBoostCombo->itemData(index).toInt();
    RadioSettings::instance()->setRxAudioBoostDb(db);
    if (m_audioController) {
        m_audioController->setRxBoostDb(db);
    }
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
