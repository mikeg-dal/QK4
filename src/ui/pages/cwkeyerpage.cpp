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

#include <chrono>

namespace {
// A plugged-in HaliKey shows up within this long while the page is open.
constexpr std::chrono::milliseconds kRescanInterval{2000};
} // namespace

CwKeyerPage::CwKeyerPage(HalikeyDevice *halikeyDevice, RadioState *radioState, CwController *cwController,
                         QWidget *parent)
    : QWidget(parent), m_halikeyDevice(halikeyDevice), m_radioState(radioState), m_cwController(cwController) {
    namespace D = K4Styles::Dimensions;
    auto *layout = OptionsPageLayout::createPage(this);
    OptionsPageLayout::addTitle(layout, this, "HaliKey");

    m_cwKeyerDescLabel = OptionsPageLayout::addDescription(layout, this, QString());

    m_portScan = new HaliKeyPortScan(this);
    connect(m_portScan, &HaliKeyPortScan::finished, this, &CwKeyerPage::applyCwKeyerPorts);
    m_rescanTimer.setInterval(kRescanInterval);
    connect(&m_rescanTimer, &QTimer::timeout, this, &CwKeyerPage::rescanInBackground);

    auto *form = OptionsPageLayout::addForm(layout);

    m_cwKeyerDeviceTypeCombo = new QComboBox(this);
    m_cwKeyerDeviceTypeCombo->setStyleSheet(K4Styles::Dialog::comboBox());
    m_cwKeyerDeviceTypeCombo->addItem("HaliKey V1.4", HaliKeyPortScan::kSerialType);
    m_cwKeyerDeviceTypeCombo->addItem("HaliKey MIDI", HaliKeyPortScan::kMidiType);
    m_cwKeyerDeviceTypeCombo->setCurrentIndex(RadioSettings::instance()->halikeyDeviceType());
    updateCwKeyerDescription();
    connect(m_cwKeyerDeviceTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        int type = m_cwKeyerDeviceTypeCombo->itemData(index).toInt();
        RadioSettings::instance()->setHalikeyDeviceType(type);
        // Close even a port still opening: it was opened as the old device type.
        if (m_halikeyDevice) {
            m_halikeyDevice->closePort();
        }
        updateCwKeyerDescription();
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

    // Keyer section: the PaddleKeyer settings (port of K4-Companion's keyer).
    OptionsPageLayout::addSection(form, this, "Keyer");

    auto *modeCombo = new QComboBox(this);
    modeCombo->setStyleSheet(K4Styles::Dialog::comboBox());
    modeCombo->addItem("Follow K4 (Iambic A / B)", 0);
    modeCombo->addItem("Plain Iambic (no memory)", 1);
    modeCombo->addItem("Ultimatic", 2);
    modeCombo->setCurrentIndex(qBound(0, RadioSettings::instance()->halikeyKeyerMode(), 2));
    connect(modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [modeCombo](int index) {
        RadioSettings::instance()->setHalikeyKeyerMode(modeCombo->itemData(index).toInt());
    });
    OptionsPageLayout::addRow(form, this, "Keyer Mode", modeCombo);

    auto *inputCombo = new QComboBox(this);
    inputCombo->setStyleSheet(K4Styles::Dialog::comboBox());
    inputCombo->addItem("Paddles", false);
    inputCombo->addItem("Straight key / external keyer", true);
    inputCombo->setCurrentIndex(RadioSettings::instance()->halikeyStraightKey() ? 1 : 0);
    connect(inputCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [inputCombo](int index) {
        RadioSettings::instance()->setHalikeyStraightKey(inputCombo->itemData(index).toBool());
    });
    OptionsPageLayout::addRow(form, this, "Input", inputCombo);

    auto *ditMemSlider = new QSlider(Qt::Horizontal, this);
    ditMemSlider->setRange(0, 100);
    ditMemSlider->setValue(RadioSettings::instance()->halikeyDitMemoryDelay());
    ditMemSlider->setStyleSheet(
        K4Styles::sliderHorizontal(K4Styles::Colors::DarkBackground, K4Styles::Colors::AccentAmber));
    auto *ditMemValue = OptionsPageLayout::sliderValue(QString("%1%").arg(ditMemSlider->value()), this);
    // WHY applied on release, not on every value: each setting is a full save of every setting.
    // Keyboard and click steps arrive with the slider up and are applied at once.
    connect(ditMemSlider, &QSlider::valueChanged, this, [ditMemSlider, ditMemValue](int value) {
        ditMemValue->setText(QString("%1%").arg(value));
        if (!ditMemSlider->isSliderDown())
            RadioSettings::instance()->setHalikeyDitMemoryDelay(value);
    });
    connect(ditMemSlider, &QSlider::sliderReleased, this,
            [ditMemSlider]() { RadioSettings::instance()->setHalikeyDitMemoryDelay(ditMemSlider->value()); });
    OptionsPageLayout::addRow(form, this, "Dit Memory Delay",
                              OptionsPageLayout::sliderField(ditMemSlider, ditMemValue, this));

    auto *muteCheck = new QCheckBox("Mute the K4's CW monitor while keying", this);
    muteCheck->setStyleSheet(K4Styles::Dialog::checkBox());
    muteCheck->setChecked(RadioSettings::instance()->halikeyMuteRadioMonitor());
    connect(muteCheck, &QCheckBox::toggled, this,
            [](bool on) { RadioSettings::instance()->setHalikeyMuteRadioMonitor(on); });
    form->addRow(QString(), muteCheck);

    OptionsPageLayout::addFieldHelp(
        form, this,
        "Iambic A or B follows the K4's own keyer setting. Dit Memory Delay applies to Iambic B: a dit "
        "tapped in the first part of a dah is ignored rather than remembered, which stops unwanted extra "
        "dits from a quick squeeze. 33% is the tested default. Mute the K4's monitor only if you hear the "
        "radio's sidetone come back late through the receive audio.");

    OptionsPageLayout::addSection(form, this, "Sidetone");

    m_sidetoneVolumeSlider = new QSlider(Qt::Horizontal, this);
    m_sidetoneVolumeSlider->setRange(0, 100);
    m_sidetoneVolumeSlider->setValue(RadioSettings::instance()->sidetoneVolume());
    m_sidetoneVolumeSlider->setStyleSheet(
        K4Styles::sliderHorizontal(K4Styles::Colors::DarkBackground, K4Styles::Colors::AccentAmber));
    m_sidetoneVolumeValueLabel =
        OptionsPageLayout::sliderValue(QString("%1%").arg(RadioSettings::instance()->sidetoneVolume()), this);

    // Applied on release for the same reason as Dit Memory Delay above.
    connect(m_sidetoneVolumeSlider, &QSlider::valueChanged, this, [this](int value) {
        m_sidetoneVolumeValueLabel->setText(QString("%1%").arg(value));
        if (!m_sidetoneVolumeSlider->isSliderDown())
            RadioSettings::instance()->setSidetoneVolume(value);
    });
    connect(m_sidetoneVolumeSlider, &QSlider::sliderReleased, this,
            [this]() { RadioSettings::instance()->setSidetoneVolume(m_sidetoneVolumeSlider->value()); });

    OptionsPageLayout::addRow(form, this, "Volume",
                              OptionsPageLayout::sliderField(m_sidetoneVolumeSlider, m_sidetoneVolumeValueLabel, this));
    OptionsPageLayout::addFieldHelp(
        form, this, "Local sidetone volume for CW keying feedback. Frequency is linked to K4's CW pitch setting.");

    // Remote keying: the K4's key-down initial delay (KZL). Same control as K4-Companion's "Key Down
    // Initial Delay": 0-1000 ms in 10 ms steps, showing what the radio holds.
    OptionsPageLayout::addSection(form, this, "Remote Keying");

    m_keyDownDelaySlider = new QSlider(Qt::Horizontal, this);
    m_keyDownDelaySlider->setRange(0, 1000);
    m_keyDownDelaySlider->setSingleStep(10);
    m_keyDownDelaySlider->setPageStep(100);
    m_keyDownDelaySlider->setStyleSheet(
        K4Styles::sliderHorizontal(K4Styles::Colors::DarkBackground, K4Styles::Colors::AccentAmber));
    m_keyDownDelayValueLabel = OptionsPageLayout::sliderValue(QString(), this);

    // WHY send on release, not on every value: a drag would otherwise put dozens of KZL SETs on the
    // wire. Keyboard and click steps arrive with the slider up and are sent at once.
    connect(m_keyDownDelaySlider, &QSlider::valueChanged, this, [this](int value) {
        m_keyDownDelayValueLabel->setText(QString("%1 ms").arg(value));
        if (!m_keyDownDelaySlider->isSliderDown())
            sendKeyDownDelay();
    });
    connect(m_keyDownDelaySlider, &QSlider::sliderReleased, this, &CwKeyerPage::sendKeyDownDelay);

    OptionsPageLayout::addRow(form, this, "Key-Down Delay",
                              OptionsPageLayout::sliderField(m_keyDownDelaySlider, m_keyDownDelayValueLabel, this));
    OptionsPageLayout::addFieldHelp(form, this,
                                    "How long the K4 holds a remote key-down before it transmits, so keying sent over "
                                    "the network arrives in time. Raise it if CW breaks up over a slow or busy link; "
                                    "lower it for a quicker start on a fast LAN. Stored in the K4, whose default is "
                                    "80 ms. Applies to HaliKey and KPOD+ keying.");

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
    startScan(deviceType);
}

void CwKeyerPage::rescanInBackground() {
    if (!m_cwKeyerPortCombo || !m_cwKeyerDeviceTypeCombo || m_portScan->isScanning())
        return;
    const int deviceType = m_cwKeyerDeviceTypeCombo->currentData().toInt();
    if (deviceType != m_scanType)
        return; // a full scan for the new type is due; populateCwKeyerPorts() owns that
    startScan(deviceType);
}

void CwKeyerPage::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    m_rescanTimer.start();
}

