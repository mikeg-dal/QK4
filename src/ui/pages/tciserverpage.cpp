#include "ui/pages/tciserverpage.h"
#include "controllers/tcicontroller.h"
#include "settings/radiosettings.h"
#include "ui/styling/k4styles.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>

TciServerPage::TciServerPage(TciController *tciController, QWidget *parent)
    : QWidget(parent), m_tciController(tciController) {
    setStyleSheet(K4Styles::Dialog::pageBackground());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin,
                               K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin);
    layout->setSpacing(K4Styles::Dimensions::PaddingLarge);

    auto *titleLabel = new QLabel("TCI Server", this);
    titleLabel->setStyleSheet(K4Styles::Dialog::titleLabel());
    layout->addWidget(titleLabel);

    auto *descLabel = new QLabel("Enable the TCI server to let WSJT-X and other TCI clients reach the K4 for both "
                                 "control and audio over one connection. No loopback sound card is needed.",
                                 this);
    descLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                 .arg(K4Styles::Colors::TextGray)
                                 .arg(K4Styles::Dimensions::FontSizeButton));
    descLabel->setWordWrap(true);
    layout->addWidget(descLabel);

    auto *line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(K4Styles::Dialog::separator());
    line->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line);

    // Status
    auto *statusLayout = new QHBoxLayout();
    auto *statusTitleLabel = new QLabel("Status:", this);
    statusTitleLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    statusTitleLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_statusLabel = new QLabel("Not running", this);
    m_statusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::ErrorRed));

    statusLayout->addWidget(statusTitleLabel);
    statusLayout->addWidget(m_statusLabel);
    statusLayout->addStretch();
    layout->addLayout(statusLayout);

    // Clients
    auto *clientsLayout = new QHBoxLayout();
    auto *clientsTitleLabel = new QLabel("Clients:", this);
    clientsTitleLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    clientsTitleLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_clientsLabel = new QLabel("0 connected", this);
    m_clientsLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                      .arg(K4Styles::Colors::TextWhite)
                                      .arg(K4Styles::Dimensions::FontSizePopup));

    clientsLayout->addWidget(clientsTitleLabel);
    clientsLayout->addWidget(m_clientsLabel);
    clientsLayout->addStretch();
    layout->addLayout(clientsLayout);

    auto *line2 = new QFrame(this);
    line2->setFrameShape(QFrame::HLine);
    line2->setStyleSheet(K4Styles::Dialog::separator());
    line2->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line2);

    auto *sectionLabel = new QLabel("Settings", this);
    sectionLabel->setStyleSheet(K4Styles::Dialog::sectionHeader());
    layout->addWidget(sectionLabel);

    // Port
    auto *portLayout = new QHBoxLayout();
    auto *portLabel = new QLabel("Port:", this);
    portLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    portLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_portEdit = new QLineEdit(this);
    m_portEdit->setPlaceholderText("50001");
    m_portEdit->setFixedWidth(K4Styles::Dimensions::InputFieldWidthSmall);
    m_portEdit->setStyleSheet(K4Styles::Dialog::lineEdit());
    m_portEdit->setText(QString::number(RadioSettings::instance()->tciServerPort()));

    auto *portHint = new QLabel("(default: 50001)", this);
    portHint->setStyleSheet(QString("color: %1; font-size: %2px;")
                                .arg(K4Styles::Colors::TextGray)
                                .arg(K4Styles::Dimensions::FontSizeLarge));

    portLayout->addWidget(portLabel);
    portLayout->addWidget(m_portEdit);
    portLayout->addWidget(portHint);
    portLayout->addStretch();
    layout->addLayout(portLayout);

    auto *line3 = new QFrame(this);
    line3->setFrameShape(QFrame::HLine);
    line3->setStyleSheet(K4Styles::Dialog::separator());
    line3->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line3);

    m_enableCheckbox = new QCheckBox("Enable TCI server", this);
    m_enableCheckbox->setStyleSheet(K4Styles::Dialog::checkBox());
    m_enableCheckbox->setChecked(RadioSettings::instance()->tciServerEnabled());
    layout->addWidget(m_enableCheckbox);

    m_audioCheckbox = new QCheckBox("Carry audio over TCI", this);
    m_audioCheckbox->setStyleSheet(K4Styles::Dialog::checkBox());
    m_audioCheckbox->setChecked(RadioSettings::instance()->tciAudioEnabled());
    layout->addWidget(m_audioCheckbox);

    auto *helpLabel =
        new QLabel("In WSJT-X choose rig \"TCI Client RX1\", set the TCI server to 127.0.0.1 and the port above, "
                   "and set both audio devices to \"TCI audio\". Transmit level is the Mic Gain on the Audio Input "
                   "page. Note that 50001 is also AetherSDR's default — if you run both, change one.",
                   this);
    helpLabel->setStyleSheet(K4Styles::Dialog::helpText());
    helpLabel->setWordWrap(true);
    layout->addWidget(helpLabel);

    connect(m_portEdit, &QLineEdit::editingFinished, this, [this]() {
        bool ok = false;
        const quint16 port = m_portEdit->text().toUShort(&ok);
        if (ok && port >= 1024) {
            RadioSettings::instance()->setTciServerPort(port);
        } else {
            m_portEdit->setText(QString::number(RadioSettings::instance()->tciServerPort()));
        }
    });

    // The settings object owns the decision; MainWindow watches it and starts or stops the
    // listener. This page never drives the controller directly, so the toggle behaves the same
    // whether it is changed here or restored at startup.
    connect(m_enableCheckbox, &QCheckBox::toggled, this,
            [](bool checked) { RadioSettings::instance()->setTciServerEnabled(checked); });
    connect(m_audioCheckbox, &QCheckBox::toggled, this,
            [](bool checked) { RadioSettings::instance()->setTciAudioEnabled(checked); });

    layout->addStretch();

    if (m_tciController) {
        connect(m_tciController, &TciController::listeningChanged, this, &TciServerPage::updateStatus);
        connect(m_tciController, &TciController::clientCountChanged, this, &TciServerPage::updateStatus);
    }

    updateStatus();
}

void TciServerPage::refresh() {
    if (m_portEdit) {
        m_portEdit->setText(QString::number(RadioSettings::instance()->tciServerPort()));
    }
    if (m_enableCheckbox) {
        QSignalBlocker block(m_enableCheckbox);
        m_enableCheckbox->setChecked(RadioSettings::instance()->tciServerEnabled());
    }
    if (m_audioCheckbox) {
        QSignalBlocker block(m_audioCheckbox);
        m_audioCheckbox->setChecked(RadioSettings::instance()->tciAudioEnabled());
    }
    updateStatus();
}

void TciServerPage::updateStatus() {
    if (!m_statusLabel || !m_clientsLabel) {
        return;
    }

    const bool listening = m_tciController && m_tciController->isListening();
    if (listening) {
        m_statusLabel->setText(QString("Listening on port %1").arg(RadioSettings::instance()->tciServerPort()));
        m_statusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::StatusGreen));
    } else {
        m_statusLabel->setText("Not running");
        m_statusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::ErrorRed));
    }

    const int clients = m_tciController ? m_tciController->clientCount() : 0;
    m_clientsLabel->setText(QString("%1 connected").arg(clients));
}
