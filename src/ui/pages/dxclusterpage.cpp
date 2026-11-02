#include "ui/pages/dxclusterpage.h"
#include "controllers/dxclustercontroller.h"
#include "settings/radiosettings.h"
#include "ui/styling/k4styles.h"
#include "ui/pages/optionspagelayout.h"
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

DxClusterPage::DxClusterPage(DxClusterController *controller, QWidget *parent)
    : QWidget(parent), m_controller(controller) {
    namespace D = K4Styles::Dimensions;
    auto *layout = OptionsPageLayout::createPage(this);
    OptionsPageLayout::addTitle(layout, this, "DX Cluster");

    auto *statusForm = OptionsPageLayout::addForm(layout);
    m_statusLabel = new QLabel("Disconnected", this);
    m_statusLabel->setStyleSheet(K4Styles::Dialog::statusLabelByState());
    K4Styles::setStyleState(m_statusLabel, K4Styles::StateProperty, K4Styles::Dialog::StatusIdle);
    OptionsPageLayout::addRow(statusForm, this, "Status", m_statusLabel);

    // === Servers: list (left) + the selected entry (right) ===
    auto *splitLayout = new QHBoxLayout();
    splitLayout->setSpacing(D::OptionsLabelGap);

    auto *leftLayout = new QVBoxLayout();
    leftLayout->setSpacing(D::OptionsRowSpacing);
    m_clusterList = new QListWidget(this);
    m_clusterList->setStyleSheet(
        QString("QListWidget { background-color: %1; color: %2; border: 1px solid %3; font-size: %4px; }"
                "QListWidget::item { padding: 4px 6px; }"
                "QListWidget::item:selected { background-color: %5; color: %1; }")
            .arg(K4Styles::Colors::DarkBackground, K4Styles::Colors::TextPrimary, K4Styles::Colors::DialogBorder)
            .arg(D::FontSizeReadout)
            .arg(K4Styles::Colors::AccentAmber));
    // WHY Ignored vertically: a list view asks for 192 px whatever it holds. The row is as tall as the
    // entry form beside it, and the list fills that with a floor of three entries.
    m_clusterList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    {
        QFont listFont = m_clusterList->font();
        listFont.setPixelSize(D::FontSizeReadout);
        m_clusterList->setMinimumHeight(3 * (QFontMetrics(listFont).height() + 2 * D::OptionsRowSpacing));
    }
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
    // WHY: add mode clears the selection but not the current row, so clicking that row again
    // raises no currentRowChanged. Only that click needs loading here; any other has been loaded.
    connect(m_clusterList, &QListWidget::itemClicked, this, [this, loadEntry]() {
        if (m_addMode)
            loadEntry();
    });
    leftLayout->addWidget(m_clusterList, 1);

    auto *listBtnLayout = new QHBoxLayout();
    listBtnLayout->setSpacing(D::OptionsRowSpacing);
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

    auto *formColumn = new QVBoxLayout();
    formColumn->setSpacing(D::OptionsRowSpacing);
    m_formTitleLabel = new QLabel("Edit Cluster", this);
    m_formTitleLabel->setStyleSheet(K4Styles::Dialog::sectionHeader());
    formColumn->addWidget(m_formTitleLabel);

    auto *entryForm = new QFormLayout();
    OptionsPageLayout::configureForm(entryForm);
    m_hostEdit = new QLineEdit(this);
    m_hostEdit->setStyleSheet(K4Styles::Dialog::lineEdit());
    m_hostEdit->setPlaceholderText("hostname or IP address");
    OptionsPageLayout::addRow(entryForm, this, "Host", m_hostEdit);
    m_portEdit = new QLineEdit(this);
    m_portEdit->setStyleSheet(K4Styles::Dialog::lineEdit());
    m_portEdit->setPlaceholderText("7000");
    OptionsPageLayout::capWidth(m_portEdit, D::OptionsShortFieldWidth);
    OptionsPageLayout::addRow(entryForm, this, "Port", m_portEdit);

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
    entryForm->addRow(QString(), m_autoConnectCheck);

    auto *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(D::OptionsRowSpacing);
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
            K4Styles::setStyleState(m_statusLabel, K4Styles::StateProperty, QString());
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
    entryForm->addRow(QString(), btnLayout);
    formColumn->addLayout(entryForm);
    formColumn->addStretch();
    splitLayout->addLayout(formColumn, 3);
    layout->addLayout(splitLayout);

    // === Settings shared by every cluster: two compact rows ===
    OptionsPageLayout::addSection(layout, this, "Cluster Settings");
    auto *settingsGrid = new QGridLayout();
    settingsGrid->setHorizontalSpacing(D::OptionsLabelGap);
    settingsGrid->setVerticalSpacing(D::OptionsRowSpacing);
    auto gridLabel = [this](const QString &text) {
        auto *label = new QLabel(text, this);
        label->setStyleSheet(K4Styles::Dialog::formLabel());
        return label;
    };

    m_callsignEdit = new QLineEdit(this);
    m_callsignEdit->setStyleSheet(K4Styles::Dialog::lineEdit());
    m_callsignEdit->setPlaceholderText("Required");
    m_callsignEdit->setText(RadioSettings::instance()->dxClusterCallsign());
    OptionsPageLayout::capWidth(m_callsignEdit, D::OptionsShortFieldWidth);
    connect(m_callsignEdit, &QLineEdit::editingFinished, this,
            [this]() { RadioSettings::instance()->setDxClusterCallsign(m_callsignEdit->text()); });

    m_spotFontSizeSpin = new QSpinBox(this);
    m_spotFontSizeSpin->setStyleSheet(K4Styles::Dialog::spinBox());
    m_spotFontSizeSpin->setRange(D::FontSizeSpotMin, D::FontSizeSpotMax);
    m_spotFontSizeSpin->setSingleStep(1);
    m_spotFontSizeSpin->setSuffix(" px");
    m_spotFontSizeSpin->setValue(RadioSettings::instance()->dxClusterSpotFontSize());
    connect(m_spotFontSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [](int value) { RadioSettings::instance()->setDxClusterSpotFontSize(value); });

    m_ageSlider = new QSlider(Qt::Horizontal, this);
    m_ageSlider->setRange(5, 30);
    m_ageSlider->setSingleStep(1);
    m_ageSlider->setValue(RadioSettings::instance()->dxClusterSpotAge() / 60);
    m_ageSlider->setStyleSheet(
        K4Styles::sliderHorizontal(K4Styles::Colors::DarkBackground, K4Styles::Colors::AccentAmber));
    m_ageValueLabel =
        OptionsPageLayout::sliderValue(QString("%1 min").arg(RadioSettings::instance()->dxClusterSpotAge() / 60), this);
    // WHY apply on release, not on every value: each set is a full settings save. Keyboard and
    // click steps arrive with the slider up and are applied at once.
    auto applySpotAge = [this]() {
        const int seconds = m_ageSlider->value() * 60;
        RadioSettings::instance()->setDxClusterSpotAge(seconds);
        m_controller->setSpotMaxAge(seconds);
    };
    connect(m_ageSlider, &QSlider::valueChanged, this, [this, applySpotAge](int value) {
        m_ageValueLabel->setText(QString("%1 min").arg(value));
        if (!m_ageSlider->isSliderDown())
            applySpotAge();
    });
    connect(m_ageSlider, &QSlider::sliderReleased, this, applySpotAge);

    settingsGrid->addWidget(gridLabel("Callsign"), 0, 0);
    settingsGrid->addWidget(m_callsignEdit, 0, 1);
    settingsGrid->addWidget(gridLabel("Spot label size"), 0, 2);
    settingsGrid->addWidget(m_spotFontSizeSpin, 0, 3, Qt::AlignLeft);
    settingsGrid->addWidget(gridLabel("Spot age"), 1, 0);
    settingsGrid->addWidget(OptionsPageLayout::sliderField(m_ageSlider, m_ageValueLabel, this), 1, 1, 1, 3);
    settingsGrid->setColumnStretch(3, 1);
    layout->addLayout(settingsGrid);

    // === Console: everything left over goes to it ===
    OptionsPageLayout::addSection(layout, this, "Console");
    m_consoleOutput = new QPlainTextEdit(this);
    m_consoleOutput->setReadOnly(true);
    m_consoleOutput->setFocusPolicy(Qt::NoFocus);
    m_consoleOutput->setMaximumBlockCount(500);
    // WHY no wrap: cluster lines are fixed-column (call, frequency, comment, then the time at the
    // right), and wrapping put the time alone on the next line. Long lines scroll sideways instead.
    m_consoleOutput->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_consoleOutput->setStyleSheet(
        QString("QPlainTextEdit { background-color: %1; color: %2; border: 1px solid %3; }")
            .arg(K4Styles::Colors::DarkBackground, K4Styles::Colors::TextPrimary, K4Styles::Colors::DialogBorder));
    // WHY the family list: the system fixed font is Courier New on Windows, far less legible at this
    // size than Consolas; Menlo (macOS) and Consolas (Windows) come first, the platform's own
    // monospace is the fallback.
    QFont consoleFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    consoleFont.setFamilies({QStringLiteral("Menlo"), QStringLiteral("Consolas"), consoleFont.family()});
    consoleFont.setPixelSize(D::FontSizeReadout);
    m_consoleOutput->setFont(consoleFont);
    m_consoleOutput->setMinimumHeight(D::OptionsConsoleMinHeight);
    m_consoleOutput->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(m_consoleOutput, 1);

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
        // WHY: defer setFocus() one event-loop iteration so Qt finishes processing the
        // current return-key event before we steal focus back — otherwise focus can land
        // on the wrong widget during the Enter keypress.
        QTimer::singleShot(0, m_consoleInput, [this]() { m_consoleInput->setFocus(); });
    });
    layout->addWidget(m_consoleInput);

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
                K4Styles::setStyleState(m_statusLabel, K4Styles::StateProperty, QString());
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
            item->setForeground(QColor(K4Styles::Colors::TextPrimary));
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
        // WHY first: the controller keys live connections by list row. Removing the entry shifts
        // every later row up, so their connections have to move with them (and this one closes).
        if (m_controller)
            m_controller->removeCluster(row);
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
        K4Styles::setStyleState(m_statusLabel, K4Styles::StateProperty, K4Styles::Dialog::StatusIdle);
    } else if (connectedCount == 1) {
        m_statusLabel->setText("1 cluster connected");
        K4Styles::setStyleState(m_statusLabel, K4Styles::StateProperty, K4Styles::Dialog::StatusOk);
    } else {
        m_statusLabel->setText(QString("%1 clusters connected").arg(connectedCount));
        K4Styles::setStyleState(m_statusLabel, K4Styles::StateProperty, K4Styles::Dialog::StatusOk);
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
