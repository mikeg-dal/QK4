#include "kpa1500panel.h"
#include "k4styles.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPainter>
#include <QLinearGradient>

KPA1500Panel::KPA1500Panel(QWidget *parent) : QWidget(parent) {
    setFixedSize(270, 270);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setupUi();

    // Setup decay timer for peak hold
    m_decayTimer = new QTimer(this);
    connect(m_decayTimer, &QTimer::timeout, this, &KPA1500Panel::onDecayTimer);
}

void KPA1500Panel::setupUi() {
    // Create button row at bottom
    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->setContentsMargins(8, 0, 8, 8);
    buttonLayout->setSpacing(4);

    m_modeBtn = new QPushButton("STANDBY", this);
    m_modeBtn->setFixedHeight(K4Styles::Dimensions::ButtonHeightSmall);
    m_modeBtn->setStyleSheet(K4Styles::panelButtonWithDisabled());
    connect(m_modeBtn, &QPushButton::clicked, this, [this]() { emit modeToggled(!m_operate); });

    m_atuBtn = new QPushButton("ATU", this);
    m_atuBtn->setFixedHeight(K4Styles::Dimensions::ButtonHeightSmall);
    m_atuBtn->setStyleSheet(K4Styles::panelButtonWithDisabled());
    connect(m_atuBtn, &QPushButton::clicked, this, [this]() { emit atuModeToggled(!m_atuIn); });

    m_antBtn = new QPushButton("ANT1", this);
    m_antBtn->setFixedHeight(K4Styles::Dimensions::ButtonHeightSmall);
    m_antBtn->setStyleSheet(K4Styles::panelButtonWithDisabled());
    connect(m_antBtn, &QPushButton::clicked, this, [this]() {
        int nextAnt = (m_antenna % 3) + 1; // 1→2→3→1
        emit antennaChanged(nextAnt);
    });

    m_tuneBtn = new QPushButton("TUNE", this);
    m_tuneBtn->setFixedHeight(K4Styles::Dimensions::ButtonHeightSmall);
    m_tuneBtn->setStyleSheet(K4Styles::panelButtonWithDisabled());
    connect(m_tuneBtn, &QPushButton::clicked, this, &KPA1500Panel::atuTuneRequested);

    buttonLayout->addWidget(m_modeBtn);
    buttonLayout->addWidget(m_atuBtn);
    buttonLayout->addWidget(m_antBtn);
    buttonLayout->addWidget(m_tuneBtn);

    // Main layout
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addStretch(); // Push buttons to bottom
    mainLayout->addLayout(buttonLayout);

    updateButtonStates();
}

void KPA1500Panel::setMode(bool operate) {
    if (m_operate != operate) {
        m_operate = operate;
        updateButtonStates();
        update();
    }
}

void KPA1500Panel::setAtuMode(bool in) {
    if (m_atuIn != in) {
        m_atuIn = in;
        updateButtonStates();
        update();
    }
}

void KPA1500Panel::setAntenna(int ant) {
    if (m_antenna != ant && ant >= 1 && ant <= 3) {
        m_antenna = ant;
        updateButtonStates();
        update();
    }
}

void KPA1500Panel::setForwardPower(float watts) {
    m_forwardPower = qBound(0.0f, watts, 1500.0f);
    // Update peak if new value is higher
    if (m_forwardPower > m_peakForwardPower) {
        m_peakForwardPower = m_forwardPower;
    }
    startDecayTimer();
    update();
}

void KPA1500Panel::setReflectedPower(float watts) {
    m_reflectedPower = qBound(0.0f, watts, 100.0f);
    // Update peak if new value is higher
    if (m_reflectedPower > m_peakReflectedPower) {
        m_peakReflectedPower = m_reflectedPower;
    }
    startDecayTimer();
    update();
}

