#include "ui/widgets/featuremenubar.h"
#include "ui/styling/k4styles.h"
#include <QHBoxLayout>
#include <QPainter>
#include <QPushButton>
#include <QWheelEvent>

namespace {
// The visible card hugs the row: this much frame around the keys horizontally and vertically.
const int CardPaddingH = 8;
const int CardPaddingV = 4;
const int RowSpacing = 8;
} // namespace

FeatureMenuBar::FeatureMenuBar(QWidget *parent) : K4PopupBase(parent) {
    setupUi();
    initPopup();
}

void FeatureMenuBar::setupUi() {
    auto *layout = new QHBoxLayout(this);
    // Card = the keys plus a thin frame; the base's shadow margin surrounds it.
    const int sm = K4Styles::Dimensions::ShadowMargin;
    layout->setContentsMargins(sm + CardPaddingH, sm + CardPaddingV, sm + CardPaddingH, sm + CardPaddingV);
    layout->setSpacing(RowSpacing);

    // Title — non-interactive label across all features.
    m_titleLabel = new QPushButton("ATTENUATOR", this);
    m_titleLabel->setFixedSize(140, K4Styles::Dimensions::ButtonHeightMedium);
    m_titleLabel->setFocusPolicy(Qt::NoFocus);
    m_titleLabel->setStyleSheet(K4Styles::menuBarButtonSmall());

    // OFF/ON toggle button — use menuBarButtonSmall (no vertical padding, 12px font)
    // instead of menuBarButton (6px padding, 11px font) so text baseline matches labels
    m_toggleBtn = new QPushButton("OFF", this);
    m_toggleBtn->setMinimumWidth(60);
    m_toggleBtn->setFixedHeight(K4Styles::Dimensions::ButtonHeightMedium);
    m_toggleBtn->setCursor(Qt::PointingHandCursor);
    m_toggleBtn->setStyleSheet(K4Styles::menuBarButtonSmall());

    // NR engine toggle — only shown for NrAdjust. Tapping flips LMS <-> NR+ (SSNR).
    m_nrEngineBtn = new QPushButton("LMS", this);
    m_nrEngineBtn->setMinimumWidth(60);
    m_nrEngineBtn->setFixedHeight(K4Styles::Dimensions::ButtonHeightMedium);
    m_nrEngineBtn->setCursor(Qt::PointingHandCursor);
    m_nrEngineBtn->setStyleSheet(K4Styles::menuBarButtonSmall());
    m_nrEngineBtn->hide();

    // Extra button (only shown for NB LEVEL - "FILTER NONE/NARROW/WIDE")
    m_extraBtn = new QPushButton(nbFilterText(m_nbFilter), this);
    m_extraBtn->setMinimumWidth(90);
    m_extraBtn->setFixedHeight(K4Styles::Dimensions::ButtonHeightMedium);
    m_extraBtn->setCursor(Qt::PointingHandCursor);
    m_extraBtn->setStyleSheet(K4Styles::menuBarButtonSmall());
    m_extraBtn->hide(); // Hidden by default

    // Value display — non-interactive QPushButton so text rendering matches buttons
    m_valueLabel = new QPushButton("0", this);
    m_valueLabel->setFixedHeight(K4Styles::Dimensions::ButtonHeightMedium);
    m_valueLabel->setMinimumWidth(80);
    m_valueLabel->setFocusPolicy(Qt::NoFocus);
    m_valueLabel->setStyleSheet(K4Styles::popupValueLabel(K4Styles::Colors::TextPrimary));

    // Decrement button
    m_decrementBtn = new QPushButton("-", this);
    m_decrementBtn->setFixedSize(K4Styles::Dimensions::ButtonHeightLarge, K4Styles::Dimensions::ButtonHeightMedium);
    m_decrementBtn->setCursor(Qt::PointingHandCursor);
    m_decrementBtn->setStyleSheet(K4Styles::menuBarButtonSmall());

    // Increment button
    m_incrementBtn = new QPushButton("+", this);
    m_incrementBtn->setFixedSize(K4Styles::Dimensions::ButtonHeightLarge, K4Styles::Dimensions::ButtonHeightMedium);
    m_incrementBtn->setCursor(Qt::PointingHandCursor);
    m_incrementBtn->setStyleSheet(K4Styles::menuBarButtonSmall());

    // Layout - compact, no stretches (the base centres the card on its anchor)
    layout->addWidget(m_titleLabel, 0, Qt::AlignVCenter);
    layout->addWidget(m_toggleBtn, 0, Qt::AlignVCenter);
    layout->addWidget(m_nrEngineBtn, 0, Qt::AlignVCenter);
    layout->addWidget(m_extraBtn, 0, Qt::AlignVCenter);
    layout->addWidget(m_valueLabel, 0, Qt::AlignVCenter);
    layout->addWidget(m_decrementBtn, 0, Qt::AlignVCenter);
    layout->addWidget(m_incrementBtn, 0, Qt::AlignVCenter);

    // Connect signals
    connect(m_toggleBtn, &QPushButton::clicked, this, &FeatureMenuBar::toggleRequested);
    connect(m_decrementBtn, &QPushButton::clicked, this, &FeatureMenuBar::decrementRequested);
    connect(m_incrementBtn, &QPushButton::clicked, this, &FeatureMenuBar::incrementRequested);
    connect(m_extraBtn, &QPushButton::clicked, this, &FeatureMenuBar::extraButtonClicked);
    connect(m_nrEngineBtn, &QPushButton::clicked, this, &FeatureMenuBar::nrEngineToggleRequested);
}

