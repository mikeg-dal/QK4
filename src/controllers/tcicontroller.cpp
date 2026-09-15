#include "controllers/tcicontroller.h"

#include <QThread>

#include "controllers/audiocontroller.h"
#include "models/radiostate.h"
#include "network/tciaudiobridge.h"
#include "network/tciserver.h"

namespace {

// K4 mode -> TCI modulation name. Only the modes in modulations_list are legal on the wire; an
// unknown one is reported as usb rather than invented, because a client that cannot parse the
// modulation aborts rather than ignoring the field.
QString tciModulationFor(RadioState::Mode mode) {
    switch (mode) {
    case RadioState::LSB:
        return QStringLiteral("lsb");
    case RadioState::USB:
        return QStringLiteral("usb");
    case RadioState::CW:
        return QStringLiteral("cw");
    case RadioState::CW_R:
        return QStringLiteral("cwr");
    case RadioState::FM:
        return QStringLiteral("nfm");
    case RadioState::AM:
        return QStringLiteral("am");
    case RadioState::DATA:
        return QStringLiteral("digu");
    case RadioState::DATA_R:
        return QStringLiteral("digl");
    case RadioState::Unknown:
        break;
    }
    return QStringLiteral("usb");
}

} // namespace

TciController::TciController(AudioController *audioController, RadioState *radioState, QObject *parent)
    : QObject(parent), m_audioController(audioController), m_radioState(radioState), m_server(new TciServer(nullptr)),
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

    // Keep the server's snapshot in step with the radio. Without this the init burst reports the
    // struct's defaults forever - which showed up immediately as a client stuck on 20m while the
    // K4 was on 40m.
    //
    // A whole snapshot is pushed on every change rather than individual fields: RadioState emits
    // fine-grained signals with no batch boundary, so a client seeded field-by-field could see a
    // frequency and mode that never coexisted.
    if (m_radioState) {
        connect(m_radioState, &RadioState::frequencyChanged, this, [this](quint64) { publishSnapshot(); });
        connect(m_radioState, &RadioState::frequencyBChanged, this, [this](quint64) { publishSnapshot(); });
        connect(m_radioState, &RadioState::modeChanged, this, [this](RadioState::Mode) { publishSnapshot(); });
        connect(m_radioState, &RadioState::splitChanged, this, [this](bool) { publishSnapshot(); });
        publishSnapshot();
    }
}

void TciController::publishSnapshot() {
    if (!m_radioState) {
        return;
    }
    TciRadioSnapshot snapshot;
    snapshot.vfoAHz = static_cast<qint64>(m_radioState->vfoA());
    snapshot.vfoBHz = static_cast<qint64>(m_radioState->vfoB());
    snapshot.split = m_radioState->splitEnabled();
    snapshot.modulation = tciModulationFor(m_radioState->mode());

    // Queued: the server reads this from its own thread, so it must be handed over by value
    // through the event loop rather than written under it.
    QMetaObject::invokeMethod(m_server, [this, snapshot]() { m_server->setSnapshot(snapshot); }, Qt::QueuedConnection);
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