void KPA1500Panel::setSWR(float swr) {
    m_swr = qMax(1.0f, swr);
    // Update peak if new value is higher
    if (m_swr > m_peakSwr) {
        m_peakSwr = m_swr;
    }
    startDecayTimer();
    update();
}

void KPA1500Panel::setTemperature(float celsius) {
    m_temperature = qBound(0.0f, celsius, 100.0f);
    startDecayTimer();
    update();
}

void KPA1500Panel::setFault(bool fault) {
    if (m_fault != fault) {
        m_fault = fault;
        update();
    }
}

void KPA1500Panel::setConnected(bool connected) {
    if (m_connected != connected) {
        m_connected = connected;
        m_modeBtn->setEnabled(connected);
        m_atuBtn->setEnabled(connected);
        m_antBtn->setEnabled(connected);
        m_tuneBtn->setEnabled(connected);
        update();
    }
}

void KPA1500Panel::startDecayTimer() {
    if (!m_decayTimer->isActive()) {
        m_decayTimer->start(DECAY_INTERVAL_MS);
    }
}

void KPA1500Panel::onDecayTimer() {
    bool needsUpdate = false;
    bool allSettled = true;

    // Helper to animate a display value toward target
    auto animate = [&](float &display, float target, float minStep) {
        if (qAbs(display - target) < minStep) {
            if (display != target) {
                display = target;
                needsUpdate = true;
            }
        } else if (display < target) {
            // Rising - use faster attack rate
            float step = (target - display) * ATTACK_RATE;
            display += qMax(step, minStep);
            if (display > target)
                display = target;
            needsUpdate = true;
            allSettled = false;
        } else {
            // Falling - use slower decay rate
            float step = (display - target) * DECAY_RATE;
            display -= qMax(step, minStep);
            if (display < target)
                display = target;
            needsUpdate = true;
            allSettled = false;
        }
    };

    // Animate display values toward actual values
    animate(m_displayForwardPower, m_forwardPower, 2.0f);     // 2W min step
    animate(m_displayReflectedPower, m_reflectedPower, 0.2f); // 0.2W min step
    animate(m_displaySwr, m_swr, 0.01f);                      // 0.01 min step
    animate(m_displayTemperature, m_temperature, 0.2f);       // 0.2°C min step

    // Decay peak markers (slower than display animation)
    if (m_peakForwardPower > m_forwardPower) {
        float decay = (m_peakForwardPower - m_forwardPower) * PEAK_DECAY_RATE;
        m_peakForwardPower -= qMax(decay, 1.0f);
        if (m_peakForwardPower < m_forwardPower) {
            m_peakForwardPower = m_forwardPower;
        }
        needsUpdate = true;
        allSettled = false;
    }

    if (m_peakReflectedPower > m_reflectedPower) {
        float decay = (m_peakReflectedPower - m_reflectedPower) * PEAK_DECAY_RATE;
        m_peakReflectedPower -= qMax(decay, 0.1f);
        if (m_peakReflectedPower < m_reflectedPower) {
            m_peakReflectedPower = m_reflectedPower;
        }
        needsUpdate = true;
        allSettled = false;
    }

    if (m_peakSwr > m_swr) {
        float decay = (m_peakSwr - m_swr) * PEAK_DECAY_RATE;
        m_peakSwr -= qMax(decay, 0.005f);
        if (m_peakSwr < m_swr) {
            m_peakSwr = m_swr;
        }
        needsUpdate = true;
        allSettled = false;
    }

    // Stop timer when everything has settled
    if (allSettled) {
        m_decayTimer->stop();
    }

    if (needsUpdate) {
        update();
    }
}

void KPA1500Panel::updateButtonStates() {
    m_modeBtn->setText(m_operate ? "OPERATE" : "STANDBY");
    // ATU button stays "ATU" - status shown in header labels
    m_antBtn->setText(QString("ANT%1").arg(m_antenna));
}

