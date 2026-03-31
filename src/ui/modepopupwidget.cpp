#include "modepopupwidget.h"
#include "k4styles.h"
#include <QGridLayout>
#include <QVBoxLayout>

namespace {
// Layout constants (primary dimensions from K4Styles::Dimensions)
const int ButtonWidth = K4Styles::Dimensions::PopupButtonWidth;
const int ButtonHeight = K4Styles::Dimensions::PopupButtonHeight;
const int ButtonSpacing = K4Styles::Dimensions::PopupButtonSpacing;
const int RowSpacing = 2;

// Mode codes (from K4 protocol)
const int MODE_LSB = 1;
const int MODE_USB = 2;
const int MODE_CW = 3;
const int MODE_FM = 4;
const int MODE_AM = 5;
const int MODE_DATA = 6;
const int MODE_CW_R = 7;
const int MODE_DATA_R = 9;

// Data sub-mode codes
const int DT_DATA_A = 0;
const int DT_AFSK_A = 1;
const int DT_FSK_D = 2;
const int DT_PSK_D = 3;
} // namespace

ModePopupWidget::ModePopupWidget(QWidget *parent) : K4PopupBase(parent) {
    setupUi();
}

QSize ModePopupWidget::contentSize() const {
    int cm = K4Styles::Dimensions::PopupContentMargin;

    int width = 4 * ButtonWidth + 3 * ButtonSpacing + 2 * cm;
    int height = 2 * ButtonHeight + RowSpacing + 2 * cm;
    return QSize(width, height);
}

void ModePopupWidget::setupUi() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(contentMargins());
    mainLayout->setSpacing(RowSpacing);

    // Create 2x4 grid
    auto *gridLayout = new QGridLayout();
    gridLayout->setSpacing(ButtonSpacing);

    // Row 1: CW, SSB, DATA, AFSK
    m_cwBtn = createModeButton("CW");
    m_cwBtn->setProperty("modeType", "CW");
    gridLayout->addWidget(m_cwBtn, 0, 0);

    m_ssbBtn = createModeButton("USB"); // Will show LSB or USB based on current mode
    m_ssbBtn->setProperty("modeType", "SSB");
    gridLayout->addWidget(m_ssbBtn, 0, 1);

    m_dataBtn = createModeButton("DATA");
    m_dataBtn->setProperty("modeType", "DATA");
    gridLayout->addWidget(m_dataBtn, 0, 2);

    m_afskBtn = createModeButton("AFSK");
    m_afskBtn->setProperty("modeType", "AFSK");
    gridLayout->addWidget(m_afskBtn, 0, 3);

    // Row 2: AM, FM, PSK, FSK
    m_amBtn = createModeButton("AM");
    m_amBtn->setProperty("modeType", "AM");
    gridLayout->addWidget(m_amBtn, 1, 0);

    m_fmBtn = createModeButton("FM");
    m_fmBtn->setProperty("modeType", "FM");
    gridLayout->addWidget(m_fmBtn, 1, 1);

    m_pskBtn = createModeButton("PSK");
    m_pskBtn->setProperty("modeType", "PSK");
    gridLayout->addWidget(m_pskBtn, 1, 2);

    m_fskBtn = createModeButton("FSK");
    m_fskBtn->setProperty("modeType", "FSK");
    gridLayout->addWidget(m_fskBtn, 1, 3);

    mainLayout->addLayout(gridLayout);

    // Store button references
    m_buttonMap["CW"] = m_cwBtn;
    m_buttonMap["SSB"] = m_ssbBtn;
    m_buttonMap["DATA"] = m_dataBtn;
    m_buttonMap["AFSK"] = m_afskBtn;
    m_buttonMap["AM"] = m_amBtn;
    m_buttonMap["FM"] = m_fmBtn;
    m_buttonMap["PSK"] = m_pskBtn;
    m_buttonMap["FSK"] = m_fskBtn;

    updateButtonStyles();

    // Initialize popup size from base class
    initPopup();
}

QPushButton *ModePopupWidget::createModeButton(const QString &text) {
    auto *btn = new QPushButton(text, this);
    btn->setFixedSize(ButtonWidth, ButtonHeight);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setCursor(Qt::PointingHandCursor);

    connect(btn, &QPushButton::clicked, this, &ModePopupWidget::onModeButtonClicked);

    return btn;
}

