#ifndef NETHEALTHWIDGET_H
#define NETHEALTHWIDGET_H

#include <QWidget>
#include "network/networkmetrics.h"

class NetHealthPopup;
class QMouseEvent;

/**
 * @brief Signal-bars glyph in the status bar showing NetworkMetrics::HealthTier
 *        (green/yellow/orange/red), derived from RTT, jitter, buffer fill, dropouts and missed PONGs.
 *        A click opens or closes a live sparkline detail popup.
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
