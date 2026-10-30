#include "connectioncontroller.h"
#include "logging/logcategories.h"
#include "logging/logsink.h"
#include "network/networkmetrics.h"
#include "network/protocol.h"
#include "utils/macroids.h"
#include "utils/threadshutdown.h"
#include <QLoggingCategory>
#include <chrono>

namespace {
// WHY 1 s: the I/O thread only runs an event loop and never blocks by design, so the graceful
// disconnect and the join take a few milliseconds. Anything near this long means it is wedged.
constexpr std::chrono::milliseconds kIoStopTimeout{1000};
} // namespace

ConnectionController::ConnectionController(QObject *parent) : QObject(parent), m_tcpClient(new TcpClient(nullptr)) {
    // Move TcpClient (+ Protocol, socket, timers as children) to dedicated I/O thread
    // so network data flows independently of UI work
    m_ioThread = new QThread(this);
    m_ioThread->setObjectName("I/O");
    Logging::markRealtimeOnStart(m_ioThread);
    // High, not real-time: every RX audio decode and every keyer KZ frame passes through here, but
    // so do TLS, spectrum and CAT, too much work per wake-up for a real-time budget.
    RtScheduling::promoteOnStart(m_ioThread, RtScheduling::Threads::kIo);
    m_tcpClient->moveToThread(m_ioThread);
    // Destroyed on its own thread when that thread finishes (Rule 14), with Protocol, the socket
    // and the timers as its children.
    connect(m_ioThread, &QThread::finished, m_tcpClient, &QObject::deleteLater);
    m_ioThread->start();

    // NetworkMetrics tracks connection health (latency, buffer, packet loss)
    m_networkMetrics = new NetworkMetrics(this);

    // Wire TcpClient signals to our internal handlers
    connect(m_tcpClient, &TcpClient::stateChanged, this, &ConnectionController::onStateChanged);
    connect(m_tcpClient, &TcpClient::errorOccurred, this, &ConnectionController::connectionError);
    connect(m_tcpClient, &TcpClient::errorOccurred, this,
            [](const QString &error) { qCWarning(lcLink) << "Link error:" << error; });
    connect(m_tcpClient, &TcpClient::authenticated, this, &ConnectionController::radioReady);

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
    // The graceful disconnect (a held straight-key KZU, then the K4's disconnect command) runs on
    // the I/O thread and is waited for before the quit: a call queued just ahead of quit() is not
    // guaranteed to be delivered. One deadline covers both steps.
    const QDeadlineTimer deadline(kIoStopTimeout);
    TcpClient *client = m_tcpClient;
    ThreadShutdown::callBlocking(client, [client]() { client->disconnectFromHost(); }, deadline);
    // Joined: TcpClient was deleted on the I/O thread. Disowned: it is left alive on that thread.
    ThreadShutdown::retire(m_ioThread, deadline, lcLink());
    m_tcpClient = nullptr;
}

void ConnectionController::connectToRadio(const RadioEntry &radio) {
    qCDebug(lcLink) << "connectToRadio: isConnected=" << m_tcpClient->isConnected()
                    << "connectionState=" << m_tcpClient->connectionState();
    m_currentRadio = radio;

    // Load startup macro so TcpClient can send it before RDY (state dump reflects changes)
    MacroEntry startupMacro = RadioSettings::instance()->macro(MacroIds::Startup);
    QString macroToSend = startupMacro.command;
    if (!macroToSend.isEmpty()) {
        QString forbidden = MacroIds::checkForbiddenStartupCommand(macroToSend);
        if (!forbidden.isEmpty()) {
            qCWarning(lcLink) << "Startup macro blocked: contains forbidden command" << forbidden;
            macroToSend.clear();
        }
    }
    // WHY on every attempt, even when empty: TcpClient only clears its copy after sending it on a
    // successful auth. A macro queued for an attempt that failed would otherwise survive into the
    // next one and run although the operator has since removed or changed it.
    QMetaObject::invokeMethod(m_tcpClient, &TcpClient::setStartupMacro, Qt::QueuedConnection, macroToSend);

    qCInfo(lcLink) << "Connecting to" << radio.host << ":" << radio.port
                   << (radio.useTls ? "(TLS/PSK)" : "(unencrypted)") << "encodeMode:" << radio.encodeMode
                   << "streamingLatency:" << radio.streamingLatency;
    // connectToHost() ends whatever attempt or session is current itself (TcpClient's one teardown
    // path), so there is no separate disconnect to queue ahead of it.
    QMetaObject::invokeMethod(m_tcpClient, &TcpClient::connectToHost, Qt::QueuedConnection, radio.host, radio.port,
                              radio.password, radio.useTls, radio.identity, radio.encodeMode, radio.streamingLatency);
}

void ConnectionController::disconnectFromRadio() {
    qCInfo(lcLink) << "Disconnect requested";
    QMetaObject::invokeMethod(m_tcpClient, &TcpClient::disconnectFromHost, Qt::QueuedConnection);
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
    qCInfo(lcLink) << "Link state:" << state;
    if (state == TcpClient::Connected)
        m_latencyActivity.begin();
    else if (state == TcpClient::Disconnected)
        m_latencyActivity.end();
    emit connectionStateChanged(state);
}
