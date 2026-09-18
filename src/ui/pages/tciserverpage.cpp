#include "ui/pages/tciserverpage.h"
#include "controllers/tcicontroller.h"
#include "settings/radiosettings.h"
#include "ui/styling/k4styles.h"

#include <QColor>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QVBoxLayout>

namespace {

// Columns of the client table.
enum ClientColumn { ColAddress = 0, ColLastSeen = 1, ColLastMessage = 2, ColCount = 3 };

// Direction markers for the last-message cell. The arrow points the way the message travelled,
// with the client at the far end: right means QK4 sent it, left means the client did. Which is
// exactly the ambiguity an arrow alone leaves, so the column carries a tooltip saying so.
constexpr QChar kToClient = QChar(0x2192);   // right arrow
constexpr QChar kFromClient = QChar(0x2190); // left arrow

// Room for a handful of clients without the table growing into the settings below it. TCI caps
// connections at WebSocketServer::MAX_CLIENTS (8); beyond four rows the table scrolls rather than
// pushing the page around.
constexpr int kVisibleRows = 4;
constexpr int kRowHeight = 22;

// Not in K4Styles: this is the only table in the app, and k4styles.cpp is at 782 of its 800-line
// budget (CONVENTIONS.md rule 7). It moves there when a second table needs it.
QString clientTableStyle() {
    return QString("QTableWidget { background-color: %1; color: %2; border: 1px solid %3; "
                   "               font-size: %4px; gridline-color: %3; }"
                   "QTableWidget::item { padding: 2px 6px; }"
                   "QHeaderView::section { background-color: %5; color: %6; border: 0px; "
                   "               border-bottom: 1px solid %3; padding: 2px 6px; font-size: %4px; }")
        .arg(K4Styles::Colors::DarkBackground, K4Styles::Colors::TextWhite, K4Styles::Colors::DialogBorder)
        .arg(K4Styles::Dimensions::FontSizeLarge)
        .arg(K4Styles::Colors::Background, K4Styles::Colors::TextGray);
}

} // namespace

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

    // Who is connected, and whether they are still talking.
    //
    // WHY a table and not a longer count: a count cannot answer the question that actually comes up
    // - "is WSJT-X still there, or did it wander off?" - and with two clients it cannot say which
    // one is which. The last-message column also makes a client that connected but never spoke
    // (wrong port, wrong protocol) visible as such.
    m_clientsTable = new QTableWidget(0, ColCount, this);
    m_clientsTable->setHorizontalHeaderLabels({"Address", "Last seen", "Last message"});
    m_clientsTable->horizontalHeaderItem(ColLastMessage)
        ->setToolTip(QString("%1 sent to the client    %2 received from the client").arg(kToClient).arg(kFromClient));
    m_clientsTable->verticalHeader()->setVisible(false);
    m_clientsTable->verticalHeader()->setDefaultSectionSize(kRowHeight);
    m_clientsTable->horizontalHeader()->setStretchLastSection(true);
    m_clientsTable->horizontalHeader()->setSectionResizeMode(ColAddress, QHeaderView::ResizeToContents);
    m_clientsTable->horizontalHeader()->setSectionResizeMode(ColLastSeen, QHeaderView::ResizeToContents);
    // Read-only, and not a selection widget: nothing acts on a row, so letting one look picked
    // would suggest an action that does not exist.
    m_clientsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_clientsTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_clientsTable->setFocusPolicy(Qt::NoFocus);
    m_clientsTable->setShowGrid(false);
    m_clientsTable->setAlternatingRowColors(false);
    m_clientsTable->setStyleSheet(clientTableStyle());
    m_clientsTable->setFixedHeight(kRowHeight * kVisibleRows + m_clientsTable->horizontalHeader()->height() + 4);
    layout->addWidget(m_clientsTable);

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
        connect(m_tciController, &TciController::clientsChanged, this, &TciServerPage::updateClients);
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

    // The roster is redrawn here as well as on clientsChanged, because the page is built long after
    // the server started: opening the options dialog has to show the clients that are already
    // there, and no signal is coming to say so.
    updateClients(m_tciController ? m_tciController->clients() : QVector<TciClientInfo>());
}

void TciServerPage::updateClients(const QVector<TciClientInfo> &clients) {
    if (!m_clientsTable) {
        return;
    }

    if (clients.isEmpty()) {
        // One row saying so, rather than an empty box that reads as broken.
        m_clientsTable->setRowCount(1);
        auto *item = new QTableWidgetItem("No clients connected");
        item->setForeground(QColor(K4Styles::Colors::TextGray));
        m_clientsTable->setItem(0, ColAddress, item);
        m_clientsTable->setItem(0, ColLastSeen, new QTableWidgetItem(QString()));
        m_clientsTable->setItem(0, ColLastMessage, new QTableWidgetItem(QString()));
        return;
    }

    m_clientsTable->setRowCount(clients.size());
    for (int row = 0; row < clients.size(); ++row) {
        const TciClientInfo &c = clients[row];
        m_clientsTable->setItem(row, ColAddress, new QTableWidgetItem(c.address));
        // Wall-clock time, not "12 s ago": the row is only repainted when something happens, so a
        // relative age would freeze at whatever it read when the client last spoke and quietly lie.
        m_clientsTable->setItem(row, ColLastSeen,
                                new QTableWidgetItem(c.lastMessageTime.isValid()
                                                         ? c.lastMessageTime.toString(QStringLiteral("HH:mm:ss"))
                                                         : QString()));
        auto *messageItem =
            new QTableWidgetItem(QString("%1 %2").arg(c.lastMessageOutbound ? kToClient : kFromClient, c.lastMessage));
        // The arrow is the only thing distinguishing an echo from the command that caused it, and a
        // truncated message hides the rest, so the full text is one hover away.
        messageItem->setToolTip(QString("%1 %2").arg(
            c.lastMessageOutbound ? QStringLiteral("Sent to the client:") : QStringLiteral("Received from the client:"),
            c.lastMessage));
        m_clientsTable->setItem(row, ColLastMessage, messageItem);
    }
}
