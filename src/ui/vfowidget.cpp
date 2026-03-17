#include "vfowidget.h"
#include "k4styles.h"
#include "txmeterwidget.h"
#include "frequencydisplaywidget.h"
#include "../dsp/minipan_rhi.h"
#include <QMouseEvent>
#include <QKeyEvent>
VFOWidget::VFOWidget(VFOType type, QWidget *parent)
    : QWidget(parent), m_type(type),
      m_primaryColor(type == VFO_A ? K4Styles::Colors::VfoACyan : K4Styles::Colors::VfoBGreen) {
    setupUi();
}

void VFOWidget::setupUi() {
    setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::Background));

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(K4Styles::Dimensions::PopupButtonSpacing, 4,
                                   K4Styles::Dimensions::PopupButtonSpacing, 4);
    mainLayout->setSpacing(2);

    // Row 1: Frequency display with inline editing
    auto *freqRow = new QHBoxLayout();
    m_frequencyDisplay = new FrequencyDisplayWidget(this);
    m_frequencyDisplay->setEditModeColor(QColor(m_primaryColor)); // Cyan for A, Green for B
    m_frequencyDisplay->setFixedHeight(K4Styles::Dimensions::MenuItemHeight);
    m_frequencyDisplay->setTuningRateDigit(m_tuningRate); // Initialize tuning rate indicator

    // Forward frequency entry signal
    connect(m_frequencyDisplay, &FrequencyDisplayWidget::frequencyEntered, this, &VFOWidget::frequencyEntered);
    connect(m_frequencyDisplay, &FrequencyDisplayWidget::frequencyScrolled, this, &VFOWidget::frequencyScrolled);

    // VFO A: frequency on left, VFO B: frequency on right
    // Frequency container width matches stacked widget (270px) for vertical alignment
    auto *freqContainer = new QWidget(this);
    freqContainer->setFixedWidth(270);
    auto *freqContainerLayout = new QHBoxLayout(freqContainer);
    freqContainerLayout->setContentsMargins(0, 0, 0, 0);
    freqContainerLayout->setSpacing(0);
    freqContainerLayout->addWidget(m_frequencyDisplay);
    freqContainerLayout->addStretch();

    if (m_type == VFO_A) {
        freqRow->addWidget(freqContainer);
        freqRow->addStretch();
    } else {
        freqRow->addStretch();
        freqRow->addWidget(freqContainer);
    }
    mainLayout->addLayout(freqRow);
    mainLayout->addSpacing(K4Styles::Dimensions::PaddingSmall); // Room for tuning rate indicator below frequency

    // Stacked widget for normal content vs mini-pan
    // Use dynamic height - stacked widget resizes based on active page
    // Use Maximum horizontal policy so it doesn't expand beyond content width
    m_stackedWidget = new QStackedWidget(this);
    m_stackedWidget->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    m_stackedWidget->setMaximumWidth(270); // Fit all indicators with values (ATT-6, NTCH-A/M, APF-150)

    // Page 0: Normal content (multifunction meter + features)
    // Height must match MiniPanRhiWidget (150px) to prevent layout shift when toggling
    m_normalContent = new QWidget(m_stackedWidget);
    m_normalContent->setFixedSize(270, 150); // Fit all indicators with values (ATT-6, NTCH-A/M, APF-150)
    auto *normalLayout = new QVBoxLayout(m_normalContent);
    normalLayout->setContentsMargins(0, 0, 0, 0);
    normalLayout->setSpacing(2);

    // Row 2: Multifunction Meter (S/Po, ALC, COMP, SWR, Id) - replaces old S-meter
    // Meter fills full width of normal content (both are 200px)
    m_txMeter = new TxMeterWidget(m_normalContent);
    m_txMeter->setFixedWidth(260); // Match expanded indicator row
    normalLayout->addWidget(m_txMeter);

    // Row 3: AGC, PRE, ATT, NB, NR labels (aligned with meter)
    auto *featuresContainer = new QWidget(m_normalContent);
    auto *featuresRow = new QHBoxLayout(featuresContainer);
    featuresRow->setContentsMargins(0, 0, 0, 0);
    featuresRow->setSpacing(4); // Comfortable spacing for all indicators

    // Feature indicator label style
    const QString featureLabelStyle =
        QString("color: %1; font-size: %2px;").arg(K4Styles::Colors::TextGray).arg(K4Styles::Dimensions::FontSizeLarge);

    m_agcLabel = new QLabel("AGC-S", featuresContainer);
    m_agcLabel->setStyleSheet(featureLabelStyle);

    m_preampLabel = new QLabel("PRE", featuresContainer);
    m_preampLabel->setStyleSheet(featureLabelStyle);

    m_attLabel = new QLabel("ATT", featuresContainer);
    m_attLabel->setStyleSheet(featureLabelStyle);

    m_nbLabel = new QLabel("NB", featuresContainer);
    m_nbLabel->setStyleSheet(featureLabelStyle);

    m_nrLabel = new QLabel("NR", featuresContainer);
    m_nrLabel->setStyleSheet(featureLabelStyle);

    m_ntchLabel = new QLabel("NTCH", featuresContainer);
    m_ntchLabel->setStyleSheet(featureLabelStyle);

    m_apfLabel = new QLabel("APF", featuresContainer);
    m_apfLabel->setMinimumWidth(48); // Wide enough for "APF-150"
    m_apfLabel->setStyleSheet(featureLabelStyle);

    // Add labels to layout
    featuresRow->addWidget(m_agcLabel);
    featuresRow->addWidget(m_preampLabel);
    featuresRow->addWidget(m_attLabel);
    featuresRow->addWidget(m_nbLabel);
    featuresRow->addWidget(m_nrLabel);
    featuresRow->addWidget(m_ntchLabel);
    featuresRow->addWidget(m_apfLabel);

    // Features row is left-aligned within its container for both VFOs
    normalLayout->addWidget(featuresContainer, 0, Qt::AlignLeft);

    m_stackedWidget->addWidget(m_normalContent); // Index 0

    // Page 1: Placeholder for Mini-Pan widget
    // IMPORTANT: MiniPan is created lazily in showMiniPan() to avoid
    // breaking QRhiWidget initialization for other widgets.
    // Having a non-visible QRhiWidget in a QStackedWidget prevents
    // ALL QRhiWidgets in the window from initializing properly.
    m_miniPan = nullptr; // Will be created on first showMiniPan() call

    // Wrap stacked widget in HBox for edge alignment
    // VFO A: content on left, VFO B: content on right (mirrored layout)
    auto *stackedRow = new QHBoxLayout();
    stackedRow->setContentsMargins(0, 0, 0, 0);
    if (m_type == VFO_A) {
        stackedRow->addWidget(m_stackedWidget);
        stackedRow->addStretch();
    } else {
        stackedRow->addStretch();
        stackedRow->addWidget(m_stackedWidget);
    }
    mainLayout->addLayout(stackedRow);

    // NOTE: TX Meter (m_txMeter) is now created in normal content area above
    // It displays multifunction meters: S/Po, ALC, COMP, SWR, Id

    // Install event filter for click-to-toggle on normal content
    m_normalContent->installEventFilter(this);
}

