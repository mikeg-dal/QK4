#ifndef DXCLUSTERCONTROLLER_H
#define DXCLUSTERCONTROLLER_H

#include <QMap>
#include <QObject>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QVector>

#include "network/dxclusterclient.h"

// Per-cluster connection instance
struct DxClusterInstance {
    DxClusterClient *client = nullptr;
    QThread *thread = nullptr;
    DxClusterClient::ConnectionState state = DxClusterClient::Disconnected;
    QStringList consoleBuffer;
    static constexpr int MAX_CONSOLE_LINES = 500;
};

class DxClusterController : public QObject {
    Q_OBJECT

public:
    explicit DxClusterController(QObject *parent = nullptr);
    ~DxClusterController();

    // Per-cluster connection management (keyed by settings index)
    void connectCluster(int index, const QString &host, quint16 port, const QString &callsign);
    void disconnectCluster(int index);
    void disconnectAll();
    // The saved cluster at `index` was removed from the list: close its connection, and move every
    // later connection down one so each stays keyed by its entry's new row.
    void removeCluster(int index);
    DxClusterClient::ConnectionState clusterState(int index) const;
    bool isAnyConnected() const;

    // Console access for the selected cluster
    QStringList consoleBuffer(int index) const;
    void sendCommand(int index, const QString &command);

    // Aggregated spot cache (from all connected clusters)
    QVector<DxSpot> spotsForFrequencyRange(qint64 startFreqHz, qint64 endFreqHz) const;
    void setSpotMaxAge(int seconds);
    void clearSpots();

signals:
    void clusterStateChanged(int index, DxClusterClient::ConnectionState state);
    void clusterError(int index, const QString &error);
    void clusterLineReceived(int index, const QString &line);
    void spotsUpdated();

private slots:
    void onAgingTimer();

private:
    DxClusterInstance &ensureInstance(int index);
    // The current key of `client`, or -1 once its instance is gone. Signal handlers look the index
    // up at delivery rather than capturing it, because removeCluster() renumbers instances.
    int indexOf(const DxClusterClient *client) const;
    void destroyInstance(int index);
    // Drops the spots that came from the cluster at `index`, leaving every other cluster's intact.
    // A spot both clusters reported is held once, under whichever reported it last.
    void clearSpotsFrom(int index);
    void pruneExpiredSpots();

    QMap<int, DxClusterInstance> m_instances;
    QVector<DxSpot> m_spots;
    QTimer *m_agingTimer;
    int m_spotMaxAgeSec = 600;
};

#endif // DXCLUSTERCONTROLLER_H
