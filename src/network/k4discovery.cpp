// K4Discovery - Network discovery for Elecraft K4 radios

#include "k4discovery.h"
#include "logging/logcategories.h"
#include "utils/radioutils.h"
#include <QNetworkDatagram>
#include <QLoggingCategory>
#ifdef Q_OS_WIN
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif

const char *K4Discovery::DISCOVERY_MESSAGE = "findk4";
const char *K4Discovery::K4_RESPONSE_PREFIX = "k4";

K4Discovery::K4Discovery(QObject *parent)
    : QObject(parent), m_timeoutTimer(new QTimer(this)), m_discoveryActive(false) {
    m_timeoutTimer->setSingleShot(true);
    m_timeoutTimer->setInterval(TIMEOUT_MS);
    connect(m_timeoutTimer, &QTimer::timeout, this, &K4Discovery::onDiscoveryTimeout);
}

K4Discovery::~K4Discovery() {
    for (QUdpSocket *socket : m_sockets) {
        socket->close();
        socket->deleteLater();
    }
    m_sockets.clear();
}

void K4Discovery::startDiscovery() {
    if (m_discoveryActive) {
        qCWarning(lcLinkDiscovery) << "Discovery already in progress";
        return;
    }

    qCInfo(lcLinkDiscovery) << "Starting K4 radio discovery...";
    m_discoveredRadios.clear();
    m_discoveryActive = true;

    for (QUdpSocket *socket : m_sockets) {
        socket->close();
        socket->deleteLater();
    }
    m_sockets.clear();

    QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();

    int interfaceCount = 0;
    for (const QNetworkInterface &iface : interfaces) {
        if (iface.flags() & QNetworkInterface::IsLoopBack)
            continue;
        if (!(iface.flags() & QNetworkInterface::IsUp))
            continue;
        if (!(iface.flags() & QNetworkInterface::IsRunning))
            continue;

        sendDiscoveryMessage(iface);
        interfaceCount++;
    }

    if (interfaceCount == 0) {
        qCWarning(lcLinkDiscovery) << "No active network interfaces found";
        m_discoveryActive = false;
        emit discoveryFinished(0);
        return;
    }

    qCInfo(lcLinkDiscovery) << "Sent discovery messages on" << interfaceCount << "network interface(s)";
    m_timeoutTimer->start();
}

