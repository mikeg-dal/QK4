#include "txmeterwidget.h"
#include "k4styles.h"
#include <QPainter>
#include <QLinearGradient>

TxMeterWidget::TxMeterWidget(QWidget *parent) : QWidget(parent) {
    // 5 meters, each ~26px high with spacing (matches KPA1500 meter sizing)
    setFixedHeight(130);
    setMinimumWidth(200);
    setMaximumWidth(380);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    // Transparent background - let parent show through
    setAttribute(Qt::WA_TranslucentBackground);

    // Setup decay timer
    m_decayTimer = new QTimer(this);
    connect(m_decayTimer, &QTimer::timeout, this, &TxMeterWidget::decayValues);
    m_decayTimer->start(DecayIntervalMs);
}

void TxMeterWidget::setPower(double watts, bool isQrp) {
    m_isQrp = isQrp;
    double maxPower = isQrp ? MaxPowerQRP : MaxPowerDefault;
    double ratio = qMin(watts / maxPower, 1.0);
    m_powerTarget = ratio;
    if (ratio > m_powerDisplay) {
        m_powerDisplay = ratio; // Instant rise
    }
    if (ratio > m_powerPeak) {
        m_powerPeak = ratio;
        m_powerPeakHold = PeakHoldTicks;
    }
    update();
}

void TxMeterWidget::setAlc(int bars) {
    // K4 ALC meter is 0-7 bars (labeled 1, 3, 5, 7)
    double ratio = qBound(0, bars, MaxAlcBars) / static_cast<double>(MaxAlcBars);
    m_alcTarget = ratio;
    if (ratio > m_alcDisplay) {
        m_alcDisplay = ratio;
    }
    if (ratio > m_alcPeak) {
        m_alcPeak = ratio;
        m_alcPeakHold = PeakHoldTicks;
    }
    update();
}

void TxMeterWidget::setCompression(int dB) {
    double ratio = qBound(0, dB, MaxCompressionDb) / static_cast<double>(MaxCompressionDb);
    m_compTarget = ratio;
    if (ratio > m_compDisplay) {
        m_compDisplay = ratio;
    }
    if (ratio > m_compPeak) {
        m_compPeak = ratio;
        m_compPeakHold = PeakHoldTicks;
    }
    update();
}

void TxMeterWidget::setSwr(double ratio) {
    // SWR scale: 1.0 to 3.0, map to 0-1 fill ratio
    double fillRatio = qMin((qMax(1.0, ratio) - 1.0) / MaxSwrScale, 1.0);
    m_swrTarget = fillRatio;
    if (fillRatio > m_swrDisplay) {
        m_swrDisplay = fillRatio;
    }
    if (fillRatio > m_swrPeak) {
        m_swrPeak = fillRatio;
        m_swrPeakHold = PeakHoldTicks;
    }
    update();
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
    }
}

void TxMeterWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false); // Crisp pixel lines

    int w = width();
    int h = height();

    // No background fill - let parent widget show through

    // Layout constants (matched to KPA1500 meter styling)
    const int labelWidth = 40; // Width of label box (Po, ALC, etc.)
    const int rowHeight = 24;  // Height per meter row
    const int barStartX = labelWidth + 4;
    const int barWidth = w - barStartX - 4;
    const int barHeight = 14;
    const int spacing = 2;

    // Font for labels (10pt bold, matches KPA1500)
    QFont labelFont = font();
    labelFont.setPixelSize(K4Styles::Dimensions::FontSizeMedium);
    labelFont.setBold(true);
    painter.setFont(labelFont);

    // Scale font (8pt, matches KPA1500)
    QFont scaleFont = font();
    scaleFont.setPixelSize(K4Styles::Dimensions::FontSizeSmall);

    int y = 0;

    // === S/Po (S-Meter when RX, Power when TX) - Gradient ===
    {
        QStringList labels;
        double displayValue, peakValue;

        if (!m_isTransmitting) {
            // RX mode: show S-meter
            labels = {"1", "3", "5", "7", "9", "+20", "+40", "+60"};
            displayValue = m_sMeterDisplay;
            peakValue = m_sMeterPeak;
        } else if (m_isQrp) {
            // TX mode with K4 QRP: 0-10W scale
            labels = {"0", "2", "4", "6", "8", "10W"};
            displayValue = m_powerDisplay;
            peakValue = m_powerPeak;
        } else {
            // TX mode with K4: 0-110W scale
            labels = {"0", "22", "44", "66", "88", "110W"};
            displayValue = m_powerDisplay;
            peakValue = m_powerPeak;
        }

        drawMeterRow(painter, y, rowHeight, "S/Po", displayValue, peakValue, labels, scaleFont, barStartX, barWidth,
                     barHeight, MeterType::Gradient);
        y += rowHeight + spacing;
    }

    // === ALC - Gradient ===
    {
        QStringList labels = {"", "1", "3", "5", "7"};
        drawMeterRow(painter, y, rowHeight, "ALC", m_alcDisplay, m_alcPeak, labels, scaleFont, barStartX, barWidth,
                     barHeight, MeterType::Gradient);
        y += rowHeight + spacing;
    }

    // === COMP - Gradient ===
    {
        QStringList labels = {"0", "5", "10", "15", "20", "dB"};
        drawMeterRow(painter, y, rowHeight, "COMP", m_compDisplay, m_compPeak, labels, scaleFont, barStartX, barWidth,
                     barHeight, MeterType::Gradient);
        y += rowHeight + spacing;
    }

    // === SWR - Gradient ===
    {
        QStringList labels = {"1", "1.5", "2", "2.5", "3", QString::fromUtf8("\u221E")};
        drawMeterRow(painter, y, rowHeight, "SWR", m_swrDisplay, m_swrPeak, labels, scaleFont, barStartX, barWidth,
                     barHeight, MeterType::Gradient);
        y += rowHeight + spacing;
    }

    // === Id (PA Drain Current) - Red ===
    {
        QStringList labels = {"0", "5", "10", "15", "20", "25A"};
        drawMeterRow(painter, y, rowHeight, "Id", m_currentDisplay, m_currentPeak, labels, scaleFont, barStartX,
                     barWidth, barHeight, MeterType::Red);
    }
}

void TxMeterWidget::drawMeterRow(QPainter &painter, int y, int rowHeight, const QString &label, double fillRatio,
                                 double peakRatio, const QStringList &scaleLabels, const QFont &scaleFont,
                                 int barStartX, int barWidth, int barHeight, MeterType type) {
    // Label box on the left (wider for larger font)
    QRect labelRect(2, y + 2, 36, rowHeight - 4);
    painter.setPen(QColor(K4Styles::Colors::InactiveGray));
    painter.setBrush(QColor(K4Styles::Colors::Background));
    painter.drawRect(labelRect);

    // Label text (10pt bold, matches KPA1500)
    painter.setPen(QColor(K4Styles::Colors::TextWhite));
    QFont labelFont = font();
    labelFont.setPixelSize(K4Styles::Dimensions::FontSizeMedium);
    labelFont.setBold(true);
    painter.setFont(labelFont);
    painter.drawText(labelRect, Qt::AlignCenter, label);

    // Meter bar track (dark background)
    int barY = y + 2;
    QRect trackRect(barStartX, barY, barWidth, barHeight);
    painter.fillRect(trackRect, QColor(K4Styles::Colors::Background));
    painter.setPen(QColor(K4Styles::Colors::InactiveGray));
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
        painter.setPen(QPen(QColor(K4Styles::Colors::TextWhite), 2));
        painter.drawLine(peakX - 1, barY, peakX - 1, barY + barHeight);
    }

    // Scale labels below bar
    painter.setFont(scaleFont);
    int scaleY = barY + barHeight + 1;
    int numLabels = scaleLabels.size();
    if (numLabels > 0) {
        for (int i = 0; i < numLabels; i++) {
            if (scaleLabels[i].isEmpty())
                continue;
            // Color +dB labels red (S-meter over S9)
            if (scaleLabels[i].startsWith('+')) {
                painter.setPen(QColor(K4Styles::Colors::TxRed));
            } else {
                painter.setPen(QColor(K4Styles::Colors::TextGray));
            }
            int x = barStartX + (barWidth * i) / (numLabels - 1);
            // Center the label, but keep last one right-aligned
            int labelW = 20;
            int labelX = (i == numLabels - 1) ? x - labelW : x - labelW / 2;
            painter.drawText(labelX, scaleY, labelW, 8, Qt::AlignCenter, scaleLabels[i]);
        }
    }

    // Draw tick marks on the bar
    painter.setPen(QColor(K4Styles::Colors::InactiveGray));
    for (int i = 0; i < numLabels; i++) {
        int x = barStartX + (barWidth * i) / (numLabels - 1);
        painter.drawLine(x, barY, x, barY + 2);
    }
}
