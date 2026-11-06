#include "ui/widgets/kpa1500minipanel.h"
#include "network/kpa1500antennas.h"
#include "ui/styling/k4styles.h"
#include "ui/widgets/sidepanelbutton.h"
#include <QFontMetricsF>
#include <QGridLayout>
#include <QPainter>
#include <QStringList>
#include <QVBoxLayout>
#include <QtMath>

namespace {
namespace D = K4Styles::Dimensions;

// The card's inset on all four sides.
constexpr int kPad = D::SidePanelMarginH;

QFont headerFont() {
    return K4Styles::Fonts::paintFont(D::FontSizeCaption, QFont::Bold);
}
QFont labelFont() {
    return K4Styles::Fonts::paintFont(D::FontSizeCaption, QFont::Medium);
}
QFont valueFont() {
    return K4Styles::Fonts::paintFont(D::FontSizeCaption, QFont::Bold);
}
QFont lcdFont() {
    return K4Styles::Fonts::dataFont(D::FontSizeCaption, QFont::Medium);
}

const char *const kMeterLabels[] = {"FWD", "SWR", "REF", "TMP"};
// The widest reading each meter can show: full-scale FWD, SWR past its range, full REF and TMP.
const QString kWidestValues[] = {QStringLiteral("1500W"), QStringLiteral(">3.0"), QStringLiteral("100W"),
                                 QString::fromUtf8("100\u00B0")};

int lineHeight(const QFont &font, const QWidget *context) {
    return qCeil(QFontMetricsF(font, context).height());
}
} // namespace

Kpa1500MiniPanel::Geometry Kpa1500MiniPanel::measure(const QWidget *context) {
    Geometry g;
    g.headerY = kPad;
    g.headerHeight = lineHeight(headerFont(), context);
    g.ruleY = g.headerY + g.headerHeight;

    g.meterLineHeight = qMax(lineHeight(labelFont(), context), lineHeight(valueFont(), context));
    g.meterPitch = g.meterLineHeight + D::SidePanelInnerGap;
    g.firstMeterLineY = g.ruleY + D::SidePanelInnerGap;

    const QFontMetricsF labelMetrics(labelFont(), context);
    qreal labelWidth = 0;
    for (const char *label : kMeterLabels)
        labelWidth = qMax(labelWidth, labelMetrics.horizontalAdvance(QLatin1String(label)));
    g.labelWidth = qCeil(labelWidth);
    const QFontMetricsF valueMetrics(valueFont(), context);
    qreal valueWidth = 0;
    for (const QString &value : kWidestValues)
        valueWidth = qMax(valueWidth, valueMetrics.horizontalAdvance(value));
    g.valueWidth = qCeil(valueWidth);

    const int metersBottom = g.firstMeterLineY + 3 * g.meterPitch + g.meterLineHeight;
    g.lcdY = metersBottom + D::SidePanelInnerGap;
    g.lcdRowHeight = lineHeight(lcdFont(), context);
    g.lcdHeight = 2 * g.lcdRowHeight + 2 * D::SidePanelInnerGap;
    g.buttonsY = g.lcdY + g.lcdHeight + D::SideButtonRowGap;
    return g;
}

Kpa1500MiniPanel::Kpa1500MiniPanel(QWidget *parent) : QWidget(parent), m_geom(measure(this)) {
    // Meters and LCD are painted above the buttons; the layout's top margin reserves their height.
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(kPad, m_geom.buttonsY, kPad, kPad);
    layout->setSpacing(0);

    auto *btnGrid = SidePanelButton::createGrid();

    // WHY no container per button: the app's background rule cascades to every plain QWidget, and
    // a container painted that opaque square over this card's lighter tint around each button.
    auto makeBtn = [this](const QString &text, QPushButton *&btnOut) -> QPushButton * {
        btnOut = SidePanelButton::create(text, SidePanelButton::Shade::Dark, this);
        return btnOut;
    };

    btnGrid->addWidget(makeBtn("MODE", m_modeBtn), 0, 0);
    btnGrid->addWidget(makeBtn("ATU", m_atuBtn), 0, 1);
    btnGrid->addWidget(makeBtn("ANT", m_antBtn), 1, 0);
    btnGrid->addWidget(makeBtn("TUNE", m_tuneBtn), 1, 1);

    layout->addLayout(btnGrid);
    layout->setAlignment(btnGrid, Qt::AlignHCenter);

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
    update();
}

