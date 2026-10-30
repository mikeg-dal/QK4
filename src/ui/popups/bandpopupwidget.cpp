#include "ui/popups/bandpopupwidget.h"
#include "models/radiostate.h"
#include "ui/styling/k4styles.h"
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace {
// Layout constants (primary dimensions from K4Styles::Dimensions)
const int ButtonWidth = K4Styles::Dimensions::PopupButtonWidth;
const int ButtonHeight = K4Styles::Dimensions::PopupButtonHeight;
const int ButtonSpacing = K4Styles::Dimensions::PopupButtonSpacing;
const int RowSpacing = 2;

// K4 Band Number mapping (BN command)
// Band number -> button label
const QMap<int, QString> BandNumToName = {
    {0, "1.8"}, // 160m
    {1, "3.5"}, // 80m
    {2, "5"},   // 60m
    {3, "7"},   // 40m
    {4, "10"},  // 30m
    {5, "14"},  // 20m
    {6, "18"},  // 17m
    {7, "21"},  // 15m
    {8, "24"},  // 12m
    {9, "28"},  // 10m
    {10, "50"}, // 6m
    // 11-22 are transverter bands (XVTR Band 1-12). Empirically verified against K4 firmware
    // by sending BN11..BN22 and observing FA jumps to configured XVTR dial frequencies.
};

// Button label -> band number. Includes both the legacy "XVTR" → 11 fallback
// (in case anything still calls it generically) and the per-band "XVTR1".."XVTR12"
// → 11..22 mapping used by the new sub-grid picker.
const QMap<QString, int> buildBandNameToNum() {
    QMap<QString, int> m{
        {"1.8", 0}, {"3.5", 1}, {"5", 2},  {"7", 3},  {"10", 4},  {"14", 5},
        {"18", 6},  {"21", 7},  {"24", 8}, {"28", 9}, {"50", 10}, {"XVTR", 11},
    };
    for (int i = 1; i <= 12; ++i) {
        m.insert(QString("XVTR%1").arg(i), 10 + i);
    }
    return m;
}
const QMap<QString, int> BandNameToNum = buildBandNameToNum();
} // namespace

BandPopupWidget::BandPopupWidget(RadioState *radioState, QWidget *parent)
    : K4PopupBase(parent), m_radioState(radioState), m_selectedBand("14") // Default to 20m band
{
    setupUi();
    if (m_radioState) {
        // Refresh XVTR sub-grid labels when any band's config changes.
        connect(m_radioState, &RadioState::xvtrBandsChanged, this, &BandPopupWidget::refreshXvtrCellLabels);
    }
}

QSize BandPopupWidget::contentSize() const {
    int cm = K4Styles::Dimensions::PopupContentMargin;

    int width = 7 * ButtonWidth + 6 * ButtonSpacing + 2 * cm;
    int height = 2 * ButtonHeight + RowSpacing + 2 * cm;
    return QSize(width, height);
}

void BandPopupWidget::setupUi() {
    m_rootLayout = new QVBoxLayout(this);
    m_rootLayout->setContentsMargins(contentMargins());
    m_rootLayout->setSpacing(RowSpacing);

    buildHfGrid();

    // Update styles to show selected band
    updateButtonStyles();

    // Initialize popup size from base class
    initPopup();
}

void BandPopupWidget::clearGrid() {
    // WHY: QLayout inherits from QLayoutItem, so when takeAt(0) returns a row
    // layout, item->layout() and item point to the SAME object. Calling
    // delete on both is a double-free (crashed clearGrid+72 on XVTR tap).
    // Drain children first, then a single delete on the row item frees the
    // QHBoxLayout via its virtual destructor.
    while (QLayoutItem *rowItem = m_rootLayout->takeAt(0)) {
        if (QLayout *row = rowItem->layout()) {
            while (QLayoutItem *cell = row->takeAt(0)) {
                if (QWidget *w = cell->widget()) {
                    delete w;
                }
                delete cell;
            }
        } else if (QWidget *w = rowItem->widget()) {
            delete w;
        }
        delete rowItem;
    }
    m_buttonMap.clear();
}

