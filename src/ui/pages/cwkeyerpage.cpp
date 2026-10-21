#include "ui/pages/cwkeyerpage.h"
#include "ui/styling/k4styles.h"
#include "hardware/halikeydevice.h"
#include "settings/radiosettings.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>

CwKeyerPage::CwKeyerPage(HalikeyDevice *halikeyDevice, QWidget *parent)
    : QWidget(parent), m_halikeyDevice(halikeyDevice) {
    setStyleSheet(K4Styles::Dialog::pageBackground());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin,
                               K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin);
    layout->setSpacing(K4Styles::Dimensions::PaddingLarge);

    // Title
    auto *titleLabel = new QLabel("CW Keyer", this);
    titleLabel->setStyleSheet(K4Styles::Dialog::titleLabel());
    layout->addWidget(titleLabel);

    // Description (dynamic based on device type)
    m_cwKeyerDescLabel = new QLabel(this);
    m_cwKeyerDescLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                          .arg(K4Styles::Colors::TextGray)
                                          .arg(K4Styles::Dimensions::FontSizeButton));
    m_cwKeyerDescLabel->setWordWrap(true);
    layout->addWidget(m_cwKeyerDescLabel);

    // Device Type selector
    auto *deviceTypeLayout = new QHBoxLayout();
    auto *deviceTypeLabel = new QLabel("Device Type:", this);
    deviceTypeLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    deviceTypeLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_cwKeyerDeviceTypeCombo = new QComboBox(this);
    m_cwKeyerDeviceTypeCombo->setStyleSheet(K4Styles::Dialog::comboBox());
    m_cwKeyerDeviceTypeCombo->addItem("HaliKey V1.4", 0);
    m_cwKeyerDeviceTypeCombo->addItem("HaliKey MIDI", 1);
    m_cwKeyerDeviceTypeCombo->addItem("Vail Adapter (local keyer)", HalikeyDevice::Vail);

    int savedDeviceType = RadioSettings::instance()->halikeyDeviceType();
    m_cwKeyerDeviceTypeCombo->setCurrentIndex(savedDeviceType);
    updateCwKeyerDescription();

    connect(m_cwKeyerDeviceTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        int type = m_cwKeyerDeviceTypeCombo->itemData(index).toInt();
        if (m_halikeyDevice) {
            m_halikeyDevice->closePort();
        }
        RadioSettings::instance()->setHalikeyDeviceType(type);
        updateCwKeyerDescription();
        updateSidetoneControls();
        populateCwKeyerPorts();
    });

    deviceTypeLayout->addWidget(deviceTypeLabel);
    deviceTypeLayout->addWidget(m_cwKeyerDeviceTypeCombo, 1);
    layout->addLayout(deviceTypeLayout);

    // Separator line
    auto *line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(K4Styles::Dialog::separator());
    line->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line);

    // Status indicator
    auto *statusLayout = new QHBoxLayout();
    auto *statusTitleLabel = new QLabel("Status:", this);
    statusTitleLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    statusTitleLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_cwKeyerStatusLabel = new QLabel("Not Connected", this);
    m_cwKeyerStatusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::ErrorRed));

    statusLayout->addWidget(statusTitleLabel);
    statusLayout->addWidget(m_cwKeyerStatusLabel);
    statusLayout->addStretch();
    layout->addLayout(statusLayout);

    // Separator line
    auto *line2 = new QFrame(this);
    line2->setFrameShape(QFrame::HLine);
    line2->setStyleSheet(K4Styles::Dialog::separator());
    line2->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line2);

    // Connection Settings section
    auto *sectionLabel = new QLabel("Connection Settings", this);
    sectionLabel->setStyleSheet(K4Styles::Dialog::sectionHeader());
    layout->addWidget(sectionLabel);

    // Port selection
    auto *portLayout = new QHBoxLayout();
    auto *portLabel = new QLabel("Port:", this);
    portLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    portLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_cwKeyerPortCombo = new QComboBox(this);
    m_cwKeyerPortCombo->setStyleSheet(K4Styles::Dialog::comboBox());
    populateCwKeyerPorts();

    m_cwKeyerRefreshBtn = new QPushButton("Refresh", this);
    m_cwKeyerRefreshBtn->setStyleSheet(K4Styles::Dialog::actionButtonSmall());
    connect(m_cwKeyerRefreshBtn, &QPushButton::clicked, this, &CwKeyerPage::onCwKeyerRefreshClicked);

    portLayout->addWidget(portLabel);
    portLayout->addWidget(m_cwKeyerPortCombo, 1);
    portLayout->addWidget(m_cwKeyerRefreshBtn);
    layout->addLayout(portLayout);

    // Connect/Disconnect button
    m_cwKeyerConnectBtn = new QPushButton("Connect", this);
    m_cwKeyerConnectBtn->setStyleSheet(K4Styles::Dialog::actionButton());
    connect(m_cwKeyerConnectBtn, &QPushButton::clicked, this, &CwKeyerPage::onCwKeyerConnectClicked);
    layout->addWidget(m_cwKeyerConnectBtn);

    // Separator line
    auto *line3 = new QFrame(this);
    line3->setFrameShape(QFrame::HLine);
    line3->setStyleSheet(K4Styles::Dialog::separator());
    line3->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line3);

    // Sidetone Settings section
    auto *sidetoneLabel = new QLabel("Sidetone Settings", this);
    sidetoneLabel->setStyleSheet(K4Styles::Dialog::sectionHeader());
    layout->addWidget(sidetoneLabel);

    m_sidetoneSourceRow = new QWidget(this);
    auto *sourceLayout = new QHBoxLayout(m_sidetoneSourceRow);
    sourceLayout->setContentsMargins(0, 0, 0, 0);
    auto *sourceLabel = new QLabel("Output:", m_sidetoneSourceRow);
    sourceLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    sourceLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);
    m_sidetoneSourceCombo = new QComboBox(m_sidetoneSourceRow);
    m_sidetoneSourceCombo->setObjectName(QStringLiteral("vailSidetoneSource"));
    m_sidetoneSourceCombo->setStyleSheet(K4Styles::Dialog::comboBox());
    m_sidetoneSourceCombo->addItem("Vail buzzer (computer muted)", false);
    m_sidetoneSourceCombo->addItem("Computer audio (mute Vail manually)", true);
    m_sidetoneSourceCombo->setCurrentIndex(RadioSettings::instance()->vailComputerSidetone() ? 1 : 0);
    connect(m_sidetoneSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        RadioSettings::instance()->setVailComputerSidetone(m_sidetoneSourceCombo->currentData().toBool());
        updateSidetoneControls();
    });
    sourceLayout->addWidget(sourceLabel);
    sourceLayout->addWidget(m_sidetoneSourceCombo, 1);
    layout->addWidget(m_sidetoneSourceRow);

    m_sidetoneVolumeRow = new QWidget(this);
    auto *volumeLayout = new QHBoxLayout(m_sidetoneVolumeRow);
    volumeLayout->setContentsMargins(0, 0, 0, 0);
    auto *volumeLabel = new QLabel("Volume:", m_sidetoneVolumeRow);
    volumeLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    volumeLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_sidetoneVolumeSlider = new QSlider(Qt::Horizontal, this);
    m_sidetoneVolumeSlider->setRange(0, 100);
    m_sidetoneVolumeSlider->setValue(RadioSettings::instance()->sidetoneVolume());
    m_sidetoneVolumeSlider->setStyleSheet(
        K4Styles::sliderHorizontal(K4Styles::Colors::DarkBackground, K4Styles::Colors::AccentAmber));

    m_sidetoneVolumeValueLabel = new QLabel(QString("%1%").arg(RadioSettings::instance()->sidetoneVolume()), this);
    m_sidetoneVolumeValueLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                                  .arg(K4Styles::Colors::TextWhite)
                                                  .arg(K4Styles::Dimensions::FontSizePopup));
    m_sidetoneVolumeValueLabel->setFixedWidth(K4Styles::Dimensions::SliderValueLabelWidth);

    connect(m_sidetoneVolumeSlider, &QSlider::valueChanged, this, [this](int value) {
        m_sidetoneVolumeValueLabel->setText(QString("%1%").arg(value));
        RadioSettings::instance()->setSidetoneVolume(value);
    });

    volumeLayout->addWidget(volumeLabel);
    volumeLayout->addWidget(m_sidetoneVolumeSlider, 1);
    volumeLayout->addWidget(m_sidetoneVolumeValueLabel);
    layout->addWidget(m_sidetoneVolumeRow);

    m_sidetoneHelpLabel = new QLabel(this);
    m_sidetoneHelpLabel->setObjectName(QStringLiteral("sidetoneHelp"));
    m_sidetoneHelpLabel->setStyleSheet(K4Styles::Dialog::helpText());
    m_sidetoneHelpLabel->setWordWrap(true);
    layout->addWidget(m_sidetoneHelpLabel);
    updateSidetoneControls();

    layout->addStretch();

    // Connect to HalikeyDevice signals for status updates
    if (m_halikeyDevice) {
        connect(m_halikeyDevice, &HalikeyDevice::connected, this, &CwKeyerPage::updateCwKeyerStatus);
        connect(m_halikeyDevice, &HalikeyDevice::disconnected, this, &CwKeyerPage::updateCwKeyerStatus);
    }

    // Initialize status
    updateCwKeyerStatus();
}