void Kpa1500MiniPanel::setAtuMode(bool modeInline) {
    m_atuModeInline = modeInline;
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
        p.setPen(Qt::NoPen);
        p.setBrush(K4Styles::withAlpha(K4Styles::Colors::White, K4Styles::Colors::Alpha::CardTint));
        p.drawRoundedRect(0, 0, w, h, D::BorderRadius, D::BorderRadius);

        p.setPen(K4Styles::withAlpha(K4Styles::Colors::White, K4Styles::Colors::Alpha::CardBorder));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(0, 0, w - 1, h - 1, D::BorderRadius, D::BorderRadius);
    }

    // --- Header: centered title ---
    p.setFont(headerFont());

    const int cx = kPad;         // Consistent left inset for all content
    const int cw = w - kPad * 2; // Content width

    p.setPen(QColor(K4Styles::Colors::AccentAmber));
    p.drawText(cx, m_geom.headerY, cw, m_geom.headerHeight, Qt::AlignCenter, "KPA1500");

    p.setPen(QColor(K4Styles::Colors::MutedGraphic));
    p.drawLine(cx, m_geom.ruleY, cx + cw, m_geom.ruleY);

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
        {kMeterLabels[0], m_displayForward, 1500.0f, m_peakForward, true},
        {kMeterLabels[1], swrRatio, 1.0f, swrPeakRatio, true},
        {kMeterLabels[2], m_displayReflected, 100.0f, m_peakReflected, true},
        {kMeterLabels[3], m_displayTemp, 100.0f, 0.0f, false},
    };

    // Label column | gap | bar | gap | value column, the columns as wide as their widest text.
    const int barX = cx + m_geom.labelWidth + D::SidePanelInnerGap;
    const int valueX = cx + cw - m_geom.valueWidth;
    const int barW = valueX - D::SidePanelInnerGap - barX;

    for (int i = 0; i < 4; ++i) {
        const int lineY = m_geom.firstMeterLineY + i * m_geom.meterPitch;
        const int y = lineY + (m_geom.meterLineHeight - BAR_HEIGHT) / 2;
        const auto &m = meters[i];

        // WHY TextSecondary: 5.0:1 on the card (Background under its white tint), over the 4.5:1 small
        // text needs.
        p.setFont(labelFont());
        p.setPen(QColor(K4Styles::Colors::TextSecondary));
        p.drawText(cx, lineY, m_geom.labelWidth, m_geom.meterLineHeight, Qt::AlignLeft | Qt::AlignVCenter, m.label);

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
            p.setPen(QColor(K4Styles::Colors::TextPrimary));
            p.drawLine(peakX, y + 1, peakX, y + BAR_HEIGHT - 2);
        }

        // Value text (right-aligned within content area)
        p.setFont(valueFont());
        p.setPen(QColor(K4Styles::Colors::TextPrimary));
        QString valStr;
        if (i == 0)
            valStr = QString("%1W").arg(qRound(m_displayForward));
        else if (i == 1)
            valStr = m_displaySwr >= 3.0f ? ">3.0" : QString::number(m_displaySwr, 'f', 1);
        else if (i == 2)
            valStr = QString("%1W").arg(qRound(m_displayReflected));
        else
            valStr = QString::fromUtf8("%1\u00B0").arg(qRound(m_displayTemp));

        p.drawText(valueX, lineY, m_geom.valueWidth, m_geom.meterLineHeight, Qt::AlignRight | Qt::AlignVCenter, valStr);
    }

    // --- LCD: operate/fault state, active antenna, band, ATU state ---
    if (m_connected) {
        const int lcdY = m_geom.lcdY;
        const int lcdH = m_geom.lcdHeight;
        const int rowH = m_geom.lcdRowHeight;

        // Alpha-channel overlays matching the panel's card style. Alpha, not palette.
        p.setPen(K4Styles::withAlpha(K4Styles::Colors::White, K4Styles::Colors::Alpha::LcdBorder));
        p.setBrush(K4Styles::withAlpha(K4Styles::Colors::Black, K4Styles::Colors::Alpha::LcdShade));
        p.drawRoundedRect(cx, lcdY, cw, lcdH, D::SideCompactBorderRadius, D::SideCompactBorderRadius);

        p.setFont(lcdFont());
        const int textX = cx + D::SidePanelInnerGap;
        const int textW = cw - D::SidePanelInnerGap * 2;
        const int line1Y = lcdY + D::SidePanelInnerGap;
        const int line2Y = line1Y + rowH;

        // Line 1: state (left), antenna (centre), band (right)
        QString state;
        QColor stateColor;
        if (m_fault) {
            state = QStringLiteral("FAULT");
            stateColor = QColor(K4Styles::Colors::TextRed);
        } else if (m_operate) {
            state = QStringLiteral("OPER");
            stateColor = QColor(K4Styles::Colors::StatusGreen);
        } else {
            state = QStringLiteral("STBY");
            stateColor = QColor(K4Styles::Colors::AccentAmber);
        }
        p.setPen(stateColor);
        p.drawText(textX, line1Y, textW, rowH, Qt::AlignLeft | Qt::AlignVCenter, state);

        p.setPen(QColor(K4Styles::Colors::TextPrimary));
        p.drawText(textX, line1Y, textW, rowH, Qt::AlignHCenter | Qt::AlignVCenter,
                   Kpa1500Antennas::label(m_antenna, m_antennaConnector));
        p.drawText(textX, line1Y, textW, rowH, Qt::AlignRight | Qt::AlignVCenter, m_band);

        // Line 2: ATU. IN whenever the ATU mode is inline, BYP whenever the relays are bypassed, so
        // "mode inline but relays bypassed" reads "ATU IN BYP".
        QStringList atu{QStringLiteral("ATU")};
        if (m_atuModeInline)
            atu << QStringLiteral("IN");
        if (!m_atuRelayInline)
            atu << QStringLiteral("BYP");
        p.drawText(textX, line2Y, textW, rowH, Qt::AlignLeft | Qt::AlignVCenter, atu.join(QLatin1Char(' ')));
    }
}