void BandPopupWidget::setMode(Mode mode) {
    if (m_mode == mode) {
        return;
    }
    m_mode = mode;
    clearGrid();
    if (mode == Mode::Hf) {
        buildHfGrid();
    } else {
        buildXvtrGrid();
    }
    updateButtonStyles();
}

void BandPopupWidget::buildHfGrid() {
    // Row 1: 1.8, 3.5, 7, 14, 21, 28, MEM
    QStringList row1Bands = {"1.8", "3.5", "7", "14", "21", "28", "MEM"};
    auto *row1Layout = new QHBoxLayout();
    row1Layout->setSpacing(ButtonSpacing);
    for (const QString &band : row1Bands) {
        QPushButton *btn = createBandButton(band);
        m_buttonMap[band] = btn;
        row1Layout->addWidget(btn);
    }
    m_rootLayout->addLayout(row1Layout);

    // Row 2: GEN, 5, 10, 18, 24, 50, XVTR
    QStringList row2Bands = {"GEN", "5", "10", "18", "24", "50", "XVTR"};
    auto *row2Layout = new QHBoxLayout();
    row2Layout->setSpacing(ButtonSpacing);
    for (const QString &band : row2Bands) {
        QPushButton *btn = createBandButton(band);
        m_buttonMap[band] = btn;
        row2Layout->addWidget(btn);
    }
    m_rootLayout->addLayout(row2Layout);
}

void BandPopupWidget::buildXvtrGrid() {
    // Row 1: XVTR1, XVTR3, XVTR5, XVTR7, XVTR9, XVTR11, MEM
    auto *row1Layout = new QHBoxLayout();
    row1Layout->setSpacing(ButtonSpacing);
    for (int n : {1, 3, 5, 7, 9, 11}) {
        QPushButton *btn = createXvtrCell(n);
        m_buttonMap[QString("XVTR%1").arg(n)] = btn;
        row1Layout->addWidget(btn);
    }
    QPushButton *memBtn = createBandButton("MEM");
    m_buttonMap["MEM"] = memBtn;
    row1Layout->addWidget(memBtn);
    m_rootLayout->addLayout(row1Layout);

    // Row 2: XVTR2, XVTR4, XVTR6, XVTR8, XVTR10, XVTR12, HF
    auto *row2Layout = new QHBoxLayout();
    row2Layout->setSpacing(ButtonSpacing);
    for (int n : {2, 4, 6, 8, 10, 12}) {
        QPushButton *btn = createXvtrCell(n);
        m_buttonMap[QString("XVTR%1").arg(n)] = btn;
        row2Layout->addWidget(btn);
    }
    QPushButton *hfBtn = createBandButton("HF");
    m_buttonMap["HF"] = hfBtn;
    row2Layout->addWidget(hfBtn);
    m_rootLayout->addLayout(row2Layout);
}

QPushButton *BandPopupWidget::createBandButton(const QString &text) {
    QPushButton *btn = new QPushButton(text, this);
    btn->setFixedSize(ButtonWidth, ButtonHeight);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setProperty("bandName", text);

    connect(btn, &QPushButton::clicked, this, &BandPopupWidget::onBandButtonClicked);

    return btn;
}

