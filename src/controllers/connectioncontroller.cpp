#include "connectioncontroller.h"
#include "network/networkmetrics.h"
#include "network/protocol.h"
#include "models/radiostate.h"
#include "ui/fnpopupwidget.h"
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(qk4Connection, "qk4.connection")

ConnectionController::ConnectionController(RadioState *radioState, QObject *parent)
    : QObject(parent), m_tcpClient(new TcpClient(nullptr)), m_radioState(radioState) {
    // Move TcpClient (+ Protocol, socket, timers as children) to dedicated I/O thread
    // so network data flows independently of UI work
    m_ioThread = new QThread(this);
    m_ioThread->setObjectName("I/O");
    m_tcpClient->moveToThread(m_ioThread);
    m_ioThread->start();

    // NetworkMetrics tracks connection health (latency, buffer, packet loss)
    m_networkMetrics = new NetworkMetrics(this);

    // Wire TcpClient signals to our internal handlers
    connect(m_tcpClient, &TcpClient::stateChanged, this, &ConnectionController::onStateChanged);
    connect(m_tcpClient, &TcpClient::errorOccurred, this, &ConnectionController::connectionError);
    connect(m_tcpClient, &TcpClient::authenticated, this, &ConnectionController::radioReady);
    connect(m_tcpClient, &TcpClient::authenticationFailed, this, &ConnectionController::authFailed);

    // Re-emit Protocol signals so callers don't need tcpClient()->protocol() chains
    auto *protocol = m_tcpClient->protocol();
    connect(protocol, &Protocol::catResponseReceived, this, &ConnectionController::catResponseReceived);
    connect(protocol, &Protocol::spectrumDataReady, this, &ConnectionController::spectrumDataReceived);
    connect(protocol, &Protocol::miniSpectrumDataReady, this, &ConnectionController::miniSpectrumDataReceived);

    // Network health metrics
    connect(m_tcpClient, &TcpClient::latencyChanged, m_networkMetrics, &NetworkMetrics::onLatencyChanged);
    connect(m_tcpClient, &TcpClient::connected, m_networkMetrics,
            [this]() { m_networkMetrics->onConnectionStateChanged(true); });
    connect(m_tcpClient, &TcpClient::disconnected, m_networkMetrics,
            [this]() { m_networkMetrics->onConnectionStateChanged(false); });
}

ConnectionController::~ConnectionController() {
    disconnect(this);
    if (m_ioThread) {
        QMetaObject::invokeMethod(m_tcpClient, "disconnectFromHost", Qt::BlockingQueuedConnection);
        m_ioThread->quit();
        m_ioThread->wait(2000);
    }
    delete m_tcpClient; // No parent, must delete manually
}

void ConnectionController::connectToRadio(const RadioEntry &radio) {
    qCDebug(qk4Connection) << "connectToRadio: isConnected=" << m_tcpClient->isConnected()
                           << "connectionState=" << m_tcpClient->connectionState();
    if (m_tcpClient->isConnected()) {
        QMetaObject::invokeMethod(m_tcpClient, "disconnectFromHost", Qt::QueuedConnection);
    }

    m_currentRadio = radio;

    // Load startup macro so TcpClient can send it before RDY (state dump reflects changes)
    MacroEntry startupMacro = RadioSettings::instance()->macro(MacroIds::Startup);
    if (!startupMacro.command.isEmpty()) {
        QString forbidden = MacroIds::checkForbiddenStartupCommand(startupMacro.command);
        if (!forbidden.isEmpty()) {
            qWarning() << "Startup macro blocked: contains forbidden command" << forbidden;
        } else {
            QMetaObject::invokeMethod(m_tcpClient, "setStartupMacro", Qt::QueuedConnection,
                                      Q_ARG(QString, startupMacro.command));
        }
    }

    qCDebug(qk4Connection) << "Connecting to" << radio.host << ":" << radio.port
                           << (radio.useTls ? "(TLS/PSK)" : "(unencrypted)") << "encodeMode:" << radio.encodeMode
                           << "streamingLatency:" << radio.streamingLatency;
    QMetaObject::invokeMethod(m_tcpClient, "connectToHost", Qt::QueuedConnection, Q_ARG(QString, radio.host),
                              Q_ARG(quint16, radio.port), Q_ARG(QString, radio.password), Q_ARG(bool, radio.useTls),
                              Q_ARG(QString, radio.identity), Q_ARG(int, radio.encodeMode),
                              Q_ARG(int, radio.streamingLatency));
}

void ConnectionController::disconnectFromRadio() {
    QMetaObject::invokeMethod(m_tcpClient, "disconnectFromHost", Qt::QueuedConnection);
}

void ConnectionController::sendCAT(const QString &command) {
    m_tcpClient->sendCAT(command);
}

void ConnectionController::sendRawPacket(const QByteArray &packet) {
    m_tcpClient->sendRaw(packet);
}

bool ConnectionController::isConnected() const {
    return m_tcpClient->isConnected();
}

TcpClient::ConnectionState ConnectionController::connectionState() const {
    return m_tcpClient->connectionState();
}

void ConnectionController::onStateChanged(TcpClient::ConnectionState state) {
    emit connectionStateChanged(state);
    if (state == TcpClient::Disconnected) {
        emit connectionLost();
    }
}
