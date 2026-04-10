#include "dxclustercontroller.h"
#include "settings/radiosettings.h"

#include <QDebug>
#include <QMetaObject>
#include <algorithm>

DxClusterController::DxClusterController(RadioState *radioState, QObject *parent)
    : QObject(parent), m_radioState(radioState) {
    // Aging timer runs on UI thread — prunes expired spots every 30s
    m_agingTimer = new QTimer(this);
    m_agingTimer->setInterval(30000);
    connect(m_agingTimer, &QTimer::timeout, this, &DxClusterController::onAgingTimer);
    m_agingTimer->start();
}

DxClusterController::~DxClusterController() {
    disconnect(this);
    // Shut down all cluster instances
    for (auto it = m_instances.begin(); it != m_instances.end(); ++it) {
        auto &inst = it.value();
        if (inst.thread) {
            QMetaObject::invokeMethod(inst.client, "disconnectFromHost", Qt::BlockingQueuedConnection);
            inst.thread->quit();
            inst.thread->wait(2000);
        }
        delete inst.client;
    }
    m_instances.clear();
}

DxClusterInstance &DxClusterController::ensureInstance(int index) {
    if (!m_instances.contains(index)) {
        DxClusterInstance inst;
        inst.client = new DxClusterClient(nullptr);
        inst.thread = new QThread(this);
        inst.thread->setObjectName(QString("DxCluster-%1").arg(index));
        inst.client->moveToThread(inst.thread);
        inst.thread->start();

        // Wire signals with index captured
        connect(inst.client, &DxClusterClient::stateChanged, this, [this, index](DxClusterClient::ConnectionState s) {
            if (m_instances.contains(index))
                m_instances[index].state = s;
            // When a cluster disconnects, purge all spots and refresh overlay
            if (s == DxClusterClient::Disconnected) {
                m_spots.clear();
                emit spotsUpdated();
            }
            emit clusterStateChanged(index, s);
            if (index == m_legacyIndex)
                emit connectionStateChanged(s);
        });
        connect(inst.client, &DxClusterClient::errorOccurred, this, [this, index](const QString &error) {
            emit clusterError(index, error);
            if (index == m_legacyIndex)
                emit errorOccurred(error);
        });
        connect(inst.client, &DxClusterClient::rawLineReceived, this, [this, index](const QString &line) {
            if (m_instances.contains(index)) {
                auto &buf = m_instances[index].consoleBuffer;
                buf.append(line);
                while (buf.size() > DxClusterInstance::MAX_CONSOLE_LINES)
                    buf.removeFirst();
            }
            emit clusterLineReceived(index, line);
            if (index == m_legacyIndex)
                emit rawLineReceived(line);
        });
        connect(inst.client, &DxClusterClient::spotReceived, this, [this](const DxSpot &spot) {
            qDebug() << "[DxCluster] Spot received:" << spot.spottedCall << spot.frequencyHz << spot.mode;
            // Deduplicate: same callsign within 500 Hz replaces older entry
            for (int i = 0; i < m_spots.size(); ++i) {
                if (m_spots[i].spottedCall == spot.spottedCall &&
                    qAbs(m_spots[i].frequencyHz - spot.frequencyHz) <= 500) {
                    m_spots.removeAt(i);
                    break;
                }
            }
            // Insert sorted by frequency
            auto it = std::lower_bound(m_spots.begin(), m_spots.end(), spot.frequencyHz,
                                       [](const DxSpot &s, qint64 freq) { return s.frequencyHz < freq; });
            m_spots.insert(it, spot);
            emit spotsUpdated();
        });

        m_instances[index] = inst;
    }
    return m_instances[index];
}

void DxClusterController::destroyInstance(int index) {
    if (!m_instances.contains(index))
        return;
    auto &inst = m_instances[index];
    if (inst.thread) {
        QMetaObject::invokeMethod(inst.client, "disconnectFromHost", Qt::BlockingQueuedConnection);
        inst.thread->quit();
        inst.thread->wait(2000);
    }
    delete inst.client;
    m_instances.remove(index);
}

