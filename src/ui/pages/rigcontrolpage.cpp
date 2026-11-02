#include "ui/pages/rigcontrolpage.h"
#include "ui/styling/k4styles.h"
#include "network/catserver.h"
#include "settings/radiosettings.h"
#include "ui/pages/optionspagelayout.h"
#include <QFormLayout>
#include <QVBoxLayout>

RigControlPage::RigControlPage(CatServer *catServer, QWidget *parent) : QWidget(parent), m_catServer(catServer) {
    auto *layout = OptionsPageLayout::createPage(this);
    OptionsPageLayout::addTitle(layout, this, "CAT Server");
    OptionsPageLayout::addDescription(
        layout, this,
        "Enable the CAT server to allow external applications (WSJT-X, MacLoggerDX, fldigi) "
        "to connect using their native Elecraft K4 support. No protocol translation needed.");

    auto *form = OptionsPageLayout::addForm(layout);
    m_catServerStatusLabel = new QLabel("Not running", this);
    m_catServerStatusLabel->setStyleSheet(K4Styles::Dialog::statusLabelByState());
    OptionsPageLayout::addRow(form, this, "Status", m_catServerStatusLabel);

    m_catServerClientsLabel = new QLabel("0 connected", this);
    m_catServerClientsLabel->setStyleSheet(
        K4Styles::Dialog::labelText(K4Styles::Colors::TextPrimary, K4Styles::Dimensions::FontSizeHeading));
    OptionsPageLayout::addRow(form, this, "Clients", m_catServerClientsLabel);

    OptionsPageLayout::addSection(form, this, "Settings");

    m_catServerEnableCheckbox = new QCheckBox("Enable CAT server", this);
    m_catServerEnableCheckbox->setStyleSheet(K4Styles::Dialog::checkBox());
    m_catServerEnableCheckbox->setChecked(RadioSettings::instance()->catServerEnabled());
    form->addRow(QString(), m_catServerEnableCheckbox);

    m_catServerPortEdit = new QLineEdit(this);
    m_catServerPortEdit->setPlaceholderText("9299");
    m_catServerPortEdit->setStyleSheet(K4Styles::Dialog::lineEdit());
    m_catServerPortEdit->setText(QString::number(RadioSettings::instance()->catServerPort()));
    OptionsPageLayout::capWidth(m_catServerPortEdit, K4Styles::Dimensions::OptionsShortFieldWidth);
    OptionsPageLayout::addRow(form, this, "Port", m_catServerPortEdit);
    OptionsPageLayout::addFieldHelp(form, this,
                                    "Default 9299. Configure external apps to use Elecraft K4, host 127.0.0.1, and "
                                    "this port. Commands are forwarded to the real K4.");

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

    if (m_catServer) {
        connect(m_catServer, &CatServer::started, this, &RigControlPage::updateCatServerStatus);
        connect(m_catServer, &CatServer::stopped, this, &RigControlPage::updateCatServerStatus);
        connect(m_catServer, &CatServer::clientConnected, this, &RigControlPage::updateCatServerStatus);
        connect(m_catServer, &CatServer::clientDisconnected, this, &RigControlPage::updateCatServerStatus);
    }

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
        K4Styles::setStyleState(m_catServerStatusLabel, K4Styles::StateProperty, K4Styles::Dialog::StatusOk);
    } else {
        m_catServerStatusLabel->setText("Not running");
        K4Styles::setStyleState(m_catServerStatusLabel, K4Styles::StateProperty, QString());
    }

    int clientCount = m_catServer ? m_catServer->clientCount() : 0;
    m_catServerClientsLabel->setText(QString("%1 connected").arg(clientCount));
}
