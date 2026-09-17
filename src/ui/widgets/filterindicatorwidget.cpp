#include "ui/widgets/filterindicatorwidget.h"
#include "models/radiostate.h"
#include "ui/styling/k4constants.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <algorithm>

FilterIndicatorWidget::FilterIndicatorWidget(QWidget *parent) : QWidget(parent) {
    setFixedSize(62, 62); // 50 * 1.25 = 62
    setCursor(Qt::PointingHandCursor);
}

void FilterIndicatorWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()))
        emit clicked();
    QWidget::mouseReleaseEvent(event);
}

void FilterIndicatorWidget::observe(RadioState *state, Vfo vfo) {
    if (!state)
        return;
    if (vfo == Vfo::A) {
        connect(state, &RadioState::filterPositionChanged, this, &FilterIndicatorWidget::setFilterPosition);
        connect(state, &RadioState::filterBandwidthChanged, this, &FilterIndicatorWidget::setBandwidth);
        connect(state, &RadioState::ifShiftChanged, this, &FilterIndicatorWidget::setShift);
        connect(state, &RadioState::modeChanged, this,
                [this](RadioState::Mode mode) { setMode(RadioState::modeToString(mode)); });
        connect(state, &RadioState::dataSubModeChanged, this, &FilterIndicatorWidget::setDataSubMode);
    } else {
        connect(state, &RadioState::filterPositionBChanged, this, &FilterIndicatorWidget::setFilterPosition);
        connect(state, &RadioState::filterBandwidthBChanged, this, &FilterIndicatorWidget::setBandwidth);
        connect(state, &RadioState::ifShiftBChanged, this, &FilterIndicatorWidget::setShift);
        connect(state, &RadioState::modeBChanged, this,
                [this](RadioState::Mode mode) { setMode(RadioState::modeToString(mode)); });
        connect(state, &RadioState::dataSubModeBChanged, this, &FilterIndicatorWidget::setDataSubMode);
    }
}

void FilterIndicatorWidget::setFilterPosition(int position) {
    if (position >= 1 && position <= 3 && position != m_filterPosition) {
        m_filterPosition = position;
        update();
    }
}

void FilterIndicatorWidget::setBandwidth(int bandwidthHz) {
    int clamped = std::clamp(bandwidthHz, m_minBandwidthHz, m_maxBandwidthHz);
    if (clamped != m_bandwidthHz) {
        m_bandwidthHz = clamped;
        update();
    }
}

void FilterIndicatorWidget::setShift(int shift) {
    // Shift is in decahertz (10 Hz units), range typically 0-300+ for SSB
    int clamped = std::clamp(shift, 0, 400);
    if (clamped != m_shift) {
        m_shift = clamped;
        update();
    }
}

void FilterIndicatorWidget::setMode(const QString &mode) {
    if (mode != m_mode) {
        m_mode = mode;
        update();
    }
}

void FilterIndicatorWidget::setDataSubMode(int subMode) {
    if (subMode != m_dataSubMode) {
        m_dataSubMode = subMode;
        update();
    }
}

void FilterIndicatorWidget::setBandwidthRange(int minHz, int maxHz) {
    m_minBandwidthHz = minHz;
    m_maxBandwidthHz = maxHz;
    update();
}

void FilterIndicatorWidget::setShapeColor(const QColor &fill, const QColor &outline) {
    m_shapeColor = fill;
    m_shapeOutline = outline;
    update();
}

