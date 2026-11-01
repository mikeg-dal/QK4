#include "ui/popups/rxeqpopupwidget.h"
#include "ui/styling/k4styles.h"
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QMenu>
#include <QVBoxLayout>
#include <QWheelEvent>

// =============================================================================
// EqBandWidget Implementation
// =============================================================================

EqBandWidget::EqBandWidget(int bandIndex, const QString &freqLabel, const QString &accentColor, QWidget *parent)
    : QWidget(parent), m_bandIndex(bandIndex) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    // dB value label (top)
    m_valueLabel = new QLabel("0", this);
    m_valueLabel->setAlignment(Qt::AlignCenter);
    m_valueLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                    .arg(K4Styles::Colors::TextPrimary)
                                    .arg(K4Styles::Dimensions::FontSizeCaption));
    m_valueLabel->setFixedHeight(K4Styles::Dimensions::CheckboxSize);
    layout->addWidget(m_valueLabel);

    // Plus button
    m_plusBtn = new QPushButton("+", this);
    m_plusBtn->setFixedSize(32, K4Styles::Dimensions::ButtonHeightMini);
    m_plusBtn->setStyleSheet(K4Styles::menuBarButtonSmall());
    connect(m_plusBtn, &QPushButton::clicked, this, &EqBandWidget::onPlusClicked);
    layout->addWidget(m_plusBtn, 0, Qt::AlignCenter);

    // Vertical slider
    m_slider = new QSlider(Qt::Vertical, this);
    m_slider->setMinimum(MIN_DB);
    m_slider->setMaximum(MAX_DB);
    m_slider->setValue(0);
    m_slider->setFixedSize(32, K4Styles::Dimensions::InputFieldWidthMedium);

    // Vertical slider stylesheet with the accent-colour fill below the handle
    QString sliderStyle = QString("QSlider::groove:vertical {"
                                  "  border: 1px solid %1;"
                                  "  width: 10px;"
                                  "  background: %2;"
                                  "  border-radius: 5px;"
                                  "  margin: 0 10px;"
                                  "}"
                                  "QSlider::handle:vertical {"
                                  "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
                                  "    stop:0 %3, stop:0.5 %4, stop:1 %5);"
                                  "  border: 1px solid %6;"
                                  "  height: 16px;"
                                  "  margin: 0 -10px;"
                                  "  border-radius: %8px;"
                                  "}"
                                  "QSlider::add-page:vertical {"
                                  "  background: %7;"
                                  "  border-radius: 5px;"
                                  "  margin: 0 10px;"
                                  "}"
                                  "QSlider::sub-page:vertical {"
                                  "  background: %2;"
                                  "  border-radius: 5px;"
                                  "  margin: 0 10px;"
                                  "}")
                              .arg(K4Styles::Colors::BorderNormal)            // groove border
                              .arg(K4Styles::Colors::DarkBackground)          // groove bg (unfilled)
                              .arg(K4Styles::Colors::SelectedTop)             // handle gradient top
                              .arg(K4Styles::Colors::SelectedMid1)            // handle gradient mid
                              .arg(K4Styles::Colors::SelectedBottom)          // handle gradient bottom
                              .arg(K4Styles::Colors::BorderNormal)            // handle border
                              .arg(accentColor)                               // fill colour (accent)
                              .arg(K4Styles::Dimensions::SliderBorderRadius); // handle border radius

    m_slider->setStyleSheet(sliderStyle);
    connect(m_slider, &QSlider::valueChanged, this, &EqBandWidget::onSliderChanged);
    layout->addWidget(m_slider, 0, Qt::AlignCenter);

    // Minus button
    m_minusBtn = new QPushButton("-", this);
    m_minusBtn->setFixedSize(32, K4Styles::Dimensions::ButtonHeightMini);
    m_minusBtn->setStyleSheet(K4Styles::menuBarButtonSmall());
    connect(m_minusBtn, &QPushButton::clicked, this, &EqBandWidget::onMinusClicked);
    layout->addWidget(m_minusBtn, 0, Qt::AlignCenter);

    // Frequency label (bottom)
    m_freqLabel = new QLabel(freqLabel, this);
    m_freqLabel->setAlignment(Qt::AlignCenter);
    m_freqLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;"
                                       "background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
                                       "  stop:0 %3, stop:0.4 %4, stop:0.6 %5, stop:1 %6);"
                                       "border: 1px solid %7;"
                                       "border-radius: 4px;"
                                       "padding: 2px 4px;")
                                   .arg(K4Styles::Colors::TextPrimary)
                                   .arg(K4Styles::Dimensions::FontSizeCaption)
                                   .arg(K4Styles::Colors::GradientTop)
                                   .arg(K4Styles::Colors::GradientMid1)
                                   .arg(K4Styles::Colors::GradientMid2)
                                   .arg(K4Styles::Colors::GradientBottom)
                                   .arg(K4Styles::Colors::BorderNormal));
    m_freqLabel->setFixedHeight(22);
    layout->addWidget(m_freqLabel);

    setFixedWidth(50);
}

