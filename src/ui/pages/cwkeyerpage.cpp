#include "ui/pages/cwkeyerpage.h"
#include "ui/styling/k4styles.h"
#include "controllers/cwcontroller.h"
#include "hardware/halikeydevice.h"
#include "models/radiostate.h"
#include "settings/radiosettings.h"
#include "hardware/halikeyportscan.h"
#include "ui/pages/optionspagelayout.h"
#include <QCheckBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QVBoxLayout>

CwKeyerPage::CwKeyerPage(HalikeyDevice *halikeyDevice, RadioState *radioState, CwController *cwController,
                         QWidget *parent)
    : QWidget(parent), m_halikeyDevice(halikeyDevice), m_radioState(radioState), m_cwController(cwController) {
    namespace D = K4Styles::Dimensions;
    auto *layout = OptionsPageLayout::createPage(this);
    OptionsPageLayout::addTitle(layout, this, "CW Keyer");
    m_cwKeyerDescLabel = OptionsPageLayout::addDescription(layout, this, QString());
    m_portScan = new HaliKeyPortScan(this);
    connect(m_portScan, &HaliKeyPortScan::finished, this, &CwKeyerPage::applyCwKeyerPorts);

    auto *form = OptionsPageLayout::addForm(layout);
    m_cwKeyerDeviceTypeCombo = new QComboBox(this);
    m_cwKeyerDeviceTypeCombo->setStyleSheet(K4Styles::Dialog::comboBox());
    m_cwKeyerDeviceTypeCombo->addItem("HaliKey V1.4", HaliKeyPortScan::kSerialType);
    m_cwKeyerDeviceTypeCombo->addItem("HaliKey MIDI", HaliKeyPortScan::kMidiType);
    m_cwKeyerDeviceTypeCombo->addItem("Vail Adapter (local keyer)", HalikeyDevice::Vail);
    m_cwKeyerDeviceTypeCombo->setCurrentIndex(
        m_cwKeyerDeviceTypeCombo->findData(RadioSettings::instance()->halikeyDeviceType()));
    updateCwKeyerDescription();
    connect(m_cwKeyerDeviceTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_halikeyDevice)
            m_halikeyDevice->closePort();
        RadioSettings::instance()->setHalikeyDeviceType(m_cwKeyerDeviceTypeCombo->itemData(index).toInt());
        updateCwKeyerDescription();
        updateSidetoneControls();
        populateCwKeyerPorts();
    });
    OptionsPageLayout::addRow(form, this, "Device Type", m_cwKeyerDeviceTypeCombo);

    m_cwKeyerStatusLabel = new QLabel("Not Connected", this);
    m_cwKeyerStatusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::TextRed));
    OptionsPageLayout::addRow(form, this, "Status", m_cwKeyerStatusLabel);
    OptionsPageLayout::addSection(form, this, "Connection");
    m_cwKeyerPortCombo = new QComboBox(this);
    m_cwKeyerPortCombo->setStyleSheet(K4Styles::Dialog::comboBox());
    OptionsPageLayout::fitDeviceCombo(m_cwKeyerPortCombo);
    connect(m_cwKeyerPortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        const QString portName = index >= 0 ? m_cwKeyerPortCombo->itemData(index).toString() : QString();
        if (!portName.isEmpty())
            RadioSettings::instance()->setHalikeyPortName(portName);
    });
    m_cwKeyerRefreshBtn = new QPushButton("Refresh", this);
    m_cwKeyerRefreshBtn->setStyleSheet(K4Styles::Dialog::actionButtonSmall());
    connect(m_cwKeyerRefreshBtn, &QPushButton::clicked, this, &CwKeyerPage::onCwKeyerRefreshClicked);
    auto *portField = new QWidget(this);
    auto *portRow = new QHBoxLayout(portField);
    portRow->setContentsMargins(0, 0, 0, 0);
    portRow->setSpacing(D::OptionsRowSpacing);
    portRow->addWidget(m_cwKeyerPortCombo, 1);
    portRow->addWidget(m_cwKeyerRefreshBtn);
    OptionsPageLayout::capWidth(portField, D::OptionsFieldMaxWidth);
    OptionsPageLayout::addRow(form, this, "Port", portField);
    m_cwKeyerConnectBtn = new QPushButton("Connect", this);
    m_cwKeyerConnectBtn->setStyleSheet(K4Styles::Dialog::actionButtonSmall());
    m_cwKeyerConnectBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(m_cwKeyerConnectBtn, &QPushButton::clicked, this, &CwKeyerPage::onCwKeyerConnectClicked);
    form->addRow(QString(), m_cwKeyerConnectBtn);

    m_halikeyKeyerSettings = new QWidget(this);
    m_halikeyKeyerSettings->setObjectName(QStringLiteral("halikeyKeyerSettings"));
    auto *keyerLayout = new QVBoxLayout(m_halikeyKeyerSettings);
    keyerLayout->setContentsMargins(0, 0, 0, 0);
    keyerLayout->setSpacing(D::OptionsRowSpacing);
    layout->addWidget(m_halikeyKeyerSettings);
    auto *keyerForm = OptionsPageLayout::addForm(keyerLayout);
    OptionsPageLayout::addSection(keyerForm, m_halikeyKeyerSettings, "Keyer");
    auto *modeCombo = new QComboBox(m_halikeyKeyerSettings);
    modeCombo->setObjectName(QStringLiteral("halikeyKeyerMode"));
    modeCombo->setStyleSheet(K4Styles::Dialog::comboBox());
    modeCombo->addItem("Follow K4 (Iambic A / B)", 0);
    modeCombo->addItem("Plain Iambic (no memory)", 1);
    modeCombo->addItem("Ultimatic", 2);
    modeCombo->setCurrentIndex(qBound(0, RadioSettings::instance()->halikeyKeyerMode(), 2));
    connect(modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [modeCombo](int index) {
        RadioSettings::instance()->setHalikeyKeyerMode(modeCombo->itemData(index).toInt());
    });
    OptionsPageLayout::addRow(keyerForm, m_halikeyKeyerSettings, "Keyer Mode", modeCombo);
    auto *inputCombo = new QComboBox(m_halikeyKeyerSettings);
    inputCombo->setObjectName(QStringLiteral("halikeyInput"));
    inputCombo->setStyleSheet(K4Styles::Dialog::comboBox());
    inputCombo->addItem("Paddles", false);
    inputCombo->addItem("Straight key / external keyer", true);
    inputCombo->setCurrentIndex(RadioSettings::instance()->halikeyStraightKey() ? 1 : 0);
    connect(inputCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [inputCombo](int index) {
        RadioSettings::instance()->setHalikeyStraightKey(inputCombo->itemData(index).toBool());
    });
    OptionsPageLayout::addRow(keyerForm, m_halikeyKeyerSettings, "Input", inputCombo);
    auto *ditMemSlider = new QSlider(Qt::Horizontal, m_halikeyKeyerSettings);
    ditMemSlider->setObjectName(QStringLiteral("halikeyDitMemoryDelay"));
    ditMemSlider->setRange(0, 100);
    ditMemSlider->setValue(RadioSettings::instance()->halikeyDitMemoryDelay());
    ditMemSlider->setStyleSheet(
        K4Styles::sliderHorizontal(K4Styles::Colors::DarkBackground, K4Styles::Colors::AccentAmber));
    auto *ditMemValue =
        OptionsPageLayout::sliderValue(QString("%1%").arg(ditMemSlider->value()), m_halikeyKeyerSettings);
    connect(ditMemSlider, &QSlider::valueChanged, this, [ditMemSlider, ditMemValue](int value) {
        ditMemValue->setText(QString("%1%").arg(value));
        if (!ditMemSlider->isSliderDown())
            RadioSettings::instance()->setHalikeyDitMemoryDelay(value);
    });
    connect(ditMemSlider, &QSlider::sliderReleased, this,
            [ditMemSlider]() { RadioSettings::instance()->setHalikeyDitMemoryDelay(ditMemSlider->value()); });
    OptionsPageLayout::addRow(keyerForm, m_halikeyKeyerSettings, "Dit Memory Delay",
                              OptionsPageLayout::sliderField(ditMemSlider, ditMemValue, m_halikeyKeyerSettings));
    OptionsPageLayout::addFieldHelp(
        keyerForm, m_halikeyKeyerSettings,
        "Iambic A or B follows the K4's own keyer setting. Dit Memory Delay applies to Iambic B: a dit "
        "tapped in the first part of a dah is ignored rather than remembered, which stops unwanted extra "
        "dits from a quick squeeze. 33% is the tested default.");

    m_sidetoneForm = OptionsPageLayout::addForm(layout);
    OptionsPageLayout::addSection(m_sidetoneForm, this, "Sidetone");
    m_sidetoneSourceRow = new QWidget(this);
    auto *sourceLayout = new QHBoxLayout(m_sidetoneSourceRow);
    sourceLayout->setContentsMargins(0, 0, 0, 0);
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
    sourceLayout->addWidget(m_sidetoneSourceCombo);
    OptionsPageLayout::capWidth(m_sidetoneSourceRow, D::OptionsFieldMaxWidth);
    OptionsPageLayout::addRow(m_sidetoneForm, this, "Output", m_sidetoneSourceRow);
    m_sidetoneVolumeSlider = new QSlider(Qt::Horizontal, this);
    m_sidetoneVolumeSlider->setObjectName(QStringLiteral("computerSidetoneVolume"));
    m_sidetoneVolumeSlider->setRange(0, 100);
    m_sidetoneVolumeSlider->setValue(RadioSettings::instance()->sidetoneVolume());
    m_sidetoneVolumeSlider->setStyleSheet(
        K4Styles::sliderHorizontal(K4Styles::Colors::DarkBackground, K4Styles::Colors::AccentAmber));
    m_sidetoneVolumeValueLabel =
        OptionsPageLayout::sliderValue(QString("%1%").arg(m_sidetoneVolumeSlider->value()), this);
    connect(m_sidetoneVolumeSlider, &QSlider::valueChanged, this, [this](int value) {
        m_sidetoneVolumeValueLabel->setText(QString("%1%").arg(value));
        if (!m_sidetoneVolumeSlider->isSliderDown())
            RadioSettings::instance()->setSidetoneVolume(value);
    });
    connect(m_sidetoneVolumeSlider, &QSlider::sliderReleased, this,
            [this]() { RadioSettings::instance()->setSidetoneVolume(m_sidetoneVolumeSlider->value()); });
    m_sidetoneVolumeRow = OptionsPageLayout::sliderField(m_sidetoneVolumeSlider, m_sidetoneVolumeValueLabel, this);
    OptionsPageLayout::addRow(m_sidetoneForm, this, "Computer Volume", m_sidetoneVolumeRow);
    m_sidetoneHelpLabel = OptionsPageLayout::addFieldHelp(m_sidetoneForm, this, QString());
    m_sidetoneHelpLabel->setObjectName(QStringLiteral("sidetoneHelp"));
    updateSidetoneControls();
    auto *muteCheck = new QCheckBox("Mute the K4's CW monitor while keying", this);
    muteCheck->setObjectName(QStringLiteral("halikeyMuteRadioMonitor"));
    muteCheck->setStyleSheet(K4Styles::Dialog::checkBox());
    muteCheck->setChecked(RadioSettings::instance()->halikeyMuteRadioMonitor());
    connect(muteCheck, &QCheckBox::toggled, this,
            [](bool on) { RadioSettings::instance()->setHalikeyMuteRadioMonitor(on); });
    m_sidetoneForm->addRow(QString(), muteCheck);
    auto *monitorHelp = OptionsPageLayout::addFieldHelp(
        m_sidetoneForm, this,
        "Enable this if the K4's CW monitor comes back late through receive audio. "
        "It mutes the radio monitor while keying; the Vail buzzer and Computer Volume are separate.");
    monitorHelp->setObjectName(QStringLiteral("remoteCwMonitorHelp"));

    OptionsPageLayout::addSection(m_sidetoneForm, this, "Remote Keying");
    m_keyDownDelaySlider = new QSlider(Qt::Horizontal, this);
    m_keyDownDelaySlider->setObjectName(QStringLiteral("keyDownInitialDelay"));
    m_keyDownDelaySlider->setRange(0, 1000);
    m_keyDownDelaySlider->setSingleStep(10);
    m_keyDownDelaySlider->setPageStep(100);
    m_keyDownDelaySlider->setStyleSheet(
        K4Styles::sliderHorizontal(K4Styles::Colors::DarkBackground, K4Styles::Colors::AccentAmber));
    m_keyDownDelayValueLabel = OptionsPageLayout::sliderValue(QString(), this);
    connect(m_keyDownDelaySlider, &QSlider::valueChanged, this, [this](int value) {
        m_keyDownDelayValueLabel->setText(QString("%1 ms").arg(value));
        if (!m_keyDownDelaySlider->isSliderDown())
            sendKeyDownDelay();
    });
    connect(m_keyDownDelaySlider, &QSlider::sliderReleased, this, &CwKeyerPage::sendKeyDownDelay);
    OptionsPageLayout::addRow(m_sidetoneForm, this, "Key-Down Delay",
                              OptionsPageLayout::sliderField(m_keyDownDelaySlider, m_keyDownDelayValueLabel, this));
    OptionsPageLayout::addFieldHelp(m_sidetoneForm, this,
                                    "How long the K4 holds a remote key-down before it transmits, so keying sent over "
                                    "the network arrives in time. Raise it if CW breaks up over a slow or busy link; "
                                    "lower it for a quicker start on a fast LAN. Stored in the K4, whose default is "
                                    "80 ms. Applies to HaliKey, Vail and KPOD+ keying.");
    if (m_radioState)
        connect(m_radioState, &RadioState::keyDownInitialDelayChanged, this, &CwKeyerPage::showKeyDownDelay);
    showKeyDownDelay(m_radioState ? m_radioState->keyDownInitialDelayMs() : -1);
    layout->addStretch();
    if (m_halikeyDevice) {
        connect(m_halikeyDevice, &HalikeyDevice::connected, this, &CwKeyerPage::updateCwKeyerStatus);
        connect(m_halikeyDevice, &HalikeyDevice::disconnected, this, &CwKeyerPage::updateCwKeyerStatus);
    }
    populateCwKeyerPorts();
    updateCwKeyerStatus();
}

