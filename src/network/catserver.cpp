// WHY: CatServer reads a fixed set of RadioState getters to build CAT
// responses for WSJT-X / MacLoggerDX. Those getters are a frozen public
// API contract — see docs/radiostate-catserver-api-contract.md for the
// list and the rules. The contract survived the 2026-04 RadioState
// subsystem decomposition unchanged and must continue to. The regression
// gate is tests/test_catserver.cpp, pinned in CI.
#include "catserver.h"
#include "catframes.h"
#include "catpushbroadcaster.h"
#include "models/radiostate.h"
#include "protocol.h"
#include "tcpclient.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(netCat, "net.cat")

CatServer::CatServer(RadioState *state, QObject *parent)
    : QObject(parent), m_server(new QTcpServer(this)), m_radioState(state) {
    m_broadcaster = new CatPushBroadcaster(state, this);
    connect(m_server, &QTcpServer::newConnection, this, &CatServer::onNewConnection);
}

CatServer::~CatServer() {
    stop();
}

void CatServer::setTcpClient(TcpClient *client) {
    m_tcpClient = client;
}

bool CatServer::start(quint16 port) {
    if (m_server->isListening()) {
        if (m_server->serverPort() == port) {
            return true;
        }
        stop();
    }

    if (!m_server->listen(QHostAddress::LocalHost, port)) {
        emit errorOccurred(QString("Failed to start CAT server: %1").arg(m_server->errorString()));
        return false;
    }

    m_port = m_server->serverPort(); // use actual port (may differ from requested if 0)
    qCInfo(netCat) << "CAT server listening on port" << m_port;
    emit started(port);
    return true;
}

void CatServer::stop() {
    // Unkey before tearing the listener down. Disabling the CAT server while a client is
    // transmitting used to leave the mic gate open with no client left to close it.
    releasePttIfOwner(m_pttOwner);

    const auto sockets = m_clients.keys();
    for (QTcpSocket *client : sockets) {
        m_broadcaster->removeClient(client);
        client->disconnectFromHost();
    }
    m_clients.clear();

    if (m_server->isListening()) {
        m_server->close();
        m_port = 0;
        emit stopped();
    }
}

bool CatServer::isListening() const {
    return m_server->isListening();
}

quint16 CatServer::port() const {
    return m_port;
}

int CatServer::clientCount() const {
    return m_clients.count();
}

void CatServer::onNewConnection() {
    while (m_server->hasPendingConnections()) {
        QTcpSocket *client = m_server->nextPendingConnection();
        m_clients.insert(client, ClientState{});
        m_broadcaster->addClient(client);

        connect(client, &QTcpSocket::readyRead, this, [this, client]() {
            QByteArray &buffer = m_clients[client].buffer;
            buffer.append(client->readAll());

            // Protect against unbounded buffer growth from misbehaving clients
            if (buffer.size() > K4Protocol::MAX_BUFFER_SIZE) {
                qCWarning(netCat) << "CAT client buffer overflow from" << client->peerAddress().toString()
                                  << "- disconnecting";
                client->disconnectFromHost();
                return;
            }

            // K4 CAT commands are semicolon-terminated — single-pass offset parsing
            int offset = 0;
            int idx;
            while ((idx = buffer.indexOf(';', offset)) != -1) {
                QString command = QString::fromUtf8(buffer.constData() + offset, idx - offset + 1).trimmed();
                offset = idx + 1;

                if (!command.isEmpty()) {
                    qCDebug(netCat) << "RX <-" << client->peerPort() << command;
                    QByteArray response = handleCommand(command, client);
                    if (!response.isEmpty()) {
                        qCDebug(netCat) << "TX ->" << client->peerPort() << response;
                        client->write(response);
                    } else {
                        qCDebug(netCat) << "   (no response)" << client->peerPort() << command;
                    }
                }
            }
            // Keep only unprocessed remainder
            if (offset > 0) {
                buffer.remove(0, offset);
            }
        });

        connect(client, &QTcpSocket::disconnected, this, [this, client]() {
            QString address = QString("%1:%2").arg(client->peerAddress().toString()).arg(client->peerPort());
            qCInfo(netCat) << "CAT client disconnected:" << address;
            // A client that crashes or is killed mid-transmission never sends RX;. Without this
            // the mic gate stayed open and the K4 kept transmitting until the operator pressed
            // Escape.
            releasePttIfOwner(client);
            m_broadcaster->removeClient(client);
            m_clients.remove(client);
            client->deleteLater();

            emit clientDisconnected(address);
        });

        QString address = QString("%1:%2").arg(client->peerAddress().toString()).arg(client->peerPort());
        qCInfo(netCat) << "CAT client connected:" << address;
        emit clientConnected(address);
    }
}

