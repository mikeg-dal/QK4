#include "ui/widgets/nethealthpopup.h"
#include "ui/styling/k4constants.h"
#include "ui/styling/k4styles.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <algorithm>

namespace {
// Repaint cadence while the popup is open. ~4 Hz keeps the fastest series (buffer) scrolling
// smoothly without burning CPU; the window is only one small widget and only ticks on hover.
constexpr int kChartRefreshMs = 250;
} // namespace

NetHealthPopup::NetHealthPopup(NetworkMetrics *metrics, QWidget *parent) : K4PopupBase(parent), m_metrics(metrics) {
    // A read-out, not a control: never steals the keyboard from the main window.
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor); // hint: click to dismiss

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(kChartRefreshMs);
    connect(m_refreshTimer, &QTimer::timeout, this, qOverload<>(&QWidget::update));

    initPopup();
}

QSize NetHealthPopup::contentSize() const {
    return QSize(K4Styles::Dimensions::ChartPopupContentWidth,
                 3 * K4Styles::Dimensions::ChartRowHeight + 2 * K4Styles::Dimensions::PopupContentMargin);
}

void NetHealthPopup::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    m_refreshTimer->start();
}

void NetHealthPopup::hideEvent(QHideEvent *event) {
    K4PopupBase::hideEvent(event);
    m_refreshTimer->stop();
}

void NetHealthPopup::mousePressEvent(QMouseEvent *event) {
    event->accept();
    hidePopup();
}

void NetHealthPopup::paintContent(QPainter &painter, const QRect &cr) {
    // The base has drawn the drop shadow and the rounded dark card.
    const bool connected = m_metrics->rttCurrent() >= 0;

    const int pad = K4Styles::Dimensions::PopupContentMargin;
    const int rowH = K4Styles::Dimensions::ChartRowHeight;
    const int rowX = cr.left() + pad;
    const int rowW = cr.width() - 2 * pad;
    int rowY = cr.top() + pad;

    const QString rttVal = connected ? QString("%1ms").arg(m_metrics->rttCurrent()) : QStringLiteral("--");
    const QString jitVal = connected ? QString::number(m_metrics->rttJitter(), 'f', 1) : QStringLiteral("--");
    const QString bufVal =
        connected ? QString("%1ms").arg(static_cast<int>(m_metrics->bufferMs())) : QStringLiteral("--");

    drawSparklineRow(painter, QRect(rowX, rowY, rowW, rowH), QStringLiteral("RTT"), rttVal,
                     K4Styles::Colors::ChartSeriesRtt, 0, QStringLiteral("ms"), m_metrics->rttHistory(), connected);
    rowY += rowH;
    drawSparklineRow(painter, QRect(rowX, rowY, rowW, rowH), QStringLiteral("JIT"), jitVal,
                     K4Styles::Colors::ChartSeriesJitter, 1, QString(), m_metrics->jitterHistory(), connected);
    rowY += rowH;
    drawSparklineRow(painter, QRect(rowX, rowY, rowW, rowH), QStringLiteral("BUF"), bufVal,
                     K4Styles::Colors::ChartSeriesBuffer, 0, QStringLiteral("ms"), m_metrics->bufferHistory(),
                     connected);
}

