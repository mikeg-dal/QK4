#include "ui/pages/kpa1500page.h"
#include "ui/styling/k4styles.h"
#include "network/kpa1500client.h"
#include "settings/radiosettings.h"
#include "ui/pages/optionspagelayout.h"
#include <QFormLayout>
#include <QVBoxLayout>
#include <QSlider>

Kpa1500Page::Kpa1500Page(KPA1500Client *kpa1500Client, QWidget *parent)
    : QWidget(parent), m_kpa1500Client(kpa1500Client) {
    auto *layout = OptionsPageLayout::createPage(this);
    OptionsPageLayout::addTitle(layout, this, "KPA1500");
    auto *form = OptionsPageLayout::addForm(layout);

    m_kpa1500StatusLabel = new QLabel("Not Connected", this);
    m_kpa1500StatusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::TextRed));
    OptionsPageLayout::addRow(form, this, "Status", m_kpa1500StatusLabel);

    OptionsPageLayout::addSection(form, this, "Connection Settings");

    m_kpa1500EnableCheckbox = new QCheckBox("Enable KPA1500", this);
    m_kpa1500EnableCheckbox->setStyleSheet(K4Styles::Dialog::checkBox());
    m_kpa1500EnableCheckbox->setChecked(RadioSettings::instance()->kpa1500Enabled());
    connect(m_kpa1500EnableCheckbox, &QCheckBox::toggled, this,
            [](bool checked) { RadioSettings::instance()->setKpa1500Enabled(checked); });
    form->addRow(QString(), m_kpa1500EnableCheckbox);

    m_kpa1500HostEdit = new QLineEdit(this);
    m_kpa1500HostEdit->setStyleSheet(K4Styles::Dialog::lineEdit());
    m_kpa1500HostEdit->setPlaceholderText("IP address or hostname");
    m_kpa1500HostEdit->setText(RadioSettings::instance()->kpa1500Host());
    OptionsPageLayout::capWidth(m_kpa1500HostEdit, K4Styles::Dimensions::OptionsFieldMaxWidth);
    connect(m_kpa1500HostEdit, &QLineEdit::editingFinished, this,
            [this]() { RadioSettings::instance()->setKpa1500Host(m_kpa1500HostEdit->text().trimmed()); });
    OptionsPageLayout::addRow(form, this, "Host", m_kpa1500HostEdit);

    m_kpa1500PortEdit = new QLineEdit(this);
    m_kpa1500PortEdit->setStyleSheet(K4Styles::Dialog::lineEdit());
    m_kpa1500PortEdit->setText(QString::number(RadioSettings::instance()->kpa1500Port()));
    OptionsPageLayout::capWidth(m_kpa1500PortEdit, K4Styles::Dimensions::OptionsShortFieldWidth);
    connect(m_kpa1500PortEdit, &QLineEdit::editingFinished, this, [this]() {
        bool ok;
        quint16 port = m_kpa1500PortEdit->text().toUShort(&ok);
        if (ok && port >= 1024) {
            RadioSettings::instance()->setKpa1500Port(port);
        } else {
            m_kpa1500PortEdit->setText(QString::number(RadioSettings::instance()->kpa1500Port()));
        }
    });
    OptionsPageLayout::addRow(form, this, "Port", m_kpa1500PortEdit);

    m_kpa1500PollLabel =
        OptionsPageLayout::sliderValue(QString("%1 ms").arg(RadioSettings::instance()->kpa1500PollInterval()), this);
    auto *pollSlider = new QSlider(Qt::Horizontal, this);
    pollSlider->setRange(100, 2000);
    pollSlider->setSingleStep(100);
    pollSlider->setValue(RadioSettings::instance()->kpa1500PollInterval());
    pollSlider->setStyleSheet(
        K4Styles::sliderHorizontal(K4Styles::Colors::DarkBackground, K4Styles::Colors::AccentAmber));
    // WHY persist on release, not on every value: each set is a full settings save. Keyboard and
    // click steps arrive with the slider up and are saved at once.
    connect(pollSlider, &QSlider::valueChanged, this, [this, pollSlider](int value) {
        m_kpa1500PollLabel->setText(QString("%1 ms").arg(value));
        if (!pollSlider->isSliderDown())
            RadioSettings::instance()->setKpa1500PollInterval(value);
    });
    connect(pollSlider, &QSlider::sliderReleased, this,
            [pollSlider]() { RadioSettings::instance()->setKpa1500PollInterval(pollSlider->value()); });
    OptionsPageLayout::addRow(form, this, "Poll", OptionsPageLayout::sliderField(pollSlider, m_kpa1500PollLabel, this));

    m_kpa1500ConnectBtn = new QPushButton("Connect", this);
    m_kpa1500ConnectBtn->setStyleSheet(K4Styles::Dialog::actionButtonSmall());
    m_kpa1500ConnectBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_kpa1500ConnectBtn->setCursor(Qt::PointingHandCursor);
    connect(m_kpa1500ConnectBtn, &QPushButton::clicked, this, [this]() {
        if (!m_kpa1500Client)
            return;
        if (m_kpa1500Client->isConnected()) {
            m_kpa1500Client->disconnectFromHost();
        } else {
            QString host = m_kpa1500HostEdit->text().trimmed();
            quint16 port = m_kpa1500PortEdit->text().toUShort();
            if (!host.isEmpty() && port >= 1024) {
                m_kpa1500Client->connectToHost(host, port);
            }
        }
    });
    form->addRow(QString(), m_kpa1500ConnectBtn);

    OptionsPageLayout::addSection(form, this, "Amplifier Info");
    auto addInfoRow = [&](const QString &label, QLabel *&valueLabel) {
        valueLabel = new QLabel("--", this);
        valueLabel->setStyleSheet(K4Styles::Dialog::formValue());
        OptionsPageLayout::addRow(form, this, label, valueLabel);
    };
    addInfoRow("Band", m_kpa1500BandLabel);
    addInfoRow("Firmware", m_kpa1500FirmwareLabel);
    addInfoRow("Serial", m_kpa1500SerialLabel);

    OptionsPageLayout::addHelp(layout, this, "When enabled, QK4 polls the KPA1500 for power, SWR, and status data.");
    layout->addStretch();

    if (m_kpa1500Client) {
        connect(m_kpa1500Client, &KPA1500Client::connected, this, &Kpa1500Page::updateKpa1500Status);
        connect(m_kpa1500Client, &KPA1500Client::disconnected, this, &Kpa1500Page::updateKpa1500Status);
        // FW, SN and band arrive with the first polls, after `connected`.
        connect(m_kpa1500Client, &KPA1500Client::deviceInfoChanged, this, &Kpa1500Page::updateKpa1500Status);
        connect(m_kpa1500Client, &KPA1500Client::bandNumberChanged, this, &Kpa1500Page::updateKpa1500Status);
    }

    updateKpa1500Status();
}

