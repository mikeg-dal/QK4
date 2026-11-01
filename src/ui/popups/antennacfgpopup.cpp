#include "ui/popups/antennacfgpopup.h"
#include "ui/styling/k4styles.h"
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QPainter>
#include <QPen>
#include <QVBoxLayout>

AntennaCfgPopupWidget::AntennaCfgPopupWidget(AntennaCfgVariant variant, QWidget *parent)
    : K4PopupBase(parent), m_variant(variant) {
    m_antennaCount = (variant == AntennaCfgVariant::Tx) ? 3 : 7;
    setupUi();
    initPopup();
}

void AntennaCfgPopupWidget::setupUi() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(contentMargins());
    mainLayout->setSpacing(4);

    // Title - compact
    QString title;
    switch (m_variant) {
    case AntennaCfgVariant::MainRx:
        title = "RX ANT SWITCH";
        break;
    case AntennaCfgVariant::SubRx:
        title = "SUB ANT SWITCH";
        break;
    case AntennaCfgVariant::Tx:
        title = "TX ANT SWITCH";
        break;
    }

    auto *titleLabel = new QLabel(title, this);
    titleLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;"
                                      "background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
                                      "  stop:0 %3, stop:1 %4);"
                                      "padding: 4px 10px;"
                                      "border-radius: 3px;")
                                  .arg(K4Styles::Colors::TextPrimary)
                                  .arg(K4Styles::Dimensions::FontSizeReadout)
                                  .arg(K4Styles::Colors::GradientTop)
                                  .arg(K4Styles::Colors::GradientBottom));
    mainLayout->addWidget(titleLabel);

    // Mode selection row
    auto *modeLayout = new QHBoxLayout();
    modeLayout->setSpacing(K4Styles::Dimensions::PopupButtonSpacing);

    // DISPLAY ALL radio button - smaller
    m_displayAllBtn = new QPushButton("DISPLAY ALL", this);
    m_displayAllBtn->setCheckable(true);
    m_displayAllBtn->setChecked(true);
    m_displayAllBtn->setFixedHeight(24);
    m_displayAllBtn->setStyleSheet(
        K4Styles::radioButton() +
        QString("QPushButton { font-size: %1px; padding: 4px 8px; }").arg(K4Styles::Dimensions::FontSizeCaption));
    connect(m_displayAllBtn, &QPushButton::clicked, this, &AntennaCfgPopupWidget::onDisplayAllClicked);
    modeLayout->addWidget(m_displayAllBtn);

    modeLayout->addStretch();

    mainLayout->addLayout(modeLayout);

    // USE SUBSET row with checkboxes
    auto *subsetLayout = new QHBoxLayout();
    subsetLayout->setSpacing(22);

    // USE SUBSET radio button - smaller
    m_useSubsetBtn = new QPushButton("USE SUBSET:", this);
    m_useSubsetBtn->setCheckable(true);
    m_useSubsetBtn->setChecked(false);
    m_useSubsetBtn->setFixedHeight(24);
    m_useSubsetBtn->setStyleSheet(
        K4Styles::radioButton() +
        QString("QPushButton { font-size: %1px; padding: 4px 8px; }").arg(K4Styles::Dimensions::FontSizeCaption));
    connect(m_useSubsetBtn, &QPushButton::clicked, this, &AntennaCfgPopupWidget::onUseSubsetClicked);
    subsetLayout->addWidget(m_useSubsetBtn);

    subsetLayout->addSpacing(8);

    // Antenna checkboxes with labels - smaller
    const char *const *labels = (m_variant == AntennaCfgVariant::Tx) ? TX_LABELS : RX_LABELS;
    constexpr int checkboxSize = 20;

    for (int i = 0; i < m_antennaCount; i++) {
        auto *antLayout = new QVBoxLayout();
        antLayout->setSpacing(1);

        // Label above checkbox - smaller font
        auto *label = new QLabel(labels[i], this);
        label->setAlignment(Qt::AlignCenter);
        label->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                 .arg(K4Styles::Colors::TextPrimary)
                                 .arg(K4Styles::Dimensions::FontSizeCaption));
        m_labels.append(label);
        antLayout->addWidget(label);

        // Checkbox button with checkmark text
        auto *checkbox = new QPushButton("\u2713", this); // ✓ checkmark
        checkbox->setCheckable(true);
        checkbox->setChecked(false);
        checkbox->setEnabled(false); // Disabled when DISPLAY ALL is selected
        checkbox->setStyleSheet(K4Styles::checkboxButton(checkboxSize));
        connect(checkbox, &QPushButton::toggled, this, [this, i]() { onCheckboxToggled(i); });
        m_checkboxes.append(checkbox);
        antLayout->addWidget(checkbox, 0, Qt::AlignCenter);

        subsetLayout->addLayout(antLayout);
    }

    subsetLayout->addStretch();
    mainLayout->addLayout(subsetLayout);

    // Note label (for RX variants only) — right margin leaves room for the
    // bracket-line paint that connects the text to the =OPP TX ANT checkbox.
    if (m_variant != AntennaCfgVariant::Tx) {
        m_noteLabel = new QLabel("Requires ATU: set TX>ANT CFG for 2-antenna subset", this);
        m_noteLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-style: italic;")
                                       .arg(K4Styles::Colors::TextSecondary)
                                       .arg(K4Styles::Dimensions::FontSizeCaption));
        m_noteLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_noteLabel->setContentsMargins(0, 0, 0, 0);
        mainLayout->addWidget(m_noteLabel);
    }
}

