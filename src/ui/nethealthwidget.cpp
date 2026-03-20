#include "nethealthwidget.h"
#include "k4styles.h"
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>

NetHealthWidget::NetHealthWidget(NetworkMetrics *metrics, QWidget *parent) : QWidget(parent), m_metrics(metrics) {
    setFixedSize(20, 16);
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

    QColor dimColor(K4Styles::Colors::InactiveGray);

    int barWidth = 3;
    int gap = 2;
    int totalWidth = BarCount * barWidth + (BarCount - 1) * gap;
    int xOffset = (width() - totalWidth) / 2;

    for (int i = 0; i < BarCount; i++) {
        int barH = static_cast<int>(height() * barHeights[i]);
        int x = xOffset + i * (barWidth + gap);
        int y = height() - barH;

        p.setPen(Qt::NoPen);
        p.setBrush(i < litBars ? litColor : dimColor);
        p.drawRect(x, y, barWidth, barH);
    }
}

void NetHealthWidget::enterEvent(QEnterEvent *event) {
    QWidget::enterEvent(event);
    showMetricsPopup();
}

void NetHealthWidget::leaveEvent(QEvent *event) {
    QWidget::leaveEvent(event);
    hideMetricsPopup();
}

void NetHealthWidget::showMetricsPopup() {
    if (m_popup)
        return;

    m_popup = new QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint);
    m_popup->setAttribute(Qt::WA_ShowWithoutActivating);
    m_popup->setStyleSheet(
        QString("background-color: %1; border: 1px solid #444444;").arg(K4Styles::Colors::PopupBackground));

    auto *layout = new QVBoxLayout(m_popup);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(2);

    QString labelStyle = QString("color: %1; font-size: 10px; border: none;").arg(K4Styles::Colors::TextGray);
    QString valueStyle = QString("color: %1; font-size: 10px; border: none;").arg(K4Styles::Colors::TextWhite);

    // RTT line
    auto *rttLabel = new QLabel(m_popup);
    if (m_metrics->rttCurrent() >= 0) {
        rttLabel->setText(QString("<span style='%1'>RTT </span><span style='%2'>%3ms</span>"
                                  "<span style='%1'>  (jit: </span><span style='%2'>%4</span><span style='%1'>)</span>")
                              .arg(labelStyle, valueStyle)
                              .arg(m_metrics->rttCurrent())
                              .arg(m_metrics->rttJitter(), 0, 'f', 1));
    } else {
        rttLabel->setText(
            QString("<span style='%1'>RTT </span><span style='%2'>--</span>").arg(labelStyle, valueStyle));
    }
    rttLabel->setTextFormat(Qt::RichText);
    layout->addWidget(rttLabel);

    // BUF line
    auto *bufLabel = new QLabel(m_popup);
    int bufMs = static_cast<int>(m_metrics->bufferBytes() / 96.0);
    if (m_metrics->rttCurrent() >= 0) {
        bufLabel->setText(
            QString("<span style='%1'>BUF </span><span style='%2'>%3ms</span>").arg(labelStyle, valueStyle).arg(bufMs));
    } else {
        bufLabel->setText(
            QString("<span style='%1'>BUF </span><span style='%2'>--</span>").arg(labelStyle, valueStyle));
    }
    bufLabel->setTextFormat(Qt::RichText);
    layout->addWidget(bufLabel);

    // PKT + tier line
    auto *pktLabel = new QLabel(m_popup);
    static const char *tierNames[] = {"GREEN", "YELLOW", "ORANGE", "RED"};
    static const char *tierColors[] = {K4Styles::Colors::StatusGreen, K4Styles::Colors::MeterYellow,
                                       K4Styles::Colors::MeterOrange, K4Styles::Colors::TxRed};
    QString tierStyle = QString("color: %1; font-size: 10px; font-weight: bold; border: none;").arg(tierColors[m_tier]);
    if (m_metrics->rttCurrent() >= 0) {
        pktLabel->setText(QString("<span style='%1'>PKT </span><span style='%2'>%3/2s</span>"
                                  "   <span style='%4'>%5</span>")
                              .arg(labelStyle, valueStyle)
                              .arg(m_metrics->packetRate())
                              .arg(tierStyle, tierNames[m_tier]));
    } else {
        pktLabel->setText(QString("<span style='%1'>PKT </span><span style='%2'>--</span>"
                                  "   <span style='%4'>%5</span>")
                              .arg(labelStyle, valueStyle, tierStyle, tierNames[m_tier]));
    }
    pktLabel->setTextFormat(Qt::RichText);
    layout->addWidget(pktLabel);

    m_popup->adjustSize();

    // Position above the bar, 4px gap
    QPoint globalPos = mapToGlobal(QPoint(0, 0));
    int popupX = globalPos.x() - (m_popup->width() - width()) / 2; // Center horizontally
    int popupY = globalPos.y() - m_popup->height() - 4;

    // If popup would go above screen, show below instead
    if (popupY < 0)
        popupY = globalPos.y() + height() + 4;

    m_popup->move(popupX, popupY);
    m_popup->show();
}

void NetHealthWidget::hideMetricsPopup() {
    if (m_popup) {
        m_popup->hide();
        m_popup->deleteLater();
        m_popup = nullptr;
    }
}
