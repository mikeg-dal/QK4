#include "dxclusterpage.h"
#include "../controllers/dxclustercontroller.h"
#include "../settings/radiosettings.h"
#include "k4styles.h"
#include <QFrame>
#include <QHBoxLayout>
#include <QTimer>
#include <QVBoxLayout>

DxClusterPage::DxClusterPage(DxClusterController *controller, QWidget *parent)
    : QWidget(parent), m_controller(controller) {
    setStyleSheet(K4Styles::Dialog::pageBackground());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin,
                               K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin);
    layout->setSpacing(K4Styles::Dimensions::PaddingLarge);

    // === Status row ===
    auto *statusLayout = new QHBoxLayout();
    auto *statusTitleLabel = new QLabel("Status:", this);
    statusTitleLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    statusTitleLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);
    m_statusLabel = new QLabel("Disconnected", this);
    m_statusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::TextGray));
    statusLayout->addWidget(statusTitleLabel);
    statusLayout->addWidget(m_statusLabel);
    statusLayout->addStretch();
    layout->addLayout(statusLayout);

    // === Separator ===
    auto *line1 = new QFrame(this);
    line1->setFrameShape(QFrame::HLine);
    line1->setStyleSheet(K4Styles::Dialog::separator());
    line1->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line1);

    // === Cluster Servers section ===
    auto *sectionLabel = new QLabel("Cluster Servers", this);
    sectionLabel->setStyleSheet(K4Styles::Dialog::sectionHeader());
    layout->addWidget(sectionLabel);

    // Split layout: cluster list (left) + form (right)
    auto *splitLayout = new QHBoxLayout();
    splitLayout->setSpacing(K4Styles::Dimensions::DialogMargin);

    // ── Left column: cluster list + New/Remove buttons ──
    auto *leftLayout = new QVBoxLayout();
    m_clusterList = new QListWidget(this);
    m_clusterList->setStyleSheet(
        QString("QListWidget { background-color: %1; color: %2; border: 1px solid %3; font-size: %4px; }"
                "QListWidget::item { padding: 6px 8px; }"
                "QListWidget::item:selected { background-color: %5; color: %1; }")
            .arg(K4Styles::Colors::DarkBackground, K4Styles::Colors::TextWhite, K4Styles::Colors::DialogBorder)
            .arg(K4Styles::Dimensions::FontSizeMedium)
            .arg(K4Styles::Colors::AccentAmber));
    m_clusterList->setMinimumHeight(100);
    auto loadEntry = [this]() {
        int row = m_clusterList->currentRow();
        if (row >= 0) {
            m_addMode = false;
            loadSelectedEntry();
            updateFormState();
            updateStatus();
            switchConsole(row);
        }
    };
    connect(m_clusterList, &QListWidget::currentRowChanged, this, loadEntry);
    connect(m_clusterList, &QListWidget::itemClicked, this, loadEntry);
    leftLayout->addWidget(m_clusterList, 1);

    auto *listBtnLayout = new QHBoxLayout();
    m_newBtn = new QPushButton("+ New", this);
    m_newBtn->setStyleSheet(K4Styles::Dialog::actionButtonSmall());
    m_newBtn->setCursor(Qt::PointingHandCursor);
    connect(m_newBtn, &QPushButton::clicked, this, &DxClusterPage::enterAddMode);

    m_removeBtn = new QPushButton("- Remove", this);
    m_removeBtn->setStyleSheet(K4Styles::Dialog::actionButtonSmall());
    m_removeBtn->setCursor(Qt::PointingHandCursor);
    connect(m_removeBtn, &QPushButton::clicked, this, &DxClusterPage::removeEntry);

    listBtnLayout->addWidget(m_newBtn);
    listBtnLayout->addWidget(m_removeBtn);
    listBtnLayout->addStretch();
    leftLayout->addLayout(listBtnLayout);
    splitLayout->addLayout(leftLayout, 2);

    // ── Right column: edit form ──
    auto *formLayout = new QVBoxLayout();
    formLayout->setSpacing(K4Styles::Dimensions::PaddingMedium);

    m_formTitleLabel = new QLabel("Edit Cluster", this);
    m_formTitleLabel->setStyleSheet(K4Styles::Dialog::sectionHeader());
    formLayout->addWidget(m_formTitleLabel);

    // Host
    auto *hostLayout = new QHBoxLayout();
    auto *hostLabel = new QLabel("Host:", this);
    hostLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    hostLabel->setFixedWidth(50);
    m_hostEdit = new QLineEdit(this);
    m_hostEdit->setStyleSheet(K4Styles::Dialog::lineEdit());
    m_hostEdit->setPlaceholderText("hostname or IP address");
    hostLayout->addWidget(hostLabel);
    hostLayout->addWidget(m_hostEdit, 1);
    formLayout->addLayout(hostLayout);

    // Port
    auto *portLayout = new QHBoxLayout();
    auto *portLabel = new QLabel("Port:", this);
    portLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    portLabel->setFixedWidth(50);
    m_portEdit = new QLineEdit(this);
    m_portEdit->setStyleSheet(K4Styles::Dialog::lineEdit());
    m_portEdit->setFixedWidth(K4Styles::Dimensions::InputFieldWidthSmall);
    m_portEdit->setPlaceholderText("7000");
    portLayout->addWidget(portLabel);
    portLayout->addWidget(m_portEdit);
    portLayout->addStretch();
    formLayout->addLayout(portLayout);

    // Auto-connect checkbox
    m_autoConnectCheck = new QCheckBox("Auto-connect on startup", this);
    m_autoConnectCheck->setStyleSheet(K4Styles::Dialog::checkBox());
    connect(m_autoConnectCheck, &QCheckBox::toggled, this, [this](bool checked) {
        int row = m_clusterList->currentRow();
        if (row < 0 || m_addMode)
            return;
        auto clusters = RadioSettings::instance()->dxClusters();
        if (row >= clusters.size())
            return;
        DxClusterEntry entry = clusters[row];
        entry.autoConnect = checked;
        RadioSettings::instance()->updateDxCluster(row, entry);
    });
    formLayout->addWidget(m_autoConnectCheck);

    // Save + Connect/Disconnect buttons
    auto *btnLayout = new QHBoxLayout();
    m_saveBtn = new QPushButton("Save", this);
    m_saveBtn->setStyleSheet(K4Styles::Dialog::actionButtonSmall());
    m_saveBtn->setCursor(Qt::PointingHandCursor);
    connect(m_saveBtn, &QPushButton::clicked, this, &DxClusterPage::saveEntry);

    m_connectBtn = new QPushButton("Connect", this);
    m_connectBtn->setStyleSheet(K4Styles::Dialog::actionButtonSmall());
    m_connectBtn->setCursor(Qt::PointingHandCursor);
    connect(m_connectBtn, &QPushButton::clicked, this, [this]() {
        int row = m_clusterList->currentRow();
        if (row < 0)
            return;
        auto clusters = RadioSettings::instance()->dxClusters();
        if (row >= clusters.size())
            return;
        QString callsign = RadioSettings::instance()->dxClusterCallsign();
        if (callsign.isEmpty()) {
            m_statusLabel->setText("Set your callsign in Cluster Settings below");
            m_statusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::ErrorRed));
            return;
        }
        m_controller->connectCluster(row, clusters[row].host, clusters[row].port, callsign);
    });

    m_disconnectBtn = new QPushButton("Disconnect", this);
    m_disconnectBtn->setStyleSheet(K4Styles::Dialog::actionButtonSmall());
    m_disconnectBtn->setCursor(Qt::PointingHandCursor);
    connect(m_disconnectBtn, &QPushButton::clicked, this, [this]() {
        int row = m_clusterList->currentRow();
        if (row >= 0)
            m_controller->disconnectCluster(row);
    });

    btnLayout->addWidget(m_saveBtn);
    btnLayout->addWidget(m_connectBtn);
    btnLayout->addWidget(m_disconnectBtn);
    btnLayout->addStretch();
    formLayout->addLayout(btnLayout);

    formLayout->addStretch();
    splitLayout->addLayout(formLayout, 3);
    layout->addLayout(splitLayout);

    // === Separator ===
    auto *line2 = new QFrame(this);
    line2->setFrameShape(QFrame::HLine);
    line2->setStyleSheet(K4Styles::Dialog::separator());
    line2->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line2);

    // === Cluster Settings section ===
    auto *settingsLabel = new QLabel("Cluster Settings", this);
    settingsLabel->setStyleSheet(K4Styles::Dialog::sectionHeader());
    layout->addWidget(settingsLabel);

    // Callsign (global)
    auto *callLayout = new QHBoxLayout();
    auto *callLabel = new QLabel("Callsign:", this);
    callLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    callLabel->setFixedWidth(70);
    m_callsignEdit = new QLineEdit(this);
    m_callsignEdit->setStyleSheet(K4Styles::Dialog::lineEdit());
    m_callsignEdit->setPlaceholderText("Your callsign (required)");
    m_callsignEdit->setFixedWidth(140);
    m_callsignEdit->setText(RadioSettings::instance()->dxClusterCallsign());
    connect(m_callsignEdit, &QLineEdit::editingFinished, this,
            [this]() { RadioSettings::instance()->setDxClusterCallsign(m_callsignEdit->text()); });
    callLayout->addWidget(callLabel);
    callLayout->addWidget(m_callsignEdit);
    callLayout->addStretch();
    layout->addLayout(callLayout);

    // Spot age slider
    auto *ageLayout = new QHBoxLayout();
    auto *ageLabel = new QLabel("Spot age:", this);
    ageLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    ageLabel->setFixedWidth(70);
    m_ageSlider = new QSlider(Qt::Horizontal, this);
    m_ageSlider->setRange(5, 30);
    m_ageSlider->setSingleStep(1);
    m_ageSlider->setValue(RadioSettings::instance()->dxClusterSpotAge() / 60);
    m_ageSlider->setStyleSheet(
        K4Styles::sliderHorizontal(K4Styles::Colors::DarkBackground, K4Styles::Colors::AccentAmber));
    m_ageValueLabel = new QLabel(QString("%1 min").arg(RadioSettings::instance()->dxClusterSpotAge() / 60), this);
    m_ageValueLabel->setStyleSheet(K4Styles::Dialog::formValue());
    connect(m_ageSlider, &QSlider::valueChanged, this, [this](int value) {
        RadioSettings::instance()->setDxClusterSpotAge(value * 60);
        m_controller->setSpotMaxAge(value * 60);
        m_ageValueLabel->setText(QString("%1 min").arg(value));
    });
    ageLayout->addWidget(ageLabel);
    ageLayout->addWidget(m_ageSlider, 1);
    ageLayout->addWidget(m_ageValueLabel);
    layout->addLayout(ageLayout);

    // === Separator ===
    auto *line3 = new QFrame(this);
    line3->setFrameShape(QFrame::HLine);
    line3->setStyleSheet(K4Styles::Dialog::separator());
    line3->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line3);

    // === Console section ===
    auto *consoleLabel = new QLabel("Console", this);
    consoleLabel->setStyleSheet(K4Styles::Dialog::sectionHeader());
    layout->addWidget(consoleLabel);

    m_consoleOutput = new QPlainTextEdit(this);
    m_consoleOutput->setReadOnly(true);
    m_consoleOutput->setFocusPolicy(Qt::NoFocus);
    m_consoleOutput->setMaximumBlockCount(500);
    m_consoleOutput->setStyleSheet(
        QString("QPlainTextEdit { background-color: %1; color: %2; border: 1px solid %3; "
                "font-family: 'Menlo', 'Consolas', monospace; font-size: %4px; }")
            .arg(K4Styles::Colors::DarkBackground, K4Styles::Colors::TextWhite, K4Styles::Colors::DialogBorder)
            .arg(K4Styles::Dimensions::FontSizeSmall));
    m_consoleOutput->setMinimumHeight(80);
    m_consoleOutput->setMaximumHeight(200);
    layout->addWidget(m_consoleOutput, 1);

    // Command input
    auto *inputLayout = new QHBoxLayout();
    m_consoleInput = new QLineEdit(this);
    m_consoleInput->setStyleSheet(K4Styles::Dialog::lineEdit());
    m_consoleInput->setPlaceholderText("Send command (e.g., set/dx filter skimmer)");
    connect(m_consoleInput, &QLineEdit::returnPressed, this, [this]() {
        QString cmd = m_consoleInput->text().trimmed();
        int row = m_clusterList->currentRow();
        if (!cmd.isEmpty() && m_controller && row >= 0) {
            m_controller->sendCommand(row, cmd);
            m_consoleOutput->appendPlainText("> " + cmd);
            m_consoleInput->clear();
        }
        QTimer::singleShot(0, m_consoleInput, [this]() { m_consoleInput->setFocus(); });
    });
    inputLayout->addWidget(m_consoleInput, 1);
    layout->addLayout(inputLayout);

    // Wire controller signals
    if (m_controller) {
        // Per-cluster state changes — update list indicators and status
        connect(m_controller, &DxClusterController::clusterStateChanged, this,
                [this](int, DxClusterClient::ConnectionState) {
                    updateListIndicators();
                    updateStatus();
                });
        connect(m_controller, &DxClusterController::clusterError, this, [this](int index, const QString &error) {
            if (index == m_clusterList->currentRow()) {
                m_statusLabel->setText("Error: " + error);
                m_statusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::ErrorRed));
            }
        });
        // Per-cluster console lines — only append if this cluster is selected
        connect(m_controller, &DxClusterController::clusterLineReceived, this, [this](int index, const QString &line) {
            if (index == m_clusterList->currentRow())
                m_consoleOutput->appendPlainText(line);
        });
    }

    // Initialize
    populateClusterList();
    updateStatus();
    updateFormState();
}

