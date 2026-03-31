#include "catserver.h"
#include "models/radiostate.h"
#include "protocol.h"
#include "tcpclient.h"

CatServer::CatServer(RadioState *state, QObject *parent)
    : QObject(parent), m_server(new QTcpServer(this)), m_radioState(state) {
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
    emit started(port);
    return true;
}

void CatServer::stop() {
    for (QTcpSocket *client : m_clients) {
        client->disconnectFromHost();
    }
    m_clients.clear();
    m_clientBuffers.clear();

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
        m_clients.append(client);
        m_clientBuffers[client] = QByteArray();

        connect(client, &QTcpSocket::readyRead, this, &CatServer::onClientData);
        connect(client, &QTcpSocket::disconnected, this, &CatServer::onClientDisconnected);

        QString address = QString("%1:%2").arg(client->peerAddress().toString()).arg(client->peerPort());
        emit clientConnected(address);
    }
}

void CatServer::onClientData() {
    QTcpSocket *client = qobject_cast<QTcpSocket *>(sender());
    if (!client)
        return;

    QByteArray &buffer = m_clientBuffers[client];
    buffer.append(client->readAll());

    // Protect against unbounded buffer growth from misbehaving clients
    if (buffer.size() > K4Protocol::MAX_BUFFER_SIZE) {
        qWarning() << "CAT client buffer overflow from" << client->peerAddress().toString() << "- disconnecting";
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
            QString response = handleCommand(command);
            if (!response.isEmpty()) {
                client->write(response.toUtf8());
            }
        }
    }
    // Keep only unprocessed remainder
    if (offset > 0) {
        buffer.remove(0, offset);
    }
}

void CatServer::onClientDisconnected() {
    QTcpSocket *client = qobject_cast<QTcpSocket *>(sender());
    if (!client)
        return;

    QString address = QString("%1:%2").arg(client->peerAddress().toString()).arg(client->peerPort());
    m_clients.removeOne(client);
    m_clientBuffers.remove(client);
    client->deleteLater();

    emit clientDisconnected(address);
}