bool VFOWidget::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_normalContent && event->type() == QEvent::MouseButtonPress) {
        emit normalContentClicked();
        return true;
    }

    // FrequencyDisplayWidget handles its own click/key events internally

    return QWidget::eventFilter(watched, event);
}

void VFOWidget::setFrequency(const QString &formatted) {
    m_frequencyDisplay->setFrequency(formatted);
}

void VFOWidget::setTuningRate(int rate) {
    if (rate >= 0 && rate <= 5 && rate != m_tuningRate) {
        m_tuningRate = rate;
        // VT rate maps to digit position from right:
        // Rate 0 = 1Hz (pos 0), Rate 1 = 10Hz (pos 1), Rate 2 = 100Hz (pos 2)
        // Rate 3 = 1kHz (pos 3), Rate 4 = 10kHz (pos 4)
        // Rate 5 = 100Hz (pos 2) - special case: KHZ button tunes at 100Hz
        int digitPosition = (rate == 5) ? 2 : rate;
        m_frequencyDisplay->setTuningRateDigit(digitPosition);
    }
}

void VFOWidget::setSMeterValue(double value) {
    if (m_txMeter)
        m_txMeter->setSMeter(value);
}

void VFOWidget::setAGC(const QString &mode) {
    m_agcLabel->setText(mode);
    // AGC is always shown, color indicates active state
    bool active = !mode.contains("-") || mode == "AGC-F" || mode == "AGC-S" || mode == "AGC-M";
    m_agcLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(active ? "#FFFFFF" : "#999999"));
}

void VFOWidget::setPreamp(bool on, int level) {
    // Show level when active (PRE-1, PRE-2, PRE-3), just "PRE" when off
    if (on && level > 0) {
        m_preampLabel->setText(QString("PRE-%1").arg(level));
    } else {
        m_preampLabel->setText("PRE");
    }
    m_preampLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(on ? "#FFFFFF" : "#999999"));
}

void VFOWidget::setAtt(bool on, int level) {
    // Show level when active (ATT-3, ATT-6, etc.), just "ATT" when off
    if (on && level > 0) {
        m_attLabel->setText(QString("ATT-%1").arg(level));
    } else {
        m_attLabel->setText("ATT");
    }
    m_attLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(on ? "#FFFFFF" : "#999999"));
}

void VFOWidget::setNB(bool on) {
    m_nbLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(on ? "#FFFFFF" : "#999999"));
}

void VFOWidget::setNR(bool on) {
    m_nrLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(on ? "#FFFFFF" : "#999999"));
}

void VFOWidget::setNotch(bool autoEnabled, bool manualEnabled) {
    QString text = "NTCH";
    bool active = autoEnabled || manualEnabled;

    if (autoEnabled && manualEnabled) {
        text = "NTCH-A/M";
    } else if (autoEnabled) {
        text = "NTCH-A";
    } else if (manualEnabled) {
        text = "NTCH-M";
    }

    m_ntchLabel->setText(text);
    m_ntchLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(active ? "#FFFFFF" : "#999999"));
}

