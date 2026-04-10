#include "dxspotoverlay.h"
#include "k4styles.h"

#include <QMouseEvent>
#include <QPainter>

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

    // Dynamic row count based on available vertical space
    int availableHeight = height() - BOTTOM_MARGIN - TOP_MARGIN;
    if (availableHeight < ROW_HEIGHT)
        return;
    int maxRows = qMin(MAX_DISPLAY_ROWS, qMax(1, availableHeight / ROW_HEIGHT));

    QFont font = K4Styles::Fonts::paintFont(K4Styles::Dimensions::FontSizeSmall);
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
            // Take only the most recent spots (they're at the end since newer spots replace older)
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
        int labelLeft = xInt - textWidth / 2;
        int labelRight = labelLeft + textWidth;

        // Clamp to widget bounds
        if (labelLeft < 0) {
            labelLeft = 0;
            labelRight = textWidth;
        } else if (labelRight > width()) {
            labelRight = width();
            labelLeft = labelRight - textWidth;
        }

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
        // Bottom-up: row 0 just above the margin, higher rows stack upward
        int y = height() - BOTTOM_MARGIN - (assignedRow + 1) * ROW_HEIGHT;
        label.rect = QRect(labelLeft, y, textWidth, ROW_HEIGHT);
        m_labels.append(label);
    }

    // Position "+N" badges above the last used row at their frequency
    for (auto &label : m_labels) {
        if (label.row != -1)
            continue;
        // Find the highest row used at this X position
        int lastRow = 0;
        for (const auto &other : m_labels) {
            if (other.row >= 0 && qAbs(other.xPixel - label.xPixel) < 40)
                lastRow = qMax(lastRow, other.row);
        }
        int badgeRow = qMin(lastRow + 1, maxRows - 1);
        int tw = fm.horizontalAdvance(label.callsign);
        int lx = label.xPixel - tw / 2;
        label.row = badgeRow;
        int y = height() - BOTTOM_MARGIN - (badgeRow + 1) * ROW_HEIGHT;
        label.rect = QRect(lx, y, tw, ROW_HEIGHT);
    }
}

void DxSpotOverlay::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)
    if (m_labels.isEmpty())
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    QFont font = K4Styles::Fonts::paintFont(K4Styles::Dimensions::FontSizeSmall);
    painter.setFont(font);

    QColor textColor(K4Styles::Colors::VfoACyan);
    QColor badgeColor(K4Styles::Colors::TextGray); // Dimmer for "+N more" badges
    QColor outlineColor(0, 0, 0, 153);             // Black at 60% alpha

    QPen outlinePen(outlineColor);
    outlinePen.setWidth(1);
    QPen textPen(textColor);

    for (const auto &label : m_labels) {
        int textY = label.rect.y() + label.rect.height() - 2;

        // Text outline (draw text offset in 4 directions for readability)
        painter.setPen(outlinePen);
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                if (dx == 0 && dy == 0)
                    continue;
                painter.drawText(label.rect.x() + dx, textY + dy, label.callsign);
            }
        }

        // Foreground text — use dimmer color for "+N" overflow badges
        bool isBadge = label.callsign.startsWith('+');
        painter.setPen(isBadge ? QPen(badgeColor) : textPen);
        painter.drawText(label.rect.x(), textY, label.callsign);
    }
}

void DxSpotOverlay::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        QPoint pos = event->pos();
        for (const auto &label : m_labels) {
            if (label.rect.contains(pos)) {
                emit spotClicked(label.frequencyHz);
                event->accept();
                return;
            }
        }
    }
    // Not on a spot label — pass through to panadapter
    event->ignore();
}
