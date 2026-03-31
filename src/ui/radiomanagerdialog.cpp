#include "radiomanagerdialog.h"
#include "k4styles.h"
#include "network/protocol.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>

RadioManagerDialog::RadioManagerDialog(QWidget *parent) : QDialog(parent), m_currentIndex(-1), m_discovery(nullptr) {
    setupUi();
    refreshList();
    updateButtonStates();

    connect(RadioSettings::instance(), &RadioSettings::radiosChanged, this, &RadioManagerDialog::refreshList);

    startDiscovery();
}

void RadioManagerDialog::setupUi() {
    setWindowTitle("Server Manager");
    setFixedSize(580, 395);

    // Dark theme for the dialog
    setStyleSheet(QString("QDialog { background-color: %1; }").arg(K4Styles::Colors::Background));

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(K4Styles::Dimensions::PopupContentMargin);
    mainLayout->setContentsMargins(K4Styles::Dimensions::PaddingLarge, K4Styles::Dimensions::PaddingLarge,
                                   K4Styles::Dimensions::PaddingLarge, K4Styles::Dimensions::PaddingLarge);

    // Top horizontal section - servers list on left, edit fields on right
    auto *topLayout = new QHBoxLayout();
    topLayout->setSpacing(K4Styles::Dimensions::DialogMargin);

    // === LEFT SIDE: Available Servers ===
    auto *leftSection = new QVBoxLayout();
    leftSection->setSpacing(K4Styles::Dimensions::PopupButtonSpacing);

    auto *serversTitle = new QLabel("Available Servers", this);
    serversTitle->setStyleSheet(QString("QLabel { color: %1; font-weight: bold; font-size: %2px; }")
                                    .arg(K4Styles::Colors::AccentAmber)
                                    .arg(K4Styles::Dimensions::FontSizePopup));
    leftSection->addWidget(serversTitle);

    m_radioList = new QListWidget(this);
    m_radioList->setMinimumWidth(180);
    m_radioList->setMaximumWidth(200);
    m_radioList->setStyleSheet(
        QString("QListWidget { "
                "  background-color: %1; "
                "  color: %2; "
                "  border: 1px solid %3; "
                "  border-radius: 4px; "
                "  padding: 4px; "
                "} "
                "QListWidget::item { "
                "  padding: %4px; "
                "} "
                "QListWidget::item:selected { "
                "  background-color: %5; "
                "  color: %1; "
                "} "
                "QListWidget::item:hover { "
                "  background-color: %6; "
                "} "
                "QListWidget::item:selected:hover { "
                "  background-color: %5; "
                "  color: %1; "
                "}")
            .arg(K4Styles::Colors::DarkBackground, K4Styles::Colors::TextWhite, K4Styles::Colors::DialogBorder)
            .arg(K4Styles::Dimensions::PaddingSmall)
            .arg(K4Styles::Colors::AccentAmber, K4Styles::Colors::GradientBottom));
    leftSection->addWidget(m_radioList);
    topLayout->addLayout(leftSection);

    // === RIGHT SIDE: Edit Connect ===
    auto *rightSection = new QVBoxLayout();
    rightSection->setSpacing(K4Styles::Dimensions::PopupButtonSpacing);

    auto *editTitle = new QLabel("Edit Connect", this);
    editTitle->setStyleSheet(QString("QLabel { color: %1; font-weight: bold; font-size: %2px; }")
                                 .arg(K4Styles::Colors::AccentAmber)
                                 .arg(K4Styles::Dimensions::FontSizePopup));
    rightSection->addWidget(editTitle);

    // Form fields - label on LEFT of text box
    auto *formLayout = new QGridLayout();
    formLayout->setHorizontalSpacing(K4Styles::Dimensions::PaddingMedium);
    formLayout->setVerticalSpacing(K4Styles::Dimensions::PaddingMedium);

    QString lineEditStyle =
        QString("QLineEdit { "
                "  background-color: %1; "
                "  color: %2; "
                "  border: 1px solid %3; "
                "  border-radius: 4px; "
                "  padding: %4px; "
                "  min-width: 150px; "
                "}")
            .arg(K4Styles::Colors::DarkBackground, K4Styles::Colors::TextWhite, K4Styles::Colors::DialogBorder)
            .arg(K4Styles::Dimensions::PaddingSmall);

    QString labelStyle = QString("QLabel { color: %1; font-size: %2px; }")
                             .arg(K4Styles::Colors::TextGray)
                             .arg(K4Styles::Dimensions::FontSizeButton);

    // Row 0: Name
    auto *nameLabel = new QLabel("Name", this);
    nameLabel->setStyleSheet(labelStyle);
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setStyleSheet(lineEditStyle);
    m_nameEdit->setPlaceholderText("Server Name");
    formLayout->addWidget(nameLabel, 0, 0);
    formLayout->addWidget(m_nameEdit, 0, 1);

    // Row 1: Host or IP
    auto *hostLabel = new QLabel("Host or IP", this);
    hostLabel->setStyleSheet(labelStyle);
    m_hostEdit = new QLineEdit(this);
    m_hostEdit->setStyleSheet(lineEditStyle);
    m_hostEdit->setPlaceholderText("192.168.1.100");
    formLayout->addWidget(hostLabel, 1, 0);
    formLayout->addWidget(m_hostEdit, 1, 1);

    // Row 2: Port
    auto *portLabel = new QLabel("Port", this);
    portLabel->setStyleSheet(labelStyle);
    m_portEdit = new QLineEdit(this);
    m_portEdit->setStyleSheet(lineEditStyle);
    m_portEdit->setPlaceholderText("64242");
    m_portEdit->setMaximumWidth(80);
    formLayout->addWidget(portLabel, 2, 0);
    formLayout->addWidget(m_portEdit, 2, 1);

    // Row 3: Password
    auto *passwordLabel = new QLabel("Password", this);
    passwordLabel->setStyleSheet(labelStyle);
    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setStyleSheet(lineEditStyle);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText("Password");
    formLayout->addWidget(passwordLabel, 3, 0);
    formLayout->addWidget(m_passwordEdit, 3, 1);

    // Row 4: ID (only visible when TLS is checked)
    m_identityLabel = new QLabel("ID", this);
    m_identityLabel->setStyleSheet(labelStyle);
    m_identityEdit = new QLineEdit(this);
    m_identityEdit->setStyleSheet(lineEditStyle);
    m_identityEdit->setPlaceholderText("Identity (optional)");
    formLayout->addWidget(m_identityLabel, 4, 0);
    formLayout->addWidget(m_identityEdit, 4, 1);

    // Row 5: TLS Checkbox (below ID field)
    m_tlsCheckbox = new QCheckBox("Use TLS (Encrypted)", this);
    m_tlsCheckbox->setStyleSheet(QString("QCheckBox { color: %1; font-size: %2px; spacing: %3px; } "
                                         "QCheckBox::indicator { width: 14px; height: 14px; }")
                                     .arg(K4Styles::Colors::TextGray)
                                     .arg(K4Styles::Dimensions::FontSizeButton)
                                     .arg(K4Styles::Dimensions::BorderRadiusLarge));
    formLayout->addWidget(m_tlsCheckbox, 5, 0, 1, 2);

    // Row 6: Encode Mode dropdown
    auto *encodeModeLabel = new QLabel("Audio Mode", this);
    encodeModeLabel->setStyleSheet(labelStyle);
    m_encodeModeCombo = new QComboBox(this);
    m_encodeModeCombo->setStyleSheet(
        QString("QComboBox { "
                "  background-color: %1; "
                "  color: %2; "
                "  border: 1px solid %3; "
                "  border-radius: 4px; "
                "  padding: %4px; "
                "} "
                "QComboBox::drop-down { "
                "  border: none; "
                "  width: 20px; "
                "} "
                "QComboBox::down-arrow { "
                "  image: none; "
                "  border-left: 5px solid transparent; "
                "  border-right: 5px solid transparent; "
                "  border-top: 5px solid %2; "
                "} "
                "QComboBox QAbstractItemView { "
                "  background-color: %1; "
                "  color: %2; "
                "  selection-background-color: %5; "
                "}")
            .arg(K4Styles::Colors::DarkBackground, K4Styles::Colors::TextWhite, K4Styles::Colors::DialogBorder)
            .arg(K4Styles::Dimensions::PaddingSmall)
            .arg(K4Styles::Colors::AccentAmber));
    m_encodeModeCombo->addItem("EM3 - Opus Float", 3); // Default
    m_encodeModeCombo->addItem("EM2 - Opus Int", 2);
    m_encodeModeCombo->addItem("EM1 - RAW 16-bit", 1);
    m_encodeModeCombo->addItem("EM0 - RAW 32-bit", 0);
    m_encodeModeCombo->setCurrentIndex(0); // EM3 default
    formLayout->addWidget(encodeModeLabel, 6, 0);
    formLayout->addWidget(m_encodeModeCombo, 6, 1);

    // Row 7: Streaming Latency dropdown
    auto *streamingLatencyLabel = new QLabel("Streaming Latency", this);
    streamingLatencyLabel->setStyleSheet(labelStyle);
    m_streamingLatencyCombo = new QComboBox(this);
    m_streamingLatencyCombo->setStyleSheet(m_encodeModeCombo->styleSheet());
    for (int i = 0; i <= 7; i++) {
        m_streamingLatencyCombo->addItem(QString::number(i), i);
    }
    m_streamingLatencyCombo->setCurrentIndex(3); // Default: 3
    formLayout->addWidget(streamingLatencyLabel, 7, 0);
    formLayout->addWidget(m_streamingLatencyCombo, 7, 1);

    // Initially hide ID field (shown when TLS is checked)
    m_identityLabel->setVisible(false);
    m_identityEdit->setVisible(false);

    rightSection->addLayout(formLayout);
    rightSection->addStretch();
    topLayout->addLayout(rightSection);
    topLayout->addStretch();

    mainLayout->addLayout(topLayout);

    // === BOTTOM: Button Row ===
    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(16); // More spacing between buttons

    m_connectButton = new QPushButton("Connect", this);
    m_connectButton->setStyleSheet(K4Styles::dialogButton());
    buttonLayout->addWidget(m_connectButton);

    m_newButton = new QPushButton("Add", this);
    m_newButton->setStyleSheet(K4Styles::dialogButton());
    buttonLayout->addWidget(m_newButton);

    m_saveButton = new QPushButton("Save", this);
    m_saveButton->setStyleSheet(K4Styles::dialogButton());
    buttonLayout->addWidget(m_saveButton);

    m_deleteButton = new QPushButton("Delete", this);
    m_deleteButton->setStyleSheet(K4Styles::dialogButton());
    buttonLayout->addWidget(m_deleteButton);

    m_scanButton = new QPushButton("Scan", this);
    m_scanButton->setStyleSheet(K4Styles::dialogButton());
    m_scanButton->setToolTip("Scan LAN for K4 radios");
    connect(m_scanButton, &QPushButton::clicked, this, &RadioManagerDialog::startDiscovery);
    buttonLayout->addWidget(m_scanButton);

    // Back button - smaller with curved arrow
    m_backButton = new QPushButton(QString::fromUtf8("\xE2\x86\xA9"), this); // ↩ Curved arrow
    m_backButton->setStyleSheet(K4Styles::dialogButton());
    m_backButton->setFixedSize(K4Styles::Dimensions::ButtonHeightMedium, K4Styles::Dimensions::ButtonHeightMedium);
    m_backButton->setToolTip("Back / Exit");
    buttonLayout->addWidget(m_backButton);

    mainLayout->addLayout(buttonLayout);

    // Connections
    connect(m_connectButton, &QPushButton::clicked, this, &RadioManagerDialog::onConnectClicked);
    connect(m_newButton, &QPushButton::clicked, this, &RadioManagerDialog::onNewClicked);
    connect(m_saveButton, &QPushButton::clicked, this, &RadioManagerDialog::onSaveClicked);
    connect(m_deleteButton, &QPushButton::clicked, this, &RadioManagerDialog::onDeleteClicked);
    connect(m_backButton, &QPushButton::clicked, this, &RadioManagerDialog::onBackClicked);
    connect(m_radioList, &QListWidget::itemSelectionChanged, this, &RadioManagerDialog::onSelectionChanged);
    connect(m_radioList, &QListWidget::itemDoubleClicked, this, &RadioManagerDialog::onItemDoubleClicked);
    connect(m_tlsCheckbox, &QCheckBox::toggled, this, &RadioManagerDialog::onTlsCheckboxToggled);

    // Update button states when host field changes
    connect(m_hostEdit, &QLineEdit::textChanged, this, [this]() { updateButtonStates(); });
}

