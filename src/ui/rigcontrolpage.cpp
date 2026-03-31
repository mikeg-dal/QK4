#include "rigcontrolpage.h"
#include "k4styles.h"
#include "../network/catserver.h"
#include "../settings/radiosettings.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>

RigControlPage::RigControlPage(CatServer *catServer, QWidget *parent) : QWidget(parent), m_catServer(catServer) {
    setStyleSheet(K4Styles::Dialog::pageBackground());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin,
                               K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin);
    layout->setSpacing(K4Styles::Dimensions::PaddingLarge);

    // Title
    auto *titleLabel = new QLabel("CAT Server", this);
    titleLabel->setStyleSheet(K4Styles::Dialog::titleLabel());
    layout->addWidget(titleLabel);

    // Description
    auto *descLabel = new QLabel("Enable the CAT server to allow external applications (WSJT-X, MacLoggerDX, fldigi) "
                                 "to connect using their native Elecraft K4 support. No protocol translation needed.",
                                 this);
    descLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                 .arg(K4Styles::Colors::TextGray)
                                 .arg(K4Styles::Dimensions::FontSizeButton));
    descLabel->setWordWrap(true);
    layout->addWidget(descLabel);

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

    m_catServerStatusLabel = new QLabel("Not running", this);
    m_catServerStatusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::ErrorRed));

    statusLayout->addWidget(statusTitleLabel);
    statusLayout->addWidget(m_catServerStatusLabel);
    statusLayout->addStretch();
    layout->addLayout(statusLayout);

    // Clients indicator
    auto *clientsLayout = new QHBoxLayout();
    auto *clientsTitleLabel = new QLabel("Clients:", this);
    clientsTitleLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    clientsTitleLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_catServerClientsLabel = new QLabel("0 connected", this);
    m_catServerClientsLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                               .arg(K4Styles::Colors::TextWhite)
                                               .arg(K4Styles::Dimensions::FontSizePopup));

    clientsLayout->addWidget(clientsTitleLabel);
    clientsLayout->addWidget(m_catServerClientsLabel);
    clientsLayout->addStretch();
    layout->addLayout(clientsLayout);

    // Separator line
    auto *line2 = new QFrame(this);
    line2->setFrameShape(QFrame::HLine);
    line2->setStyleSheet(K4Styles::Dialog::separator());
    line2->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line2);

    // Connection Settings section
    auto *sectionLabel = new QLabel("Settings", this);
    sectionLabel->setStyleSheet(K4Styles::Dialog::sectionHeader());
    layout->addWidget(sectionLabel);

    // Port input
    auto *portLayout = new QHBoxLayout();
    auto *portLabel = new QLabel("Port:", this);
    portLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    portLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_catServerPortEdit = new QLineEdit(this);
    m_catServerPortEdit->setPlaceholderText("9299");
    m_catServerPortEdit->setFixedWidth(K4Styles::Dimensions::InputFieldWidthSmall);
    m_catServerPortEdit->setStyleSheet(K4Styles::Dialog::lineEdit());
    m_catServerPortEdit->setText(QString::number(RadioSettings::instance()->catServerPort()));

    auto *portHint = new QLabel("(default: 9299)", this);
    portHint->setStyleSheet(QString("color: %1; font-size: %2px;")
                                .arg(K4Styles::Colors::TextGray)
                                .arg(K4Styles::Dimensions::FontSizeLarge));

    portLayout->addWidget(portLabel);
    portLayout->addWidget(m_catServerPortEdit);
    portLayout->addWidget(portHint);
    portLayout->addStretch();
    layout->addLayout(portLayout);

    // Separator line
    auto *line3 = new QFrame(this);
    line3->setFrameShape(QFrame::HLine);
    line3->setStyleSheet(K4Styles::Dialog::separator());
    line3->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line3);

    // Enable checkbox
    m_catServerEnableCheckbox = new QCheckBox("Enable CAT server", this);
    m_catServerEnableCheckbox->setStyleSheet(K4Styles::Dialog::checkBox());
    m_catServerEnableCheckbox->setChecked(RadioSettings::instance()->catServerEnabled());
    layout->addWidget(m_catServerEnableCheckbox);

    // Help text
    auto *helpLabel = new QLabel("Configure external apps to use Elecraft K4, host 127.0.0.1, and the port above. "
                                 "Commands are forwarded to the real K4.",
                                 this);
    helpLabel->setStyleSheet(K4Styles::Dialog::helpText());
    helpLabel->setWordWrap(true);
    layout->addWidget(helpLabel);

    // Connect signals to save settings
    connect(m_catServerPortEdit, &QLineEdit::editingFinished, this, [this]() {
        bool ok;
        quint16 port = m_catServerPortEdit->text().toUShort(&ok);
        if (ok && port >= 1024) {
            RadioSettings::instance()->setCatServerPort(port);
        } else {
            // Reset to current value if invalid
            m_catServerPortEdit->setText(QString::number(RadioSettings::instance()->catServerPort()));
        }
    });

    connect(m_catServerEnableCheckbox, &QCheckBox::toggled, this,
            [](bool checked) { RadioSettings::instance()->setCatServerEnabled(checked); });

    layout->addStretch();

    // Connect to CatServer signals for status updates
    if (m_catServer) {
        connect(m_catServer, &CatServer::started, this, &RigControlPage::updateCatServerStatus);
        connect(m_catServer, &CatServer::stopped, this, &RigControlPage::updateCatServerStatus);
        connect(m_catServer, &CatServer::clientConnected, this, &RigControlPage::updateCatServerStatus);
        connect(m_catServer, &CatServer::clientDisconnected, this, &RigControlPage::updateCatServerStatus);
    }

    // Initialize status display
    updateCatServerStatus();
}

void RigControlPage::refresh() {
    updateCatServerStatus();
}

void RigControlPage::updateCatServerStatus() {
    if (!m_catServerStatusLabel || !m_catServerClientsLabel) {
        return;
    }

    bool isListening = m_catServer && m_catServer->isListening();

    if (isListening) {
        m_catServerStatusLabel->setText(QString("Listening on port %1").arg(m_catServer->port()));
        m_catServerStatusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::StatusGreen));
    } else {
        m_catServerStatusLabel->setText("Not running");
        m_catServerStatusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::ErrorRed));
    }

    int clientCount = m_catServer ? m_catServer->clientCount() : 0;
    m_catServerClientsLabel->setText(QString("%1 connected").arg(clientCount));
}