void CwKeyerPage::hideEvent(QHideEvent *event) {
    m_rescanTimer.stop();
    QWidget::hideEvent(event);
}

void CwKeyerPage::startScan(int deviceType) {
    m_portScan->request(deviceType, [deviceType]() {
        if (deviceType == HaliKeyPortScan::kMidiType)
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

    QStringList shown;
    if (m_cwKeyerPortCombo->isEnabled()) {
        for (int i = 0; i < m_cwKeyerPortCombo->count(); ++i)
            shown.append(m_cwKeyerPortCombo->itemData(i).toString());
    }
    if (m_cwKeyerPortCombo->isEnabled() && shown == ports) {
        // Nothing came or went: leave the list, and an open drop-down, as they are.
        m_cwKeyerRefreshBtn->setEnabled(true);
        return;
    }

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
    if (type == 1) {
        m_cwKeyerDescLabel->setText("Connect a HaliKey MIDI paddle interface to send CW via the K4's keyer. "
                                    "The HaliKey MIDI uses standard MIDI note events to detect paddle and PTT inputs.");
    } else {
        m_cwKeyerDescLabel->setText("Connect a HaliKey paddle interface to send CW via the K4's keyer. "
                                    "The HaliKey uses serial port flow control signals to detect paddle inputs.");
    }
}

void CwKeyerPage::onCwKeyerRefreshClicked() {
    populateCwKeyerPorts();
}

void CwKeyerPage::onCwKeyerConnectClicked() {
    if (!m_halikeyDevice)
        return;

    if (m_halikeyDevice->isConnected()) {
        m_halikeyDevice->closePort();
    } else {
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
