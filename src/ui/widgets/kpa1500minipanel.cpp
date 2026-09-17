#include "ui/widgets/kpa1500minipanel.h"
#include "network/kpa1500antennas.h"
#include "ui/styling/k4styles.h"
#include <QGridLayout>
#include <QPainter>
#include <QStringList>
#include <QVBoxLayout>

Kpa1500MiniPanel::Kpa1500MiniPanel(QWidget *parent) : QWidget(parent) {
    // Main layout: top margin reserves space for painted meters + LCD, buttons below
    // Side/bottom padding makes the panel background visible around button edges
    int metersHeight = METER_START_Y + (METER_SPACING * 4) + LCD_HEIGHT;
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(PANEL_PAD, metersHeight, PANEL_PAD, PANEL_PAD);
    layout->setSpacing(4);

    // 2x2 button grid
    auto *btnGrid = new QGridLayout();
    btnGrid->setContentsMargins(0, 12, 0, 0);
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

    btnGrid->addWidget(makeBtn("MODE", m_modeBtn), 0, 0);
    btnGrid->addWidget(makeBtn("ATU", m_atuBtn), 0, 1);
    btnGrid->addWidget(makeBtn("ANT", m_antBtn), 1, 0);
    btnGrid->addWidget(makeBtn("TUNE", m_tuneBtn), 1, 1);

    layout->addLayout(btnGrid);

    // Button connections
    connect(m_modeBtn, &QPushButton::clicked, this, [this]() { emit modeToggled(!m_operate); });

    connect(m_atuBtn, &QPushButton::clicked, this, [this]() { emit atuModeToggled(!m_atuModeInline); });

    // WHY no local next-antenna choice: which antennas exist is the amp's per-band configuration
    // (sub-antennas 3-32), so the amp picks the next one, exactly as its own ANTENNA button does.
    connect(m_antBtn, &QPushButton::clicked, this, &Kpa1500MiniPanel::nextAntennaRequested);

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

void Kpa1500MiniPanel::setAtuMode(bool modeInline) {
    m_atuModeInline = modeInline;
    updateButtonLabels();
    update();
}

void Kpa1500MiniPanel::setAtuInline(bool relayInline) {
    m_atuRelayInline = relayInline;
    update();
}

void Kpa1500MiniPanel::setAntenna(int antenna, int connector) {
    m_antenna = antenna;
    m_antennaConnector = connector;
    update();
}

void Kpa1500MiniPanel::setBand(const QString &bandLabel) {
    m_band = bandLabel;
    update();
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
    m_modeBtn->setText("MODE");
    m_atuBtn->setText("ATU");
    m_antBtn->setText("ANT");
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
    int h = height();

    // --- Background panel shading (only when connected) ---
    if (m_connected) {
        // Alpha-channel overlays for the mini-panel connected-state look. Base color (white or
        // black) is semantic only — the alpha is the actual value. Not palette entries.
        constexpr int kPanelTintAlpha = 18;   // Subtle light fill over dark theme
        constexpr int kPanelBorderAlpha = 35; // Thin panel border
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, kPanelTintAlpha));
        p.drawRoundedRect(0, 0, w, h, 6, 6);

        p.setPen(QColor(255, 255, 255, kPanelBorderAlpha));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(0, 0, w - 1, h - 1, 6, 6);
    }

    // --- Header: centered title ---
    QFont headerFont = K4Styles::Fonts::paintFont(K4Styles::Dimensions::FontSizeMedium, QFont::Bold);
    p.setFont(headerFont);

    int cx = PANEL_PAD;         // Consistent left inset for all content
    int cw = w - PANEL_PAD * 2; // Content width

    p.setPen(QColor(K4Styles::Colors::AccentAmber));
    p.drawText(cx, TOP_PAD, cw, HEADER_HEIGHT, Qt::AlignCenter, "KPA1500");

    // Separator
    int sepY = TOP_PAD + HEADER_HEIGHT + 1;
    p.setPen(QColor(K4Styles::Colors::InactiveGray));
    p.drawLine(cx, sepY, cx + cw, sepY);

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
    QFont valueFont = K4Styles::Fonts::paintFont(9, QFont::Bold);

    int barX = cx + LABEL_WIDTH;
    int barW = cw - LABEL_WIDTH - VALUE_WIDTH;

    for (int i = 0; i < 4; ++i) {
        int y = METER_START_Y + i * METER_SPACING;
        const auto &m = meters[i];

        // Label
        p.setFont(labelFont);
        p.setPen(QColor(K4Styles::Colors::TextGray));
        p.drawText(cx, y, LABEL_WIDTH, BAR_HEIGHT, Qt::AlignLeft | Qt::AlignVCenter, m.label);

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

        // Value text (right-aligned within content area)
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

        int valX = barX + barW + 1;
        p.drawText(valX, y, VALUE_WIDTH - 1, BAR_HEIGHT, Qt::AlignRight | Qt::AlignVCenter, valStr);
    }

    // --- LCD: operate/fault state, active antenna, band, ATU state ---
    if (m_connected) {
        const int lcdY = METER_START_Y + (METER_SPACING * 4) + LCD_TOP_PAD;
        const int lcdH = LCD_ROW_HEIGHT * 2 + 4;

        // Alpha-channel overlays matching the panel's card style. Alpha, not palette.
        constexpr int kLcdBorderAlpha = 20; // White border at low alpha
        constexpr int kLcdShadeAlpha = 40;  // Black shade fill
        p.setPen(QColor(255, 255, 255, kLcdBorderAlpha));
        p.setBrush(QColor(0, 0, 0, kLcdShadeAlpha));
        p.drawRoundedRect(cx, lcdY, cw, lcdH, 3, 3);

        p.setFont(K4Styles::Fonts::dataFont(K4Styles::Dimensions::FontSizeSmall, QFont::Bold));
        const int textX = cx + LCD_TEXT_PAD;
        const int textW = cw - LCD_TEXT_PAD * 2;
        const int line1Y = lcdY + 2;
        const int line2Y = line1Y + LCD_ROW_HEIGHT;

        // Line 1: state (left), antenna (centre), band (right)
        QString state;
        QColor stateColor;
        if (m_fault) {
            state = QStringLiteral("FAULT");
            stateColor = QColor(K4Styles::Colors::TxRed);
        } else if (m_operate) {
            state = QStringLiteral("OPER");
            stateColor = QColor(K4Styles::Colors::StatusGreen);
        } else {
            state = QStringLiteral("STBY");
            stateColor = QColor(K4Styles::Colors::AccentAmber);
        }
        p.setPen(stateColor);
        p.drawText(textX, line1Y, textW, LCD_ROW_HEIGHT, Qt::AlignLeft | Qt::AlignVCenter, state);

        p.setPen(QColor(K4Styles::Colors::TextWhite));
        p.drawText(textX, line1Y, textW, LCD_ROW_HEIGHT, Qt::AlignHCenter | Qt::AlignVCenter,
                   Kpa1500Antennas::label(m_antenna, m_antennaConnector));
        p.drawText(textX, line1Y, textW, LCD_ROW_HEIGHT, Qt::AlignRight | Qt::AlignVCenter, m_band);

        // Line 2: ATU. IN whenever the ATU mode is inline, BYP whenever the relays are bypassed, so
        // "mode inline but relays bypassed" reads "ATU IN BYP" - the same two facts the IN and BYP
        // indicators showed when both were lit.
        QStringList atu{QStringLiteral("ATU")};
        if (m_atuModeInline)
            atu << QStringLiteral("IN");
        if (!m_atuRelayInline)
            atu << QStringLiteral("BYP");
        p.drawText(textX, line2Y, textW, LCD_ROW_HEIGHT, Qt::AlignLeft | Qt::AlignVCenter, atu.join(QLatin1Char(' ')));
    }
}
