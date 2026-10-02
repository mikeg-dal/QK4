#include "dxclustercontroller.h"

#include <QDebug>
#include <QMetaObject>
#include <algorithm>

namespace {
// Spot aging cadence. 30 s matches typical cluster post rate; shorter churns the overlay,
// longer lets stale spots linger after the spotter gives up.
constexpr int kSpotAgingIntervalMs = 30000;
} // namespace

DxClusterController::DxClusterController(QObject *parent) : QObject(parent) {
    // Aging timer runs on UI thread — prunes expired spots every 30s. Started on demand
    // when the first cluster instance is added and stopped when the last one goes away,
    // so an idle app with no clusters connected does no periodic work here.
    m_agingTimer = new QTimer(this);
    m_agingTimer->setInterval(kSpotAgingIntervalMs);
    connect(m_agingTimer, &QTimer::timeout, this, &DxClusterController::onAgingTimer);
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

        // Wire signals by client, not by index: removeCluster() renumbers instances, so the index a
        // handler reports is looked up when the signal arrives. A signal still queued from a client
        // whose instance has been destroyed finds no index and is dropped.
        DxClusterClient *client = inst.client;
        connect(inst.client, &DxClusterClient::stateChanged, this, [this, client](DxClusterClient::ConnectionState s) {
            const int index = indexOf(client);
            if (index < 0)
                return;
            m_instances[index].state = s;
            // Only this cluster's spots: the others are still connected and still current.
            if (s == DxClusterClient::Disconnected)
                clearSpotsFrom(index);
            emit clusterStateChanged(index, s);
        });
        connect(inst.client, &DxClusterClient::errorOccurred, this, [this, client](const QString &error) {
            const int index = indexOf(client);
            if (index >= 0)
                emit clusterError(index, error);
        });
        connect(inst.client, &DxClusterClient::rawLineReceived, this, [this, client](const QString &line) {
            const int index = indexOf(client);
            if (index < 0)
                return;
            auto &buf = m_instances[index].consoleBuffer;
            buf.append(line);
            while (buf.size() > DxClusterInstance::MAX_CONSOLE_LINES)
                buf.removeFirst();
            emit clusterLineReceived(index, line);
        });
        connect(inst.client, &DxClusterClient::spotReceived, this, [this, client](const DxSpot &received) {
            DxSpot spot = received;
            spot.clusterIndex = indexOf(client);
            if (spot.clusterIndex < 0)
                return; // queued from a cluster that has since been removed
            qCDebug(netDxCluster) << "Spot received:" << spot.spottedCall << spot.frequencyHz << spot.mode;
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

            // WHY: hard cap memory growth between 30s prune cycles. In a heavy contest the
            // cluster stream can produce thousands of spots/min; without this cap m_spots
            // grows tens of thousands of entries and spotsForFrequencyRange() scans all of
            // them on every panadapter render. Trim to newest-by-timestamp on overflow.
            static constexpr int kMaxSpots = 5000;
            if (m_spots.size() > kMaxSpots) {
                std::sort(m_spots.begin(), m_spots.end(),
                          [](const DxSpot &a, const DxSpot &b) { return a.timestamp > b.timestamp; });
                m_spots.resize(kMaxSpots);
                std::sort(m_spots.begin(), m_spots.end(),
                          [](const DxSpot &a, const DxSpot &b) { return a.frequencyHz < b.frequencyHz; });
            }
            emit spotsUpdated();
        });

        m_instances[index] = inst;
        if (!m_agingTimer->isActive())
            m_agingTimer->start();
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
    if (m_instances.isEmpty())
        m_agingTimer->stop();
}

void DxClusterController::connectCluster(int index, const QString &host, quint16 port, const QString &callsign) {
    qCDebug(netDxCluster) << "connectCluster index:" << index << host << port << callsign;
    auto &inst = ensureInstance(index);
    QMetaObject::invokeMethod(inst.client, "connectToHost", Qt::QueuedConnection, Q_ARG(QString, host),
                              Q_ARG(quint16, port), Q_ARG(QString, callsign));
}

void DxClusterController::disconnectCluster(int index) {
    if (!m_instances.contains(index))
        return;
    QMetaObject::invokeMethod(m_instances[index].client, "disconnectFromHost", Qt::QueuedConnection);
    // WHY: clear spots synchronously so the overlay updates immediately. The async lambda on
    // stateChanged is unreliable here — setState() is a no-op when the socket already
    // self-disconnected (server kick / idle timeout), leaving stale spots on screen.
    clearSpotsFrom(index);
}

int DxClusterController::indexOf(const DxClusterClient *client) const {
    for (auto it = m_instances.constBegin(); it != m_instances.constEnd(); ++it) {
        if (it.value().client == client)
            return it.key();
    }
    return -1;
}

void DxClusterController::removeCluster(int index) {
    destroyInstance(index); // closes and deletes its connection, if it had one

    // The removed cluster's spots must not outlive it. Purged here rather than on its stateChanged,
    // which is dropped: the client is already gone when that signal is delivered.
    clearSpotsFrom(index);

    // Every later entry moved up a row in the saved list; move its connection, and its spots, with it.
    QMap<int, DxClusterInstance> renumbered;
    for (auto it = m_instances.constBegin(); it != m_instances.constEnd(); ++it)
        renumbered.insert(it.key() > index ? it.key() - 1 : it.key(), it.value());
    m_instances = renumbered;
    for (DxSpot &spot : m_spots) {
        if (spot.clusterIndex > index)
            --spot.clusterIndex;
    }
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

void DxClusterController::clearSpotsFrom(int index) {
    const auto removed = std::remove_if(m_spots.begin(), m_spots.end(),
                                        [index](const DxSpot &spot) { return spot.clusterIndex == index; });
    if (removed == m_spots.end())
        return;
    m_spots.erase(removed, m_spots.end());
    emit spotsUpdated();
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
