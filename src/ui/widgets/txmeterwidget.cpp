#include "ui/widgets/txmeterwidget.h"
#include "ui/styling/k4styles.h"
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <limits>

TxMeterWidget::TxMeterWidget(QWidget *parent) : QWidget(parent) {
    // 5 meter rows of 24 px plus 2 px spacing
    setFixedHeight(130);
    setMinimumWidth(200);
    setMaximumWidth(380);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    // Transparent background - let parent show through
    setAttribute(Qt::WA_TranslucentBackground);

    // WHY started on demand: the decay only has work while a bar or peak sits above its target.
    // Free-running, both VFOs' meters woke the GUI thread 40 times a second while idle and
    // disconnected (#92).
    m_decayTimer = new QTimer(this);
    m_decayTimer->setInterval(DecayIntervalMs);
    connect(m_decayTimer, &QTimer::timeout, this, &TxMeterWidget::decayValues);
}

void TxMeterWidget::startDecay() {
    if (!m_decayTimer->isActive())
        m_decayTimer->start();
}

void TxMeterWidget::setCurrent(double amps) {
    double ratio = qMin(qMax(0.0, amps) / MaxCurrentAmps, 1.0);
    m_currentTarget = ratio;
    if (ratio > m_currentDisplay) {
        m_currentDisplay = ratio;
    }
    if (ratio > m_currentPeak) {
        m_currentPeak = ratio;
        m_currentPeakHold = PeakHoldTicks;
    }
    startDecay();
    update();
}

void TxMeterWidget::setQrp(bool isQrp) {
    if (m_isQrp != isQrp) {
        m_isQrp = isQrp;
        update();
    }
}

void TxMeterWidget::setTxMeters(int alc, int compDb, double fwdPower, double swr) {
    // Power - use appropriate max based on mode
    double maxPower = m_isQrp ? MaxPowerQRP : MaxPowerQRO;
    double powerRatio = qMin(fwdPower / maxPower, 1.0);
    m_powerTarget = powerRatio;
    if (powerRatio > m_powerDisplay)
        m_powerDisplay = powerRatio;
    if (powerRatio > m_powerPeak) {
        m_powerPeak = powerRatio;
        m_powerPeakHold = PeakHoldTicks;
    }

    // ALC - K4 ALC meter is 0-7 bars (labeled 1, 3, 5, 7)
    double alcRatio = qBound(0, alc, MaxAlcBars) / static_cast<double>(MaxAlcBars);
    m_alcTarget = alcRatio;
    if (alcRatio > m_alcDisplay)
        m_alcDisplay = alcRatio;
    if (alcRatio > m_alcPeak) {
        m_alcPeak = alcRatio;
        m_alcPeakHold = PeakHoldTicks;
    }

    // Compression
    double compRatio = qBound(0, compDb, MaxCompressionDb) / static_cast<double>(MaxCompressionDb);
    m_compTarget = compRatio;
    if (compRatio > m_compDisplay)
        m_compDisplay = compRatio;
    if (compRatio > m_compPeak) {
        m_compPeak = compRatio;
        m_compPeakHold = PeakHoldTicks;
    }

    // SWR
    double swrRatio = qMin((qMax(1.0, swr) - 1.0) / MaxSwrScale, 1.0);
    m_swrTarget = swrRatio;
    if (swrRatio > m_swrDisplay)
        m_swrDisplay = swrRatio;
    if (swrRatio > m_swrPeak) {
        m_swrPeak = swrRatio;
        m_swrPeakHold = PeakHoldTicks;
    }

    startDecay();
    update();
}

void TxMeterWidget::setSMeter(double sValue) {
    // S-meter value: 0-9 for S1-S9, 9+ for dB over S9 (S9+10 = 10, S9+20 = 11, etc.)
    double ratio = qMin(sValue / MaxSMeter, 1.0);
    m_sMeterTarget = ratio;
    if (ratio > m_sMeterDisplay) {
        m_sMeterDisplay = ratio; // Instant rise
    }
    if (ratio > m_sMeterPeak) {
        m_sMeterPeak = ratio;
        m_sMeterPeakHold = PeakHoldTicks;
    }
    startDecay();
    update();
}

