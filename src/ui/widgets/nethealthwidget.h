#ifndef NETHEALTHWIDGET_H
#define NETHEALTHWIDGET_H

#include <QWidget>
#include "network/networkmetrics.h"

class NetHealthPopup;
class QMouseEvent;

/**
 * @brief Small LED in the status bar that reflects NetworkMetrics::HealthTier (green/yellow/red)
 *        based on buffer fill and jitter. A click opens or closes a live sparkline detail popup.
 */
class NetHealthWidget : public QWidget {
    Q_OBJECT
public:
    explicit NetHealthWidget(NetworkMetrics *metrics, QWidget *parent = nullptr);

    // Close the metrics popup if it is open (disconnect reset).
    void closeMetricsPopup();

public slots:
    void onHealthTierChanged(NetworkMetrics::HealthTier tier);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void toggleMetricsPopup();

    NetworkMetrics *m_metrics;
    NetworkMetrics::HealthTier m_tier = NetworkMetrics::Red;
    bool m_connected = false;
    NetHealthPopup *m_popup = nullptr; // child, created on first open
};

#endif // NETHEALTHWIDGET_H
