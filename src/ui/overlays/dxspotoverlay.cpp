#include "ui/overlays/dxspotoverlay.h"
#include "ui/styling/k4styles.h"

#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <utility>

DxSpotOverlay::DxSpotOverlay(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setMouseTracking(true);
}

void DxSpotOverlay::setSpots(const QVector<DxSpot> &spots) {
    m_spots = spots;
    layoutLabels();
    update();
}

void DxSpotOverlay::setFrequencyRange(qint64 centerFreq, int spanHz) {
    if (m_centerFreq != centerFreq || m_spanHz != spanHz) {
        m_centerFreq = centerFreq;
        m_spanHz = spanHz;
        layoutLabels();
        update();
    }
}

void DxSpotOverlay::setLeftReservedPx(int px) {
    const int clamped = qMax(0, px);
    if (m_leftReservedPx == clamped)
        return;
    m_leftReservedPx = clamped;
    layoutLabels();
    update();
}

int DxSpotOverlay::labelLeftFor(int xCenter, int textWidth) const {
    const int maxLeft = qMax(0, width() - textWidth);
    const int minLeft = qMin(m_leftReservedPx, maxLeft);
    return qBound(minLeft, xCenter - textWidth / 2, maxLeft);
}

void DxSpotOverlay::setFontPixelSize(int px) {
    const int clamped = qBound(K4Styles::Dimensions::FontSizeSpotMin, px, K4Styles::Dimensions::FontSizeSpotMax);
    if (m_fontPixelSize == clamped)
        return;
    m_fontPixelSize = clamped;
    layoutLabels();
    update();
}

float DxSpotOverlay::freqToX(qint64 freq) const {
    if (m_spanHz <= 0)
        return -1.0f;
    float normalized = static_cast<float>(freq - m_centerFreq) / static_cast<float>(m_spanHz) + 0.5f;
    return normalized * width();
}