void DxClusterPage::refresh() {
    populateClusterList();
    updateStatus();
    updateFormState();
}

void DxClusterPage::populateClusterList() {
    int prevRow = m_clusterList->currentRow();
    m_clusterList->clear();

    auto clusters = RadioSettings::instance()->dxClusters();
    for (const auto &entry : clusters) {
        m_clusterList->addItem(QString("%1:%2").arg(entry.host).arg(entry.port));
    }

    if (prevRow >= 0 && prevRow < m_clusterList->count())
        m_clusterList->setCurrentRow(prevRow);
    else if (m_clusterList->count() > 0)
        m_clusterList->setCurrentRow(0);

    updateListIndicators();
}

void DxClusterPage::updateListIndicators() {
    if (!m_controller)
        return;
    for (int i = 0; i < m_clusterList->count(); ++i) {
        auto *item = m_clusterList->item(i);
        auto state = m_controller->clusterState(i);
        if (state == DxClusterClient::Connected) {
            item->setForeground(QColor(K4Styles::Colors::StatusGreen));
        } else if (state == DxClusterClient::Connecting) {
            item->setForeground(QColor(K4Styles::Colors::AccentAmber));
        } else {
            item->setForeground(QColor(K4Styles::Colors::TextWhite));
        }
    }
}

