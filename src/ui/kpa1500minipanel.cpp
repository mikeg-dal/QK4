#include "kpa1500minipanel.h"
#include "k4styles.h"
#include <QGridLayout>
#include <QPainter>
#include <QVBoxLayout>

Kpa1500MiniPanel::Kpa1500MiniPanel(QWidget *parent) : QWidget(parent) {
    // Main layout: top margin reserves space for painted meters + status/ATU lines, buttons below
    int metersHeight = METER_START_Y + (METER_SPACING * 4) + ATU_LABEL_HEIGHT + 6;
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, metersHeight, 0, 0);
    layout->setSpacing(4);

    // 2x2 button grid
    auto *btnGrid = new QGridLayout();
    btnGrid->setContentsMargins(0, 6, 0, 0);
    btnGrid->setHorizontalSpacing(4);
    btnGrid->setVerticalSpacing(8);

    // Helper to create button in container (matches createFunctionButton pattern)
    auto makeBtn = [this](const QString &text, QPushButton *&btnOut) -> QWidget * {
        auto *container = new QWidget(this);
        auto *vbox = new QVBoxLayout(container);
        vbox->setContentsMargins(0, 2, 0, 2);
        vbox->setSpacing(0);
        btnOut = new QPushButton(text, container);
        btnOut->setFixedHeight(K4Styles::Dimensions::ButtonHeightSmall);
        btnOut->setCursor(Qt::PointingHandCursor);
        btnOut->setStyleSheet(K4Styles::sidePanelButton());
        vbox->addWidget(btnOut);
        return container;
    };

    btnGrid->addWidget(makeBtn("STBY", m_modeBtn), 0, 0);
    btnGrid->addWidget(makeBtn("ATU", m_atuBtn), 0, 1);
    btnGrid->addWidget(makeBtn("ANT1", m_antBtn), 1, 0);
    btnGrid->addWidget(makeBtn("TUNE", m_tuneBtn), 1, 1);

    layout->addLayout(btnGrid);

    // Button connections
    connect(m_modeBtn, &QPushButton::clicked, this, [this]() { emit modeToggled(!m_operate); });

    connect(m_atuBtn, &QPushButton::clicked, this, [this]() { emit atuModeToggled(!m_atuIn); });

    connect(m_antBtn, &QPushButton::clicked, this, [this]() {
        int next = (m_antenna % 3) + 1;
        emit antennaChanged(next);
    });

    connect(m_tuneBtn, &QPushButton::clicked, this, [this]() { emit atuTuneRequested(); });

    // Decay timer for smooth meter animation
    m_decayTimer = new QTimer(this);
    m_decayTimer->setInterval(DECAY_INTERVAL_MS);
    connect(m_decayTimer, &QTimer::timeout, this, &Kpa1500MiniPanel::onDecayTimer);
}

void Kpa1500MiniPanel::setForwardPower(float watts) {
    m_forwardPower = qBound(0.0f, watts, 1500.0f);
    if (m_forwardPower > m_peakForward)
        m_peakForward = m_forwardPower;
    if (!m_decayTimer->isActive())
        m_decayTimer->start();
}

void Kpa1500MiniPanel::setReflectedPower(float watts) {
    m_reflectedPower = qBound(0.0f, watts, 100.0f);
    if (m_reflectedPower > m_peakReflected)
        m_peakReflected = m_reflectedPower;
    if (!m_decayTimer->isActive())
        m_decayTimer->start();
}

void Kpa1500MiniPanel::setSWR(float swr) {
    m_swr = qMax(1.0f, swr);
    if (m_swr > m_peakSwr)
        m_peakSwr = m_swr;
    if (!m_decayTimer->isActive())
        m_decayTimer->start();
}

void Kpa1500MiniPanel::setTemperature(float celsius) {
    m_temperature = qBound(0.0f, celsius, 100.0f);
    if (!m_decayTimer->isActive())
        m_decayTimer->start();
}

void Kpa1500MiniPanel::setMode(bool operate) {
    m_operate = operate;
    updateButtonLabels();
    update();
}

void Kpa1500MiniPanel::setAtuMode(bool in) {
    m_atuIn = in;
    updateButtonLabels();
}