void VFOWidget::setApf(bool enabled, int bandwidth) {
    QString text = "APF";
    if (enabled) {
        static const char *bwNames[] = {"30", "50", "150"};
        text = QString("APF-%1").arg(bwNames[qBound(0, bandwidth, 2)]);
    }
    m_apfLabel->setText(text);
    m_apfLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(enabled ? "#FFFFFF" : "#999999"));
}

void VFOWidget::updateMiniPan(const QByteArray &data) {
    // Only update if mini-pan exists and is visible
    if (m_miniPan && m_stackedWidget->currentIndex() == 1) {
        m_miniPan->updateSpectrum(data);
    }
}

void VFOWidget::showMiniPan() {
    // Lazily create MiniPan on first show
    // This avoids breaking QRhiWidget initialization - see note in setupUi()
    if (!m_miniPan) {
        m_miniPan = new MiniPanRhiWidget(m_stackedWidget);
        m_stackedWidget->addWidget(m_miniPan); // Index 1

        // Apply pending configuration
        if (m_pendingPassbandColor.isValid()) {
            m_miniPan->setPassbandColor(m_pendingPassbandColor);
        }
        if (!m_pendingMode.isEmpty()) {
            m_miniPan->setMode(m_pendingMode);
        }
        m_miniPan->setDataSubMode(m_pendingDataSubMode);
        m_miniPan->setFilterBandwidth(m_pendingFilterBw);
        m_miniPan->setIfShift(m_pendingIfShift);
        m_miniPan->setCwPitch(m_pendingCwPitch);
        m_miniPan->setNotchFilter(m_pendingNotchEnabled, m_pendingNotchPitchHz);
        if (m_pendingWaterfallHeight >= 0)
            m_miniPan->setWaterfallHeight(m_pendingWaterfallHeight);
        m_miniPan->setAveraging(m_pendingAveraging);

        // Connect mini-pan click to show normal view and emit signal
        connect(m_miniPan, &MiniPanRhiWidget::clicked, this, [this]() {
            showNormal();
            emit miniPanClicked();
        });
    }
    m_stackedWidget->setCurrentIndex(1);
}

// Mini-pan configuration methods - store pending or apply immediately
void VFOWidget::setMiniPanMode(const QString &mode) {
    m_pendingMode = mode;
    if (m_miniPan)
        m_miniPan->setMode(mode);
}

void VFOWidget::setMiniPanDataSubMode(int subMode) {
    m_pendingDataSubMode = subMode;
    if (m_miniPan)
        m_miniPan->setDataSubMode(subMode);
}

void VFOWidget::setMiniPanFilterBandwidth(int bw) {
    m_pendingFilterBw = bw;
    if (m_miniPan)
        m_miniPan->setFilterBandwidth(bw);
}

void VFOWidget::setMiniPanIfShift(int shift) {
    m_pendingIfShift = shift;
    if (m_miniPan)
        m_miniPan->setIfShift(shift);
}

void VFOWidget::setMiniPanCwPitch(int pitch) {
    m_pendingCwPitch = pitch;
    if (m_miniPan)
        m_miniPan->setCwPitch(pitch);
}

void VFOWidget::setMiniPanNotchFilter(bool enabled, int pitchHz) {
    m_pendingNotchEnabled = enabled;
    m_pendingNotchPitchHz = pitchHz;
    if (m_miniPan)
        m_miniPan->setNotchFilter(enabled, pitchHz);
}

void VFOWidget::setMiniPanPassbandColor(const QColor &color) {
    m_pendingPassbandColor = color;
    if (m_miniPan)
        m_miniPan->setPassbandColor(color);
}

void VFOWidget::setMiniPanWaterfallHeight(int percent) {
    m_pendingWaterfallHeight = percent;
    if (m_miniPan)
        m_miniPan->setWaterfallHeight(percent);
}

void VFOWidget::setMiniPanAveraging(int level) {
    m_pendingAveraging = level;
    if (m_miniPan)
        m_miniPan->setAveraging(level);
}

void VFOWidget::showNormal() {
    m_stackedWidget->setCurrentIndex(0);
}

bool VFOWidget::isMiniPanVisible() const {
    return m_stackedWidget->currentIndex() == 1;
}

// Multifunction meter methods (S/Po, ALC, COMP, SWR, Id)
void VFOWidget::setTransmitting(bool isTx) {
    if (m_txMeter)
        m_txMeter->setTransmitting(isTx);
}

void VFOWidget::setTxMeters(int alc, int compDb, double fwdPower, double swr) {
    if (m_txMeter)
        m_txMeter->setTxMeters(alc, compDb, fwdPower, swr);
}

void VFOWidget::setTxMeterCurrent(double amps) {
    if (m_txMeter)
        m_txMeter->setCurrent(amps);
}

void VFOWidget::setTxMeterQrp(bool isQrp) {
    if (m_txMeter)
        m_txMeter->setQrp(isQrp);
}

bool VFOWidget::isFrequencyEntryActive() const {
    return m_frequencyDisplay && m_frequencyDisplay->isEditing();
}