QPushButton *BandPopupWidget::createXvtrCell(int bandIndex) {
    // Two-line label: "XVTRn" / "<rfMhz>" or "--".
    QString freqLine = "--";
    bool enabled = false;
    if (m_radioState && bandIndex >= 1 && bandIndex <= m_radioState->xvtrBands().size()) {
        const XvtrBandConfig &cfg = m_radioState->xvtrBands().at(bandIndex - 1);
        if (cfg.configured()) {
            if (cfg.rfMhz != XvtrBandConfig::Unknown)
                freqLine = QString::number(cfg.rfMhz);
            enabled = true;
        }
    }
    QString text = QString("XVTR%1\n%2").arg(bandIndex).arg(freqLine);
    QPushButton *btn = new QPushButton(text, this);
    btn->setFixedSize(ButtonWidth, ButtonHeight);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setEnabled(enabled);
    btn->setProperty("bandName", QString("XVTR%1").arg(bandIndex));
    connect(btn, &QPushButton::clicked, this, &BandPopupWidget::onBandButtonClicked);
    return btn;
}

void BandPopupWidget::refreshXvtrCellLabels() {
    if (m_mode != Mode::Xvtr) {
        return;
    }
    for (int n = 1; n <= 12; ++n) {
        QPushButton *btn = m_buttonMap.value(QString("XVTR%1").arg(n), nullptr);
        if (!btn) {
            continue;
        }
        QString freqLine = "--";
        bool enabled = false;
        if (m_radioState && n - 1 < m_radioState->xvtrBands().size()) {
            const XvtrBandConfig &cfg = m_radioState->xvtrBands().at(n - 1);
            if (cfg.configured()) {
                if (cfg.rfMhz != XvtrBandConfig::Unknown)
                    freqLine = QString::number(cfg.rfMhz);
                enabled = true;
            }
        }
        btn->setText(QString("XVTR%1\n%2").arg(n).arg(freqLine));
        btn->setEnabled(enabled);
    }
}

void BandPopupWidget::updateButtonStyles() {
    for (auto it = m_buttonMap.begin(); it != m_buttonMap.end(); ++it) {
        if (it.key() == m_selectedBand) {
            it.value()->setStyleSheet(K4Styles::popupButtonSelected());
        } else {
            it.value()->setStyleSheet(K4Styles::popupButtonNormal());
        }
    }
}

void BandPopupWidget::setSelectedBand(const QString &bandName) {
    if (m_buttonMap.contains(bandName)) {
        m_selectedBand = bandName;
        updateButtonStyles();
    } else {
        m_selectedBand = bandName;
    }
}

void BandPopupWidget::onBandButtonClicked() {
    QPushButton *btn = qobject_cast<QPushButton *>(sender());
    if (!btn) {
        return;
    }
    QString bandName = btn->property("bandName").toString();

    // WHY: setMode() tears down the entire grid (including the button that's
    // currently emitting this click). Deleting the sender mid-event-dispatch
    // crashes Qt — defer to the next event loop iteration so the click
    // handler returns to the QPushButton machinery first.
    if (bandName == "XVTR" && m_mode == Mode::Hf) {
        QMetaObject::invokeMethod(this, [this] { setMode(Mode::Xvtr); }, Qt::QueuedConnection);
        return;
    }
    if (bandName == "HF" && m_mode == Mode::Xvtr) {
        QMetaObject::invokeMethod(this, [this] { setMode(Mode::Hf); }, Qt::QueuedConnection);
        return;
    }

    m_selectedBand = bandName;
    updateButtonStyles();
    emit bandSelected(bandName);
    hidePopup();
}

int BandPopupWidget::getBandNumber(const QString &bandName) const {
    return BandNameToNum.value(bandName, -1); // -1 for GEN, MEM, or unknown
}

QString BandPopupWidget::getBandName(int bandNum) const {
    // Transverter bands 11-22 (XVTR Band 1-12) map to "XVTR<n>" so the
    // sub-grid can highlight the active cell. Callers that just want the
    // generic "XVTR" label can compare against the prefix.
    if (bandNum >= 11 && bandNum <= 22) {
        return QString("XVTR%1").arg(bandNum - 10);
    }
    return BandNumToName.value(bandNum, QString());
}

void BandPopupWidget::setSelectedBandByNumber(int bandNum) {
    QString bandName = getBandName(bandNum);
    if (!bandName.isEmpty()) {
        setSelectedBand(bandName);
    }
}
