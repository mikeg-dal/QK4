#ifndef DXCLUSTERCONTROLLER_H
#define DXCLUSTERCONTROLLER_H

#include <QMap>
#include <QObject>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QVector>

#include "network/dxclusterclient.h"

class RadioState;

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
    explicit DxClusterController(RadioState *radioState, QObject *parent = nullptr);
    ~DxClusterController();

    // Per-cluster connection management (keyed by settings index)
    void connectCluster(int index, const QString &host, quint16 port, const QString &callsign);
    void disconnectCluster(int index);
    void disconnectAll();
    DxClusterClient::ConnectionState clusterState(int index) const;
    bool isAnyConnected() const;

    // Console access for the selected cluster
    QStringList consoleBuffer(int index) const;
    void sendCommand(int index, const QString &command);

    // Aggregated spot cache (from all connected clusters)
    QVector<DxSpot> spotsForFrequencyRange(qint64 startFreqHz, qint64 endFreqHz) const;
    void setSpotMaxAge(int seconds);
    void clearSpots();

    // Legacy single-cluster API (used by MainWindow auto-connect)
    void connectToCluster(const QString &host, quint16 port, const QString &callsign);
    void disconnectFromCluster();
    DxClusterClient::ConnectionState connectionState() const;

signals:
    void clusterStateChanged(int index, DxClusterClient::ConnectionState state);
    void clusterError(int index, const QString &error);
    void clusterLineReceived(int index, const QString &line);
    void spotsUpdated();

    // Legacy signals (for SpectrumController compatibility)
    void connectionStateChanged(DxClusterClient::ConnectionState state);
    void errorOccurred(const QString &error);
    void rawLineReceived(const QString &line);

private slots:
    void onAgingTimer();

private:
    DxClusterInstance &ensureInstance(int index);
    void destroyInstance(int index);
    void pruneExpiredSpots();

    QMap<int, DxClusterInstance> m_instances;
    RadioState *m_radioState;
    QVector<DxSpot> m_spots;
    QTimer *m_agingTimer;
    int m_spotMaxAgeSec = 600;
    int m_legacyIndex = -1; // Track which index was used via legacy API
};

#endif // DXCLUSTERCONTROLLER_H