void Kpa1500MiniPanel::setAntenna(int ant) {
    m_antenna = qBound(1, ant, 3);
    updateButtonLabels();
}

void Kpa1500MiniPanel::setFault(bool fault) {
    m_fault = fault;
    update();
}

void Kpa1500MiniPanel::setConnected(bool connected) {
    m_connected = connected;
    m_modeBtn->setEnabled(connected);
    m_atuBtn->setEnabled(connected);
    m_antBtn->setEnabled(connected);
    m_tuneBtn->setEnabled(connected);
    if (!connected) {
        m_forwardPower = m_reflectedPower = m_temperature = 0.0f;
        m_swr = 1.0f;
        m_displayForward = m_displayReflected = m_displayTemp = 0.0f;
        m_displaySwr = 1.0f;
        m_peakForward = m_peakReflected = 0.0f;
        m_peakSwr = 1.0f;
        m_decayTimer->stop();
    }
    update();
}

void Kpa1500MiniPanel::updateButtonLabels() {
    m_modeBtn->setText(m_operate ? "OPER" : "STBY");
    m_atuBtn->setText(m_atuIn ? "ATU IN" : "ATU");
    m_antBtn->setText(QString("ANT%1").arg(m_antenna));
}

void Kpa1500MiniPanel::onDecayTimer() {
    bool settled = true;

    // Helper: animate display toward target
    auto animate = [&](float &display, float target, float minStep) {
        if (qAbs(display - target) > 0.001f) {
            float rate = (target > display) ? ATTACK_RATE : DECAY_RATE;
            float delta = (target - display) * rate;
            if (qAbs(delta) < minStep)
                delta = (target > display) ? minStep : -minStep;
            display += delta;
            if ((delta > 0 && display > target) || (delta < 0 && display < target))
                display = target;
            settled = false;
        }
    };

    // Helper: decay peak toward display
    auto decayPeak = [&](float &peak, float display, float minStep) {
        if (peak > display + 0.01f) {
            float delta = peak * PEAK_DECAY_RATE;
            if (delta < minStep)
                delta = minStep;
            peak -= delta;
            if (peak < display)
                peak = display;
            settled = false;
        }
    };

    animate(m_displayForward, m_forwardPower, 2.0f);
    animate(m_displayReflected, m_reflectedPower, 0.2f);
    animate(m_displaySwr, m_swr, 0.01f);
    animate(m_displayTemp, m_temperature, 0.2f);

    decayPeak(m_peakForward, m_displayForward, 1.0f);
    decayPeak(m_peakReflected, m_displayReflected, 0.1f);
    decayPeak(m_peakSwr, m_displaySwr, 0.005f);

    if (settled)
        m_decayTimer->stop();

    update();
}