void Kpa1500Page::refresh() {
    updateKpa1500Status();
}

void Kpa1500Page::updateKpa1500Status() {
    if (!m_kpa1500StatusLabel)
        return;

    bool connected = m_kpa1500Client && m_kpa1500Client->isConnected();

    if (connected) {
        QString fw = m_kpa1500Client->firmwareVersion();
        QString sn = m_kpa1500Client->serialNumber();
        QString statusText = "Connected";
        if (!fw.isEmpty() || !sn.isEmpty())
            statusText += QString(" (FW %1, SN %2)").arg(fw.isEmpty() ? "--" : fw, sn.isEmpty() ? "--" : sn);
        m_kpa1500StatusLabel->setText(statusText);
        m_kpa1500StatusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::StatusGreen));
        m_kpa1500ConnectBtn->setText("Disconnect");
    } else {
        m_kpa1500StatusLabel->setText("Not Connected");
        m_kpa1500StatusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::TextRed));
        if (m_kpa1500ConnectBtn)
            m_kpa1500ConnectBtn->setText("Connect");
    }

    if (m_kpa1500BandLabel)
        m_kpa1500BandLabel->setText(connected ? m_kpa1500Client->bandName() : "--");
    if (m_kpa1500FirmwareLabel)
        m_kpa1500FirmwareLabel->setText(connected ? m_kpa1500Client->firmwareVersion() : "--");
    if (m_kpa1500SerialLabel)
        m_kpa1500SerialLabel->setText(connected ? m_kpa1500Client->serialNumber() : "--");

    if (m_kpa1500HostEdit)
        m_kpa1500HostEdit->setEnabled(!connected);
    if (m_kpa1500PortEdit)
        m_kpa1500PortEdit->setEnabled(!connected);
}