void DxClusterPage::switchConsole(int index) {
    if (!m_controller)
        return;
    m_consoleOutput->clear();
    auto lines = m_controller->consoleBuffer(index);
    for (const auto &line : lines)
        m_consoleOutput->appendPlainText(line);
}

void DxClusterPage::loadSelectedEntry() {
    m_addMode = false;
    int row = m_clusterList->currentRow();
    auto clusters = RadioSettings::instance()->dxClusters();
    if (row < 0 || row >= clusters.size()) {
        m_hostEdit->clear();
        m_portEdit->clear();
        m_autoConnectCheck->setChecked(false);
        return;
    }

    const auto &entry = clusters[row];
    m_hostEdit->setText(entry.host);
    m_portEdit->setText(QString::number(entry.port));
    m_autoConnectCheck->setChecked(entry.autoConnect);
}

void DxClusterPage::enterAddMode() {
    m_addMode = true;
    m_clusterList->clearSelection();
    m_hostEdit->clear();
    m_portEdit->setText("7000");
    m_autoConnectCheck->setChecked(false);
    m_hostEdit->setFocus();
    updateFormState();
}

void DxClusterPage::saveEntry() {
    QString host = m_hostEdit->text().trimmed();
    if (host.isEmpty())
        return;

    DxClusterEntry entry;
    entry.host = host;

    bool ok;
    quint16 port = m_portEdit->text().toUShort(&ok);
    entry.port = (ok && port > 0) ? port : 7000;
    entry.autoConnect = m_autoConnectCheck->isChecked();

    if (m_addMode) {
        RadioSettings::instance()->addDxCluster(entry);
        m_addMode = false;
        populateClusterList();
        m_clusterList->setCurrentRow(m_clusterList->count() - 1);
    } else {
        int row = m_clusterList->currentRow();
        if (row >= 0) {
            RadioSettings::instance()->updateDxCluster(row, entry);
            populateClusterList();
        }
    }
    updateFormState();
}