void RadioManagerDialog::refreshList() {
    m_radioList->clear();

    const auto radios = RadioSettings::instance()->radios();
    for (const auto &radio : radios) {
        m_radioList->addItem(radio.name.isEmpty() ? radio.host : radio.name);
    }

    // Re-add discovered (unconfigured) entries — prune any that were just saved
    QList<K4RadioInfo> stillUnconfigured;
    for (const auto &radio : m_discoveredRadios) {
        if (!isAlreadyConfigured(radio)) {
            stillUnconfigured.append(radio);
            addDiscoveredItem(radio);
        }
    }
    m_discoveredRadios = stillUnconfigured;

    int lastIndex = RadioSettings::instance()->lastSelectedIndex();
    if (lastIndex >= 0 && lastIndex < RadioSettings::instance()->radios().size()) {
        m_radioList->setCurrentRow(lastIndex);
        m_currentIndex = lastIndex;
        populateFieldsFromSelection();
    }

    updateButtonStates();
}

void RadioManagerDialog::onConnectClicked() {
    QString host = m_hostEdit->text().trimmed();
    if (!host.isEmpty()) {
        // Check if this is a disconnect request (selected radio is already connected)
        if (!m_connectedHost.isEmpty() && host == m_connectedHost) {
            emit disconnectRequested();
            accept();
            return;
        }

        RadioEntry entry;
        entry.name = m_nameEdit->text().trimmed();
        entry.host = host;
        entry.password = m_passwordEdit->text();
        QString portText = m_portEdit->text().trimmed();
        entry.useTls = m_tlsCheckbox->isChecked();
        entry.identity = m_identityEdit->text();
        entry.encodeMode = m_encodeModeCombo->currentData().toInt();
        entry.streamingLatency = m_streamingLatencyCombo->currentData().toInt();

        // Set port based on TLS mode if not specified
        if (portText.isEmpty()) {
            entry.port = entry.useTls ? K4Protocol::TLS_PORT : K4Protocol::DEFAULT_PORT;
        } else {
            entry.port = portText.toUShort();
        }

        if (m_currentIndex >= 0) {
            RadioSettings::instance()->setLastSelectedIndex(m_currentIndex);
        }
        emit connectRequested(entry);
        accept();
    }
}

