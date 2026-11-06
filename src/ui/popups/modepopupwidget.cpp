#include "ui/popups/modepopupwidget.h"
#include "ui/styling/k4styles.h"
#include <QGridLayout>
#include <QVBoxLayout>

namespace {
// Layout constants (primary dimensions from K4Styles::Dimensions)
const int ButtonWidth = K4Styles::Dimensions::PopupButtonWidth;
const int ButtonHeight = K4Styles::Dimensions::PopupButtonHeight;
const int ButtonSpacing = K4Styles::Dimensions::PopupButtonSpacing;
// WHY the column gap: one gutter on both axes reads as one grid of keys. At 2 px the rows nearly
// touched and read as two separate strips (most noticeable at 150 % interface scale).
const int RowSpacing = ButtonSpacing;

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
    gridLayout->addWidget(createModeButton(ModeButton::Cw, "CW"), 0, 0);
    gridLayout->addWidget(createModeButton(ModeButton::Ssb, "USB"), 0, 1); // Shows LSB or USB
    gridLayout->addWidget(createModeButton(ModeButton::Data, "DATA"), 0, 2);
    gridLayout->addWidget(createModeButton(ModeButton::Afsk, "AFSK"), 0, 3);

    // Row 2: AM, FM, PSK, FSK
    gridLayout->addWidget(createModeButton(ModeButton::Am, "AM"), 1, 0);
    gridLayout->addWidget(createModeButton(ModeButton::Fm, "FM"), 1, 1);
    gridLayout->addWidget(createModeButton(ModeButton::Psk, "PSK"), 1, 2);
    gridLayout->addWidget(createModeButton(ModeButton::Fsk, "FSK"), 1, 3);

    mainLayout->addLayout(gridLayout);

    updateButtonStyles();

    initPopup();
}

QPushButton *ModePopupWidget::createModeButton(ModeButton mode, const QString &text) {
    auto *btn = new QPushButton(text, this);
    btn->setFixedSize(ButtonWidth, ButtonHeight);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(K4Styles::popupButton());

    connect(btn, &QPushButton::clicked, this, [this, mode]() { onModeButtonClicked(mode); });

    m_buttons[static_cast<std::size_t>(mode)] = btn;
    return btn;
}

void ModePopupWidget::updateButtonStyles() {
    // Update SSB button text based on current mode or band default
    if (m_currentMode == MODE_LSB) {
        button(ModeButton::Ssb)->setText("LSB");
    } else if (m_currentMode == MODE_USB) {
        button(ModeButton::Ssb)->setText("USB");
    } else {
        // Not in SSB mode - show band-appropriate default
        button(ModeButton::Ssb)->setText(bandDefaultSideband() == MODE_LSB ? "LSB" : "USB");
    }

    // Update CW button text based on current mode
    button(ModeButton::Cw)->setText(m_currentMode == MODE_CW_R ? "CW-R" : "CW");

    // Highlight the current mode button
    QPushButton *selected = nullptr;
    switch (m_currentMode) {
    case MODE_CW:
    case MODE_CW_R:
        selected = button(ModeButton::Cw);
        break;
    case MODE_LSB:
    case MODE_USB:
        selected = button(ModeButton::Ssb);
        break;
    case MODE_AM:
        selected = button(ModeButton::Am);
        break;
    case MODE_FM:
        selected = button(ModeButton::Fm);
        break;
    case MODE_DATA:
    case MODE_DATA_R:
        // Highlight based on data sub-mode
        switch (m_currentDataSubMode) {
        case DT_DATA_A:
            selected = button(ModeButton::Data);
            break;
        case DT_AFSK_A:
            selected = button(ModeButton::Afsk);
            break;
        case DT_FSK_D:
            selected = button(ModeButton::Fsk);
            break;
        case DT_PSK_D:
            selected = button(ModeButton::Psk);
            break;
        }
        break;
    }
    for (QPushButton *btn : m_buttons)
        K4Styles::setSelected(btn, btn == selected);
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

void ModePopupWidget::onModeButtonClicked(ModeButton mode) {
    const QString prefix = m_bSetEnabled ? "MD$" : "MD";
    const QString dtPrefix = m_bSetEnabled ? "DT$" : "DT";
    QString cmd;

    switch (mode) {
    case ModeButton::Cw:
        // Already in CW: toggle CW <-> CW-R. Otherwise switch to CW.
        cmd = prefix + (m_currentMode == MODE_CW ? "7;" : "3;");
        break;
    case ModeButton::Ssb:
        // Already in SSB: toggle LSB <-> USB. Otherwise the band-appropriate sideband (LSB <10MHz, USB >=10MHz).
        if (m_currentMode == MODE_LSB) {
            cmd = prefix + "2;";
        } else if (m_currentMode == MODE_USB) {
            cmd = prefix + "1;";
        } else {
            cmd = prefix + QString::number(bandDefaultSideband()) + ";";
        }
        break;
    case ModeButton::Data:
        cmd = prefix + "6;" + dtPrefix + "0;"; // DATA mode + DATA-A sub-mode
        break;
    case ModeButton::Afsk:
        cmd = prefix + "6;" + dtPrefix + "1;"; // DATA mode + AFSK-A sub-mode
        break;
    case ModeButton::Am:
        cmd = prefix + "5;";
        break;
    case ModeButton::Fm:
        cmd = prefix + "4;";
        break;
    case ModeButton::Psk:
        cmd = prefix + "6;" + dtPrefix + "3;"; // DATA mode + PSK-D sub-mode
        break;
    case ModeButton::Fsk:
        cmd = prefix + "6;" + dtPrefix + "2;"; // DATA mode + FSK-D sub-mode
        break;
    case ModeButton::Count:
        break;
    }

    if (!cmd.isEmpty())
        emit modeSelected(cmd);
    hidePopup();
}
