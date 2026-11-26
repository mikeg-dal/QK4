#include "ui/popups/rxeqpopupwidget.h"
#include "ui/styling/k4styles.h"
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace {
using namespace K4Styles::Dimensions;

// Hairline rules and outlines: the frame of a key or a track, never text.
constexpr int HairlineWidth = 1;
// The grip's three lines, as the K4 draws them, and how far in from the grip's edges they run.
constexpr int GripLines = 3;
constexpr int GripInsetX = 6;
constexpr int GripLineGap = 4;
} // namespace

// =============================================================================
// EqBandWidget
// =============================================================================

EqBandWidget::EqBandWidget(int bandIndex, const QString &freqLabel, const QString &accentColor, QWidget *parent)
    : QWidget(parent), m_bandIndex(bandIndex), m_freqLabel(freqLabel), m_accent(accentColor) {
    // The column owns every pixel it covers; see the WHY on the class.
    setAttribute(Qt::WA_OpaquePaintEvent);
    setFixedSize(columnSize());
    setCursor(Qt::PointingHandCursor);
}

QSize EqBandWidget::columnSize() {
    return QSize(EqBandWidth, EqValueHeight + EqColumnGap + EqTrackHeight + EqColumnGap + EqKeyHeight);
}

QRect EqBandWidget::trackRect() const {
    return QRect(0, EqValueHeight + EqColumnGap, width(), EqTrackHeight);
}

QRect EqBandWidget::plusCapRect() const {
    const QRect t = trackRect();
    return QRect(t.left(), t.top(), t.width(), EqCapHeight);
}

QRect EqBandWidget::minusCapRect() const {
    const QRect t = trackRect();
    return QRect(t.left(), t.bottom() + 1 - EqCapHeight, t.width(), EqCapHeight);
}

QRect EqBandWidget::bodyRect() const {
    const QRect t = trackRect();
    return QRect(t.left(), t.top() + EqCapHeight, t.width(), t.height() - 2 * EqCapHeight);
}

QRect EqBandWidget::handleRect() const {
    const QRect b = bodyRect();
    const int travel = b.height() - EqHandleHeight;
    // +16 puts the grip at the top of the body, -16 at the bottom.
    const double fraction = double(MAX_DB - m_value) / double(MAX_DB - MIN_DB);
    const int y = b.top() + qRound(fraction * travel);
    return QRect(b.left(), y, b.width(), EqHandleHeight);
}

QRect EqBandWidget::keyRect() const {
    const QRect t = trackRect();
    return QRect(0, t.bottom() + 1 + EqColumnGap, width(), EqKeyHeight);
}

int EqBandWidget::valueFromY(int y) const {
    const QRect b = bodyRect();
    const int travel = b.height() - EqHandleHeight;
    if (travel <= 0)
        return m_value;
    const int top = qBound(b.top(), y - EqHandleHeight / 2, b.top() + travel);
    const double fraction = double(top - b.top()) / double(travel);
    return MAX_DB - qRound(fraction * (MAX_DB - MIN_DB));
}

void EqBandWidget::setValue(int dB) {
    dB = qBound(MIN_DB, dB, MAX_DB);
    if (m_value == dB)
        return;
    m_value = dB;
    update();
}

void EqBandWidget::setSelected(bool selected) {
    if (m_selected == selected)
        return;
    m_selected = selected;
    update();
}

void EqBandWidget::applyValue(int dB) {
    emit selectionRequested(m_bandIndex);
    dB = qBound(MIN_DB, dB, MAX_DB);
    if (m_value == dB)
        return;
    m_value = dB;
    update();
    emit valueChanged(m_bandIndex, m_value);
}

void EqBandWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    const QPoint pos = event->position().toPoint();
    if (plusCapRect().contains(pos)) {
        applyValue(m_value + 1);
    } else if (minusCapRect().contains(pos)) {
        applyValue(m_value - 1);
    } else if (bodyRect().contains(pos)) {
        // A press anywhere on the body takes the grip there and keeps it, as the radio's own
        // touchscreen does; there is no separate "grab the handle first" step.
        m_dragging = true;
        applyValue(valueFromY(pos.y()));
    } else if (keyRect().contains(pos)) {
        emit selectionRequested(m_bandIndex); // the key selects without moving the value
    }
    event->accept();
}

void EqBandWidget::mouseMoveEvent(QMouseEvent *event) {
    if (m_dragging)
        applyValue(valueFromY(event->position().toPoint().y()));
    event->accept();
}