void CwKeyerPage::refresh() {
    populateCwKeyerPorts();
    updateCwKeyerStatus();
    showKeyDownDelay(m_radioState ? m_radioState->keyDownInitialDelayMs() : -1);
}

void CwKeyerPage::showKeyDownDelay(int ms) {
    const bool known = ms >= 0;
    m_keyDownDelaySlider->setEnabled(known);
    if (known) {
        const QSignalBlocker block(m_keyDownDelaySlider);
        m_keyDownDelaySlider->setValue(ms);
    }
    m_keyDownDelayValueLabel->setText(known ? QString("%1 ms").arg(ms) : QStringLiteral("--"));
}

void CwKeyerPage::sendKeyDownDelay() {
    if (!m_cwController || !m_radioState)
        return;
    // Snap to the K4's 10 ms steps.
    const int ms = (m_keyDownDelaySlider->value() + 5) / 10 * 10;
    if (ms != m_radioState->keyDownInitialDelayMs())
        m_cwController->setKeyDownInitialDelay(ms);
}

void CwKeyerPage::populateCwKeyerPorts() {
    if (!m_cwKeyerPortCombo)
        return;

    const int deviceType = m_cwKeyerDeviceTypeCombo ? m_cwKeyerDeviceTypeCombo->currentData().toInt() : 0;
    if (m_cwKeyerPortCombo->count() == 0 || m_scanType != deviceType) {
        const QSignalBlocker block(m_cwKeyerPortCombo);
        m_cwKeyerPortCombo->clear();
        m_cwKeyerPortCombo->addItem(QStringLiteral("Scanning\u2026"));
        m_cwKeyerPortCombo->setEnabled(false);
    }
    m_scanType = deviceType;
    m_cwKeyerRefreshBtn->setEnabled(false);

    m_portScan->request(deviceType, [deviceType]() {
        if (deviceType == HaliKeyPortScan::kMidiType || deviceType == HalikeyDevice::Vail)
            return HalikeyDevice::availableMidiDevices();
        QStringList names;
        const auto ports = HalikeyDevice::availablePortsDetailed();
        for (const auto &port : ports)
            names.append(port.portName);
        return names;
    });
}

