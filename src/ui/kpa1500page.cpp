#include "kpa1500page.h"
#include "k4styles.h"
#include "../network/kpa1500client.h"
#include "../settings/radiosettings.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QSlider>

Kpa1500Page::Kpa1500Page(KPA1500Client *kpa1500Client, QWidget *parent)
    : QWidget(parent), m_kpa1500Client(kpa1500Client) {
    setStyleSheet(K4Styles::Dialog::pageBackground());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin,
                               K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin);
    layout->setSpacing(K4Styles::Dimensions::PaddingLarge);

    // === Status row ===
    auto *statusLayout = new QHBoxLayout();
    auto *statusLabel = new QLabel("Status:", this);
    statusLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    statusLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);
    m_kpa1500StatusLabel = new QLabel("Not Connected", this);
    m_kpa1500StatusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::ErrorRed));
    statusLayout->addWidget(statusLabel);
    statusLayout->addWidget(m_kpa1500StatusLabel);
    statusLayout->addStretch();
    layout->addLayout(statusLayout);

    // === Separator ===
    auto *line1 = new QFrame(this);
    line1->setFrameShape(QFrame::HLine);
    line1->setStyleSheet(K4Styles::Dialog::separator());
    line1->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line1);

    // === Connection Settings section ===
    auto *sectionLabel = new QLabel("Connection Settings", this);
    sectionLabel->setStyleSheet(K4Styles::Dialog::sectionHeader());
    layout->addWidget(sectionLabel);

    // Enable checkbox
    m_kpa1500EnableCheckbox = new QCheckBox("Enable KPA1500", this);
    m_kpa1500EnableCheckbox->setStyleSheet(K4Styles::Dialog::checkBox());
    m_kpa1500EnableCheckbox->setChecked(RadioSettings::instance()->kpa1500Enabled());
    connect(m_kpa1500EnableCheckbox, &QCheckBox::toggled, this,
            [](bool checked) { RadioSettings::instance()->setKpa1500Enabled(checked); });
    layout->addWidget(m_kpa1500EnableCheckbox);

    // Host row
    auto *hostLayout = new QHBoxLayout();
    auto *hostLabel = new QLabel("Host:", this);
    hostLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    hostLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);
    m_kpa1500HostEdit = new QLineEdit(this);
    m_kpa1500HostEdit->setStyleSheet(K4Styles::Dialog::lineEdit());
    m_kpa1500HostEdit->setPlaceholderText("IP address or hostname");
    m_kpa1500HostEdit->setText(RadioSettings::instance()->kpa1500Host());
    connect(m_kpa1500HostEdit, &QLineEdit::editingFinished, this,
            [this]() { RadioSettings::instance()->setKpa1500Host(m_kpa1500HostEdit->text().trimmed()); });
    hostLayout->addWidget(hostLabel);
    hostLayout->addWidget(m_kpa1500HostEdit, 1);
    layout->addLayout(hostLayout);

    // Port row
    auto *portLayout = new QHBoxLayout();
    auto *portLabel = new QLabel("Port:", this);
    portLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    portLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);
    m_kpa1500PortEdit = new QLineEdit(this);
    m_kpa1500PortEdit->setStyleSheet(K4Styles::Dialog::lineEdit());
    m_kpa1500PortEdit->setFixedWidth(K4Styles::Dimensions::InputFieldWidthSmall);
    m_kpa1500PortEdit->setText(QString::number(RadioSettings::instance()->kpa1500Port()));
    connect(m_kpa1500PortEdit, &QLineEdit::editingFinished, this, [this]() {
        bool ok;
        quint16 port = m_kpa1500PortEdit->text().toUShort(&ok);
        if (ok && port >= 1024) {
            RadioSettings::instance()->setKpa1500Port(port);
        } else {
            m_kpa1500PortEdit->setText(QString::number(RadioSettings::instance()->kpa1500Port()));
        }
    });
    portLayout->addWidget(portLabel);
    portLayout->addWidget(m_kpa1500PortEdit);
    portLayout->addStretch();
    layout->addLayout(portLayout);

    // Poll interval row
    auto *pollLayout = new QHBoxLayout();
    auto *pollLabel = new QLabel("Poll:", this);
    pollLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    pollLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);
    m_kpa1500PollLabel = new QLabel(QString("%1 ms").arg(RadioSettings::instance()->kpa1500PollInterval()), this);
    m_kpa1500PollLabel->setStyleSheet(K4Styles::Dialog::formValue());
    auto *pollSlider = new QSlider(Qt::Horizontal, this);
    pollSlider->setRange(100, 2000);
    pollSlider->setSingleStep(100);
    pollSlider->setValue(RadioSettings::instance()->kpa1500PollInterval());
    pollSlider->setStyleSheet(
        K4Styles::sliderHorizontal(K4Styles::Colors::DarkBackground, K4Styles::Colors::AccentAmber));
    connect(pollSlider, &QSlider::valueChanged, this, [this](int value) {
        RadioSettings::instance()->setKpa1500PollInterval(value);
        if (m_kpa1500PollLabel)
            m_kpa1500PollLabel->setText(QString("%1 ms").arg(value));
    });
    pollLayout->addWidget(pollLabel);
    pollLayout->addWidget(pollSlider, 1);
    pollLayout->addWidget(m_kpa1500PollLabel);
    layout->addLayout(pollLayout);

    // Connect/Disconnect button
    m_kpa1500ConnectBtn = new QPushButton("Connect", this);
    m_kpa1500ConnectBtn->setStyleSheet(K4Styles::Dialog::actionButtonSmall());
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
    layout->addWidget(m_kpa1500ConnectBtn);

    // === Separator ===
    auto *line2 = new QFrame(this);
    line2->setFrameShape(QFrame::HLine);
    line2->setStyleSheet(K4Styles::Dialog::separator());
    line2->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line2);

    // === Amplifier Info section ===
    auto *infoLabel = new QLabel("Amplifier Info", this);
    infoLabel->setStyleSheet(K4Styles::Dialog::sectionHeader());
    layout->addWidget(infoLabel);

    auto *infoWidget = new QWidget(this);
    auto *infoGrid = new QGridLayout(infoWidget);
    infoGrid->setContentsMargins(0, 0, 0, 0);
    infoGrid->setHorizontalSpacing(K4Styles::Dimensions::DialogMargin);
    infoGrid->setVerticalSpacing(K4Styles::Dimensions::PopupButtonSpacing);

    auto addInfoRow = [&](int row, const QString &label, QLabel *&valueLabel) {
        auto *lbl = new QLabel(label, infoWidget);
        lbl->setStyleSheet(K4Styles::Dialog::formLabel());
        valueLabel = new QLabel("--", infoWidget);
        valueLabel->setStyleSheet(K4Styles::Dialog::formValue());
        infoGrid->addWidget(lbl, row, 0, Qt::AlignLeft);
        infoGrid->addWidget(valueLabel, row, 1, Qt::AlignLeft);
    };

    addInfoRow(0, "Band:", m_kpa1500BandLabel);
    addInfoRow(1, "Firmware:", m_kpa1500FirmwareLabel);
    addInfoRow(2, "Serial:", m_kpa1500SerialLabel);
    infoGrid->setColumnStretch(1, 1);
    layout->addWidget(infoWidget);

    // === Help text ===
    auto *helpLabel = new QLabel("When enabled, QK4 polls the KPA1500 for power, SWR, and status data.", this);
    helpLabel->setStyleSheet(K4Styles::Dialog::helpText());
    helpLabel->setWordWrap(true);
    layout->addWidget(helpLabel);

    layout->addStretch();

    // Connect to KPA1500 signals for real-time status updates
    if (m_kpa1500Client) {
        connect(m_kpa1500Client, &KPA1500Client::connected, this, &Kpa1500Page::updateKpa1500Status);
        connect(m_kpa1500Client, &KPA1500Client::disconnected, this, &Kpa1500Page::updateKpa1500Status);
    }

    // Initialize status display
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
        m_kpa1500StatusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::ErrorRed));
        if (m_kpa1500ConnectBtn)
            m_kpa1500ConnectBtn->setText("Connect");
    }

    // Update amplifier info labels
    if (m_kpa1500BandLabel)
        m_kpa1500BandLabel->setText(connected ? m_kpa1500Client->bandName() : "--");
    if (m_kpa1500FirmwareLabel)
        m_kpa1500FirmwareLabel->setText(connected ? m_kpa1500Client->firmwareVersion() : "--");
    if (m_kpa1500SerialLabel)
        m_kpa1500SerialLabel->setText(connected ? m_kpa1500Client->serialNumber() : "--");

    // Disable host/port editing while connected
    if (m_kpa1500HostEdit)
        m_kpa1500HostEdit->setEnabled(!connected);
    if (m_kpa1500PortEdit)
        m_kpa1500PortEdit->setEnabled(!connected);
}