void EqBandWidget::mouseReleaseEvent(QMouseEvent *event) {
    m_dragging = false;
    event->accept();
}

void EqBandWidget::wheelEvent(QWheelEvent *event) {
    const int steps = m_wheelAccumulator.accumulate(event);
    if (steps != 0)
        applyValue(m_value + steps);
    event->accept();
}

void EqBandWidget::paintEvent(QPaintEvent *event) {
    QPainter painter(this);
    // WA_OpaquePaintEvent promises every pixel: fill first, decorate after.
    painter.fillRect(event->rect(), QColor(K4Styles::Colors::PopupBackground));
    painter.setRenderHint(QPainter::Antialiasing);

    // --- the dB value -------------------------------------------------------------------------
    painter.setFont(K4Styles::Fonts::paintFont(FontSizeCaption));
    painter.setPen(QColor(K4Styles::Colors::TextPrimary));
    const QString valueText = m_value > 0 ? QStringLiteral("+%1").arg(m_value) : QString::number(m_value);
    painter.drawText(QRect(0, 0, width(), EqValueHeight), Qt::AlignCenter, valueText);

    // --- the track ----------------------------------------------------------------------------
    const QRect track = trackRect();
    const QRect body = bodyRect();
    const QRect grip = handleRect();
    QPainterPath trackPath;
    trackPath.addRoundedRect(QRectF(track).adjusted(0.5, 0.5, -0.5, -0.5), BorderRadius, BorderRadius);

    painter.save();
    painter.setClipPath(trackPath);

    // Grey from the + cap down to the grip, the accent from the grip down through the - cap, which
    // is how the radio shades it.
    painter.fillRect(QRect(track.left(), track.top(), track.width(), grip.top() - track.top()),
                     QColor(K4Styles::Colors::BorderNormal));
    painter.fillRect(QRect(track.left(), grip.bottom() + 1, track.width(), track.bottom() - grip.bottom()), m_accent);

    // The two caps: the + sits in the grey, the - in the accent, each marked off by a hairline.
    const QRect plusCap = plusCapRect();
    const QRect minusCap = minusCapRect();
    painter.fillRect(plusCap, QColor(K4Styles::Colors::LightGradientTop));
    painter.setPen(QPen(QColor(K4Styles::Colors::PanelBorder), HairlineWidth));
    painter.drawLine(plusCap.bottomLeft(), plusCap.bottomRight());
    painter.drawLine(minusCap.topLeft(), minusCap.topRight());

    painter.setFont(K4Styles::Fonts::paintFont(FontSizeReadout));
    painter.setPen(QColor(K4Styles::Colors::TextPrimary));
    painter.drawText(plusCap, Qt::AlignCenter, QStringLiteral("+"));
    painter.setPen(QColor(K4Styles::Colors::TextOnAccent));
    painter.drawText(minusCap, Qt::AlignCenter, QStringLiteral("-"));

    // The grip, with the K4's three lines across it.
    QRectF gripF = QRectF(grip).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setBrush(K4Styles::selectedGradient(grip.top(), grip.bottom()));
    painter.setPen(QPen(QColor(K4Styles::Colors::PanelBorder), HairlineWidth));
    painter.drawRoundedRect(gripF, SliderBorderRadius, SliderBorderRadius);
    painter.setPen(QPen(QColor(K4Styles::Colors::TextOnLight), HairlineWidth));
    const int gripMid = grip.center().y();
    for (int i = 0; i < GripLines; ++i) {
        const int y = gripMid + (i - GripLines / 2) * GripLineGap;
        painter.drawLine(grip.left() + GripInsetX, y, grip.right() - GripInsetX, y);
    }

    painter.restore();

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(K4Styles::borderColor(), HairlineWidth));
    painter.drawPath(trackPath);

    // --- the frequency key --------------------------------------------------------------------
    const QRect key = keyRect();
    painter.setBrush(m_selected ? K4Styles::selectedGradient(key.top(), key.bottom())
                                : K4Styles::buttonGradient(key.top(), key.bottom()));
    painter.setPen(QPen(K4Styles::borderColor(), HairlineWidth));
    painter.drawRoundedRect(QRectF(key).adjusted(0.5, 0.5, -0.5, -0.5), BorderRadius, BorderRadius);
    painter.setFont(K4Styles::Fonts::paintFont(FontSizeCaption));
    painter.setPen(QColor(m_selected ? K4Styles::Colors::TextOnLight : K4Styles::Colors::TextPrimary));
    painter.drawText(key, Qt::AlignCenter, m_freqLabel);
}