void TxMeterWidget::setTransmitting(bool isTx) {
    if (m_isTransmitting != isTx) {
        m_isTransmitting = isTx;
        update();
    }
}

void TxMeterWidget::decayValues() {
    bool needsUpdate = false;

    // Decay display values toward targets
    auto decayValue = [&](double &display, double target) {
        if (display > target) {
            display -= DecayRate;
            if (display < target)
                display = target;
            needsUpdate = true;
        }
    };

    // Decay peak values (with hold time)
    auto decayPeak = [&](double &peak, double display, int &holdCounter) {
        if (peak > display) {
            if (holdCounter > 0) {
                holdCounter--; // Wait during hold period
            } else {
                peak -= PeakDecayRate;
                if (peak < display)
                    peak = display;
            }
            needsUpdate = true;
        }
    };

    decayValue(m_powerDisplay, m_powerTarget);
    decayValue(m_alcDisplay, m_alcTarget);
    decayValue(m_compDisplay, m_compTarget);
    decayValue(m_swrDisplay, m_swrTarget);
    decayValue(m_currentDisplay, m_currentTarget);
    decayValue(m_sMeterDisplay, m_sMeterTarget); // S-meter decay

    decayPeak(m_powerPeak, m_powerDisplay, m_powerPeakHold);
    decayPeak(m_alcPeak, m_alcDisplay, m_alcPeakHold);
    decayPeak(m_compPeak, m_compDisplay, m_compPeakHold);
    decayPeak(m_swrPeak, m_swrDisplay, m_swrPeakHold);
    decayPeak(m_currentPeak, m_currentDisplay, m_currentPeakHold);
    decayPeak(m_sMeterPeak, m_sMeterDisplay, m_sMeterPeakHold); // S-meter peak decay

    if (needsUpdate) {
        update();
    } else {
        m_decayTimer->stop();
    }
}

void TxMeterWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false); // Crisp pixel lines

    int w = width();
    int h = height();

    // No background fill - let parent widget show through

    // Layout constants
    const int labelWidth = 40; // Width of label box (Po, ALC, etc.)
    const int rowHeight = 24;  // Height per meter row
    const int barStartX = labelWidth + 4;
    const int barWidth = w - barStartX - 4;
    const int barHeight = 14;
    const int spacing = 2;

    QFont labelFont = font();
    labelFont.setPixelSize(K4Styles::Dimensions::FontSizeCaption);
    labelFont.setBold(true);
    painter.setFont(labelFont);

    const QFont scaleFont = K4Styles::Fonts::dataFont(K4Styles::Dimensions::FontSizeFloor, QFont::Medium);

    int y = 0;

    // === S/Po (S-Meter when RX, Power when TX) - Gradient ===
    {
        QList<ScaleMark> marks;
        double displayValue, peakValue;

        if (!m_isTransmitting) {
            // RX mode: show S-meter.
            marks = sMeterMarks();
            displayValue = m_sMeterDisplay;
            peakValue = m_sMeterPeak;
        } else if (m_isQrp) {
            // TX mode with K4 QRP: 0-10W scale
            marks = powerMarks(true);
            displayValue = m_powerDisplay;
            peakValue = m_powerPeak;
        } else {
            // TX mode with K4: 0-110W scale
            marks = powerMarks(false);
            displayValue = m_powerDisplay;
            peakValue = m_powerPeak;
        }

        drawMeterRow(painter, y, rowHeight, "S/Po", displayValue, peakValue, marks, scaleFont, barStartX, barWidth,
                     barHeight, MeterType::Gradient);
        y += rowHeight + spacing;
    }

    // === ALC - Gradient ===
    {
        const QList<ScaleMark> marks = alcMarks();
        drawMeterRow(painter, y, rowHeight, "ALC", m_alcDisplay, m_alcPeak, marks, scaleFont, barStartX, barWidth,
                     barHeight, MeterType::Gradient);
        y += rowHeight + spacing;
    }

    // === COMP - Gradient ===
    {
        const QList<ScaleMark> marks = compMarks();
        drawMeterRow(painter, y, rowHeight, "COMP", m_compDisplay, m_compPeak, marks, scaleFont, barStartX, barWidth,
                     barHeight, MeterType::Gradient);
        y += rowHeight + spacing;
    }

    // === SWR - Gradient ===
    {
        const QList<ScaleMark> marks = swrMarks();
        drawMeterRow(painter, y, rowHeight, "SWR", m_swrDisplay, m_swrPeak, marks, scaleFont, barStartX, barWidth,
                     barHeight, MeterType::Gradient);
        y += rowHeight + spacing;
    }

    // === Id (PA Drain Current) - Green ===
    {
        const QList<ScaleMark> marks = currentMarks();
        drawMeterRow(painter, y, rowHeight, "Id", m_currentDisplay, m_currentPeak, marks, scaleFont, barStartX,
                     barWidth, barHeight, MeterType::Red);
    }
}