void RadioManagerDialog::onNewClicked() {
    m_currentIndex = -1;
    clearFields();
    m_radioList->blockSignals(true);
    m_radioList->clearSelection();
    m_radioList->setCurrentRow(-1);
    m_radioList->blockSignals(false);
    m_nameEdit->setFocus();
    updateButtonStates();
}

void RadioManagerDialog::onSaveClicked() {
    QString name = m_nameEdit->text().trimmed();
    QString host = m_hostEdit->text().trimmed();
    QString password = m_passwordEdit->text();
    QString portText = m_portEdit->text().trimmed();
    bool useTls = m_tlsCheckbox->isChecked();
    QString identity = m_identityEdit->text();

    if (name.isEmpty()) {
        name = host; // Use host as name if no name provided
    }

    if (host.isEmpty()) {
        return; // Can't save without host
    }

    RadioEntry entry;
    entry.name = name;
    entry.host = host;
    entry.password = password;
    entry.useTls = useTls;
    entry.identity = identity;
    entry.encodeMode = m_encodeModeCombo->currentData().toInt();
    entry.streamingLatency = m_streamingLatencyCombo->currentData().toInt();

    // Set port based on TLS mode if not specified
    if (portText.isEmpty()) {
        entry.port = useTls ? K4Protocol::TLS_PORT : K4Protocol::DEFAULT_PORT;
    } else {
        entry.port = portText.toUShort();
    }

    if (m_currentIndex >= 0 && m_currentIndex < RadioSettings::instance()->radios().size()) {
        // Update existing
        RadioSettings::instance()->updateRadio(m_currentIndex, entry);
    } else {
        // Add new
        RadioSettings::instance()->addRadio(entry);
        m_currentIndex = RadioSettings::instance()->radios().size() - 1;
    }

    // Re-select the saved item
    if (m_currentIndex >= 0 && m_currentIndex < m_radioList->count()) {
        m_radioList->setCurrentRow(m_currentIndex);
    }
    updateButtonStates();
}