// =============================================================================
// EqPresetRowWidget
// =============================================================================

EqPresetRowWidget::EqPresetRowWidget(int presetIndex, QWidget *parent) : QWidget(parent), m_presetIndex(presetIndex) {
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(SidePanelItemSpacing);

    m_loadBtn = new QPushButton("---", this);
    m_loadBtn->setFixedSize(EqPresetKeyWidth, ButtonHeightMini);
    m_loadBtn->setStyleSheet(K4Styles::eqPresetLoadButton());
    connect(m_loadBtn, &QPushButton::clicked, this, &EqPresetRowWidget::onLoadClicked);
    layout->addWidget(m_loadBtn);

    m_saveBtn = new QPushButton("S", this);
    m_saveBtn->setFixedSize(ButtonHeightMini, ButtonHeightMini);
    m_saveBtn->setStyleSheet(
        K4Styles::menuBarButtonSmall() +
        QString("QPushButton { font-size: %1px; font-weight: bold; }").arg(K4Styles::Dimensions::FontSizeCaption));
    m_saveBtn->setToolTip("Save current EQ to this preset");
    connect(m_saveBtn, &QPushButton::clicked, this, &EqPresetRowWidget::onSaveClicked);
    layout->addWidget(m_saveBtn);
}

void EqPresetRowWidget::setPresetName(const QString &name) {
    m_name = name;
    m_loadBtn->setText(name.isEmpty() ? QStringLiteral("---") : name);
    K4Styles::setStyleFlag(m_loadBtn, K4Styles::DimmedProperty, name.isEmpty());
}

void EqPresetRowWidget::onLoadClicked() {
    if (hasPreset())
        emit loadClicked(m_presetIndex);
}

void EqPresetRowWidget::onSaveClicked() {
    emit saveClicked(m_presetIndex);
}

void EqPresetRowWidget::contextMenuEvent(QContextMenuEvent *event) {
    if (!hasPreset())
        return;
    QMenu menu(this);
    QAction *clearAction = menu.addAction("Clear Preset");
    if (menu.exec(event->globalPos()) == clearAction)
        emit clearRequested(m_presetIndex);
}

// =============================================================================
// RxEqPopupWidget
// =============================================================================

RxEqPopupWidget::RxEqPopupWidget(const QString &title, const QString &accentColor, QWidget *parent)
    : K4PopupBase(parent), m_title(title) {
    setupUi(accentColor);
    initPopup();
}

void RxEqPopupWidget::setupUi(const QString &accentColor) {
    // Everything is a child of the opaque panel; nothing is a child of the popup itself.
    QWidget *panel = contentPanel();

    auto *mainLayout = new QVBoxLayout(panel);
    const QMargins m = panelMargins();
    // The title band is painted across the top of the panel, so the content starts below it.
    mainLayout->setContentsMargins(m.left(), EqTitleHeight + m.top(), m.right(), m.bottom());
    mainLayout->setSpacing(0);

    auto *contentLayout = new QHBoxLayout();
    contentLayout->setSpacing(0);

    auto *bandsLayout = new QHBoxLayout();
    bandsLayout->setSpacing(EqBandSpacing);
    for (int i = 0; i < 8; i++) {
        auto *band = new EqBandWidget(i, FREQ_LABELS[i], accentColor, panel);
        connect(band, &EqBandWidget::valueChanged, this, &RxEqPopupWidget::bandValueChanged);
        connect(band, &EqBandWidget::selectionRequested, this, &RxEqPopupWidget::selectBand);
        m_bands.append(band);
        bandsLayout->addWidget(band);
    }
    contentLayout->addLayout(bandsLayout);

    // The "dB" / "Hz" column and the rule before the preset column are painted by paintContent();
    // these spacers are the room they occupy.
    contentLayout->addSpacing(EqColumnGap + EqUnitsWidth);
    contentLayout->addSpacing(2 * PopupContentMargin);

    auto *rightColumn = new QVBoxLayout();
    rightColumn->setSpacing(SidePanelItemSpacing);
    for (int i = 0; i < 4; i++) {
        auto *presetRow = new EqPresetRowWidget(i, panel);
        connect(presetRow, &EqPresetRowWidget::loadClicked, this, &RxEqPopupWidget::presetLoadRequested);
        connect(presetRow, &EqPresetRowWidget::saveClicked, this, &RxEqPopupWidget::presetSaveRequested);
        connect(presetRow, &EqPresetRowWidget::clearRequested, this, &RxEqPopupWidget::presetClearRequested);
        m_presetRows.append(presetRow);
        rightColumn->addWidget(presetRow);
    }
    rightColumn->addStretch();

    m_flatBtn = new QPushButton("FLAT", panel);
    m_flatBtn->setFixedSize(EqRightColumnWidth, ButtonHeightMedium);
    m_flatBtn->setStyleSheet(K4Styles::popupButtonNormal());
    connect(m_flatBtn, &QPushButton::clicked, this, &RxEqPopupWidget::onFlatClicked);
    rightColumn->addWidget(m_flatBtn);

    contentLayout->addLayout(rightColumn);
    mainLayout->addLayout(contentLayout);

    m_bands[m_selectedBand]->setSelected(true);
}

