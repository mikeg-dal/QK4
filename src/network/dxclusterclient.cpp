#include "dxclusterclient.h"

#include <QDebug>
#include <QtMath>

// "DX de K3GMQ-#:  14031.00  K0RX           CW    18 dB  24 WPM  CQ      1902Z"
const QRegularExpression
    DxClusterClient::s_spotRegex(R"(^DX\s+de\s+(\S+):\s+(\d+\.?\d*)\s+(\S+)\s+(.*?)\s+(\d{4}Z)\s*$)");

const QRegularExpression
    DxClusterClient::s_modeRegex(R"(\b(CW|SSB|USB|LSB|FT8|FT4|RTTY|PSK31|PSK63|AM|FM|DATA|DIGI|JT65|JT9)\b)",
                                 QRegularExpression::CaseInsensitiveOption);

const QRegularExpression DxClusterClient::s_loginRegex(R"((call\s*(?:sign)?|login|your\s+call\s*(?:sign)?)\s*[:>])",
                                                       QRegularExpression::CaseInsensitiveOption);

DxClusterClient::DxClusterClient(QObject *parent) : QObject(parent), m_socket(new QTcpSocket(this)) {
    connect(m_socket, &QTcpSocket::connected, this, &DxClusterClient::onSocketConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &DxClusterClient::onSocketDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &DxClusterClient::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &DxClusterClient::onSocketError);
}

DxClusterClient::~DxClusterClient() = default;

bool DxClusterClient::parseSpotLine(const QString &line, DxSpot &spot) {
    auto match = s_spotRegex.match(line);
    if (!match.hasMatch())
        return false;

    spot.spotterCall = match.captured(1);
    double freqKhz = match.captured(2).toDouble();
    spot.frequencyHz = qRound64(freqKhz * 1000.0);
    spot.spottedCall = match.captured(3);
    spot.comment = match.captured(4).simplified();
    spot.timeUtc = match.captured(5);
    spot.timestamp = QDateTime::currentDateTimeUtc();

    // Extract mode from comment text
    auto modeMatch = s_modeRegex.match(spot.comment);
    if (modeMatch.hasMatch())
        spot.mode = modeMatch.captured(1).toUpper();

    return true;
}

void DxClusterClient::connectToHost(const QString &host, quint16 port, const QString &callsign) {
    qDebug() << "[DxCluster] connectToHost:" << host << port << "callsign:" << callsign;
    if (m_state != Disconnected)
        disconnectFromHost();

    m_callsign = callsign;
    m_receiveBuffer.clear();
    setState(Connecting);
    m_socket->connectToHost(host, port);
}

void DxClusterClient::sendCommand(const QString &command) {
    if (m_state == Connected && m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->write((command + "\r\n").toUtf8());
    }
}

void DxClusterClient::disconnectFromHost() {
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
        if (m_socket->state() != QAbstractSocket::UnconnectedState)
            m_socket->abort();
    }
    m_receiveBuffer.clear();
    setState(Disconnected);
}

void DxClusterClient::onSocketConnected() {
    qDebug() << "[DxCluster] TCP socket connected, waiting for login prompt...";
}

void DxClusterClient::onSocketDisconnected() {
    m_receiveBuffer.clear();
    setState(Disconnected);
}

void DxClusterClient::onReadyRead() {
    m_receiveBuffer += QString::fromUtf8(m_socket->readAll());
    qDebug() << "[DxCluster] onReadyRead, state:" << m_state << "buffer:" << m_receiveBuffer.left(200);

    // Process complete lines
    int newlinePos;
    while ((newlinePos = m_receiveBuffer.indexOf('\n')) != -1) {
        QString line = m_receiveBuffer.left(newlinePos).trimmed();
        m_receiveBuffer.remove(0, newlinePos + 1);

        if (!line.isEmpty())
            processLine(line);
    }

    // Login prompts typically don't end with a newline (the server waits for
    // input on the same line), so check the partial buffer for a prompt too.
    if (m_state == Connecting && !m_receiveBuffer.isEmpty()) {
        qDebug() << "[DxCluster] Checking partial buffer for login prompt:" << m_receiveBuffer;
        if (s_loginRegex.match(m_receiveBuffer).hasMatch()) {
            qDebug() << "[DxCluster] Login prompt detected, sending callsign:" << m_callsign;
            if (!m_callsign.isEmpty()) {
                m_socket->write((m_callsign + "\r\n").toUtf8());
                m_receiveBuffer.clear();
                setState(Connected);
            }
        }
    }
}

void DxClusterClient::onSocketError(QAbstractSocket::SocketError error) {
    Q_UNUSED(error)
    QString errorMsg = m_socket->errorString();
    qDebug() << "[DxCluster] Socket error:" << errorMsg;
    setState(Disconnected);
    emit errorOccurred(errorMsg);
}

void DxClusterClient::setState(ConnectionState state) {
    if (m_state != state) {
        m_state = state;
        emit stateChanged(m_state);
    }
}

void DxClusterClient::processLine(const QString &line) {
    // Always forward raw lines for the console display
    emit rawLineReceived(line);

    // Check for login prompt
    if (m_state == Connecting && s_loginRegex.match(line).hasMatch()) {
        if (!m_callsign.isEmpty()) {
            m_socket->write((m_callsign + "\r\n").toUtf8());
            setState(Connected);
        }
        return;
    }

    // If we get a welcome/hello line after sending callsign, ensure we're Connected
    if (m_state == Connecting && line.contains("Hello", Qt::CaseInsensitive)) {
        setState(Connected);
        return;
    }

    // Try to parse as a DX spot
    DxSpot spot;
    if (parseSpotLine(line, spot))
        emit spotReceived(spot);
}