void NetHealthPopup::drawSparklineRow(QPainter &painter, const QRect &rowRect, const QString &label,
                                      const QString &valueText, const char *seriesColor, int decimals,
                                      const QString &unit, const std::deque<NetworkMetrics::TimedSample> &history,
                                      bool connected) {
    const QColor color(seriesColor);
    const int dot = K4Styles::Dimensions::ChartLegendDotSize;
    const int colW = K4Styles::Dimensions::ChartValueColumnWidth;

    // --- Left column: legend dot + label (top), current value (below) ---
    QFont labelFont = K4Styles::Fonts::paintFont(K4Styles::Dimensions::FontSizeCaption);
    QFont valueFont = K4Styles::Fonts::dataFont(K4Styles::Dimensions::FontSizeHeading);
    QFontMetrics lfm(labelFont);
    QFontMetrics vfm(valueFont);

    const int textX = rowRect.left() + dot + K4Styles::Dimensions::ChartPlotMargin;
    const int labelBaselineY = rowRect.top() + rowRect.height() / 2 - K4Styles::Dimensions::ChartPlotMargin;
    const int valueBaselineY = rowRect.top() + rowRect.height() / 2 + vfm.ascent();

    // Legend dot, vertically centered on the label text.
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    const int dotY = labelBaselineY - lfm.ascent() / 2 - dot / 2;
    painter.drawEllipse(rowRect.left(), dotY, dot, dot);

    painter.setFont(labelFont);
    painter.setPen(QColor(K4Styles::Colors::TextSecondary));
    painter.drawText(textX, labelBaselineY, label);

    painter.setFont(valueFont);
    painter.setPen(QColor(K4Styles::Colors::TextPrimary));
    painter.drawText(textX, valueBaselineY, valueText);

    // --- Plot area ---
    const int plotLeft = rowRect.left() + colW;
    const QRect plot(plotLeft, rowRect.top() + K4Styles::Dimensions::ChartPlotMargin, rowRect.right() - plotLeft,
                     rowRect.height() - 2 * K4Styles::Dimensions::ChartPlotMargin);

    const QColor gridColor(K4Styles::Colors::OverlayDivider);

    if (!connected || history.size() < 2) {
        // Empty/disconnected: a single faint baseline so the row still reads as a plot.
        painter.setPen(QPen(gridColor, K4Styles::Dimensions::SeparatorHeight));
        painter.drawLine(plot.left(), plot.bottom(), plot.right(), plot.bottom());
        return;
    }

    // Actual data extremes (pre-padding) drive the scale gridlines + labels; the padded range
    // drives the plotting so the trace never touches the row edges. Padding is relative to the
    // range, so the data max/min always land at fixed fractions of the plot height.
    float dataMin = history.front().value;
    float dataMax = history.front().value;
    for (const auto &s : history) {
        dataMin = std::min(dataMin, s.value);
        dataMax = std::max(dataMax, s.value);
    }
    float vMin = dataMin;
    float vMax = dataMax;
    float range = vMax - vMin;
    if (range < 1.0f) { // flat-line floor so a constant series sits mid-row, not full height
        const float mid = (vMin + vMax) * 0.5f;
        vMin = mid - 0.5f;
        vMax = mid + 0.5f;
        range = 1.0f;
    }
    const float padV = range * 0.18f;
    vMin -= padV;
    vMax += padV;
    range = vMax - vMin;

    auto yForValue = [&](float v) { return plot.bottom() - (v - vMin) / range * plot.height(); };

    // --- Scale gridlines + left tick marks (drawn behind the trace) ---
    const bool flat = (dataMax - dataMin) < 0.1f;
    const QColor tickColor(K4Styles::Colors::OverlayDividerLight);
    const int tickLen = K4Styles::Dimensions::ChartScaleTickLen;
    auto drawScaleLine = [&](float value) {
        const float y = yForValue(value);
        painter.setPen(QPen(gridColor, K4Styles::Dimensions::SeparatorHeight)); // faint full-width gridline
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        painter.setPen(QPen(tickColor, K4Styles::Dimensions::SeparatorHeight)); // brighter left tick
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.left() + tickLen, y));
    };
    drawScaleLine(dataMax);
    if (!flat)
        drawScaleLine(dataMin);

    const qint64 now = m_metrics->clockNowMs();
    const qint64 window = m_metrics->historyWindowMs();

    auto pointFor = [&](const NetworkMetrics::TimedSample &s) {
        float ageFrac = static_cast<float>(now - s.tMs) / static_cast<float>(window);
        ageFrac = qBound(0.0f, ageFrac, 1.0f);
        const float px = plot.right() - ageFrac * plot.width();
        return QPointF(px, yForValue(s.value));
    };

    QPainterPath line;
    bool first = true;
    for (const auto &s : history) {
        const QPointF p = pointFor(s);
        if (first) {
            line.moveTo(p);
            first = false;
        } else {
            line.lineTo(p);
        }
    }

    // Subtle gradient fill under the curve (series color → transparent at the baseline).
    QPainterPath fill = line;
    fill.lineTo(pointFor(history.back()).x(), plot.bottom());
    fill.lineTo(pointFor(history.front()).x(), plot.bottom());
    fill.closeSubpath();
    QLinearGradient grad(0, plot.top(), 0, plot.bottom());
    QColor fillTop = color;
    fillTop.setAlpha(90);
    QColor fillBottom = color;
    fillBottom.setAlpha(0);
    grad.setColorAt(0.0, fillTop);
    grad.setColorAt(1.0, fillBottom);
    painter.fillPath(fill, grad);

    // Trace.
    painter.setPen(QPen(color, K4Styles::Dimensions::ChartLineWidth));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(line);

    // --- Scale labels (drawn last) ---
    // Right-aligned in the gutter between the value column and the plot (clean card background, off
    // the trace) so they read clearly. Larger bright-neutral font for readability.
    QFont scaleFont = K4Styles::Fonts::paintFont(K4Styles::Dimensions::FontSizeCaption, QFont::Normal);
    QFontMetrics sfm(scaleFont);
    painter.setFont(scaleFont);
    const int gutterRight = plot.left() - K4Styles::Dimensions::ChartPlotMargin; // just left of the plot/tick
    auto drawScaleLabel = [&](float value, bool atTop) {
        const QString text = QString::number(value, 'f', decimals) + unit;
        const int tw = sfm.horizontalAdvance(text);
        const int labelX = gutterRight - tw; // right-aligned into the gutter
        const int lineY = static_cast<int>(yForValue(value));
        // Top label sits just below its line; bottom label just above its line — both stay inside.
        const int baselineY = atTop ? lineY + sfm.ascent() : lineY - K4Styles::Dimensions::SeparatorHeight;
        painter.fillRect(QRect(labelX - 1, baselineY - sfm.ascent(), tw + 2, sfm.height()),
                         QColor(K4Styles::Colors::PopupBackground));
        painter.setPen(QColor(K4Styles::Colors::SelectionLight));
        painter.drawText(labelX, baselineY, text);
    };
    drawScaleLabel(dataMax, true);
    if (!flat)
        drawScaleLabel(dataMin, false);
}