void TxMeterWidget::drawMeterRow(QPainter &painter, int y, int rowHeight, const QString &label, double fillRatio,
                                 double peakRatio, const QList<ScaleMark> &marks, const QFont &scaleFont, int barStartX,
                                 int barWidth, int barHeight, MeterType type) {
    // Label box on the left
    QRect labelRect(2, y + 2, 36, rowHeight - 4);
    painter.setPen(QColor(K4Styles::Colors::MutedGraphic));
    painter.setBrush(QColor(K4Styles::Colors::Background));
    painter.drawRect(labelRect);

    painter.setPen(QColor(K4Styles::Colors::TextPrimary));
    QFont labelFont = font();
    labelFont.setPixelSize(K4Styles::Dimensions::FontSizeCaption);
    labelFont.setBold(true);
    painter.setFont(labelFont);
    painter.drawText(labelRect, Qt::AlignCenter, label);

    // Meter bar track (dark background)
    int barY = y + 2;
    QRect trackRect(barStartX, barY, barWidth, barHeight);
    painter.fillRect(trackRect, QColor(K4Styles::Colors::Background));
    painter.setPen(QColor(K4Styles::Colors::MutedGraphic));
    painter.drawRect(trackRect);

    // Filled meter bar
    if (fillRatio > 0.001) {
        int fillWidth = static_cast<int>(barWidth * fillRatio);
        QLinearGradient gradient(barStartX, 0, barStartX + barWidth, 0);

        if (type == MeterType::Gradient) {
            // Standard meter gradient: green → yellow → orange → red
            gradient = K4Styles::meterGradient(barStartX, 0, barStartX + barWidth, 0);
        } else {
            // Green style for Id meter (PA drain current)
            gradient.setColorAt(0.0, QColor(K4Styles::Colors::MeterIdDark));
            gradient.setColorAt(0.7, QColor(K4Styles::Colors::MeterIdDark));
            gradient.setColorAt(1.0, QColor(K4Styles::Colors::MeterIdLight));
        }
        painter.fillRect(barStartX + 1, barY + 1, fillWidth - 2, barHeight - 2, gradient);
    }

    // Draw peak indicator
    if (peakRatio > 0.01) {
        int peakX = barStartX + static_cast<int>(barWidth * peakRatio);
        painter.setPen(QPen(QColor(K4Styles::Colors::TextPrimary), 2));
        painter.drawLine(peakX - 1, barY, peakX - 1, barY + barHeight);
    }

    // Scale labels below bar, each at its own position along the bar. The digits hang from the bar:
    // baseline one cap height below it, so the glyphs fit the 2 px row spacing plus the gap under
    // the bar without growing the row (nothing here has a descender).
    painter.setFont(scaleFont);
    const QFontMetricsF fm(scaleFont, this);
    const qreal baselineY = barY + barHeight + 2 + fm.capHeight();
    // Right to left, so the end label (which carries the unit) always wins; a label that would
    // touch the one to its right is dropped rather than overprinted on a narrow meter.
    qreal leftmostDrawn = std::numeric_limits<qreal>::max();
    for (auto it = marks.crbegin(); it != marks.crend(); ++it) {
        const ScaleMark &mark = *it;
        if (mark.text.isEmpty())
            continue;
        const qreal x = barStartX + static_cast<int>(barWidth * mark.pos);
        const qreal textW = fm.horizontalAdvance(mark.text);
        const qreal textX = std::round(scaleLabelLeft(x, textW, barStartX, barStartX + barWidth));
        if (textX + textW + 2.0 > leftmostDrawn)
            continue;
        // Over-S9 marks are red. TextRed, not TxRed: pure red is under 4.5:1 on the dark background.
        painter.setPen(QColor(mark.text.startsWith('+') ? K4Styles::Colors::TextRed : K4Styles::Colors::TextSecondary));
        painter.drawText(QPointF(textX, baselineY), mark.text);
        leftmostDrawn = textX;
    }

    // Draw tick marks on the bar
    painter.setPen(QColor(K4Styles::Colors::MutedGraphic));
    for (const ScaleMark &mark : marks) {
        int x = barStartX + static_cast<int>(barWidth * mark.pos);
        painter.drawLine(x, barY, x, barY + 2);
    }
}