QSize RxEqPopupWidget::contentSize() const {
    const int cm = PopupContentMargin;
    const int bands = 8 * EqBandWidth + 7 * EqBandSpacing;
    const int width = cm + bands + EqColumnGap + EqUnitsWidth + 2 * cm + EqRightColumnWidth + cm;
    const int height = EqTitleHeight + cm + EqBandWidget::columnSize().height() + cm;
    return QSize(width, height);
}

void RxEqPopupWidget::paintContent(QPainter &painter, const QRect &panelRect) {
    const int cm = PopupContentMargin;

    // --- the title band, across the whole card as the radio draws it ----------------------------
    const QRect title(panelRect.left(), panelRect.top(), panelRect.width(), EqTitleHeight);
    painter.fillRect(title, QColor(K4Styles::Colors::OverlayHeaderBg));
    painter.setPen(QPen(QColor(K4Styles::Colors::PanelBorder), HairlineWidth));
    painter.drawLine(title.bottomLeft(), title.bottomRight());
    painter.setFont(K4Styles::Fonts::paintFont(FontSizeReadout));
    painter.setPen(QColor(K4Styles::Colors::TextPrimary));
    painter.drawText(title.adjusted(cm, 0, -cm, 0), Qt::AlignVCenter | Qt::AlignLeft, m_title);

    if (m_bands.isEmpty() || m_presetRows.isEmpty())
        return;

    // --- the units column, lined up with the rows it labels --------------------------------------
    const QRect column = m_bands.last()->geometry();
    const QRect units(column.right() + 1 + EqColumnGap, column.top(), EqUnitsWidth, column.height());
    painter.setFont(K4Styles::Fonts::paintFont(FontSizeCaption));
    painter.setPen(QColor(K4Styles::Colors::TextPrimary));
    painter.drawText(QRect(units.left(), units.top(), units.width(), EqValueHeight), Qt::AlignCenter,
                     QStringLiteral("dB"));
    painter.drawText(QRect(units.left(), units.bottom() + 1 - EqKeyHeight, units.width(), EqKeyHeight), Qt::AlignCenter,
                     QStringLiteral("Hz"));

    // --- the rule between the bands and the preset column ----------------------------------------
    const int rightColumnLeft = m_presetRows.first()->geometry().left();
    const int ruleX = (units.right() + rightColumnLeft) / 2;
    painter.setPen(QPen(QColor(K4Styles::Colors::PanelBorder), HairlineWidth));
    painter.drawLine(ruleX, title.bottom() + 1 + cm, ruleX, panelRect.bottom() - cm);
}

void RxEqPopupWidget::selectBand(int bandIndex) {
    if (bandIndex < 0 || bandIndex >= m_bands.size())
        return;
    m_selectedBand = bandIndex;
    for (int i = 0; i < m_bands.size(); ++i)
        m_bands[i]->setSelected(i == bandIndex);
}

void RxEqPopupWidget::setAllBands(const QVector<int> &values) {
    for (int i = 0; i < qMin(values.size(), m_bands.size()); i++)
        m_bands[i]->setValue(values[i]);
}

void RxEqPopupWidget::resetToFlat() {
    for (auto *band : m_bands)
        band->setValue(0);
}

void RxEqPopupWidget::onFlatClicked() {
    resetToFlat();
    emit flatRequested();
}

void RxEqPopupWidget::updatePresetName(int index, const QString &name) {
    if (index >= 0 && index < m_presetRows.size())
        m_presetRows[index]->setPresetName(name);
}