void CwKeyerPage::refresh() {
    populateCwKeyerPorts();
    updateCwKeyerStatus();
}

void CwKeyerPage::populateCwKeyerPorts() {
    if (!m_cwKeyerPortCombo)
        return;

    // Block signals to avoid triggering save during repopulation
    m_cwKeyerPortCombo->blockSignals(true);
    m_cwKeyerPortCombo->clear();

    int deviceType = m_cwKeyerDeviceTypeCombo ? m_cwKeyerDeviceTypeCombo->currentData().toInt() : 0;
    QString savedPort = RadioSettings::instance()->halikeyPortName();
    int selectedIndex = -1;

    if (deviceType == HalikeyDevice::Midi || deviceType == HalikeyDevice::Vail) {
        // MIDI device -- enumerate MIDI ports
        QStringList midiDevices = HalikeyDevice::availableMidiDevices();
        for (int i = 0; i < midiDevices.size(); i++) {
            m_cwKeyerPortCombo->addItem(midiDevices[i], midiDevices[i]);
            if (midiDevices[i] == savedPort) {
                selectedIndex = i;
            }
        }
        // Auto-select first HaliKey MIDI device if no saved selection matched
        if (selectedIndex < 0 && deviceType == HalikeyDevice::Midi) {
            for (int i = 0; i < midiDevices.size(); i++) {
                if (midiDevices[i].contains("HaliKey", Qt::CaseInsensitive)) {
                    selectedIndex = i;
                    break;
                }
            }
        }
    } else {
        // V14 device -- enumerate serial ports
        auto ports = HalikeyDevice::availablePortsDetailed();
        for (int i = 0; i < ports.size(); i++) {
            m_cwKeyerPortCombo->addItem(ports[i].portName, ports[i].portName);
            if (ports[i].portName == savedPort) {
                selectedIndex = i;
            }
        }
    }

    if (selectedIndex >= 0) {
        m_cwKeyerPortCombo->setCurrentIndex(selectedIndex);
    }
    m_cwKeyerPortCombo->blockSignals(false);

    // Save selection when changed (reconnect since we may be called multiple times)
    m_cwKeyerPortCombo->disconnect(this);
    connect(m_cwKeyerPortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index >= 0) {
            QString portName = m_cwKeyerPortCombo->itemData(index).toString();
            RadioSettings::instance()->setHalikeyPortName(portName);
        }
    });
}