void CwKeyerPage::applyCwKeyerPorts(int deviceType, const QStringList &ports) {
    if (deviceType != m_scanType)
        return;

    {
        // Blocked: refilling the list must not save a port the operator did not pick.
        const QSignalBlocker block(m_cwKeyerPortCombo);
        m_cwKeyerPortCombo->clear();
        for (const QString &port : ports)
            m_cwKeyerPortCombo->addItem(port, port);
        const int selected =
            HaliKeyPortScan::chooseIndex(ports, RadioSettings::instance()->halikeyPortName(), deviceType);
        if (selected >= 0)
            m_cwKeyerPortCombo->setCurrentIndex(selected);
    }
    m_cwKeyerPortCombo->setEnabled(true);
    m_cwKeyerRefreshBtn->setEnabled(true);
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
    m_halikeyKeyerSettings->setVisible(!vail);
    m_sidetoneForm->setRowVisible(m_sidetoneSourceRow, vail);
    m_sidetoneForm->setRowVisible(m_sidetoneVolumeRow, computer);
    if (!vail) {
        m_sidetoneHelpLabel->setText("Sound uses the speaker selected in Audio Output settings. 0% mutes computer "
                                     "sidetone. Pitch follows the K4's CW Pitch setting.");
    } else if (computer) {
        m_sidetoneHelpLabel->setText(
            "Computer sidetone is on; 0% mutes it. Sound uses the speaker selected in Audio "
            "Output settings; pitch follows the K4's CW Pitch. USB/audio buffering adds some delay.\n"
            "QK4 cannot switch the Vail buzzer off over MIDI. To mute it: Disconnect above, "
            "keep USB plugged in, hold DIT for over 5 seconds until the confirmation tones, "
            "then release and Connect. Unplugging USB restores the buzzer.");
    } else {
        m_sidetoneHelpLabel->setText("Computer sidetone is off. The Vail buzzer provides immediate sidetone at a fixed "
                                     "level; it has no volume control. Pitch follows the K4's CW Pitch setting.\n"
                                     "QK4 cannot switch the buzzer on over MIDI. If it is silent, unplug and reconnect "
                                     "USB, then Connect above.");
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
        m_cwKeyerStatusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::TextRed));
        m_cwKeyerConnectBtn->setText("Connect");
    }
}
