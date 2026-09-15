#include "controllers/tcicontroller.h"

#include <QThread>

#include "controllers/audiocontroller.h"
#include "network/tciaudiobridge.h"
#include "network/tciserver.h"

TciController::TciController(AudioController *audioController, QObject *parent)
    : QObject(parent), m_audioController(audioController), m_server(new TciServer(nullptr)),
      m_bridge(new TciAudioBridge(m_server, nullptr)) {
    m_tciThread = new QThread(this);
    m_tciThread->setObjectName(QStringLiteral("TCI"));
    m_server->moveToThread(m_tciThread);
    m_bridge->moveToThread(m_tciThread);
    m_tciThread->start();

    // RX audio: I/O thread -> TCI thread. Queued, so the resampling never runs on the I/O thread.
    if (m_audioController) {
        connect(m_audioController, &AudioController::rxAudioAvailable, m_bridge, &TciAudioBridge::onRxAudio,
                Qt::QueuedConnection);
    }

    // A listener arriving after an idle stretch must not hear samples from before the gap: the
    // bridge skips work entirely while nobody is subscribed, so the filter history is stale.
    connect(
        m_server, &TciServer::audioStartRequested, m_bridge, [this](int) { m_bridge->reset(); }, Qt::QueuedConnection);

    connect(m_server, &TciServer::clientCountChanged, this, &TciController::clientCountChanged);
}

TciController::~TciController() {
    // Rule 11: drop queued signals before partial destruction, then stop producers before consumers.
    disconnect(this);
    if (m_tciThread) {
        QMetaObject::invokeMethod(m_server, "stop", Qt::BlockingQueuedConnection);
        m_tciThread->quit();
        m_tciThread->wait(2000);
    }
    delete m_bridge;
    delete m_server;
}

void TciController::start(quint16 port, bool loopbackOnly) {
    bool ok = false;
    QMetaObject::invokeMethod(m_server, "start", Qt::BlockingQueuedConnection, Q_RETURN_ARG(bool, ok),
                              Q_ARG(quint16, port), Q_ARG(bool, loopbackOnly));
    emit listeningChanged(ok, ok ? port : quint16(0));
}

void TciController::stop() {
    QMetaObject::invokeMethod(m_server, "stop", Qt::BlockingQueuedConnection);
    emit listeningChanged(false, 0);
}

bool TciController::isListening() const {
    // Reading a bool the TCI thread may be writing is not worth a blocking round trip on every UI
    // repaint, and QTcpServer's listening flag does not tear. Callers wanting a guaranteed-fresh
    // value should watch listeningChanged instead.
    return m_server->isListening();
}

int TciController::clientCount() const {
    return m_server->clientCount();
}