void DxClusterController::connectCluster(int index, const QString &host, quint16 port, const QString &callsign) {
    qDebug() << "[DxCluster] connectCluster index:" << index << host << port << callsign;
    auto &inst = ensureInstance(index);
    QMetaObject::invokeMethod(inst.client, "connectToHost", Qt::QueuedConnection, Q_ARG(QString, host),
                              Q_ARG(quint16, port), Q_ARG(QString, callsign));
}

void DxClusterController::disconnectCluster(int index) {
    if (!m_instances.contains(index))
        return;
    QMetaObject::invokeMethod(m_instances[index].client, "disconnectFromHost", Qt::QueuedConnection);
}

void DxClusterController::disconnectAll() {
    for (auto it = m_instances.begin(); it != m_instances.end(); ++it) {
        QMetaObject::invokeMethod(it.value().client, "disconnectFromHost", Qt::QueuedConnection);
    }
    m_spots.clear();
    emit spotsUpdated();
}

DxClusterClient::ConnectionState DxClusterController::clusterState(int index) const {
    if (m_instances.contains(index))
        return m_instances[index].state;
    return DxClusterClient::Disconnected;
}

bool DxClusterController::isAnyConnected() const {
    for (auto it = m_instances.constBegin(); it != m_instances.constEnd(); ++it) {
        if (it.value().state == DxClusterClient::Connected)
            return true;
    }
    return false;
}

QStringList DxClusterController::consoleBuffer(int index) const {
    if (m_instances.contains(index))
        return m_instances[index].consoleBuffer;
    return {};
}

void DxClusterController::sendCommand(int index, const QString &command) {
    if (!m_instances.contains(index))
        return;
    QMetaObject::invokeMethod(m_instances[index].client, "sendCommand", Qt::QueuedConnection, Q_ARG(QString, command));
}

QVector<DxSpot> DxClusterController::spotsForFrequencyRange(qint64 startFreqHz, qint64 endFreqHz) const {
    QVector<DxSpot> result;
    auto it = std::lower_bound(m_spots.begin(), m_spots.end(), startFreqHz,
                               [](const DxSpot &spot, qint64 freq) { return spot.frequencyHz < freq; });
    while (it != m_spots.end() && it->frequencyHz <= endFreqHz) {
        result.append(*it);
        ++it;
    }
    return result;
}

void DxClusterController::setSpotMaxAge(int seconds) {
    m_spotMaxAgeSec = qBound(300, seconds, 1800);
}

void DxClusterController::clearSpots() {
    if (!m_spots.isEmpty()) {
        m_spots.clear();
        emit spotsUpdated();
    }
}

// Legacy single-cluster API (used by MainWindow auto-connect and SpectrumController)
void DxClusterController::connectToCluster(const QString &host, quint16 port, const QString &callsign) {
    // Find matching index in settings, or use 0
    auto clusters = RadioSettings::instance()->dxClusters();
    m_legacyIndex = 0;
    for (int i = 0; i < clusters.size(); ++i) {
        if (clusters[i].host == host && clusters[i].port == port) {
            m_legacyIndex = i;
            break;
        }
    }
    connectCluster(m_legacyIndex, host, port, callsign);
}

void DxClusterController::disconnectFromCluster() {
    if (m_legacyIndex >= 0)
        disconnectCluster(m_legacyIndex);
}

DxClusterClient::ConnectionState DxClusterController::connectionState() const {
    // Return Connected if any cluster is connected
    for (auto it = m_instances.constBegin(); it != m_instances.constEnd(); ++it) {
        if (it.value().state == DxClusterClient::Connected)
            return DxClusterClient::Connected;
        if (it.value().state == DxClusterClient::Connecting)
            return DxClusterClient::Connecting;
    }
    return DxClusterClient::Disconnected;
}

void DxClusterController::onAgingTimer() {
    pruneExpiredSpots();
}

void DxClusterController::pruneExpiredSpots() {
    QDateTime cutoff = QDateTime::currentDateTimeUtc().addSecs(-m_spotMaxAgeSec);
    int before = m_spots.size();
    m_spots.erase(std::remove_if(m_spots.begin(), m_spots.end(),
                                 [&cutoff](const DxSpot &spot) { return spot.timestamp < cutoff; }),
                  m_spots.end());
    if (m_spots.size() != before)
        emit spotsUpdated();
}