int EqBandWidget::value() const {
    return m_value;
}

void EqBandWidget::setValue(int dB) {
    dB = qBound(MIN_DB, dB, MAX_DB);
    if (m_value != dB) {
        m_value = dB;
        m_slider->blockSignals(true);
        m_slider->setValue(dB);
        m_slider->blockSignals(false);
        updateValueLabel();
    }
}

void EqBandWidget::wheelEvent(QWheelEvent *event) {
    int steps = m_wheelAccumulator.accumulate(event);
    if (steps != 0) {
        int newValue = qBound(MIN_DB, m_value + steps, MAX_DB);
        if (newValue != m_value) {
            m_value = newValue;
            m_slider->blockSignals(true);
            m_slider->setValue(m_value);
            m_slider->blockSignals(false);
            updateValueLabel();
            emit valueChanged(m_bandIndex, m_value);
        }
    }
    event->accept();
}

void EqBandWidget::onPlusClicked() {
    if (m_value < MAX_DB) {
        m_value++;
        m_slider->blockSignals(true);
        m_slider->setValue(m_value);
        m_slider->blockSignals(false);
        updateValueLabel();
        emit valueChanged(m_bandIndex, m_value);
    }
}

void EqBandWidget::onMinusClicked() {
    if (m_value > MIN_DB) {
        m_value--;
        m_slider->blockSignals(true);
        m_slider->setValue(m_value);
        m_slider->blockSignals(false);
        updateValueLabel();
        emit valueChanged(m_bandIndex, m_value);
    }
}

void EqBandWidget::onSliderChanged(int value) {
    if (m_value != value) {
        m_value = value;
        updateValueLabel();
        emit valueChanged(m_bandIndex, m_value);
    }
}

void EqBandWidget::updateValueLabel() {
    if (m_value > 0) {
        m_valueLabel->setText(QString("+%1").arg(m_value));
    } else {
        m_valueLabel->setText(QString::number(m_value));
    }
}

// =============================================================================
// EqPresetRowWidget Implementation
// =============================================================================

EqPresetRowWidget::EqPresetRowWidget(int presetIndex, QWidget *parent) : QWidget(parent), m_presetIndex(presetIndex) {
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    // Preset name/load button
    m_loadBtn = new QPushButton("---", this);
    m_loadBtn->setFixedSize(48, 26);
    m_loadBtn->setStyleSheet(
        K4Styles::menuBarButtonSmall() +
        QString("QPushButton { font-size: %1px; padding: 2px; }").arg(K4Styles::Dimensions::FontSizeCaption));
    connect(m_loadBtn, &QPushButton::clicked, this, &EqPresetRowWidget::onLoadClicked);
    layout->addWidget(m_loadBtn);

    // Save button
    m_saveBtn = new QPushButton("S", this);
    m_saveBtn->setFixedSize(K4Styles::Dimensions::ButtonHeightMini, 26);
    m_saveBtn->setStyleSheet(
        K4Styles::menuBarButtonSmall() +
        QString("QPushButton { font-size: %1px; font-weight: bold; }").arg(K4Styles::Dimensions::FontSizeCaption));
    m_saveBtn->setToolTip("Save current EQ to this preset");
    connect(m_saveBtn, &QPushButton::clicked, this, &EqPresetRowWidget::onSaveClicked);
    layout->addWidget(m_saveBtn);
}

void EqPresetRowWidget::setPresetName(const QString &name) {
    m_name = name;
    if (name.isEmpty()) {
        m_loadBtn->setText("---");
        m_loadBtn->setStyleSheet(K4Styles::menuBarButtonSmall() +
                                 QString("QPushButton { font-size: %1px; padding: 2px; color: %2; }")
                                     .arg(K4Styles::Dimensions::FontSizeCaption)
                                     .arg(K4Styles::Colors::TextSecondaryOnRaised));
    } else {
        m_loadBtn->setText(name);
        m_loadBtn->setStyleSheet(
            K4Styles::menuBarButtonSmall() +
            QString("QPushButton { font-size: %1px; padding: 2px; }").arg(K4Styles::Dimensions::FontSizeCaption));
    }
}

void EqPresetRowWidget::onLoadClicked() {
    if (hasPreset()) {
        emit loadClicked(m_presetIndex);
    }
}

void EqPresetRowWidget::onSaveClicked() {
    emit saveClicked(m_presetIndex);
}

void EqPresetRowWidget::contextMenuEvent(QContextMenuEvent *event) {
    if (hasPreset()) {
        QMenu menu(this);
        QAction *clearAction = menu.addAction("Clear Preset");
        QAction *selected = menu.exec(event->globalPos());
        if (selected == clearAction) {
            emit clearRequested(m_presetIndex);
        }
    }
}

// =============================================================================
// RxEqPopupWidget Implementation
// =============================================================================

RxEqPopupWidget::RxEqPopupWidget(const QString &title, const QString &accentColor, QWidget *parent)
    : K4PopupBase(parent) {
    setupUi(title, accentColor);
    initPopup();
}