void FeatureMenuBar::showForFeature(Feature feature, QWidget *anchor) {
    m_currentFeature = feature;
    updateForFeature();
    // The width follows the feature (NB's FILTER key, NR's engine key): size before the one show.
    initPopup();
    showAboveWidget(anchor);
}

QSize FeatureMenuBar::contentSize() const {
    // The row's own width — keys hidden for this feature drop out — without the shadow margins.
    const int sm = K4Styles::Dimensions::ShadowMargin;
    layout()->invalidate();
    return QSize(layout()->sizeHint().width() - 2 * sm, K4Styles::Dimensions::ButtonHeightMedium + 2 * CardPaddingV);
}

void FeatureMenuBar::wheelEvent(QWheelEvent *event) {
    int steps = m_wheelAccumulator.accumulate(event);
    for (int i = 0; i < qAbs(steps); ++i) {
        if (steps > 0)
            emit incrementRequested();
        else
            emit decrementRequested();
    }
    event->accept();
}

void FeatureMenuBar::updateForFeature() {
    switch (m_currentFeature) {
    case Attenuator:
        m_titleLabel->setText("ATTENUATOR");
        m_extraBtn->hide();
        m_nrEngineBtn->hide();
        m_valueUnit = " dB";
        break;
    case NbLevel:
        m_titleLabel->setText("NB LEVEL");
        m_extraBtn->setText(nbFilterText(m_nbFilter));
        m_extraBtn->show();
        m_nrEngineBtn->hide();
        m_valueUnit = "";
        break;
    case NrAdjust:
        m_titleLabel->setText("NR ADJUST");
        m_extraBtn->hide();
        m_nrEngineBtn->setText(m_nrEngine == Ssnr ? "NR+" : "LMS");
        m_nrEngineBtn->show();
        m_valueUnit = "";
        break;
    case ManualNotch:
        m_titleLabel->setText("MANUAL NOTCH");
        m_extraBtn->hide();
        m_nrEngineBtn->hide();
        m_valueUnit = " Hz";
        break;
    }

    // Update value display with unit
    setValue(m_value);
}

void FeatureMenuBar::setNrEngine(NrEngine engine) {
    if (m_nrEngine == engine)
        return;
    m_nrEngine = engine;
    if (m_currentFeature == NrAdjust)
        m_nrEngineBtn->setText(m_nrEngine == Ssnr ? "NR+" : "LMS");
}

void FeatureMenuBar::setFeatureEnabled(bool enabled) {
    m_toggleBtn->setText(enabled ? "ON" : "OFF");
}

void FeatureMenuBar::setValue(int value) {
    m_value = value;
    m_valueLabel->setText(QString::number(value) + m_valueUnit);
}

void FeatureMenuBar::setNbFilter(int filter) {
    m_nbFilter = qBound(0, filter, 2);
    m_extraBtn->setText(nbFilterText(m_nbFilter));
}

QString FeatureMenuBar::nbFilterText(int filter) {
    static const char *const filterNames[] = {"FILTER\nNONE", "FILTER\nNARROW", "FILTER\nWIDE"};
    return QString::fromLatin1(filterNames[qBound(0, filter, 2)]);
}

void FeatureMenuBar::paintContent(QPainter &painter, const QRect &contentRect) {
    // Delimiters after each group; none after the +/- pair at the end. Hidden keys draw none.
    paintCompactRowFrame(painter, contentRect, {m_titleLabel, m_toggleBtn, m_nrEngineBtn, m_extraBtn, m_valueLabel},
                         RowSpacing);
}