void FilterIndicatorWidget::drawBandwidthShape(QPainter &painter, int lineY, int lineWidth) {
    // Shape height
    const float shapeHeight = 16.0f;

    // K4 uses discrete steps with more granularity in USB/SSB range (1500-3500 Hz)
    // Max visual growth reached at ~2.70 kHz

    float baseWidth;
    float topWidth;

    // Determine which step we're at based on bandwidth
    // More granular steps in the USB/SSB working range
    int step;
    if (m_bandwidthHz <= 200) {
        step = 0; // Triangle (CW narrow)
    } else if (m_bandwidthHz <= 400) {
        step = 1; // Small trapezoid
    } else if (m_bandwidthHz <= 600) {
        step = 2;
    } else if (m_bandwidthHz <= 900) {
        step = 3;
    } else if (m_bandwidthHz <= 1200) {
        step = 4;
    } else if (m_bandwidthHz <= 1600) {
        step = 5;
    } else if (m_bandwidthHz <= 2000) {
        step = 6;
    } else if (m_bandwidthHz <= 2400) {
        step = 7;
    } else if (m_bandwidthHz <= 2800) {
        step = 8;
    } else if (m_bandwidthHz <= 3200) {
        step = 9;
    } else {
        step = 10; // Max (3200+ Hz)
    }

    if (step == 0) {
        // Triangle
        baseWidth = 16.0f;
        topWidth = 0.0f;
    } else {
        // Trapezoid - discrete steps from small to full width
        // K4 shapes are compact - don't reach full line width until max
        const float maxBase = static_cast<float>(lineWidth) * 0.85f; // Don't go full width
        const float minBase = 16.0f;                                 // Start close to triangle size
        const int maxSteps = 10;

        // Linear interpolation across steps
        float stepNorm = static_cast<float>(step - 1) / static_cast<float>(maxSteps - 1);
        baseWidth = minBase + stepNorm * (maxBase - minBase);

        // Top ratio also grows with steps
        const float minTopRatio = 0.40f;
        const float maxTopRatio = 0.70f; // Slightly narrower top ratio
        float topToBaseRatio = minTopRatio + stepNorm * (maxTopRatio - minTopRatio);
        topWidth = baseWidth * topToBaseRatio;
    }

    // Calculate center X position based on shift
    // K4 IF shift is in decahertz (10 Hz units): shift=150 means 1500 Hz
    // Default center varies by mode:
    // - SSB/DATA: ~135 (1350 Hz passband center)
    // - CW: ~50 (500 Hz, based on CW pitch)
    // - AM/FM: Always centered (ignore shift value - K4 doesn't use shift for AM/FM)
    float centerX = width() / 2.0f;

    // AM/FM modes are always carrier-centered, ignore shift
    if (m_mode != "AM" && m_mode != "FM") {
        int defaultShift;
        if (m_mode == "CW" || m_mode == "CW-R") {
            defaultShift = 50; // 500 Hz (CW pitch)
        } else {
            defaultShift = 135; // 1350 Hz (SSB/DATA)
        }
        const float shiftRange = 100.0f; // ±1000 Hz visual range
        float shiftNorm = static_cast<float>(m_shift - defaultShift) / shiftRange;
        shiftNorm = std::clamp(shiftNorm, -1.0f, 1.0f);
        float maxShiftPx = (lineWidth - baseWidth) / 2.0f;
        if (maxShiftPx > 0) {
            centerX += shiftNorm * maxShiftPx;
        }
    }

    // Calculate shape vertices
    const float gapAboveLine = 4.0f;
    float bottomY = lineY - gapAboveLine;
    float topY = bottomY - shapeHeight;

    float bottomLeft = centerX - baseWidth / 2.0f;
    float bottomRight = centerX + baseWidth / 2.0f;
    float topLeft = centerX - topWidth / 2.0f;
    float topRight = centerX + topWidth / 2.0f;

    // Build polygon
    QPolygonF shape;
    if (topWidth < 1.0f) {
        // Triangle (apex at top)
        shape << QPointF(centerX, topY) << QPointF(bottomRight, bottomY) << QPointF(bottomLeft, bottomY);
    } else {
        // Trapezoid (4 points)
        shape << QPointF(topLeft, topY) << QPointF(topRight, topY) << QPointF(bottomRight, bottomY)
              << QPointF(bottomLeft, bottomY);
    }

    // Draw filled shape (no outline)
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_shapeColor);
    painter.drawPolygon(shape);
}