void CatServer::releasePttIfOwner(QTcpSocket *client) {
    if (!client || m_pttOwner != client) {
        return;
    }
    qCInfo(netCat) << "CAT client that asserted PTT is gone - releasing";
    m_pttOwner = nullptr;
    emit pttRequested(false);
}

QByteArray CatServer::handleCommand(const QString &cmd, QTcpSocket *client) {
    // K4 CAT commands: 2-3 letter prefix, optional parameters, semicolon
    // GET commands have no parameters (e.g., "FA;", "MD;")
    // SET commands have parameters (e.g., "FA14074000;", "MD1;")

    QString command = cmd.trimmed();
    if (!command.endsWith(';')) {
        return QByteArray(); // Invalid command
    }

    // Remove trailing semicolon for parsing
    command = command.left(command.length() - 1);

    if (command.isEmpty()) {
        return QByteArray();
    }

    // Handle special commands where numbers are part of the command name
    // K2, K3, K40, PS - these need special handling before normal parsing
    if (command == "K2") {
        return QByteArray("K22;"); // K2 extended mode level 2
    }
    if (command == "K3") {
        return QByteArray("K31;"); // K3 extended mode level 1
    }
    if (command.startsWith("K2") || command.startsWith("K3") || command.startsWith("K4")) {
        // K22, K31, K40 etc - SET commands, silently acknowledge
        return QByteArray();
    }
    if (command == "PS") {
        return QByteArray("PS1;"); // Power on status
    }
    if (command == "RVM") {
        // Firmware revision - Front Panel version from RadioState
        QString fp = m_radioState->firmwareVersions().value("FP", "01.00");
        return QString("RVM%1;").arg(fp).toUtf8();
    }
    if (command == "RVD") {
        // DSP firmware revision from RadioState
        QString dsp = m_radioState->firmwareVersions().value("DSP", "01.00");
        return QString("RVD%1;").arg(dsp).toUtf8();
    }
    if (command.startsWith("PS")) {
        // PS0, PS1 - power control SET commands, silently acknowledge
        return QByteArray();
    }

    // Extract command prefix (2-3 uppercase letters)
    QString prefix;
    QString args;
    for (int i = 0; i < command.length(); i++) {
        if (command[i].isLetter()) {
            prefix += command[i].toUpper();
        } else {
            args = command.mid(i);
            break;
        }
    }

    // Handle VFO-B suffix GET queries (args == "$" means "query VFO B", no value to SET).
    // The letter-only prefix extractor strips "$" into args, so "MD$;" arrives here as
    // prefix="MD", args="$" and would otherwise fall through to the SET path.
    if (args == "$") {
        if (prefix == "MD") {
            return CatFrames::modeB(m_radioState->modeB());
        }
    }

    // Handle GET commands (no args) - respond from RadioState
    if (args.isEmpty()) {
        if (prefix == "FA") {
            return CatFrames::frequencyA(m_radioState->frequency());
        }
        if (prefix == "FB") {
            return CatFrames::frequencyB(m_radioState->vfoB());
        }
        if (prefix == "MD") {
            return CatFrames::modeA(m_radioState->mode());
        }
        if (prefix == "TQ") {
            return CatFrames::ptt(m_radioState->isTransmitting());
        }
        if (prefix == "FT") {
            return CatFrames::split(m_radioState->splitEnabled());
        }
        if (prefix == "FR") {
            return QByteArray("FR0;"); // Always VFO A for RX
        }
        if (prefix == "IF") {
            return CatFrames::ifFrame(*m_radioState);
        }
        if (prefix == "RO") {
            return CatFrames::ritOffset(m_radioState->ritXitOffset());
        }
        if (prefix == "RT") {
            return CatFrames::ritEnabled(m_radioState->ritEnabled());
        }
        if (prefix == "XT") {
            return CatFrames::xitEnabled(m_radioState->xitEnabled());
        }
        if (prefix == "PC") {
            return CatFrames::rfPower(m_radioState->rfPower(), m_radioState->powerRange());
        }
        if (prefix == "GT") {
            return CatFrames::agcSpeed(static_cast<int>(m_radioState->agcSpeed()));
        }
        if (prefix == "KS") {
            return CatFrames::keyerSpeed(m_radioState->keyerSpeed());
        }
        if (prefix == "NB") {
            return CatFrames::noiseBlanker(m_radioState->noiseBlankerEnabled());
        }
        if (prefix == "NR") {
            return CatFrames::noiseReduction(m_radioState->noiseReductionEnabled());
        }
        if (prefix == "VX") {
            return CatFrames::vox(m_radioState->voxEnabled());
        }
        if (prefix == "BW") {
            return CatFrames::filterBandwidth(m_radioState->filterBandwidth());
        }
        if (prefix == "ID") {
            return QByteArray("ID017;"); // K4 ID
        }
        if (prefix == "DT") {
            return CatFrames::dataSubMode(m_radioState->dataSubMode());
        }
        if (prefix == "OM") {
            QString om = m_radioState->optionModules();
            if (om.isEmpty()) {
                om = "AP----------"; // Basic K4 with ATU and PA (12 chars)
            }
            return QString("OM %1;").arg(om).toUtf8(); // Note: space after OM
        }
        if (prefix == "AI") {
            return CatFrames::aiMode(m_broadcaster->clientAiMode(client));
        }
        if (prefix == "TB") {
            return QByteArray("TB000;"); // No CW messages queued
        }
        if (prefix == "SB") {
            return QByteArray("SB0;"); // Sub RX off by default
        }
        if (prefix == "DV") {
            return CatFrames::diversity(m_radioState->diversityEnabled());
        }
        if (prefix == "SM") {
            return CatFrames::sMeterMain(m_radioState->sMeter());
        }
        if (prefix == "PCX") {
            return CatFrames::rfPowerExtended(m_radioState->rfPower(), m_radioState->powerRange());
        }
        if (prefix == "AG") {
            return QByteArray("AG000;");
        }
        if (prefix == "SQ") {
            return QByteArray("SQ000;");
        }
        if (prefix == "FW") {
            return CatFrames::filterWidthExtended(m_radioState->filterBandwidth());
        }
        if (prefix == "TM") {
            return CatFrames::txMeter(m_radioState->alcMeter(), m_radioState->compressionDb(),
                                      m_radioState->forwardPower(), m_radioState->swrMeter(),
                                      m_radioState->isQrpMode());
        }
    }

    // AI SET commands - update per-client AI mode subscription. K4 spec: no echo.
    if (prefix == "AI") {
        bool ok = false;
        int level = args.toInt(&ok);
        if (ok && (level == 0 || level == 1 || level == 2 || level == 4)) {
            m_broadcaster->setClientAiMode(client, level);
            qCDebug(netCat) << "   AI mode set to" << level << "for client" << client->peerPort();
        } else {
            qCDebug(netCat) << "   AI SET rejected (invalid level):" << cmd;
        }
        return QByteArray();
    }

    // TX/RX commands - control audio input gate for external app transmit.
    // Don't forward to K4 - the audio stream itself triggers K4 TX.
    // "TX;" asserts transmit; "TX/;" toggles based on the radio's current TX state.
    if (prefix == "TX") {
        const bool on = (args == "/") ? !m_radioState->isTransmitting() : true;
        qCDebug(netCat) << "   PTT request:" << (on ? "ON" : "OFF");
        // Remember who keyed so the transmission can be ended if that client vanishes.
        m_pttOwner = on ? client : nullptr;
        emit pttRequested(on);
        return QByteArray();
    }
    if (prefix == "RX") {
        qCDebug(netCat) << "   PTT request: OFF";
        m_pttOwner = nullptr;
        emit pttRequested(false);
        return QByteArray();
    }

    // SET commands (have args) - forward to real K4
    // Commands like FA14074000;, MD1;, etc.
    qCDebug(netCat) << "   forwarding SET to K4:" << cmd;
    emit catCommandReceived(cmd);

    // Most SET commands echo the new value
    return QByteArray();
}
