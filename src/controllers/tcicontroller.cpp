#include "controllers/tcicontroller.h"

#include <QThread>

#include "controllers/audiocontroller.h"
#include "controllers/connectioncontroller.h"
#include "network/catframes.h"
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

// TCI modulation name -> K4 mode. The inverse of tciModulationFor; an unrecognised name never
// reaches here because TciServer refuses anything outside modulations_list rather than coercing it.
bool k4ModeFor(const QString &modulation, RadioState::Mode *out) {
    static const QHash<QString, RadioState::Mode> kModes{
        {QStringLiteral("lsb"), RadioState::LSB},     {QStringLiteral("usb"), RadioState::USB},
        {QStringLiteral("cw"), RadioState::CW},       {QStringLiteral("cwr"), RadioState::CW_R},
        {QStringLiteral("nfm"), RadioState::FM},      {QStringLiteral("fm"), RadioState::FM},
        {QStringLiteral("am"), RadioState::AM},       {QStringLiteral("sam"), RadioState::AM},
        {QStringLiteral("digu"), RadioState::DATA},   {QStringLiteral("digl"), RadioState::DATA_R},
        {QStringLiteral("rtty"), RadioState::DATA_R},
    };
    const auto it = kModes.constFind(modulation.toLower());
    if (it == kModes.constEnd()) {
        return false;
    }
    if (out) {
        *out = it.value();
    }
    return true;
}

} // namespace

TciController::TciController(AudioController *audioController, ConnectionController *connectionController,
                             RadioState *radioState, QObject *parent)
    : QObject(parent), m_audioController(audioController), m_connectionController(connectionController),
      m_radioState(radioState), m_server(new TciServer(nullptr)), m_bridge(new TciAudioBridge(m_server, nullptr)) {
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

    // TX. Order matters on both edges and is the reason these are not one connection:
    //  - keying:   select the TCI source BEFORE asserting PTT, or the first frames out are the
    //              microphone picking up the room.
    //  - unkeying: release PTT first, then hand the transmitter back to the microphone.
    if (m_audioController) {
        connect(m_server, &TciServer::pttRequested, this, [this](bool active) {
            if (active) {
                m_audioController->setTxSource(AudioController::TxSource::Tci);
                m_audioController->setPttActive(true);
            } else {
                m_audioController->setPttActive(false);
                m_audioController->setTxSource(AudioController::TxSource::Microphone);
            }
        });

        // Queued: decoding lands on the TCI thread, the encode pipeline lives on the audio thread.
        connect(m_server, &TciServer::txAudioReceived, this, [this](const QByteArray &mono48k) {
            if (m_audioEnabled) {
                m_audioController->feedTciTxAudio(mono48k);
            }
        });
    }

    // CAT sets from a client.
    //
    // WHY these go through CatFrames rather than formatted strings: CatFrames is where K4 command
    // spelling lives (src/network/README.md), and a literal here would be a second place to get it
    // wrong. The TCI layer never spells a K4 command.
    if (m_connectionController) {
        connect(m_server, &TciServer::setFrequencyRequested, this, [this](int channel, qint64 hz) {
            if (hz <= 0) {
                return;
            }
            applyCat(channel == 1 ? CatFrames::frequencyB(static_cast<quint64>(hz))
                                  : CatFrames::frequencyA(static_cast<quint64>(hz)));
        });
        connect(m_server, &TciServer::setModulationRequested, this, [this](const QString &modulation) {
            RadioState::Mode mode = RadioState::USB;
            if (k4ModeFor(modulation, &mode)) {
                applyCat(CatFrames::modeA(mode));
            }
        });
        connect(m_server, &TciServer::setSplitRequested, this,
                [this](bool enabled) { applyCat(CatFrames::split(enabled)); });
    }

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

void TciController::setAudioEnabled(bool enabled) {
    m_audioEnabled = enabled;
    // The bridge lives on the TCI thread; its flag is a plain bool read on that thread only.
    QMetaObject::invokeMethod(m_bridge, [this, enabled]() { m_bridge->setEnabled(enabled); }, Qt::QueuedConnection);
}

void TciController::applyCat(const QByteArray &frame) {
    const QString command = QString::fromLatin1(frame);

    // Mirrors what CatServer's wiring does for an external client (mainwindow.cpp:352-375): send it,
    // then parse it locally so the panadapter passband tracks immediately. K4 spectrum packets
    // arrive BEFORE the CAT echo, so without the optimistic parse the passband goes off-screen
    // until the echo lands.
    m_connectionController->sendCAT(command);

    // parseCATCommand is main-thread-only and CI-enforced (CONVENTIONS.md rule 4). This lambda runs
    // on the main thread already, but the queue keeps that true if the signal is ever reconnected
    // from the TCI thread.
    if (m_radioState) {
        QMetaObject::invokeMethod(
            m_radioState, [this, command]() { m_radioState->parseCATCommand(command); }, Qt::QueuedConnection);
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
