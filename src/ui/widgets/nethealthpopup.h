#ifndef NETHEALTHPOPUP_H
#define NETHEALTHPOPUP_H

#include <deque>

#include "ui/popups/k4popupbase.h"

#include "network/networkmetrics.h"

class QTimer;
class QMouseEvent;

/**
 * @brief Frameless hover popup that plots the last 10 seconds of network metrics as three
 *        stacked, independently auto-scaled sparklines (RTT / JIT / BUF) on a rounded,
 *        drop-shadowed dark card. Repaints on its own timer while visible so the traces
 *        scroll live; reads NetworkMetrics history getters on the main thread (no locking).
 *
 * A K4PopupBase: placed centred above the LED (below it when the window is at the top of the
 * screen), closed by a click on it or outside it, Escape, and everything else the base watches.
 */
class NetHealthPopup : public K4PopupBase {
    Q_OBJECT
public:
    explicit NetHealthPopup(NetworkMetrics *metrics, QWidget *parent = nullptr);

protected:
    QSize contentSize() const override;
    void paintContent(QPainter &painter, const QRect &contentRect) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void drawSparklineRow(QPainter &painter, const QRect &rowRect, const QString &label, const QString &valueText,
                          const char *seriesColor, int decimals, const QString &unit,
                          const std::deque<NetworkMetrics::TimedSample> &history, bool connected);

    NetworkMetrics *m_metrics;
    QTimer *m_refreshTimer;
};

#endif // NETHEALTHPOPUP_H