void KPA1500Panel::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    int w = width();

    // Background with border
    painter.fillRect(rect(), QColor(K4Styles::Colors::Background));
    painter.setPen(QPen(QColor(K4Styles::Colors::BorderNormal), 1));
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 6, 6);

    // Status labels drawn at top
    const int statusY = 4;
    const int statusHeight = 14;
    drawStatusLabels(painter, statusY, statusHeight);

    // Separator line
    painter.setPen(QColor(K4Styles::Colors::BorderNormal));
    painter.drawLine(8, statusY + statusHeight + 2, w - 8, statusY + statusHeight + 2);

    // Meter layout constants
    const int meterSpacing = 50; // Height per full meter
    int meterY = statusY + statusHeight + 6;

    // FWD meter (0-1500W)
    float fwdRatio = m_displayForwardPower / 1500.0f;
    float fwdPeakRatio = m_peakForwardPower / 1500.0f;
    QString fwdValue = QString("%1W").arg(qRound(m_displayForwardPower));
    drawMeter(painter, meterY, "FWD", fwdValue, fwdRatio, fwdPeakRatio, {"0", "500", "1000", "1500"});
    meterY += meterSpacing;

    // SWR meter (1.0-3.0) - decays naturally like FWD
    float swrRatio = qMax(0.0f, (m_displaySwr - 1.0f) / 2.0f);
    float swrPeakRatio = qMax(0.0f, (m_peakSwr - 1.0f) / 2.0f);
    QString swrValue = m_displaySwr >= 3.0f ? ">3.0" : QString::number(qMax(1.0f, m_displaySwr), 'f', 1);
    drawMeter(painter, meterY, "SWR", swrValue, swrRatio, swrPeakRatio, {"1.0", "1.5", "2.0", "2.5", "3.0"});
    meterY += meterSpacing;

    // REF meter (0-100W) - decays naturally like FWD
    float refRatio = m_displayReflectedPower / 100.0f;
    float refPeakRatio = m_peakReflectedPower / 100.0f;
    QString refValue = QString("%1W").arg(qRound(m_displayReflectedPower));
    drawMeter(painter, meterY, "REF", refValue, refRatio, refPeakRatio, {"0", "25", "50", "75", "100"});
    meterY += meterSpacing;

    // TMP meter (0-100°C) - slightly smaller
    float tmpRatio = m_displayTemperature / 100.0f;
    QString tmpValue = QString("%1°C").arg(qRound(m_displayTemperature));
    drawMeter(painter, meterY, "TMP", tmpValue, tmpRatio, tmpRatio, {"0", "25", "50", "75", "100"}, false);
}

void KPA1500Panel::drawStatusLabels(QPainter &painter, int y, int height) {
    int w = width();

    QFont labelFont = font();
    labelFont.setPixelSize(K4Styles::Dimensions::FontSizeNormal);
    labelFont.setBold(true);
    painter.setFont(labelFont);

    int margin = 8;
    int thirdW = (w - margin * 2) / 3;

    // OPERATE/STANDBY (left third)
    QString modeText = m_operate ? "OPERATE" : "STANDBY";
    QColor modeColor = m_operate ? QColor(K4Styles::Colors::StatusGreen) : QColor(K4Styles::Colors::InactiveGray);
    painter.setPen(modeColor);
    painter.drawText(margin, y, thirdW, height, Qt::AlignCenter, modeText);

    // ATU IN/BYP (center third)
    QString atuText = m_atuIn ? "ATU IN" : "ATU BYP";
    QColor atuColor = m_atuIn ? QColor(K4Styles::Colors::StatusGreen) : QColor(K4Styles::Colors::InactiveGray);
    painter.setPen(atuColor);
    painter.drawText(margin + thirdW, y, thirdW, height, Qt::AlignCenter, atuText);

    // FAULT (right third, only if fault)
    if (m_fault) {
        painter.setPen(QColor(K4Styles::Colors::MeterRed));
        painter.drawText(margin + thirdW * 2, y, thirdW, height, Qt::AlignCenter, "FAULT");
    }
}

