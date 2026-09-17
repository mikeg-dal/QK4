#include "kpa1500uicontroller.h"

#include "controllers/statusbarcontroller.h"
#include "network/kpa1500client.h"
#include "settings/radiosettings.h"
#include "ui/styling/k4constants.h"
#include "ui/widgets/kpa1500minipanel.h"
#include "ui/widgets/rightsidepanel.h"

#include <QLoggingCategory>
#include <QString>

Q_LOGGING_CATEGORY(qk4Kpa, "qk4.kpa1500")

KPA1500UiController::KPA1500UiController(StatusBarController *statusBar, RightSidePanel *rightSidePanel,
                                         QObject *parent)
    : QObject(parent), m_statusBar(statusBar), m_miniPanel(new Kpa1500MiniPanel(rightSidePanel)),
      m_client(new KPA1500Client(this)) {

    // Hand the mini panel to RightSidePanel for layout placement —
    // RightSidePanel takes Qt parent ownership.
    m_miniPanel->setVisible(false);
    rightSidePanel->embedKpa1500Panel(m_miniPanel);

    // === Connection lifecycle signals ===
    connect(m_client, &KPA1500Client::connected, this, &KPA1500UiController::onConnected);
    connect(m_client, &KPA1500Client::disconnected, this, &KPA1500UiController::onDisconnected);
    connect(m_client, &KPA1500Client::errorOccurred, this, &KPA1500UiController::onError);

    // === Amplifier telemetry → mini panel ===
    connect(m_client, &KPA1500Client::powerChanged, this, [this](double fwd, double ref, double) {
        m_miniPanel->setForwardPower(static_cast<float>(fwd));
        m_miniPanel->setReflectedPower(static_cast<float>(ref));
    });
    connect(m_client, &KPA1500Client::swrChanged, this,
            [this](double swr) { m_miniPanel->setSWR(static_cast<float>(swr)); });
    connect(m_client, &KPA1500Client::paTemperatureChanged, this,
            [this](double tempC) { m_miniPanel->setTemperature(static_cast<float>(tempC)); });
    connect(m_client, &KPA1500Client::operatingStateChanged, this, [this](KPA1500Client::OperatingState state) {
        m_miniPanel->setMode(state == KPA1500Client::StateOperate);
    });
    connect(m_client, &KPA1500Client::atuModeChanged, this,
            [this](bool modeInline) { m_miniPanel->setAtuMode(modeInline); });
    connect(m_client, &KPA1500Client::atuInlineChanged, this,
            [this](bool relayInline) { m_miniPanel->setAtuInline(relayInline); });
    connect(m_client, &KPA1500Client::antennaChanged, this,
            [this](int antenna) { m_miniPanel->setAntenna(antenna, m_client->antennaConnector(antenna)); });
    connect(m_client, &KPA1500Client::antennaConnectorChanged, this,
            [this](int connector) { m_miniPanel->setAntenna(m_client->antenna(), connector); });
    connect(m_client, &KPA1500Client::bandNumberChanged, this,
            [this](int) { m_miniPanel->setBand(m_client->bandName()); });
    connect(m_client, &KPA1500Client::faultStatusChanged, this,
            [this](KPA1500Client::FaultStatus status, const QString &) {
                m_miniPanel->setFault(status == KPA1500Client::FaultActive);
            });
    connect(m_client, &KPA1500Client::connected, this, [this]() {
        m_miniPanel->setConnected(true);
        m_miniPanel->setVisible(true);
    });
    connect(m_client, &KPA1500Client::disconnected, this, [this]() {
        m_miniPanel->setConnected(false);
        m_miniPanel->setVisible(false);
    });

    // === Mini panel button signals → amplifier commands ===
    connect(m_miniPanel, &Kpa1500MiniPanel::modeToggled, this,
            [this](bool operate) { m_client->sendCommand(operate ? "^OS1;" : "^OS0;"); });
    connect(m_miniPanel, &Kpa1500MiniPanel::atuTuneRequested, this, [this]() { m_client->sendCommand("^FT;"); });
    connect(m_miniPanel, &Kpa1500MiniPanel::atuModeToggled, this,
            [this](bool in) { m_client->sendCommand(in ? "^AMI;" : "^AMB;"); });
    connect(m_miniPanel, &Kpa1500MiniPanel::nextAntennaRequested, this, [this]() { m_client->selectNextAntenna(); });

    // === RadioSettings observers ===
    connect(RadioSettings::instance(), &RadioSettings::kpa1500EnabledChanged, this,
            &KPA1500UiController::onEnabledChanged);
    connect(RadioSettings::instance(), &RadioSettings::kpa1500SettingsChanged, this,
            &KPA1500UiController::onSettingsChanged);

    // Initialize status bar indicator to reflect current enable/disabled state.
    updateStatus();
}

