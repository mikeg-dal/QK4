// K4Discovery - Network discovery for Elecraft K4 radios

#include "k4discovery.h"
#include <QNetworkDatagram>
#include <QLoggingCategory>
#ifdef Q_OS_WIN
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif

Q_LOGGING_CATEGORY(k4discovery, "K4Discovery")

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
        qCWarning(k4discovery) << "Discovery already in progress";
        return;
    }

    qCInfo(k4discovery) << "Starting K4 radio discovery...";
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
        qCWarning(k4discovery) << "No active network interfaces found";
        emit error("No active network interfaces found");
        m_discoveryActive = false;
        emit discoveryFinished(0);
        return;
    }

    qCInfo(k4discovery) << "Sent discovery messages on" << interfaceCount << "network interface(s)";
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

        qCDebug(k4discovery) << "Sending discovery via interface" << netInterface.humanReadableName() << "("
                             << address.toString() << ")";

        // --- Step 1: Send socket bound to this interface ---
        QUdpSocket sendSocket;
        int broadcastEnable = 1;

        if (!sendSocket.bind(address, 0, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
            qCWarning(k4discovery) << "Failed to bind to interface" << netInterface.humanReadableName() << "("
                                   << address.toString() << "):" << sendSocket.errorString();
            continue;
        }

        ::setsockopt(sendSocket.socketDescriptor(), SOL_SOCKET, SO_BROADCAST,
                     reinterpret_cast<const char *>(&broadcastEnable), sizeof(broadcastEnable));

        quint16 localPort = sendSocket.localPort();
        QByteArray message(DISCOVERY_MESSAGE);
        qint64 bytesSent = sendSocket.writeDatagram(message, QHostAddress::Broadcast, UDP_PORT);

        if (bytesSent == -1) {
            qCWarning(k4discovery) << "Failed to send on interface" << netInterface.humanReadableName() << ":"
                                   << sendSocket.errorString();
            sendSocket.close();
            continue;
        }

        qCDebug(k4discovery) << "Sent" << bytesSent << "bytes from" << address.toString() << ":" << localPort
                             << "to 255.255.255.255:" << UDP_PORT << "via" << netInterface.humanReadableName();

        // Close send socket — we only needed it to get the packet out on this interface
        sendSocket.close();

        // --- Step 2: Receive socket on 0.0.0.0:same_port ---
        QUdpSocket *recvSocket = new QUdpSocket(this);
        if (!recvSocket->bind(QHostAddress::AnyIPv4, localPort,
                              QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
            qCWarning(k4discovery) << "Failed to bind receive socket on port" << localPort << ":"
                                   << recvSocket->errorString();
            recvSocket->deleteLater();
            continue;
        }

        connect(recvSocket, &QUdpSocket::readyRead, this, &K4Discovery::onReadyRead);
        m_sockets.append(recvSocket);

        qCDebug(k4discovery) << "Listening for responses on 0.0.0.0:" << localPort;
    }
}

void K4Discovery::onReadyRead() {
    QUdpSocket *socket = qobject_cast<QUdpSocket *>(sender());
    if (!socket)
        return;

    while (socket->hasPendingDatagrams()) {
        QNetworkDatagram datagram = socket->receiveDatagram();
        QByteArray data = datagram.data();

        qCDebug(k4discovery) << "Received" << data.size() << "bytes from" << datagram.senderAddress().toString() << ":"
                             << datagram.senderPort() << "- data:" << QString::fromUtf8(data);

        K4RadioInfo radioInfo;
        if (parseK4Response(data, radioInfo)) {
            bool isDuplicate = false;
            for (const K4RadioInfo &existing : m_discoveredRadios) {
                if (existing.ipAddress == radioInfo.ipAddress) {
                    isDuplicate = true;
                    break;
                }
            }

            if (!isDuplicate) {
                m_discoveredRadios.append(radioInfo);
                qCInfo(k4discovery) << "Found K4 serial" << radioInfo.serialNumber << "at" << radioInfo.ipAddress << "("
                                    << radioInfo.hostname() << ")";
                emit radioFound(radioInfo);
            }
        }
    }
}

void K4Discovery::onDiscoveryTimeout() {
    qCInfo(k4discovery) << "Discovery timeout - found" << m_discoveredRadios.count() << "K4 radio(s)";

    for (QUdpSocket *socket : m_sockets) {
        socket->close();
        socket->deleteLater();
    }
    m_sockets.clear();

    m_discoveryActive = false;
    emit discoveryFinished(m_discoveredRadios.count());
}

bool K4Discovery::parseK4Response(const QByteArray &data, K4RadioInfo &radioInfo) {
    if (!data.startsWith(K4_RESPONSE_PREFIX))
        return false;

    QString response = QString::fromUtf8(data);
    QStringList parts = response.split(':');

    if (parts.count() != 4) {
        qCWarning(k4discovery) << "Invalid K4 response format:" << response;
        return false;
    }

    radioInfo.rigType = parts[0];
    radioInfo.rigIndex = parts[1].toInt();
    radioInfo.ipAddress = parts[2];
    radioInfo.serialNumber = parts[3];

    return true;
}