qreal TxMeterWidget::scaleLabelLeft(qreal markX, qreal textWidth, qreal barLeft, qreal barRight) {
    const qreal maxLeft = std::max(barLeft, barRight - textWidth);
    return std::clamp(markX - textWidth / 2.0, barLeft, maxLeft);
}

namespace {
// ratio = (value - min) / (max - min). Every meter on this widget is affine, including SWR,
// which simply starts at 1.0 rather than 0.
QList<TxMeterWidget::ScaleMark> affineMarks(const QList<double> &values, const QStringList &texts, double minValue,
                                            double maxValue) {
    QList<TxMeterWidget::ScaleMark> marks;
    marks.reserve(values.size());
    const double span = maxValue - minValue;
    for (int i = 0; i < values.size() && i < texts.size(); i++)
        marks.append({values[i], span > 0.0 ? (values[i] - minValue) / span : 0.0, texts[i]});
    return marks;
}
} // namespace

QList<TxMeterWidget::ScaleMark> TxMeterWidget::sMeterMarks() {
    // setSMeter maps S1..S9 to 1..9 and +20/+40/+60 over S9 to 11/13/15, so S1 sits at 1/15 of
    // the bar.
    return affineMarks({1, 3, 5, 7, 9, 11, 13, 15}, {"1", "3", "5", "7", "9", "+20", "+40", "+60"}, 0.0, MaxSMeter);
}

QList<TxMeterWidget::ScaleMark> TxMeterWidget::powerMarks(bool qrp) {
    if (qrp)
        return affineMarks({0, 2, 4, 6, 8, 10}, {"0", "2", "4", "6", "8", "10W"}, 0.0, MaxPowerQRP);
    return affineMarks({0, 22, 44, 66, 88, 110}, {"0", "22", "44", "66", "88", "110W"}, 0.0, MaxPowerQRO);
}

QList<TxMeterWidget::ScaleMark> TxMeterWidget::alcMarks() {
    // The K4 marks ALC 1/3/5/7, which are not evenly spaced over 0..7 and so cannot sit on evenly
    // spaced ticks. NOTE: the radio emits ALC 8 on occasion (observed on hardware); MaxAlcBars
    // stays 7 because that is what the scale is marked to, so 8 renders as full. Changing the
    // maximum without knowing the radio's true top of scale would be a guess.
    return affineMarks({1, 3, 5, 7}, {"1", "3", "5", "7"}, 0.0, MaxAlcBars);
}

QList<TxMeterWidget::ScaleMark> TxMeterWidget::compMarks() {
    return affineMarks({0, 5, 10, 15, 20, 25}, {"0", "5", "10", "15", "20", "25dB"}, 0.0, MaxCompressionDb);
}

QList<TxMeterWidget::ScaleMark> TxMeterWidget::swrMarks() {
    // setTxMeters maps SWR 1.0..3.0 onto the bar and clamps at 3, hence "3+" at the right end.
    return affineMarks({1.0, 1.5, 2.0, 2.5, 3.0}, {"1", "1.5", "2", "2.5", "3+"}, MinSwr, MinSwr + MaxSwrScale);
}

QList<TxMeterWidget::ScaleMark> TxMeterWidget::currentMarks() {
    return affineMarks({0, 5, 10, 15, 20, 25}, {"0", "5", "10", "15", "20", "25A"}, 0.0, MaxCurrentAmps);
}