KPA1500UiController::~KPA1500UiController() {
    // WHY: sever BOTH directions before triggering disconnectFromHost() —
    // that call synchronously emits disconnected signals. If we only cut
    // outgoing connections (`disconnect(this)` where this is sender), the
    // client's disconnected signal still reaches our onDisconnected slot,
    // which then calls into sibling controllers' widgets — and those
    // widgets may already be gone under Qt's child-destruction order.
    // See CONVENTIONS.md → Architecture Rule 11.
    if (m_client) {
        disconnect(m_client, nullptr, this, nullptr);
        m_client->disconnectFromHost();
    }
    disconnect(this);
}

void KPA1500UiController::connectIfEnabled() {
    if (RadioSettings::instance()->kpa1500Enabled() && !RadioSettings::instance()->kpa1500Host().isEmpty()) {
        m_client->connectToHost(RadioSettings::instance()->kpa1500Host(), RadioSettings::instance()->kpa1500Port());
    }
}

void KPA1500UiController::disconnectFromHost() {
    if (m_client->isConnected()) {
        m_client->disconnectFromHost();
    }
}

void KPA1500UiController::onConnected() {
    qCDebug(qk4Kpa) << "KPA1500: Connected to amplifier";
    const int pollInterval = RadioSettings::instance()->kpa1500PollInterval();
    m_client->startPolling(pollInterval);
    updateStatus();
}

void KPA1500UiController::onDisconnected() {
    qCDebug(qk4Kpa) << "KPA1500: Disconnected from amplifier";
    updateStatus();
}

void KPA1500UiController::onError(const QString &error) {
    qWarning() << "KPA1500: Error -" << error;
}

void KPA1500UiController::onEnabledChanged(bool enabled) {
    if (enabled) {
        const QString host = RadioSettings::instance()->kpa1500Host();
        if (!host.isEmpty()) {
            m_client->connectToHost(host, RadioSettings::instance()->kpa1500Port());
        }
    } else {
        m_client->disconnectFromHost();
    }
    updateStatus();
}

void KPA1500UiController::onSettingsChanged() {
    // Reconnect with new settings if currently enabled.
    if (RadioSettings::instance()->kpa1500Enabled()) {
        m_client->disconnectFromHost();
        const QString host = RadioSettings::instance()->kpa1500Host();
        if (!host.isEmpty()) {
            m_client->connectToHost(host, RadioSettings::instance()->kpa1500Port());
        }
    }
    updateStatus();
}

void KPA1500UiController::updateStatus() {
    const bool enabled = RadioSettings::instance()->kpa1500Enabled();
    const bool connected = m_client && m_client->isConnected();

    if (!enabled) {
        m_statusBar->setKpa1500Visible(false);
        return;
    }
    m_statusBar->setKpa1500Visible(true);
    if (connected) {
        m_statusBar->setKpa1500Status("KPA1500", QString("color: %1; font-size: %2px; font-weight: bold;")
                                                     .arg(K4Styles::Colors::StatusGreen)
                                                     .arg(K4Styles::Dimensions::FontSizeButton));
    } else {
        m_statusBar->setKpa1500Status("KPA1500", QString("color: %1; font-size: %2px;")
                                                     .arg(K4Styles::Colors::InactiveGray)
                                                     .arg(K4Styles::Dimensions::FontSizeButton));
    }
}