void RadioManagerDialog::onDeleteClicked() {
    if (m_currentIndex >= 0 && m_currentIndex < RadioSettings::instance()->radios().size()) {
        RadioSettings::instance()->removeRadio(m_currentIndex);
        clearFields();
        m_currentIndex = -1;
    }
    updateButtonStates();
}

void RadioManagerDialog::onBackClicked() {
    reject();
}

void RadioManagerDialog::onSelectionChanged() {
    int row = m_radioList->currentRow();
    if (row < 0) {
        updateButtonStates();
        return;
    }

    QListWidgetItem *item = m_radioList->item(row);
    if (item && item->data(Qt::UserRole).toString() == QStringLiteral("discovered")) {
        // Discovered but unconfigured entry — look up by IP stored in the item
        QString ip = item->data(Qt::UserRole + 1).toString();
        const K4RadioInfo *foundRadio = nullptr;
        for (const auto &r : m_discoveredRadios) {
            if (r.ipAddress == ip) {
                foundRadio = &r;
                break;
            }
        }
        if (foundRadio) {
            const K4RadioInfo &radio = *foundRadio;
            m_currentIndex = -1;                              // Not a saved entry
            m_nameEdit->setText(radio.hostname().chopped(6)); // Strip ".local"
            m_hostEdit->setText(radio.ipAddress);
            m_portEdit->setText(QString::number(K4Protocol::TLS_PORT));
            m_passwordEdit->clear();
            m_tlsCheckbox->setChecked(true);
            m_identityEdit->clear();
            m_identityLabel->setVisible(true);
            m_identityEdit->setVisible(true);
            m_encodeModeCombo->setCurrentIndex(0);       // EM3 default
            m_streamingLatencyCombo->setCurrentIndex(3); // SL3 default
        }
    } else {
        m_currentIndex = row;
        populateFieldsFromSelection();
    }
    updateButtonStates();
}