void Kpa1500MiniPanel::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    int w = width();

    // --- Header: centered title ---
    QFont headerFont = K4Styles::Fonts::paintFont(K4Styles::Dimensions::FontSizeMedium, QFont::Bold);
    p.setFont(headerFont);

    p.setPen(QColor(K4Styles::Colors::AccentAmber));
    p.drawText(MARGIN, TOP_PAD, w - MARGIN * 2, HEADER_HEIGHT, Qt::AlignCenter, "KPA1500");

    // Separator
    int sepY = TOP_PAD + HEADER_HEIGHT + 1;
    p.setPen(QColor(K4Styles::Colors::InactiveGray));
    p.drawLine(MARGIN, sepY, w - MARGIN, sepY);

    // --- Meters ---
    struct MeterDef {
        const char *label;
        float displayVal;
        float maxVal;
        float peakVal;
        bool hasPeak;
    };

    float swrRatio = (m_displaySwr - 1.0f) / 2.0f;
    float swrPeakRatio = (m_peakSwr - 1.0f) / 2.0f;

    MeterDef meters[] = {
        {"FWD", m_displayForward, 1500.0f, m_peakForward, true},
        {"SWR", swrRatio, 1.0f, swrPeakRatio, true},
        {"REF", m_displayReflected, 100.0f, m_peakReflected, true},
        {"TMP", m_displayTemp, 100.0f, 0.0f, false},
    };

    QFont labelFont = K4Styles::Fonts::paintFont(K4Styles::Dimensions::FontSizeSmall);
    QFont valueFont = K4Styles::Fonts::paintFont(K4Styles::Dimensions::FontSizeNormal, QFont::Bold);

    int barX = MARGIN + LABEL_WIDTH;
    int barW = w - barX - VALUE_WIDTH - MARGIN;

    for (int i = 0; i < 4; ++i) {
        int y = METER_START_Y + i * METER_SPACING;
        const auto &m = meters[i];

        // Label
        p.setFont(labelFont);
        p.setPen(QColor(K4Styles::Colors::TextGray));
        p.drawText(MARGIN, y, LABEL_WIDTH, BAR_HEIGHT, Qt::AlignLeft | Qt::AlignVCenter, m.label);

        // Track background
        p.fillRect(barX, y, barW, BAR_HEIGHT, QColor(K4Styles::Colors::DarkBackground));
        p.setPen(QColor(K4Styles::Colors::GradientBottom));
        p.drawRect(barX, y, barW - 1, BAR_HEIGHT - 1);

        // Filled bar with gradient
        float ratio =
            (m.label[0] == 'S') ? qBound(0.0f, m.displayVal, 1.0f) : qBound(0.0f, m.displayVal / m.maxVal, 1.0f);
        int fillW = static_cast<int>(ratio * (barW - 2));
        if (fillW > 0) {
            QLinearGradient gradient = K4Styles::meterGradient(barX + 1, 0, barX + barW - 1, 0);
            p.fillRect(barX + 1, y + 1, fillW, BAR_HEIGHT - 2, gradient);
        }

        // Peak marker
        float peakRatio =
            (m.label[0] == 'S') ? qBound(0.0f, m.peakVal, 1.0f) : qBound(0.0f, m.peakVal / m.maxVal, 1.0f);
        if (m.hasPeak && peakRatio > ratio + 0.01f) {
            int peakX = barX + 1 + static_cast<int>(peakRatio * (barW - 2));
            peakX = qBound(barX + 1, peakX, barX + barW - 2);
            p.setPen(QColor(K4Styles::Colors::TextWhite));
            p.drawLine(peakX, y + 1, peakX, y + BAR_HEIGHT - 2);
        }

        // Value text
        p.setFont(valueFont);
        p.setPen(QColor(K4Styles::Colors::TextWhite));
        QString valStr;
        if (i == 0)
            valStr = QString("%1W").arg(qRound(m_displayForward));
        else if (i == 1)
            valStr = m_displaySwr >= 3.0f ? ">3.0" : QString::number(m_displaySwr, 'f', 1);
        else if (i == 2)
            valStr = QString("%1W").arg(qRound(m_displayReflected));
        else
            valStr = QString::fromUtf8("%1\u00B0").arg(qRound(m_displayTemp));

        int valX = barX + barW + 2;
        p.drawText(valX, y, VALUE_WIDTH, BAR_HEIGHT, Qt::AlignRight | Qt::AlignVCenter, valStr);
    }

    // --- Status: OPER + ATU on one line, side by side ---
    if (m_connected) {
        int statusY = METER_START_Y + (METER_SPACING * 4) + 2;
        QFont statusFont = K4Styles::Fonts::paintFont(K4Styles::Dimensions::FontSizeSmall, QFont::Bold);
        p.setFont(statusFont);
        int halfW = (w - MARGIN * 2) / 2;

        // OPER/FAULT on left half
        if (m_fault) {
            p.setPen(QColor(K4Styles::Colors::TxRed));
            p.drawText(MARGIN, statusY, halfW, ATU_LABEL_HEIGHT, Qt::AlignCenter, "FAULT");
        } else {
            p.setPen(QColor(m_operate ? K4Styles::Colors::StatusGreen : K4Styles::Colors::InactiveGray));
            p.drawText(MARGIN, statusY, halfW, ATU_LABEL_HEIGHT, Qt::AlignCenter, "OPER");
        }

        // ATU on right half
        p.setPen(QColor(m_atuIn ? K4Styles::Colors::StatusGreen : K4Styles::Colors::InactiveGray));
        p.drawText(MARGIN + halfW, statusY, halfW, ATU_LABEL_HEIGHT, Qt::AlignCenter, "ATU");
    }
}