void AntennaCfgPopupWidget::paintContent(QPainter &painter, const QRect &contentRect) {
    Q_UNUSED(contentRect)
    if (!m_noteLabel || m_checkboxes.isEmpty()) {
        return;
    }

    // Map child geometries to our coordinate space (paintContent uses widget coords).
    const QRect noteRect = m_noteLabel->geometry();
    const QRect lastBox = m_checkboxes.last()->geometry();

    // Where the visible text ends: left-aligned, so right edge = left + text width.
    const QFontMetrics fm(m_noteLabel->font());
    const int textStartX = noteRect.left() + m_noteLabel->contentsMargins().left();
    const int textRightX = textStartX + fm.horizontalAdvance(m_noteLabel->text());

    // WHY: line sits at the text baseline so the italic descenders just kiss it —
    // gives the visual effect that the bracket continues the text underline.
    const int textBaselineY = noteRect.center().y() + fm.ascent() / 2;
    const int horizY = textBaselineY;
    const int vertX = lastBox.center().x();
    const int topY = lastBox.bottom() + 2;
    const int leftX = textRightX + 3;

    QPen pen{QColor(K4Styles::Colors::TextSecondary)};
    pen.setWidth(1);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(pen);
    painter.drawLine(leftX, horizY, vertX, horizY); // horizontal (text → under checkbox)
    painter.drawLine(vertX, horizY, vertX, topY);   // vertical (up to checkbox bottom)
    painter.restore();
}

QSize AntennaCfgPopupWidget::contentSize() const {
    int cm = K4Styles::Dimensions::PopupContentMargin;
    int width, height;

    if (m_variant == AntennaCfgVariant::Tx) {
        // TX: 3 antennas
        width = 390 + 2 * cm;
        height = 135 + 2 * cm;
    } else {
        // RX: 7 antennas, plus room for the note and its bracket line
        width = 630 + 2 * cm;
        height = 158 + 2 * cm;
    }

    return QSize(width, height);
}

void AntennaCfgPopupWidget::setDisplayAll(bool displayAll) {
    if (m_displayAll != displayAll) {
        m_displayAll = displayAll;
        m_displayAllBtn->setChecked(displayAll);
        m_useSubsetBtn->setChecked(!displayAll);
        updateCheckboxStates();
    }
}

void AntennaCfgPopupWidget::setAntennaMask(const QVector<bool> &mask) {
    for (int i = 0; i < qMin(mask.size(), m_checkboxes.size()); i++) {
        m_checkboxes[i]->blockSignals(true);
        m_checkboxes[i]->setChecked(mask[i]);
        m_checkboxes[i]->blockSignals(false);
    }
}

void AntennaCfgPopupWidget::setAntennaName(int index, const QString &name) {
    // Only ANT1-3 can have custom names (indices 0-2)
    if (index >= 0 && index < 3 && index < m_labels.size()) {
        m_labels[index]->setText(name.isEmpty() ? QString("ANT%1").arg(index + 1) : name);
    }
}

QVector<bool> AntennaCfgPopupWidget::antennaMask() const {
    QVector<bool> mask;
    for (auto *cb : m_checkboxes) {
        mask.append(cb->isChecked());
    }
    return mask;
}

void AntennaCfgPopupWidget::onDisplayAllClicked() {
    m_displayAll = true;
    m_displayAllBtn->setChecked(true);
    m_useSubsetBtn->setChecked(false);
    updateCheckboxStates();
    emitConfigChanged();
}

void AntennaCfgPopupWidget::onUseSubsetClicked() {
    m_displayAll = false;
    m_displayAllBtn->setChecked(false);
    m_useSubsetBtn->setChecked(true);
    updateCheckboxStates();
    emitConfigChanged();
}

void AntennaCfgPopupWidget::onCheckboxToggled(int index) {
    Q_UNUSED(index)
    emitConfigChanged();
}

void AntennaCfgPopupWidget::updateCheckboxStates() {
    // Enable/disable checkboxes based on mode
    for (auto *cb : m_checkboxes) {
        cb->setEnabled(!m_displayAll);
    }
}

void AntennaCfgPopupWidget::emitConfigChanged() {
    emit configChanged(m_displayAll, antennaMask());
}
