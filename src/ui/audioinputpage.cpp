#include "audioinputpage.h"
#include "k4styles.h"
#include "../audio/audioengine.h"
#include "../settings/radiosettings.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QMetaObject>

AudioInputPage::AudioInputPage(AudioEngine *audioEngine, QWidget *parent)
    : QWidget(parent), m_audioEngine(audioEngine) {
    setStyleSheet(K4Styles::Dialog::pageBackground());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin,
                               K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin);
    layout->setSpacing(K4Styles::Dimensions::PaddingLarge);

    // Title
    auto *titleLabel = new QLabel("Audio Input", this);
    titleLabel->setStyleSheet(K4Styles::Dialog::titleLabel());
    layout->addWidget(titleLabel);

    // Separator line
    auto *line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(K4Styles::Dialog::separator());
    line->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line);

    // === Microphone Device Selection ===
    auto *deviceLabel = new QLabel("Microphone:", this);
    deviceLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    layout->addWidget(deviceLabel);

    m_micDeviceCombo = new QComboBox(this);
    m_micDeviceCombo->setStyleSheet(K4Styles::Dialog::comboBox());
    populateMicDevices();
    connect(m_micDeviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &AudioInputPage::onMicDeviceChanged);
    layout->addWidget(m_micDeviceCombo);

    layout->addSpacing(K4Styles::Dimensions::PaddingMedium);

    // === Microphone Gain ===
    auto *gainLayout = new QHBoxLayout();
    auto *gainLabel = new QLabel("Mic Gain:", this);
    gainLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    gainLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);
    gainLayout->addWidget(gainLabel);

    m_micGainSlider = new QSlider(Qt::Horizontal, this);
    m_micGainSlider->setRange(0, 100);
    m_micGainSlider->setValue(RadioSettings::instance()->micGain());
    m_micGainSlider->setStyleSheet(
        K4Styles::sliderHorizontal(K4Styles::Colors::TextDark, K4Styles::Colors::AccentAmber));
    connect(m_micGainSlider, &QSlider::valueChanged, this, &AudioInputPage::onMicGainChanged);
    gainLayout->addWidget(m_micGainSlider, 1);

    m_micGainValueLabel = new QLabel(QString("%1%").arg(m_micGainSlider->value()), this);
    m_micGainValueLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                           .arg(K4Styles::Colors::TextWhite)
                                           .arg(K4Styles::Dimensions::FontSizePopup));
    m_micGainValueLabel->setFixedWidth(K4Styles::Dimensions::SliderValueLabelWidth);
    m_micGainValueLabel->setAlignment(Qt::AlignRight);
    gainLayout->addWidget(m_micGainValueLabel);

    layout->addLayout(gainLayout);

    auto *gainHelpLabel = new QLabel(
        "Adjust the microphone input level. Uses an exponential curve for fine control at low levels.", this);
    gainHelpLabel->setStyleSheet(K4Styles::Dialog::helpText());
    layout->addWidget(gainHelpLabel);

    layout->addStretch();
}

void AudioInputPage::refresh() {
    populateMicDevices();
}

void AudioInputPage::populateMicDevices() {
    if (!m_micDeviceCombo)
        return;

    m_micDeviceCombo->clear();

    auto devices = AudioEngine::availableInputDevices();
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

    if (m_audioEngine) {
        QMetaObject::invokeMethod(m_audioEngine, "setMicDevice", Qt::QueuedConnection, Q_ARG(QString, deviceId));
    }
}

void AudioInputPage::onMicGainChanged(int value) {
    if (m_micGainValueLabel) {
        m_micGainValueLabel->setText(QString("%1%").arg(value));
    }

    RadioSettings::instance()->setMicGain(value);

    if (m_audioEngine) {
        m_audioEngine->setMicGain(value / 100.0f);
    }
}
