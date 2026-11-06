#include "ui/pages/tciserverpage.h"
#include "controllers/audiocontroller.h"
#include "controllers/tcicontroller.h"
#include "settings/radiosettings.h"
#include "ui/styling/k4styles.h"

#include <QColor>
#include "ui/pages/optionspagelayout.h"
#include <QFormLayout>
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

// Not in K4Styles: this is the only table in the app, and k4styles.cpp is already over CONVENTIONS.md
// rule 7's 800 lines. It moves there when a second table needs it.
QString clientTableStyle() {
    return QString("QTableWidget { background-color: %1; color: %2; border: 1px solid %3; "
                   "               font-size: %4px; gridline-color: %3; }"
                   "QTableWidget::item { padding: 2px 6px; }"
                   "QHeaderView::section { background-color: %5; color: %6; border: 0px; "
                   "               border-bottom: 1px solid %3; padding: 2px 6px; font-size: %4px; }")
        .arg(K4Styles::Colors::DarkBackground, K4Styles::Colors::TextPrimary, K4Styles::Colors::DialogBorder)
        .arg(K4Styles::Dimensions::FontSizeReadout)
        .arg(K4Styles::Colors::Background, K4Styles::Colors::TextSecondary);
}

} // namespace

TciServerPage::TciServerPage(TciController *tciController, AudioController *audioController, QWidget *parent)
    : QWidget(parent), m_tciController(tciController), m_audioController(audioController) {
    auto *layout = OptionsPageLayout::createPage(this);
    OptionsPageLayout::addTitle(layout, this, "TCI Server");
    OptionsPageLayout::addDescription(layout, this,
                                      "Enable the TCI server to let WSJT-X and other TCI clients reach the K4 for both "
                                      "control and audio over one connection. No loopback sound card is needed.");

    auto *form = OptionsPageLayout::addForm(layout);
    m_statusLabel = new QLabel("Not running", this);
    m_statusLabel->setStyleSheet(K4Styles::Dialog::statusLabelByState());
    OptionsPageLayout::addRow(form, this, "Status", m_statusLabel);

    m_clientsLabel = new QLabel("0 connected", this);
    m_clientsLabel->setStyleSheet(
        K4Styles::Dialog::labelText(K4Styles::Colors::TextPrimary, K4Styles::Dimensions::FontSizeHeading));
    OptionsPageLayout::addRow(form, this, "Clients", m_clientsLabel);

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
    form->addRow(m_clientsTable);

    OptionsPageLayout::addSection(form, this, "Settings");

    m_enableCheckbox = new QCheckBox("Enable TCI server", this);
    m_enableCheckbox->setStyleSheet(K4Styles::Dialog::checkBox());
    m_enableCheckbox->setChecked(RadioSettings::instance()->tciServerEnabled());
    form->addRow(QString(), m_enableCheckbox);

    m_audioCheckbox = new QCheckBox("Carry audio over TCI", this);
    m_audioCheckbox->setStyleSheet(K4Styles::Dialog::checkBox());
    m_audioCheckbox->setChecked(RadioSettings::instance()->tciAudioEnabled());
    form->addRow(QString(), m_audioCheckbox);

    m_portEdit = new QLineEdit(this);
    m_portEdit->setPlaceholderText("50001");
    m_portEdit->setStyleSheet(K4Styles::Dialog::lineEdit());
    m_portEdit->setText(QString::number(RadioSettings::instance()->tciServerPort()));
    OptionsPageLayout::capWidth(m_portEdit, K4Styles::Dimensions::OptionsShortFieldWidth);
    OptionsPageLayout::addRow(form, this, "Port", m_portEdit);
    OptionsPageLayout::addFieldHelp(form, this,
                                    "Default 50001, which is also AetherSDR's default — if you run both, "
                                    "change one.");

    // TCI transmit level. Same shape as Mic Gain on the Audio Input page, and deliberately so -
    // it is the same kind of control doing the same job for the other transmit source.
    m_txGainSlider = new QSlider(Qt::Horizontal, this);
    m_txGainSlider->setRange(0, 100);
    m_txGainSlider->setValue(RadioSettings::instance()->tciTxGain());
    m_txGainSlider->setStyleSheet(
        K4Styles::sliderHorizontal(K4Styles::Colors::DialogBorder, K4Styles::Colors::AccentAmber));
    connect(m_txGainSlider, &QSlider::valueChanged, this, &TciServerPage::onTciTxGainChanged);
    m_txGainValueLabel = OptionsPageLayout::sliderValue(QString("%1%").arg(m_txGainSlider->value()), this);
    OptionsPageLayout::addRow(form, this, "TX Level",
                              OptionsPageLayout::sliderField(m_txGainSlider, m_txGainValueLabel, this));

    OptionsPageLayout::addHelp(
        layout, this,
        "In WSJT-X choose rig \"TCI Client RX1\", set the TCI server to 127.0.0.1 and the port above, "
        "and set both audio devices to \"TCI audio\". TX Level above sets how hard a TCI client drives "
        "the transmitter — it is separate from Mic Gain, which now affects the microphone only. Set it "
        "by watching the radio: ALC should not go above 5. The K4 has no line-in level for LAN audio, "
        "so this and your client's own output level are the only adjustments in the chain.");

    connect(m_portEdit, &QLineEdit::editingFinished, this, [this]() {
        bool ok = false;
        const quint16 port = m_portEdit->text().toUShort(&ok);
        if (ok && port >= 1024) {
            RadioSettings::instance()->setTciServerPort(port);
        } else {
            m_portEdit->setText(QString::number(RadioSettings::instance()->tciServerPort()));
        }
    });

    // The settings object owns the decision; TciController watches it and starts or stops the
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

void TciServerPage::onTciTxGainChanged(int value) {
    if (m_txGainValueLabel) {
        m_txGainValueLabel->setText(QString("%1%").arg(value));
    }

    // Persisted AND pushed live, the same pair AudioInputPage does for Mic Gain: the setting is
    // what survives a restart, the controller call is what the operator hears while dragging.
    RadioSettings::instance()->setTciTxGain(value);

    if (m_audioController) {
        m_audioController->setTciTxGain(value / 100.0f);
    }
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
        K4Styles::setStyleState(m_statusLabel, K4Styles::StateProperty, K4Styles::Dialog::StatusOk);
    } else {
        m_statusLabel->setText("Not running");
        K4Styles::setStyleState(m_statusLabel, K4Styles::StateProperty, QString());
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
        item->setForeground(QColor(K4Styles::Colors::TextSecondary));
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
        m_clientsTable->setItem(
            row, ColLastSeen,
            new QTableWidgetItem(c.lastMessageTime.isValid()
                                     ? c.lastMessageTime.toLocalTime().toString(QStringLiteral("HH:mm:ss"))
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