void RxEqPopupWidget::setupUi(const QString &title, const QString &accentColor) {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(contentMargins());
    mainLayout->setSpacing(K4Styles::Dimensions::PopupButtonSpacing);

    // Title bar
    auto *titleLabel = new QLabel(title, this);
    titleLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;"
                                      "background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
                                      "  stop:0 %3, stop:1 %4);"
                                      "padding: 6px 12px;"
                                      "border-radius: 4px;")
                                  .arg(K4Styles::Colors::TextPrimary)
                                  .arg(K4Styles::Dimensions::FontSizeHeading)
                                  .arg(K4Styles::Colors::GradientTop)
                                  .arg(K4Styles::Colors::GradientBottom));
    mainLayout->addWidget(titleLabel);

    // Content area: bands + right side buttons
    auto *contentLayout = new QHBoxLayout();
    contentLayout->setSpacing(4);

    // EQ bands row
    auto *bandsLayout = new QHBoxLayout();
    bandsLayout->setSpacing(K4Styles::Dimensions::PopupButtonSpacing);

    for (int i = 0; i < 8; i++) {
        auto *band = new EqBandWidget(i, FREQ_LABELS[i], accentColor, this);
        connect(band, &EqBandWidget::valueChanged, this, &RxEqPopupWidget::onBandValueChanged);
        m_bands.append(band);
        bandsLayout->addWidget(band);
    }

    contentLayout->addLayout(bandsLayout);

    // dB and Hz labels column
    auto *labelsLayout = new QVBoxLayout();
    labelsLayout->setSpacing(0);

    auto *dbLabel = new QLabel("dB", this);
    dbLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                               .arg(K4Styles::Colors::TextPrimary)
                               .arg(K4Styles::Dimensions::FontSizeCaption));
    dbLabel->setAlignment(Qt::AlignCenter);
    labelsLayout->addWidget(dbLabel);

    labelsLayout->addStretch();

    auto *hzLabel = new QLabel("Hz", this);
    hzLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                               .arg(K4Styles::Colors::TextPrimary)
                               .arg(K4Styles::Dimensions::FontSizeCaption));
    hzLabel->setAlignment(Qt::AlignCenter);
    labelsLayout->addWidget(hzLabel);

    contentLayout->addLayout(labelsLayout);

    // Right side buttons column
    auto *buttonsLayout = new QVBoxLayout();
    buttonsLayout->setSpacing(4);

    // 4 preset rows
    for (int i = 0; i < 4; i++) {
        auto *presetRow = new EqPresetRowWidget(i, this);
        connect(presetRow, &EqPresetRowWidget::loadClicked, this, &RxEqPopupWidget::onPresetLoadClicked);
        connect(presetRow, &EqPresetRowWidget::saveClicked, this, &RxEqPopupWidget::onPresetSaveClicked);
        connect(presetRow, &EqPresetRowWidget::clearRequested, this, &RxEqPopupWidget::onPresetClearClicked);
        m_presetRows.append(presetRow);
        buttonsLayout->addWidget(presetRow);
    }

    buttonsLayout->addStretch();

    // FLAT button
    m_flatBtn = new QPushButton("FLAT", this);
    m_flatBtn->setFixedSize(76, K4Styles::Dimensions::ButtonHeightMedium);
    m_flatBtn->setStyleSheet(K4Styles::popupButtonNormal());
    connect(m_flatBtn, &QPushButton::clicked, this, &RxEqPopupWidget::onFlatClicked);
    buttonsLayout->addWidget(m_flatBtn);

    contentLayout->addLayout(buttonsLayout);

    mainLayout->addLayout(contentLayout);
}

QSize RxEqPopupWidget::contentSize() const {
    int cm = K4Styles::Dimensions::PopupContentMargin;
    // 8 bands * 50px + 7 gaps * 8px + labels column ~30px + buttons column ~85px + margins
    int width = 8 * 50 + 7 * 8 + 30 + 85 + 2 * cm;
    int height = 310 + 2 * cm;
    return QSize(width, height);
}

void RxEqPopupWidget::setAllBands(const QVector<int> &values) {
    for (int i = 0; i < qMin(values.size(), m_bands.size()); i++) {
        m_bands[i]->setValue(values[i]);
    }
}

void RxEqPopupWidget::resetToFlat() {
    for (auto *band : m_bands) {
        band->setValue(0);
    }
}

void RxEqPopupWidget::onBandValueChanged(int bandIndex, int dB) {
    emit bandValueChanged(bandIndex, dB);
}

void RxEqPopupWidget::onFlatClicked() {
    resetToFlat();
    emit flatRequested();
}

void RxEqPopupWidget::updatePresetName(int index, const QString &name) {
    if (index >= 0 && index < m_presetRows.size()) {
        m_presetRows[index]->setPresetName(name);
    }
}

void RxEqPopupWidget::onPresetLoadClicked(int index) {
    emit presetLoadRequested(index);
}

void RxEqPopupWidget::onPresetSaveClicked(int index) {
    emit presetSaveRequested(index);
}

void RxEqPopupWidget::onPresetClearClicked(int index) {
    emit presetClearRequested(index);
}