void CwKeyerPage::updateCwKeyerDescription() {
    if (!m_cwKeyerDescLabel || !m_cwKeyerDeviceTypeCombo)
        return;

    int type = m_cwKeyerDeviceTypeCombo->currentData().toInt();
    if (type == HalikeyDevice::Vail) {
        m_cwKeyerDescLabel->setText(
            "The Vail adapter generates CW locally. "
            "QK4 follows the K4's speed, pitch and Iambic A/B mode. Select the adapter's MIDI "
            "port (it may use its board name). Keep radio mode off. "
            "Reverse paddles physically; the K4's reversal setting does not affect Vail. "
            "A continuous key-down over 2.4 seconds stops transmission until all inputs release.");
    } else if (type == 1) {
        m_cwKeyerDescLabel->setText("Connect a HaliKey MIDI paddle interface to send CW via the K4's keyer. "
                                    "The HaliKey MIDI uses standard MIDI note events to detect paddle and PTT inputs.");
    } else {
        m_cwKeyerDescLabel->setText("Connect a HaliKey paddle interface to send CW via the K4's keyer. "
                                    "The HaliKey uses serial port flow control signals to detect paddle inputs.");
    }
}

void CwKeyerPage::updateSidetoneControls() {
    const bool vail = m_cwKeyerDeviceTypeCombo->currentData().toInt() == HalikeyDevice::Vail;
    const bool computer = !vail || RadioSettings::instance()->vailComputerSidetone();
    m_sidetoneSourceRow->setVisible(vail);
    m_sidetoneVolumeRow->setVisible(computer);
    if (!vail) {
        m_sidetoneHelpLabel->setText("Sound uses the speaker selected in Audio settings. 0% mutes computer "
                                     "sidetone. Pitch follows the K4's CW Pitch setting.");
    } else if (computer) {
        m_sidetoneHelpLabel->setText("Computer sidetone is on; 0% mutes it. Sound uses the speaker selected in Audio "
                                     "settings; pitch follows the K4's CW Pitch. USB/audio buffering adds some delay.\n"
                                     "QK4 cannot switch the Vail buzzer off over MIDI. To mute it: Disconnect above, "
                                     "keep USB plugged in, hold DIT for over 5 seconds until the confirmation tones, "
                                     "then release and Connect. Unplugging USB restores the buzzer.");
    } else {
        m_sidetoneHelpLabel->setText(
            "Computer sidetone is off. The Vail buzzer provides immediate sidetone at a fixed "
            "level; it has no volume control. Pitch follows the K4's CW Pitch setting.\n"
            "QK4 cannot switch the buzzer on over MIDI. If it is silent, unplug and reconnect "
            "USB, then Connect above. Set the K4's remote TX monitor to 0 to avoid a delayed echo.");
    }
}