void DxSpotOverlay::layoutLabels() {
    m_labels.clear();
    if (m_spots.isEmpty() || m_spanHz <= 0 || width() <= 0)
        return;

    // Float spots at the vertical midpoint of the spectrum area. Labels stack upward
    // from the midpoint; the bottom of the lowest row sits exactly on height()/2.
    const int rh = rowHeight();
    const int stackBase = height() / 2;

    int availableHeight = stackBase - TOP_MARGIN;
    if (availableHeight < rh)
        return;
    int maxRows = qMin(MAX_DISPLAY_ROWS, qMax(1, availableHeight / rh));

    QFont font = K4Styles::Fonts::paintFont(m_fontPixelSize);
    QFontMetrics fm(font);

    // Pre-pass: count spots per frequency cluster (within 500 Hz) and limit each to
    // MAX_PER_FREQUENCY to prevent digital modes (FT8/FT4) from creating huge columns.
    // Spots are sorted by frequency, so clusters are contiguous.
    QVector<const DxSpot *> filteredSpots;
    int clusterStart = 0;
    for (int i = 0; i < m_spots.size(); ++i) {
        bool endOfCluster =
            (i == m_spots.size() - 1) || (m_spots[i + 1].frequencyHz - m_spots[clusterStart].frequencyHz > 500);
        if (endOfCluster) {
            int clusterSize = i - clusterStart + 1;
            // Keep the cluster's last MAX_PER_FREQUENCY entries: its highest frequencies, since
            // m_spots is in frequency order
            int startIdx = (clusterSize > MAX_PER_FREQUENCY) ? i - MAX_PER_FREQUENCY + 1 : clusterStart;
            int overflow = clusterSize - MAX_PER_FREQUENCY;
            for (int j = startIdx; j <= i; ++j)
                filteredSpots.append(&m_spots[j]);
            // If we capped this cluster, record for the "+N" badge
            if (overflow > 0) {
                float cx = freqToX(m_spots[clusterStart].frequencyHz);
                int cxInt = static_cast<int>(cx);
                if (cxInt >= 0 && cxInt <= width()) {
                    SpotLabel badge;
                    badge.callsign = QString("+%1").arg(overflow);
                    badge.frequencyHz = m_spots[clusterStart].frequencyHz;
                    badge.xPixel = cxInt;
                    badge.row = -1;       // Sentinel: placed after layout
                    badge.rect = QRect(); // Set below
                    m_labels.append(badge);
                }
            }
            clusterStart = i + 1;
        }
    }

    // Track occupied ranges per row
    QVector<QVector<QPair<int, int>>> rowOccupied(maxRows);

    for (const auto *spot : filteredSpots) {
        float x = freqToX(spot->frequencyHz);
        int xInt = static_cast<int>(x);

        if (xInt < -20 || xInt > width() + 20)
            continue;

        int textWidth = fm.horizontalAdvance(spot->spottedCall);
        int labelLeft = labelLeftFor(xInt, textWidth);
        int labelRight = labelLeft + textWidth;

        // Find first available row (top-down)
        int assignedRow = -1;
        for (int row = 0; row < maxRows; ++row) {
            bool overlap = false;
            for (const auto &interval : rowOccupied[row]) {
                if (labelLeft < interval.second + LABEL_GAP && labelRight + LABEL_GAP > interval.first) {
                    overlap = true;
                    break;
                }
            }
            if (!overlap) {
                assignedRow = row;
                break;
            }
        }

        if (assignedRow < 0)
            continue;

        rowOccupied[assignedRow].append({labelLeft, labelRight});

        SpotLabel label;
        label.callsign = spot->spottedCall;
        label.frequencyHz = spot->frequencyHz;
        label.xPixel = xInt;
        label.row = assignedRow;
        // Bottom-up: row 0 sits on the midpoint, higher rows stack upward
        int y = qBound(TOP_MARGIN, stackBase - (assignedRow + 1) * rh, height() - rh);
        label.rect = QRect(labelLeft, y, textWidth, rh);
        m_labels.append(label);
    }

    // Position "+N" badges above the last used row at their frequency
    for (auto &label : m_labels) {
        if (label.row != -1)
            continue;
        // Find the highest row used at this X position
        int lastRow = 0;
        for (const auto &other : std::as_const(m_labels)) {
            if (other.row >= 0 && qAbs(other.xPixel - label.xPixel) < 40)
                lastRow = qMax(lastRow, other.row);
        }
        int badgeRow = qMin(lastRow + 1, maxRows - 1);
        int tw = fm.horizontalAdvance(label.callsign);
        int lx = labelLeftFor(label.xPixel, tw);
        label.row = badgeRow;
        int y = qBound(TOP_MARGIN, stackBase - (badgeRow + 1) * rh, height() - rh);
        label.rect = QRect(lx, y, tw, rh);
    }
}

void DxSpotOverlay::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)
    if (m_labels.isEmpty())
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    QFont font = K4Styles::Fonts::paintFont(m_fontPixelSize);
    painter.setFont(font);

    const QColor textColor(K4Styles::Colors::VfoACyan);
    const QColor badgeColor(K4Styles::Colors::TextSecondary); // Dimmer for "+N more" badges

    for (const auto &label : m_labels) {
        const int textY = label.rect.y() + label.rect.height() - 2;
        const bool isBadge = label.callsign.startsWith('+');
        K4Styles::drawHaloText(painter, QPointF(label.rect.x(), textY), label.callsign,
                               isBadge ? badgeColor : textColor);
    }
}

void DxSpotOverlay::mousePressEvent(QMouseEvent *event) {
    const Qt::MouseButton button = event->button();
    if (button == Qt::LeftButton || button == Qt::RightButton) {
        QPoint pos = event->pos();
        for (const auto &label : m_labels) {
            // WHY: pad the hit rect by 2 px in each direction. The painted text rect is only
            // one text line tall and the visible glyph has a 1 px outline halo; without padding the
            // user has to land pixel-perfect on the rect and easily clicks 1–2 px outside it.
            if (label.rect.adjusted(-2, -2, 2, 2).contains(pos)) {
                if (button == Qt::LeftButton)
                    emit spotClicked(label.frequencyHz);
                else
                    emit spotRightClicked(label.frequencyHz);
                event->accept();
                return;
            }
        }
    }
    // Not on a spot label — pass through to panadapter click-tune (left → VFO A,
    // right → VFO B at the mouse-x frequency).
    event->ignore();
}

void DxSpotOverlay::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    // Label positions and click targets are laid out against the width and height.
    layoutLabels();
}