void DxClusterPage::removeEntry() {
    int row = m_clusterList->currentRow();
    if (row >= 0) {
        RadioSettings::instance()->removeDxCluster(row);
        populateClusterList();
        updateFormState();
    }
}

void DxClusterPage::updateStatus() {
    if (!m_controller || !m_statusLabel)
        return;

    // Aggregate status: count how many clusters are connected
    int connectedCount = 0;
    int totalCount = m_clusterList->count();
    for (int i = 0; i < totalCount; ++i) {
        if (m_controller->clusterState(i) == DxClusterClient::Connected)
            ++connectedCount;
    }

    if (connectedCount == 0) {
        m_statusLabel->setText("Disconnected");
        m_statusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::TextGray));
    } else if (connectedCount == 1) {
        m_statusLabel->setText("1 cluster connected");
        m_statusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::StatusGreen));
    } else {
        m_statusLabel->setText(QString("%1 clusters connected").arg(connectedCount));
        m_statusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::StatusGreen));
    }

    // Per-cluster button state based on selected entry
    int row = m_clusterList->currentRow();
    auto state = (row >= 0) ? m_controller->clusterState(row) : DxClusterClient::Disconnected;
    bool selectedConnected = (state == DxClusterClient::Connected);
    m_connectBtn->setEnabled(!selectedConnected && !m_addMode && row >= 0);
    m_disconnectBtn->setEnabled(selectedConnected);
}

void DxClusterPage::updateFormState() {
    bool hasSelection = m_clusterList->currentRow() >= 0;

    if (m_addMode) {
        m_formTitleLabel->setText("New Cluster");
        m_saveBtn->setText("Add");
        m_removeBtn->setEnabled(false);
        m_connectBtn->setEnabled(false);
    } else if (hasSelection) {
        m_formTitleLabel->setText("Edit Cluster");
        m_saveBtn->setText("Save");
        m_removeBtn->setEnabled(true);
        int row = m_clusterList->currentRow();
        bool connected = m_controller && m_controller->clusterState(row) == DxClusterClient::Connected;
        m_connectBtn->setEnabled(!connected);
    } else {
        m_formTitleLabel->setText("No Cluster Selected");
        m_saveBtn->setText("Save");
        m_saveBtn->setEnabled(false);
        m_removeBtn->setEnabled(false);
        m_connectBtn->setEnabled(false);
        return;
    }
    m_saveBtn->setEnabled(true);
}