void ModePopupWidget::updateButtonStyles() {
    // Update SSB button text based on current mode or band default
    if (m_currentMode == MODE_LSB) {
        m_ssbBtn->setText("LSB");
    } else if (m_currentMode == MODE_USB) {
        m_ssbBtn->setText("USB");
    } else {
        // Not in SSB mode - show band-appropriate default
        m_ssbBtn->setText(bandDefaultSideband() == MODE_LSB ? "LSB" : "USB");
    }

    // Update CW button text based on current mode
    m_cwBtn->setText(m_currentMode == MODE_CW_R ? "CW-R" : "CW");

    // Reset all buttons to normal style
    for (auto it = m_buttonMap.begin(); it != m_buttonMap.end(); ++it) {
        it.value()->setStyleSheet(K4Styles::popupButtonNormal());
    }

    // Highlight the current mode button
    switch (m_currentMode) {
    case MODE_CW:
    case MODE_CW_R:
        m_cwBtn->setStyleSheet(K4Styles::popupButtonSelected());
        break;
    case MODE_LSB:
    case MODE_USB:
        m_ssbBtn->setStyleSheet(K4Styles::popupButtonSelected());
        break;
    case MODE_AM:
        m_amBtn->setStyleSheet(K4Styles::popupButtonSelected());
        break;
    case MODE_FM:
        m_fmBtn->setStyleSheet(K4Styles::popupButtonSelected());
        break;
    case MODE_DATA:
    case MODE_DATA_R:
        // Highlight based on data sub-mode
        switch (m_currentDataSubMode) {
        case DT_DATA_A:
            m_dataBtn->setStyleSheet(K4Styles::popupButtonSelected());
            break;
        case DT_AFSK_A:
            m_afskBtn->setStyleSheet(K4Styles::popupButtonSelected());
            break;
        case DT_FSK_D:
            m_fskBtn->setStyleSheet(K4Styles::popupButtonSelected());
            break;
        case DT_PSK_D:
            m_pskBtn->setStyleSheet(K4Styles::popupButtonSelected());
            break;
        }
        break;
    }
}

void ModePopupWidget::setCurrentMode(int modeCode) {
    m_currentMode = modeCode;
    updateButtonStyles();
}

void ModePopupWidget::setCurrentDataSubMode(int subMode) {
    m_currentDataSubMode = subMode;
    updateButtonStyles();
}

void ModePopupWidget::setBSetEnabled(bool enabled) {
    m_bSetEnabled = enabled;
}

void ModePopupWidget::setFrequency(quint64 freqHz) {
    m_frequency = freqHz;
    updateButtonStyles();
}

int ModePopupWidget::bandDefaultSideband() const {
    // Amateur radio convention: LSB below 10 MHz, USB at 10 MHz and above
    // 160m (1.8), 80m (3.5), 60m (5), 40m (7) = LSB
    // 30m (10), 20m (14), 17m (18), 15m (21), 12m (24), 10m (28), 6m (50) = USB
    return (m_frequency < 10000000) ? MODE_LSB : MODE_USB;
}

void ModePopupWidget::onModeButtonClicked() {
    auto *btn = qobject_cast<QPushButton *>(sender());
    if (!btn)
        return;

    QString modeType = btn->property("modeType").toString();
    QString cmd;
    QString prefix = m_bSetEnabled ? "MD$" : "MD";
    QString dtPrefix = m_bSetEnabled ? "DT$" : "DT";

    if (modeType == "CW") {
        // If already in CW mode, toggle between CW and CW-R
        // Otherwise, switch to CW
        if (m_currentMode == MODE_CW) {
            cmd = prefix + "7;"; // Toggle to CW-R
        } else if (m_currentMode == MODE_CW_R) {
            cmd = prefix + "3;"; // Toggle to CW
        } else {
            cmd = prefix + "3;"; // Default to CW
        }
    } else if (modeType == "SSB") {
        // If already in SSB mode, toggle between LSB and USB
        // Otherwise, switch to band-appropriate sideband (LSB <10MHz, USB >=10MHz)
        if (m_currentMode == MODE_LSB) {
            cmd = prefix + "2;"; // Toggle to USB
        } else if (m_currentMode == MODE_USB) {
            cmd = prefix + "1;"; // Toggle to LSB
        } else {
            // Not in SSB - use band-appropriate default
            cmd = prefix + QString::number(bandDefaultSideband()) + ";";
        }
    } else if (modeType == "DATA") {
        cmd = prefix + "6;" + dtPrefix + "0;"; // DATA mode + DATA-A sub-mode
    } else if (modeType == "AFSK") {
        cmd = prefix + "6;" + dtPrefix + "1;"; // DATA mode + AFSK-A sub-mode
    } else if (modeType == "AM") {
        cmd = prefix + "5;";
    } else if (modeType == "FM") {
        cmd = prefix + "4;";
    } else if (modeType == "PSK") {
        cmd = prefix + "6;" + dtPrefix + "3;"; // DATA mode + PSK-D sub-mode
    } else if (modeType == "FSK") {
        cmd = prefix + "6;" + dtPrefix + "2;"; // DATA mode + FSK-D sub-mode
    }

    if (!cmd.isEmpty()) {
        emit modeSelected(cmd);
    }
    hidePopup();
}