void RadioManagerDialog::onItemDoubleClicked(QListWidgetItem *item) {
    // Don't allow double-click connect on discovered (unconfigured) entries
    if (item && item->data(Qt::UserRole).toString() == QStringLiteral("discovered"))
        return;
    onConnectClicked();
}

void RadioManagerDialog::updateButtonStates() {
    bool isSavedEntry = m_currentIndex >= 0 && m_currentIndex < RadioSettings::instance()->radios().size();
    QString host = m_hostEdit->text().trimmed();
    bool hasHost = !host.isEmpty();

    // Check if the selected radio is the connected one
    bool isConnectedRadio = !m_connectedHost.isEmpty() && host == m_connectedHost;

    // Check if currently viewing a discovered (unconfigured) entry
    int row = m_radioList->currentRow();
    QListWidgetItem *currentItem = (row >= 0) ? m_radioList->item(row) : nullptr;
    bool isDiscoveredEntry = currentItem && currentItem->data(Qt::UserRole).toString() == QStringLiteral("discovered");

    // Connect disabled for discovered entries — user must Save first
    m_connectButton->setEnabled(hasHost && !isDiscoveredEntry);
    m_connectButton->setText(isConnectedRadio ? "Disconnect" : "Connect");
    m_deleteButton->setEnabled(isSavedEntry);
    m_saveButton->setEnabled(hasHost);
}

void RadioManagerDialog::clearFields() {
    m_nameEdit->clear();
    m_hostEdit->clear();
    m_portEdit->clear();
    m_passwordEdit->clear();
    m_tlsCheckbox->setChecked(false);
    m_identityEdit->clear();
    m_identityLabel->setVisible(false);
    m_identityEdit->setVisible(false);
    m_encodeModeCombo->setCurrentIndex(0);       // Reset to EM3 (default)
    m_streamingLatencyCombo->setCurrentIndex(3); // Reset to SL3 (default)
}

void RadioManagerDialog::populateFieldsFromSelection() {
    if (m_currentIndex >= 0 && m_currentIndex < RadioSettings::instance()->radios().size()) {
        const RadioEntry &radio = RadioSettings::instance()->radios().at(m_currentIndex);
        m_nameEdit->setText(radio.name);
        m_hostEdit->setText(radio.host);
        m_portEdit->setText(QString::number(radio.port));
        m_passwordEdit->setText(radio.password);
        m_tlsCheckbox->setChecked(radio.useTls);
        m_identityEdit->setText(radio.identity);
        m_identityLabel->setVisible(radio.useTls);
        m_identityEdit->setVisible(radio.useTls);
        // Set encode mode combo to match saved value
        int encodeModeIndex = m_encodeModeCombo->findData(radio.encodeMode);
        if (encodeModeIndex >= 0) {
            m_encodeModeCombo->setCurrentIndex(encodeModeIndex);
        }
        // Set streaming latency combo to match saved value
        int latencyIndex = m_streamingLatencyCombo->findData(radio.streamingLatency);
        if (latencyIndex >= 0) {
            m_streamingLatencyCombo->setCurrentIndex(latencyIndex);
        }
    }
}

