#include "ui/widgets/nethealthwidget.h"
#include "ui/styling/k4constants.h"
#include "ui/widgets/nethealthpopup.h"
#include <QPainter>

NetHealthWidget::NetHealthWidget(NetworkMetrics *metrics, QWidget *parent) : QWidget(parent), m_metrics(metrics) {
    // WHY larger than the bars: the glyph keeps its size, centred, and the widget around it is the
    // click target.
    setFixedSize(K4Styles::Dimensions::MinClickTarget, K4Styles::Dimensions::MinClickTarget);
    setCursor(Qt::PointingHandCursor); // hint: click to open the metrics popup
    connect(m_metrics, &NetworkMetrics::healthTierChanged, this, &NetHealthWidget::onHealthTierChanged);
}

void NetHealthWidget::onHealthTierChanged(NetworkMetrics::HealthTier tier) {
    m_tier = tier;
    // NetworkMetrics only emits Green/Yellow/Orange when connected; Red on disconnect
    // But Red can also mean connected-but-degraded, so track explicitly
    m_connected = (tier != NetworkMetrics::Red) || (m_metrics->rttCurrent() >= 0);
    update();
}

void NetHealthWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    // 4 ascending bars: bar heights as fraction of widget height
    static constexpr int BarCount = 4;
    static constexpr double barHeights[BarCount] = {0.25, 0.50, 0.75, 1.0};

    // How many bars are "lit" by tier
    int litBars = 0;
    QColor litColor;
    if (m_connected) {
        switch (m_tier) {
        case NetworkMetrics::Green:
            litBars = 4;
            litColor = QColor(K4Styles::Colors::StatusGreen);
            break;
        case NetworkMetrics::Yellow:
            litBars = 3;
            litColor = QColor(K4Styles::Colors::MeterYellow);
            break;
        case NetworkMetrics::Orange:
            litBars = 2;
            litColor = QColor(K4Styles::Colors::MeterOrange);
            break;
        case NetworkMetrics::Red:
        default:
            litBars = 1;
            litColor = QColor(K4Styles::Colors::TxRed);
            break;
        }
    }

    QColor dimColor(K4Styles::Colors::MutedGraphic);

    const QRect glyph(QPoint((width() - K4Styles::Dimensions::SmallIconSize) / 2,
                             (height() - K4Styles::Dimensions::NetHealthGlyphHeight) / 2),
                      QSize(K4Styles::Dimensions::SmallIconSize, K4Styles::Dimensions::NetHealthGlyphHeight));

    int barWidth = 3;
    int gap = 2;
    int totalWidth = BarCount * barWidth + (BarCount - 1) * gap;
    int xOffset = glyph.left() + (glyph.width() - totalWidth) / 2;

    for (int i = 0; i < BarCount; i++) {
        int barH = static_cast<int>(glyph.height() * barHeights[i]);
        int x = xOffset + i * (barWidth + gap);
        int y = glyph.top() + glyph.height() - barH;

        p.setPen(Qt::NoPen);
        p.setBrush(i < litBars ? litColor : dimColor);
        p.drawRect(x, y, barWidth, barH);
    }
}

void NetHealthWidget::mousePressEvent(QMouseEvent *event) {
    Q_UNUSED(event)
    // Click-toggle: click the LED to open/close the live metrics popup (predictable, and avoids
    // the hover/leave-tracking that orphaned the popup across fullscreen transitions).
    toggleMetricsPopup();
}

void NetHealthWidget::toggleMetricsPopup() {
    // WHY isVisibleOrJustHidden: the press on the LED reaches the popup's click-away filter first
    // and closes it; reading isVisible() would reopen it on every second click.
    if (m_popup && m_popup->closeIfOpenForToggle())
        return;
    if (!m_popup)
        m_popup = new NetHealthPopup(m_metrics, this); // a child: it cannot outlive the LED
    m_popup->showAboveWidget(this);
}

void NetHealthWidget::closeMetricsPopup() {
    if (m_popup)
        m_popup->hidePopup();
}