void FilterIndicatorWidget::drawRttyToneTriangles(QPainter &painter, int lineY, int lineWidth) {
    const float shapeHeight = 16.0f;
    const float gapAboveLine = 4.0f;
    const float triangleBaseWidth = 12.0f;

    // Place mark and space triangles so their bases just touch
    // Separation between centers = base width, so inner edges meet
    float separationPx = triangleBaseWidth;

    float centerX = width() / 2.0f;
    float bottomY = lineY - gapAboveLine;
    float topY = bottomY - shapeHeight;

    // Space triangle (left) and Mark triangle (right), symmetric around center
    float spaceCenterX = centerX - separationPx / 2.0f;
    float markCenterX = centerX + separationPx / 2.0f;

    painter.setPen(Qt::NoPen);
    painter.setBrush(m_shapeColor);

    // Space triangle (left)
    QPolygonF space;
    space << QPointF(spaceCenterX, topY) << QPointF(spaceCenterX + triangleBaseWidth / 2.0f, bottomY)
          << QPointF(spaceCenterX - triangleBaseWidth / 2.0f, bottomY);
    painter.drawPolygon(space);

    // Mark triangle (right)
    QPolygonF mark;
    mark << QPointF(markCenterX, topY) << QPointF(markCenterX + triangleBaseWidth / 2.0f, bottomY)
         << QPointF(markCenterX - triangleBaseWidth / 2.0f, bottomY);
    painter.drawPolygon(mark);
}

void FilterIndicatorWidget::drawPskToneTriangle(QPainter &painter, int lineY, int /* lineWidth */) {
    const float shapeHeight = 16.0f;
    const float gapAboveLine = 4.0f;
    const float triangleBaseWidth = 12.0f;

    float centerX = width() / 2.0f;
    float bottomY = lineY - gapAboveLine;
    float topY = bottomY - shapeHeight;

    painter.setPen(Qt::NoPen);
    painter.setBrush(m_shapeColor);

    QPolygonF triangle;
    triangle << QPointF(centerX, topY) << QPointF(centerX + triangleBaseWidth / 2.0f, bottomY)
             << QPointF(centerX - triangleBaseWidth / 2.0f, bottomY);
    painter.drawPolygon(triangle);
}

void FilterIndicatorWidget::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    int w = width();
    int h = height();

    // Line parameters
    int lineY = 40;
    int lineHeight = 3;
    int lineWidth = 58; // 38 + 20 (10px wider on each side)
    int lineX = (w - lineWidth) / 2;

    // Draw bandwidth shape above the line
    bool isDataMode = (m_mode == "DATA" || m_mode == "DATA-R");
    bool isRtty = isDataMode && (m_dataSubMode == 1 || m_dataSubMode == 2);
    bool isPskD = isDataMode && m_dataSubMode == 3;
    if (isRtty) {
        drawRttyToneTriangles(painter, lineY, lineWidth);
    } else if (isPskD) {
        drawPskToneTriangle(painter, lineY, lineWidth);
    } else {
        drawBandwidthShape(painter, lineY, lineWidth);
    }

    // Draw horizontal line
    QRectF lineRect(lineX, lineY, lineWidth, lineHeight);
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_lineColor);
    painter.drawRect(lineRect);

    // FIL text below line
    QFont textFont = font();
    textFont.setPixelSize(K4Styles::Dimensions::FontSizeButton);
    textFont.setBold(true);
    painter.setFont(textFont);
    painter.setPen(m_textColor);

    QString text = QString("FIL%1").arg(m_filterPosition);
    int textY = lineY + lineHeight + 2;
    QRectF textRect(0, textY, w, h - textY);
    painter.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop, text);
}

void FilterIndicatorWidget::resetToDefaults() {
    setBandwidth(0);
    setShift(50);
    setFilterPosition(1);
    setMode("");
    setDataSubMode(0);
}
