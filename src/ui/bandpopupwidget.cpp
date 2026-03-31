#include "bandpopupwidget.h"
#include "k4styles.h"
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
    // 16-25 are transverter bands, all map to "XVTR"
};

// Button label -> band number
const QMap<QString, int> BandNameToNum = {
    {"1.8", 0}, {"3.5", 1}, {"5", 2},  {"7", 3},  {"10", 4},  {"14", 5},
    {"18", 6},  {"21", 7},  {"24", 8}, {"28", 9}, {"50", 10}, {"XVTR", 16}, // First transverter band (16-25 range)
};
} // namespace

BandPopupWidget::BandPopupWidget(QWidget *parent)
    : K4PopupBase(parent), m_selectedBand("14") // Default to 20m band
{
    setupUi();
}

QSize BandPopupWidget::contentSize() const {
    int cm = K4Styles::Dimensions::PopupContentMargin;

    int width = 7 * ButtonWidth + 6 * ButtonSpacing + 2 * cm;
    int height = 2 * ButtonHeight + RowSpacing + 2 * cm;
    return QSize(width, height);
}

void BandPopupWidget::setupUi() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(contentMargins());
    mainLayout->setSpacing(RowSpacing);

    // Row 1: 1.8, 3.5, 7, 14, 21, 28, MEM
    QStringList row1Bands = {"1.8", "3.5", "7", "14", "21", "28", "MEM"};
    auto *row1Layout = new QHBoxLayout();
    row1Layout->setSpacing(ButtonSpacing);
    for (const QString &band : row1Bands) {
        QPushButton *btn = createBandButton(band);
        m_buttonMap[band] = btn;
        row1Layout->addWidget(btn);
    }
    mainLayout->addLayout(row1Layout);

    // Row 2: GEN, 5, 10, 18, 24, 50, XVTR
    QStringList row2Bands = {"GEN", "5", "10", "18", "24", "50", "XVTR"};
    auto *row2Layout = new QHBoxLayout();
    row2Layout->setSpacing(ButtonSpacing);
    for (const QString &band : row2Bands) {
        QPushButton *btn = createBandButton(band);
        m_buttonMap[band] = btn;
        row2Layout->addWidget(btn);
    }
    mainLayout->addLayout(row2Layout);

    // Update styles to show selected band
    updateButtonStyles();

    // Initialize popup size from base class
    initPopup();
}

QPushButton *BandPopupWidget::createBandButton(const QString &text) {
    QPushButton *btn = new QPushButton(text, this);
    btn->setFixedSize(ButtonWidth, ButtonHeight);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setProperty("bandName", text);

    connect(btn, &QPushButton::clicked, this, &BandPopupWidget::onBandButtonClicked);

    return btn;
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
    }
}

void BandPopupWidget::onBandButtonClicked() {
    QPushButton *btn = qobject_cast<QPushButton *>(sender());
    if (btn) {
        QString bandName = btn->property("bandName").toString();
        m_selectedBand = bandName;
        updateButtonStyles();
        emit bandSelected(bandName);
        hidePopup();
    }
}

int BandPopupWidget::getBandNumber(const QString &bandName) const {
    return BandNameToNum.value(bandName, -1); // -1 for GEN, MEM, or unknown
}

QString BandPopupWidget::getBandName(int bandNum) const {
    // Transverter bands 16-25 all map to XVTR
    if (bandNum >= 16 && bandNum <= 25) {
        return "XVTR";
    }
    return BandNumToName.value(bandNum, QString());
}

void BandPopupWidget::setSelectedBandByNumber(int bandNum) {
    QString bandName = getBandName(bandNum);
    if (!bandName.isEmpty()) {
        setSelectedBand(bandName);
    }
}