void KPA1500Panel::drawMeter(QPainter &painter, int y, const QString &label, const QString &valueStr,
                             float displayRatio, float peakRatio, const QStringList &scaleLabels, bool large) {
    int w = width();
    const int margin = 8;
    const int labelWidth = 32;
    const int valueWidth = large ? 55 : 45;
    const int barHeight = large ? 14 : 10;
    const int fontSize = large ? 10 : 9;
    const int valueFontSize = large ? 14 : 11;

    // Label
    QFont labelFont = font();
    labelFont.setPixelSize(fontSize);
    labelFont.setBold(true);
    painter.setFont(labelFont);
    painter.setPen(QColor(K4Styles::Colors::TextWhite));
    painter.drawText(margin, y, labelWidth, barHeight, Qt::AlignLeft | Qt::AlignVCenter, label);

    // Value (right side)
    QFont valueFont = font();
    valueFont.setPixelSize(valueFontSize);
    valueFont.setBold(true);
    painter.setFont(valueFont);
    painter.drawText(w - margin - valueWidth, y, valueWidth, barHeight + 6, Qt::AlignRight | Qt::AlignVCenter,
                     valueStr);

    // Bar dimensions
    int barX = margin + labelWidth + 4;
    int barY = y + 4;
    int barWidth = w - barX - margin - valueWidth - 4;

    // Meter track
    QRect trackRect(barX, barY, barWidth, barHeight);
    painter.fillRect(trackRect, QColor(K4Styles::Colors::DarkBackground));
    painter.setPen(QColor(K4Styles::Colors::GradientBottom));
    painter.drawRect(trackRect);

    // Filled bar
    if (displayRatio > 0.001f) {
        int fillWidth = static_cast<int>(barWidth * qMin(displayRatio, 1.0f));
        QLinearGradient gradient = K4Styles::meterGradient(barX, 0, barX + barWidth, 0);
        painter.fillRect(barX + 1, barY + 1, fillWidth - 2, barHeight - 2, gradient);
    }

    // Peak marker
    if (peakRatio > displayRatio + 0.01f) {
        drawPeakMarker(painter, barX, barY, barWidth, barHeight, peakRatio);
    }

    // Scale labels below bar
    QFont scaleFont = font();
    scaleFont.setPixelSize(K4Styles::Dimensions::FontSizeSmall);
    painter.setFont(scaleFont);
    painter.setPen(QColor(K4Styles::Colors::TextGray));

    int scaleY = barY + barHeight + 2;
    int numLabels = scaleLabels.size();
    for (int i = 0; i < numLabels; i++) {
        int x = barX + (barWidth * i) / (numLabels - 1);
        int labelW = 36;
        int labelX = x - labelW / 2;
        if (i == 0)
            labelX = x;
        else if (i == numLabels - 1)
            labelX = x - labelW;
        painter.drawText(labelX, scaleY, labelW, 10, Qt::AlignCenter, scaleLabels[i]);
    }

    // Tick marks
    painter.setPen(QColor(K4Styles::Colors::InactiveGray));
    for (int i = 0; i < numLabels; i++) {
        int x = barX + (barWidth * i) / (numLabels - 1);
        painter.drawLine(x, barY, x, barY + 2);
    }
}

void KPA1500Panel::drawPeakMarker(QPainter &painter, int barX, int barY, int barWidth, int barHeight, float peakRatio) {
    // Draw a thin vertical line at the peak position
    int peakX = barX + static_cast<int>(barWidth * qMin(peakRatio, 1.0f));

    // Clamp to bar bounds
    peakX = qBound(barX + 1, peakX, barX + barWidth - 2);

    // Draw peak marker - bright white line
    painter.setPen(QPen(QColor(K4Styles::Colors::TextWhite), 2));
    painter.drawLine(peakX, barY + 1, peakX, barY + barHeight - 1);
}