QString CatServer::handleCommand(const QString &cmd) {
    // K4 CAT commands: 2-3 letter prefix, optional parameters, semicolon
    // GET commands have no parameters (e.g., "FA;", "MD;")
    // SET commands have parameters (e.g., "FA14074000;", "MD1;")

    QString command = cmd.trimmed();
    if (!command.endsWith(';')) {
        return QString(); // Invalid command
    }

    // Remove trailing semicolon for parsing
    command = command.left(command.length() - 1);

    if (command.isEmpty()) {
        return QString();
    }

    // Handle special commands where numbers are part of the command name
    // K2, K3, K40, PS - these need special handling before normal parsing
    if (command == "K2") {
        return "K22;"; // K2 extended mode level 2
    }
    if (command == "K3") {
        return "K31;"; // K3 extended mode level 1
    }
    if (command.startsWith("K2") || command.startsWith("K3") || command.startsWith("K4")) {
        // K22, K31, K40 etc - SET commands, silently acknowledge
        return QString();
    }
    if (command == "PS") {
        return "PS1;"; // Power on status
    }
    if (command == "RVM") {
        // Firmware revision - Front Panel version from RadioState
        QString fp = m_radioState->firmwareVersions().value("FP", "01.00");
        return QString("RVM%1;").arg(fp);
    }
    if (command == "RVD") {
        // DSP firmware revision from RadioState
        QString dsp = m_radioState->firmwareVersions().value("DSP", "01.00");
        return QString("RVD%1;").arg(dsp);
    }
    if (command.startsWith("PS")) {
        // PS0, PS1 - power control SET commands, silently acknowledge
        return QString();
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

    // Handle GET commands (no args) - respond from RadioState
    if (args.isEmpty()) {
        // VFO A frequency
        if (prefix == "FA") {
            return buildFrequencyResponse(m_radioState->frequency(), "FA");
        }
        // VFO B frequency
        if (prefix == "FB") {
            return buildFrequencyResponse(m_radioState->vfoB(), "FB");
        }
        // Mode (VFO A)
        if (prefix == "MD") {
            return buildModeResponse(m_radioState->mode());
        }
        // PTT state
        if (prefix == "TQ") {
            return QString("TQ%1;").arg(m_radioState->isTransmitting() ? 1 : 0);
        }
        // Split state
        if (prefix == "FT") {
            return QString("FT%1;").arg(m_radioState->splitEnabled() ? 1 : 0);
        }
        // RX VFO indicator
        if (prefix == "FR") {
            return "FR0;"; // Always VFO A for RX
        }
        // IF command - comprehensive status (K4 format, 38 chars total)
        // Format:
        // IF[freq:11][blanks:5][±offset:6][rit:1][xit:1][bank:1][ch:2][tx:1][mode:2][vfo:1][scan:1][split:1][data:2];
        if (prefix == "IF") {
            quint64 freq = m_radioState->frequency();
            int offset = m_radioState->ritXitOffset();
            int ritOn = m_radioState->ritEnabled() ? 1 : 0;
            int xitOn = m_radioState->xitEnabled() ? 1 : 0;
            int mode = m_radioState->mode();
            int tx = m_radioState->isTransmitting() ? 1 : 0;
            int split = m_radioState->splitEnabled() ? 1 : 0;

            QString response = QString("IF%1     %2%3%4%5%6%7%8%9%10%11%12%13;")
                                   .arg(freq, 11, 10, QChar('0')) // P1: freq (11)
                                   // 5 blanks for step size (P2)
                                   .arg(offset >= 0 ? "+" : "-")         // P3: offset sign
                                   .arg(qAbs(offset), 5, 10, QChar('0')) // P3: offset value (5 digits)
                                   .arg(ritOn)                           // P4: RIT on/off (1)
                                   .arg(xitOn)                           // P5: XIT on/off (1)
                                   .arg(0)                               // P6: Memory bank (1)
                                   .arg("00")                            // P7: Memory channel (2)
                                   .arg(tx)                              // P8: TX status (1)
                                   .arg(mode, 2, 10, QChar('0'))         // P9: Mode (2 digits)
                                   .arg(0)                               // P10: VFO/Mem (1)
                                   .arg(0)                               // P11: Scan (1)
                                   .arg(split)                           // P12: Split (1)
                                   .arg("00");                           // P13: Data submode (2)
            return response;
        }
        // RIT offset
        if (prefix == "RO") {
            int offset = m_radioState->ritXitOffset();
            return QString("RO%1%2;").arg(offset >= 0 ? "+" : "-").arg(qAbs(offset), 4, 10, QChar('0'));
        }
        // RIT on/off
        if (prefix == "RT") {
            return QString("RT%1;").arg(m_radioState->ritEnabled() ? 1 : 0);
        }
        // XIT on/off
        if (prefix == "XT") {
            return QString("XT%1;").arg(m_radioState->xitEnabled() ? 1 : 0);
        }
        // RF power
        if (prefix == "PC") {
            return QString("PC%1;").arg(static_cast<int>(m_radioState->rfPower()), 3, 10, QChar('0'));
        }
        // AGC
        if (prefix == "GT") {
            int agc = static_cast<int>(m_radioState->agcSpeed());
            // K4 AGC: 0=off, 1=slow, 2=fast
            return QString("GT%1;").arg(agc, 3, 10, QChar('0'));
        }
        // Keyer speed
        if (prefix == "KS") {
            return QString("KS%1;").arg(m_radioState->keyerSpeed(), 3, 10, QChar('0'));
        }
        // Noise blanker
        if (prefix == "NB") {
            return QString("NB%1;").arg(m_radioState->noiseBlankerEnabled() ? 1 : 0);
        }
        // Noise reduction
        if (prefix == "NR") {
            return QString("NR%1;").arg(m_radioState->noiseReductionEnabled() ? 1 : 0);
        }
        // VOX
        if (prefix == "VX") {
            return QString("VX%1;").arg(m_radioState->voxEnabled() ? 1 : 0);
        }
        // Filter bandwidth
        if (prefix == "BW") {
            int bw = m_radioState->filterBandwidth();
            return QString("BW%1;").arg(bw, 4, 10, QChar('0'));
        }
        // ID - Radio identification
        if (prefix == "ID") {
            return "ID017;"; // K4 ID
        }
        // DT - Data sub-mode
        if (prefix == "DT") {
            return QString("DT%1;").arg(m_radioState->dataSubMode());
        }
        // OM - Option modules query
        if (prefix == "OM") {
            // Return option modules from RadioState if available, else default
            // Format: OM <options>; where options is a 12-char string with dashes for absent
            QString om = m_radioState->optionModules();
            if (om.isEmpty()) {
                om = "AP----------"; // Basic K4 with ATU and PA (12 chars)
            }
            return QString("OM %1;").arg(om); // Note: space after OM
        }
        // AI - Auto-information (transceive mode)
        // QK4 uses AI4 globally - report that, don't let external apps change it
        if (prefix == "AI") {
            return "AI4;";
        }
        // TB - Text buffer (CW message queue status)
        if (prefix == "TB") {
            return "TB000;"; // No CW messages queued
        }
        // SB - Sub RX on/off
        if (prefix == "SB") {
            // Check if sub receiver is active (typically based on dual watch or diversity)
            return "SB0;"; // Sub RX off by default
        }
        // SM - S-meter reading
        if (prefix == "SM") {
            // S-meter value: 0000-0021 typical range
            // RadioState stores S-units (0-9 for S1-S9, higher for +dB)
            int smeter = static_cast<int>(m_radioState->sMeter());
            // Convert to K4 format (roughly 3 per S-unit)
            int k4Value = qBound(0, smeter * 3, 21);
            return QString("SM%1;").arg(k4Value, 4, 10, QChar('0'));
        }
        // PCX - Extended power reading
        if (prefix == "PCX") {
            // Format: PCnnnM; where nnn=power (3 digits), M=mode (H=high, L=low/QRP)
            int power = static_cast<int>(m_radioState->rfPower());
            QString mode = m_radioState->isQrpMode() ? "L" : "H";
            return QString("PC%1%2;").arg(power, 3, 10, QChar('0')).arg(mode);
        }
        // AG - AF gain (audio volume)
        if (prefix == "AG") {
            // Audio gain 0-255, but we don't track this - return 0
            return "AG000;";
        }
        // SQ - Squelch level
        if (prefix == "SQ") {
            // Squelch 0-255, but we don't track this - return 0
            return "SQ000;";
        }
        // FW - Filter width (bandwidth)
        if (prefix == "FW") {
            int bw = m_radioState->filterBandwidth();
            return QString("FW%1;").arg(bw, 8, 10, QChar('0'));
        }
        // TM - TX metering (polled during TX)
        if (prefix == "TM") {
            // Return TX meter values - format varies by mode
            // For now return a basic response
            return "TM0;";
        }
    }

    // AI SET commands - silently ignore, don't let external apps change our AI4 mode
    if (prefix == "AI") {
        return QString();
    }

    // TX/RX commands - control audio input gate for external app transmit
    // Don't forward to K4 - the audio stream itself triggers K4 TX
    if (prefix == "TX") {
        emit pttRequested(true);
        return QString();
    }
    if (prefix == "RX") {
        emit pttRequested(false);
        return QString();
    }

    // SET commands (have args) - forward to real K4
    // Commands like FA14074000;, MD1;, etc.
    emit catCommandReceived(cmd);

    // Most SET commands echo the new value
    return QString();
}

QString CatServer::buildFrequencyResponse(quint64 freq, const QString &prefix) const {
    // K4 frequency format: 11 digits with leading zeros
    return QString("%1%2;").arg(prefix).arg(freq, 11, 10, QChar('0'));
}

QString CatServer::buildModeResponse(int mode) const {
    // K4 mode numbers: 1=LSB, 2=USB, 3=CW, 4=FM, 5=AM, 6=DATA, 7=CW-R, 9=DATA-R
    int k4Mode = 2; // Default USB
    switch (mode) {
    case RadioState::LSB:
        k4Mode = 1;
        break;
    case RadioState::USB:
        k4Mode = 2;
        break;
    case RadioState::CW:
        k4Mode = 3;
        break;
    case RadioState::FM:
        k4Mode = 4;
        break;
    case RadioState::AM:
        k4Mode = 5;
        break;
    case RadioState::DATA:
        k4Mode = 6;
        break;
    case RadioState::CW_R:
        k4Mode = 7;
        break;
    case RadioState::DATA_R:
        k4Mode = 9;
        break;
    }
    return QString("MD%1;").arg(k4Mode);
}