void K4Discovery::sendDiscoveryMessage(const QNetworkInterface &netInterface) {
    // Two-socket approach (matches the working findk4.py pattern):
    // 1. Send socket: bind to interface IP with SO_BROADCAST, send to 255.255.255.255, then close
    // 2. Receive socket: bind to 0.0.0.0 on the SAME ephemeral port to catch responses
    //
    // This is required on macOS where VLAN-bound sockets can't send broadcasts,
    // but a socket bound to the same port on 0.0.0.0 can receive the K4's reply.

    QList<QNetworkAddressEntry> entries = netInterface.addressEntries();

    for (const QNetworkAddressEntry &entry : entries) {
        QHostAddress address = entry.ip();

        if (address.protocol() != QAbstractSocket::IPv4Protocol)
            continue;

        qCDebug(lcLinkDiscovery) << "Sending discovery via interface" << netInterface.humanReadableName() << "("
                                 << address.toString() << ")";

        // --- Step 1: Send socket bound to this interface ---
        QUdpSocket sendSocket;
        int broadcastEnable = 1;

        if (!sendSocket.bind(address, 0, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
            qCWarning(lcLinkDiscovery) << "Failed to bind to interface" << netInterface.humanReadableName() << "("
                                       << address.toString() << "):" << sendSocket.errorString();
            continue;
        }

        // WHY set by hand: sending to 255.255.255.255 needs SO_BROADCAST, and QAbstractSocket has
        // no option for it. Qt's documentation for QUdpSocket does not promise to set it, so a
        // platform where it is left unset would refuse the probe.
        ::setsockopt(sendSocket.socketDescriptor(), SOL_SOCKET, SO_BROADCAST,
                     reinterpret_cast<const char *>(&broadcastEnable), sizeof(broadcastEnable));

        quint16 localPort = sendSocket.localPort();
        QByteArray message(DISCOVERY_MESSAGE);
        qint64 bytesSent = sendSocket.writeDatagram(message, QHostAddress::Broadcast, UDP_PORT);

        if (bytesSent == -1) {
            qCWarning(lcLinkDiscovery) << "Failed to send on interface" << netInterface.humanReadableName() << ":"
                                       << sendSocket.errorString();
            sendSocket.close();
            continue;
        }

        qCDebug(lcLinkDiscovery) << "Sent" << bytesSent << "bytes from" << address.toString() << ":" << localPort
                                 << "to 255.255.255.255:" << UDP_PORT << "via" << netInterface.humanReadableName();

        // Close send socket — we only needed it to get the packet out on this interface
        sendSocket.close();

        // --- Step 2: Receive socket on 0.0.0.0:same_port ---
        QUdpSocket *recvSocket = new QUdpSocket(this);
        if (!recvSocket->bind(QHostAddress::AnyIPv4, localPort,
                              QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
            qCWarning(lcLinkDiscovery) << "Failed to bind receive socket on port" << localPort << ":"
                                       << recvSocket->errorString();
            recvSocket->deleteLater();
            continue;
        }

        connect(recvSocket, &QUdpSocket::readyRead, this, &K4Discovery::onReadyRead);
        m_sockets.append(recvSocket);

        qCDebug(lcLinkDiscovery) << "Listening for responses on 0.0.0.0:" << localPort;
    }
}

void K4Discovery::onReadyRead() {
    QUdpSocket *socket = qobject_cast<QUdpSocket *>(sender());
    if (!socket)
        return;

    while (socket->hasPendingDatagrams()) {
        QNetworkDatagram datagram = socket->receiveDatagram();
        QByteArray data = datagram.data();

        qCDebug(lcLinkDiscovery) << "Received" << data.size() << "bytes from" << datagram.senderAddress().toString()
                                 << ":" << datagram.senderPort() << "- data:" << QString::fromUtf8(data);

        if (const std::optional<K4RadioInfo> parsed = parseResponse(data)) {
            const K4RadioInfo &radioInfo = *parsed;
            bool isDuplicate = false;
            for (const K4RadioInfo &existing : m_discoveredRadios) {
                if (existing.ipAddress == radioInfo.ipAddress) {
                    isDuplicate = true;
                    break;
                }
            }

            if (!isDuplicate) {
                m_discoveredRadios.append(radioInfo);
                qCInfo(lcLinkDiscovery) << "Found K4 serial" << radioInfo.serialNumber << "at" << radioInfo.ipAddress
                                        << "(" << radioInfo.hostname() << ")";
                emit radioFound(radioInfo);
            }
        }
    }
}

void K4Discovery::onDiscoveryTimeout() {
    qCInfo(lcLinkDiscovery) << "Discovery timeout - found" << m_discoveredRadios.count() << "K4 radio(s)";

    for (QUdpSocket *socket : m_sockets) {
        socket->close();
        socket->deleteLater();
    }
    m_sockets.clear();

    m_discoveryActive = false;
    emit discoveryFinished(m_discoveredRadios.count());
}

std::optional<K4RadioInfo> K4Discovery::parseResponse(const QByteArray &data) {
    // WHY trim NULs as well as whitespace: a reply may carry a terminator, and a trailing byte left
    // on the address would fail validation (or, before validation, become part of the host name).
    QByteArray trimmed = data.trimmed();
    while (trimmed.endsWith('\0'))
        trimmed.chop(1);
    trimmed = trimmed.trimmed();
    if (!trimmed.startsWith(K4_RESPONSE_PREFIX))
        return std::nullopt;

    const QStringList parts = QString::fromUtf8(trimmed).split(':');
    if (parts.count() != 4) {
        qCWarning(lcLinkDiscovery) << "Invalid K4 response format:" << trimmed;
        return std::nullopt;
    }

    K4RadioInfo info;
    info.rigType = parts[0].trimmed();
    bool indexOk = false;
    info.rigIndex = parts[1].trimmed().toInt(&indexOk);
    info.ipAddress = parts[2].trimmed();
    info.serialNumber = parts[3].trimmed();

    bool serialOk = false;
    info.serialNumber.toUInt(&serialOk);
    if (!indexOk || !serialOk || !RadioUtils::isValidIpv4(info.ipAddress)) {
        qCWarning(lcLinkDiscovery) << "K4 response refused, malformed field:" << trimmed;
        return std::nullopt;
    }
    return info;
}