void RadioManagerDialog::onTlsCheckboxToggled(bool checked) {
    // Show/hide ID field based on TLS checkbox state
    m_identityLabel->setVisible(checked);
    m_identityEdit->setVisible(checked);

    // Auto-update port if it was at default
    QString portText = m_portEdit->text().trimmed();
    if (portText.isEmpty() || portText == QString::number(K4Protocol::DEFAULT_PORT) ||
        portText == QString::number(K4Protocol::TLS_PORT)) {
        m_portEdit->setText(QString::number(checked ? K4Protocol::TLS_PORT : K4Protocol::DEFAULT_PORT));
    }
}

RadioEntry RadioManagerDialog::selectedRadio() const {
    if (m_currentIndex >= 0) {
        return RadioSettings::instance()->radios().at(m_currentIndex);
    }
    return RadioEntry();
}

bool RadioManagerDialog::hasSelection() const {
    return m_currentIndex >= 0;
}

void RadioManagerDialog::setConnectedHost(const QString &host) {
    m_connectedHost = host;
    updateButtonStates();
}

void RadioManagerDialog::startDiscovery() {
    if (m_discovery)
        return; // Already scanning
    m_discovery = new K4Discovery(this);
    connect(m_discovery, &K4Discovery::radioFound, this, &RadioManagerDialog::onRadioFound);
    connect(m_discovery, &K4Discovery::discoveryFinished, this, &RadioManagerDialog::onDiscoveryFinished);
    m_scanButton->setEnabled(false);

    // Start countdown display on the button
    m_scanSecondsLeft = K4Discovery::TIMEOUT_MS / 1000;
    m_scanButton->setText(QString("Scanning (%1s)").arg(m_scanSecondsLeft));
    if (!m_scanCountdown) {
        m_scanCountdown = new QTimer(this);
        m_scanCountdown->setInterval(1000);
        connect(m_scanCountdown, &QTimer::timeout, this, [this]() {
            if (--m_scanSecondsLeft > 0) {
                m_scanButton->setText(QString("Scanning (%1s)").arg(m_scanSecondsLeft));
            } else {
                m_scanCountdown->stop();
            }
        });
    }
    m_scanCountdown->start();

    m_discovery->startDiscovery();
}

bool RadioManagerDialog::isAlreadyConfigured(const K4RadioInfo &radio) const {
    const auto radios = RadioSettings::instance()->radios();
    for (const auto &entry : radios) {
        // Match by IP address or hostname
        if (entry.host == radio.ipAddress || entry.host == radio.hostname())
            return true;
    }
    return false;
}

void RadioManagerDialog::addDiscoveredItem(const K4RadioInfo &radio) {
    bool isK4Z = radio.isK4Zero();
    QString label = isK4Z ? QString("%1 (info only)").arg(radio.hostname()) : radio.hostname();
    auto *item = new QListWidgetItem(label);
    QFont font = item->font();
    font.setItalic(true);
    item->setFont(font);
    item->setData(Qt::UserRole, QStringLiteral("discovered"));
    item->setData(Qt::UserRole + 1, radio.ipAddress);
    if (isK4Z) {
        // K4Z (K4/0) is not supported by QK4 — show grayed out and unselectable
        item->setFlags(item->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsEnabled));
        item->setForeground(QColor(K4Styles::Colors::TextGray));
    }
    m_radioList->addItem(item);
}

void RadioManagerDialog::onRadioFound(const K4RadioInfo &radio) {
    // Skip if already in the configured server list
    if (isAlreadyConfigured(radio))
        return;

    // Skip if already in our discovered list
    for (const auto &existing : m_discoveredRadios) {
        if (existing.ipAddress == radio.ipAddress)
            return;
    }

    m_discoveredRadios.append(radio);
    addDiscoveredItem(radio);
}

void RadioManagerDialog::onDiscoveryFinished(int count) {
    Q_UNUSED(count)
    m_discovery->deleteLater();
    m_discovery = nullptr;
    if (m_scanCountdown)
        m_scanCountdown->stop();
    m_scanButton->setEnabled(true);
    m_scanButton->setText("Scan");
}