void CwKeyerPage::onCwKeyerRefreshClicked() {
    populateCwKeyerPorts();
}

void CwKeyerPage::onCwKeyerConnectClicked() {
    if (!m_halikeyDevice)
        return;

    if (m_halikeyDevice->isConnected()) {
        // Disconnect
        m_halikeyDevice->closePort();
    } else {
        // Connect -- use port name from item data (without annotation suffix)
        QString portName = m_cwKeyerPortCombo->currentData().toString();
        if (!portName.isEmpty()) {
            RadioSettings::instance()->setHalikeyPortName(portName);
            m_halikeyDevice->openPort(portName);
        }
    }
}

void CwKeyerPage::updateCwKeyerStatus() {
    if (!m_cwKeyerStatusLabel || !m_cwKeyerConnectBtn)
        return;

    bool isConnected = m_halikeyDevice && m_halikeyDevice->isConnected();

    if (isConnected) {
        m_cwKeyerStatusLabel->setText(QString("Connected to %1").arg(m_halikeyDevice->portName()));
        m_cwKeyerStatusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::StatusGreen));
        m_cwKeyerConnectBtn->setText("Disconnect");
    } else {
        m_cwKeyerStatusLabel->setText("Not Connected");
        m_cwKeyerStatusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::ErrorRed));
        m_cwKeyerConnectBtn->setText("Connect");
    }
}
