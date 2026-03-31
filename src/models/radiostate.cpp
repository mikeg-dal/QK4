#include "radiostate.h"
#include <QDateTime>
#include <QDebug>
#include <QThread>
#include <algorithm>

RadioState::RadioState(QObject *parent) : QObject(parent) {
    registerCommandHandlers();
}

void RadioState::reset() {
    // Frequency and VFO
    m_frequency = 0;
    m_vfoA = 0;
    m_vfoB = 0;
    m_tuningStep = -1;
    m_tuningStepB = -1;

    // Mode and filter
    m_mode = Unknown;
    m_modeB = Unknown;
    m_filterBandwidth = -1;
    m_filterBandwidthB = -1;
    m_filterPosition = -1;
    m_filterPositionB = -1;
    m_ifShift = -1;
    m_ifShiftB = -1;
    m_cwPitch = -1;

    // Power and levels
    m_rfPower = -1.0;
    m_isQrpMode = false;
    m_micGain = -1;
    m_compression = -1;
    m_rfGain = -999;
    m_squelchLevel = -1;
    m_rfGainB = -999;
    m_squelchLevelB = -1;
    m_keyerSpeed = -1;
    m_iambicMode = QChar();
    m_paddleOrientation = QChar();
    m_keyingWeight = -1;

    // Meters
    m_sMeter = 0.0;
    m_sMeterB = 0.0;
    m_powerMeter = 0;
    m_swrMeter = 1.0;
    m_alcMeter = 0;
    m_compressionDb = 0;
    m_forwardPower = 0.0;

    // Power supply
    m_supplyVoltage = 0.0;
    m_supplyCurrent = 0.0;

    // Control states
    m_isTransmitting = false;
    m_subReceiverEnabled = false;
    m_diversityEnabled = false;
    m_splitEnabled = false;

    // Processing - Main RX
    m_noiseBlankerLevel = 0;
    m_noiseBlankerEnabled = false;
    m_noiseBlankerFilterWidth = 0;
    m_noiseReductionLevel = 0;
    m_noiseReductionEnabled = false;

    // Notch filter - Main RX
    m_autoNotchEnabled = false;
    m_manualNotchEnabled = false;
    m_manualNotchPitch = 1000;

    // Notch filter - Sub RX
    m_autoNotchEnabledB = false;
    m_manualNotchEnabledB = false;
    m_manualNotchPitchB = 1000;

    m_preamp = 0;
    m_preampEnabled = false;
    m_attenuatorLevel = 0;
    m_attenuatorEnabled = false;
    m_agcSpeed = AGC_Slow;

    // Processing - Sub RX
    m_noiseBlankerLevelB = 0;
    m_noiseBlankerEnabledB = false;
    m_noiseBlankerFilterWidthB = 0;
    m_noiseReductionLevelB = 0;
    m_noiseReductionEnabledB = false;
    m_preampB = 0;
    m_preampEnabledB = false;
    m_attenuatorLevelB = 0;
    m_attenuatorEnabledB = false;
    m_agcSpeedB = AGC_Slow;

    // Antenna
    m_selectedAntenna = -1;
    m_receiveAntenna = -1;
    m_receiveAntennaSub = -1;
    m_atuMode = -1;
    m_antennaNames.clear();

    // RIT/XIT
    m_ritEnabled = false;
    m_xitEnabled = false;
    m_ritXitOffset = 0;
    m_ritEnabledB = false;
    m_ritXitOffsetB = 0;

    // Message bank
    m_messageBank = -1;

    // VOX
    m_voxCW = false;
    m_voxVoice = false;
    m_voxData = false;

    // QSK / TEST / B SET
    m_qskEnabled = false;
    m_testMode = false;
    m_bSetEnabled = false;

    // QSK/VOX Delay
    m_qskDelayCW = -1;
    m_qskDelayVoice = -1;
    m_qskDelayData = -1;

    // Audio effects
    m_afxMode = 0;
    m_apfEnabled = false;
    m_apfBandwidth = 0;
    m_apfEnabledB = false;
    m_apfBandwidthB = 0;

    // VFO Link/Lock
    m_vfoLink = false;
    m_lockA = false;
    m_lockB = false;

    // Audio mix/balance
    m_audioMixLeft = -1;
    m_audioMixRight = -1;
    m_balanceMode = -1;
    m_balanceOffset = -99;

    // Monitor Level
    m_monitorLevelCW = -1;
    m_monitorLevelData = -1;
    m_monitorLevelVoice = -1;

    // Panadapter
    m_refLevel = -110;
    m_scale = -1;
    m_spanHz = 0;
    m_refLevelB = -110;
    m_spanHzB = 0;

    // Radio info
    m_radioID.clear();
    m_radioModel.clear();
    m_optionModules.clear();
    m_firmwareVersions.clear();

    // Mini-Pan
    m_miniPanAEnabled = false;
    m_miniPanBEnabled = false;

    // Display state
    m_dualPanModeLcd = -1;
    m_dualPanModeExt = -1;
    m_displayModeLcd = -1;
    m_displayModeExt = -1;
    m_displayFps = 30;
    m_waterfallColor = -1;
    m_waterfallHeight = 50;
    m_waterfallHeightExt = 50;
    m_averaging = -1;
    m_peakMode = -1;
    m_fixedTune = -1;
    m_fixedTuneMode = -1;
    m_freeze = -1;
    m_vfoACursor = -1;
    m_vfoBCursor = -1;
    m_autoRefLevel = -1;
    m_ddcNbMode = -1;
    m_ddcNbLevel = -1;

    // Data sub-mode
    m_dataSubMode = -1;
    m_dataSubModeB = -1;
    m_dataSubModeOptimisticTime = 0;
    m_dataSubModeBOptimisticTime = 0;

    // Data rate
    m_dataRate = -1;
    m_dataRateB = -1;
    m_dataRateOptimisticTime = 0;
    m_dataRateBOptimisticTime = 0;

    // EQ bands
    std::fill(std::begin(m_rxEqBands), std::end(m_rxEqBands), 0);
    std::fill(std::begin(m_txEqBands), std::end(m_txEqBands), 0);

    // Antenna config masks
    m_mainRxDisplayAll = true;
    std::fill(std::begin(m_mainRxAntMask), std::end(m_mainRxAntMask), false);
    m_subRxDisplayAll = true;
    std::fill(std::begin(m_subRxAntMask), std::end(m_subRxAntMask), false);
    m_txDisplayAll = true;
    std::fill(std::begin(m_txAntMask), std::end(m_txAntMask), false);

    // Line Out
    m_lineOutLeft = -1;
    m_lineOutRight = -1;
    m_lineOutRightEqualsLeft = false;

    // Line In
    m_lineInSoundCard = -1;
    m_lineInJack = -1;
    m_lineInSource = -1;

    // Mic Input/Setup
    m_micInput = -1;
    m_micFrontPreamp = -1;
    m_micFrontBias = -1;
    m_micFrontButtons = -1;
    m_micRearPreamp = -1;
    m_micRearBias = -1;

    // VOX Gain / Anti-VOX
    m_voxGainVoice = -1;
    m_voxGainData = -1;
    m_antiVox = -1;

    // ESSB
    m_essbEnabled = false;
    m_ssbTxBw = -1;

    // Text Decode
    m_textDecodeMode = -1;
    m_textDecodeThreshold = -1;
    m_textDecodeLines = -1;
    m_textDecodeModeB = -1;
    m_textDecodeThresholdB = -1;
    m_textDecodeLinesB = -1;
}

void RadioState::parseCATCommand(const QString &command) {
    // RadioState is not thread-safe — all callers must be on the main (GUI) thread.
    // If cross-thread parsing is ever needed, use QMetaObject::invokeMethod with Qt::QueuedConnection.
    Q_ASSERT(QThread::currentThread() == thread());

    QString cmd = command.trimmed();
    if (cmd.isEmpty())
        return;

    // Remove trailing semicolon for parsing
    if (cmd.endsWith(';')) {
        cmd.chop(1);
    }

    // Dispatch through handler registry (sorted by prefix length, longest first)
    for (const auto &entry : m_commandHandlers) {
        if (cmd.startsWith(entry.prefix)) {
            entry.handler(cmd);
            return;
        }
    }

    // Unknown command - no handler matched
    // qDebug() << "Unhandled CAT command:" << cmd;
}

// Legacy parseCATCommand content removed - now using handler registry above
// Old implementation was ~1500 lines of if-else chain

RadioState::Mode RadioState::modeFromCode(int code) {
    switch (code) {
    case 1:
        return LSB;
    case 2:
        return USB;
    case 3:
        return CW;
    case 4:
        return FM;
    case 5:
        return AM;
    case 6:
        return DATA;
    case 7:
        return CW_R;
    case 9:
        return DATA_R;
    default:
        return USB;
    }
}

QString RadioState::modeToString(Mode mode) {
    switch (mode) {
    case LSB:
        return "LSB";
    case USB:
        return "USB";
    case CW:
        return "CW";
    case FM:
        return "FM";
    case AM:
        return "AM";
    case DATA:
        return "DATA";
    case CW_R:
        return "CW-R";
    case DATA_R:
        return "DATA-R";
    case Unknown:
    default:
        return "";
    }
}

QString RadioState::modeString() const {
    return modeToString(m_mode);
}

QString RadioState::dataSubModeToString(int subMode) {
    switch (subMode) {
    case 0:
        return "DATA"; // DATA-A
    case 1:
        return "AFSK"; // AFSK-A
    case 2:
        return "FSK"; // FSK-D
    case 3:
        return "PSK"; // PSK-D
    default:
        return "DATA";
    }
}

QString RadioState::modeStringFull() const {
    // For DATA/DATA-R modes, show the sub-mode instead
    if (m_mode == DATA || m_mode == DATA_R) {
        return dataSubModeToString(m_dataSubMode);
    }
    return modeToString(m_mode);
}

QString RadioState::modeStringFullB() const {
    // For DATA/DATA-R modes, show the sub-mode instead
    if (m_modeB == DATA || m_modeB == DATA_R) {
        return dataSubModeToString(m_dataSubModeB);
    }
    return modeToString(m_modeB);
}

// Optimistic setters for scroll wheel updates (radio doesn't echo these commands)
void RadioState::setKeyerSpeed(int wpm) {
    if (m_keyerSpeed != wpm) {
        m_keyerSpeed = wpm;
        emit keyerSpeedChanged(m_keyerSpeed);
    }
}

void RadioState::setCwPitch(int pitchHz) {
    if (m_cwPitch != pitchHz) {
        m_cwPitch = pitchHz;
        emit cwPitchChanged(m_cwPitch);
    }
}

void RadioState::setRfPower(double watts) {
    bool qrp = (watts <= 10.0);
    bool changed = false;
    if (m_rfPower != watts) {
        m_rfPower = watts;
        changed = true;
    }
    if (qrp != m_isQrpMode) {
        m_isQrpMode = qrp;
        changed = true;
    }
    if (changed)
        emit rfPowerChanged(m_rfPower, m_isQrpMode);
}

void RadioState::setFilterBandwidth(int bwHz) {
    if (m_filterBandwidth != bwHz) {
        m_filterBandwidth = bwHz;
        emit filterBandwidthChanged(m_filterBandwidth);
    }
}

void RadioState::setIfShift(int shift) {
    if (m_ifShift != shift) {
        m_ifShift = shift;
        emit ifShiftChanged(m_ifShift);
    }
}

void RadioState::setFilterBandwidthB(int bwHz) {
    if (m_filterBandwidthB != bwHz) {
        m_filterBandwidthB = bwHz;
        emit filterBandwidthBChanged(m_filterBandwidthB);
    }
}

void RadioState::setIfShiftB(int shift) {
    if (m_ifShiftB != shift) {
        m_ifShiftB = shift;
        emit ifShiftBChanged(m_ifShiftB);
    }
}

void RadioState::setRfGain(int gain) {
    if (m_rfGain != gain) {
        m_rfGain = gain;
        emit rfGainChanged(m_rfGain);
    }
}

void RadioState::setSquelchLevel(int level) {
    if (m_squelchLevel != level) {
        m_squelchLevel = level;
        emit squelchChanged(m_squelchLevel);
    }
}

void RadioState::setRfGainB(int gain) {
    if (m_rfGainB != gain) {
        m_rfGainB = gain;
        emit rfGainBChanged(m_rfGainB);
    }
}

void RadioState::setSquelchLevelB(int level) {
    if (m_squelchLevelB != level) {
        m_squelchLevelB = level;
        emit squelchBChanged(m_squelchLevelB);
    }
}

void RadioState::setMicGain(int gain) {
    if (m_micGain != gain) {
        m_micGain = gain;
        emit micGainChanged(m_micGain);
    }
}

void RadioState::setCompression(int level) {
    if (m_compression != level) {
        m_compression = level;
        emit compressionChanged(m_compression);
    }
}

void RadioState::setBalance(int mode, int offset) {
    mode = qBound(0, mode, 1);
    offset = qBound(-50, offset, 50);
    if (m_balanceMode != mode || m_balanceOffset != offset) {
        m_balanceMode = mode;
        m_balanceOffset = offset;
        emit balanceChanged(mode, offset);
    }
}

void RadioState::setMonitorLevel(int mode, int level) {
    level = qBound(0, level, 100);
    int *target = nullptr;
    switch (mode) {
    case 0:
        target = &m_monitorLevelCW;
        break;
    case 1:
        target = &m_monitorLevelData;
        break;
    case 2:
        target = &m_monitorLevelVoice;
        break;
    default:
        return;
    }
    if (*target != level) {
        *target = level;
        emit monitorLevelChanged(mode, level);
    }
}

void RadioState::setNoiseBlankerLevel(int level) {
    if (m_noiseBlankerLevel != level) {
        m_noiseBlankerLevel = qMin(level, 15);
        emit processingChanged();
    }
}

void RadioState::setNoiseBlankerLevelB(int level) {
    if (m_noiseBlankerLevelB != level) {
        m_noiseBlankerLevelB = qMin(level, 15);
        emit processingChangedB();
    }
}

void RadioState::setNoiseBlankerFilter(int filter) {
    filter = qBound(0, filter, 2);
    if (m_noiseBlankerFilterWidth != filter) {
        m_noiseBlankerFilterWidth = filter;
        emit processingChanged();
    }
}

void RadioState::setNoiseBlankerFilterB(int filter) {
    filter = qBound(0, filter, 2);
    if (m_noiseBlankerFilterWidthB != filter) {
        m_noiseBlankerFilterWidthB = filter;
        emit processingChangedB();
    }
}

void RadioState::setNoiseReductionLevel(int level) {
    if (m_noiseReductionLevel != level) {
        m_noiseReductionLevel = qMin(level, 10);
        emit processingChanged();
    }
}

void RadioState::setNoiseReductionLevelB(int level) {
    if (m_noiseReductionLevelB != level) {
        m_noiseReductionLevelB = qMin(level, 10);
        emit processingChangedB();
    }
}

void RadioState::setManualNotchPitch(int pitch) {
    pitch = qBound(150, pitch, 5000);
    if (m_manualNotchPitch != pitch) {
        m_manualNotchPitch = pitch;
        emit notchChanged();
    }
}

void RadioState::setManualNotchPitchB(int pitch) {
    pitch = qBound(150, pitch, 5000);
    if (m_manualNotchPitchB != pitch) {
        m_manualNotchPitchB = pitch;
        emit notchBChanged();
    }
}

void RadioState::setDataSubMode(int subMode) {
    subMode = qBound(0, subMode, 3);
    if (m_dataSubMode != subMode) {
        m_dataSubMode = subMode;
        m_dataSubModeOptimisticTime = QDateTime::currentMSecsSinceEpoch();
        emit dataSubModeChanged(subMode);
    }
}

void RadioState::setDataSubModeB(int subMode) {
    subMode = qBound(0, subMode, 3);
    if (m_dataSubModeB != subMode) {
        m_dataSubModeB = subMode;
        m_dataSubModeBOptimisticTime = QDateTime::currentMSecsSinceEpoch();
        emit dataSubModeBChanged(subMode);
    }
}

void RadioState::setDataRate(int rate) {
    rate = qBound(0, rate, 1);
    if (m_dataRate != rate) {
        m_dataRate = rate;
        m_dataRateOptimisticTime = QDateTime::currentMSecsSinceEpoch();
        emit dataRateChanged(rate);
    }
}

void RadioState::setDataRateB(int rate) {
    rate = qBound(0, rate, 1);
    if (m_dataRateB != rate) {
        m_dataRateB = rate;
        m_dataRateBOptimisticTime = QDateTime::currentMSecsSinceEpoch();
        emit dataRateBChanged(rate);
    }
}

void RadioState::setMiniPanAEnabled(bool enabled) {
    if (m_miniPanAEnabled != enabled) {
        m_miniPanAEnabled = enabled;
        emit miniPanAEnabledChanged(enabled);
    }
}

void RadioState::setMiniPanBEnabled(bool enabled) {
    if (m_miniPanBEnabled != enabled) {
        m_miniPanBEnabled = enabled;
        emit miniPanBEnabledChanged(enabled);
    }
}

void RadioState::setWaterfallHeight(int percent) {
    percent = qBound(10, percent, 90);
    if (m_waterfallHeight != percent) {
        m_waterfallHeight = percent;
        emit waterfallHeightChanged(percent);
    }
}

void RadioState::setWaterfallHeightExt(int percent) {
    percent = qBound(10, percent, 90);
    if (m_waterfallHeightExt != percent) {
        m_waterfallHeightExt = percent;
        emit waterfallHeightExtChanged(percent);
    }
}

void RadioState::setAveraging(int value) {
    value = qBound(1, value, 20);
    if (m_averaging != value) {
        m_averaging = value;
        emit averagingChanged(value);
    }
}

void RadioState::setRxEqBand(int index, int dB) {
    if (index < 0 || index >= 8)
        return;
    dB = qBound(-16, dB, 16);
    if (m_rxEqBands[index] != dB) {
        m_rxEqBands[index] = dB;
        emit rxEqBandChanged(index, dB);
        emit rxEqChanged();
    }
}

void RadioState::setRxEqBands(const QVector<int> &bands) {
    bool changed = false;
    for (int i = 0; i < qMin(bands.size(), 8); i++) {
        int dB = qBound(-16, bands[i], 16);
        if (m_rxEqBands[i] != dB) {
            m_rxEqBands[i] = dB;
            changed = true;
        }
    }
    if (changed) {
        emit rxEqChanged();
    }
}

void RadioState::setTxEqBand(int index, int dB) {
    if (index < 0 || index >= 8)
        return;
    dB = qBound(-16, dB, 16);
    if (m_txEqBands[index] != dB) {
        m_txEqBands[index] = dB;
        emit txEqBandChanged(index, dB);
        emit txEqChanged();
    }
}

void RadioState::setTxEqBands(const QVector<int> &bands) {
    bool changed = false;
    for (int i = 0; i < qMin(bands.size(), 8); i++) {
        int dB = qBound(-16, bands[i], 16);
        if (m_txEqBands[i] != dB) {
            m_txEqBands[i] = dB;
            changed = true;
        }
    }
    if (changed) {
        emit txEqChanged();
    }
}

void RadioState::setMainRxAntConfig(bool displayAll, const QVector<bool> &mask) {
    bool changed = false;
    if (displayAll != m_mainRxDisplayAll) {
        m_mainRxDisplayAll = displayAll;
        changed = true;
    }
    for (int i = 0; i < qMin(mask.size(), 7); i++) {
        if (mask[i] != m_mainRxAntMask[i]) {
            m_mainRxAntMask[i] = mask[i];
            changed = true;
        }
    }
    if (changed) {
        emit mainRxAntCfgChanged();
    }
}

void RadioState::setSubRxAntConfig(bool displayAll, const QVector<bool> &mask) {
    bool changed = false;
    if (displayAll != m_subRxDisplayAll) {
        m_subRxDisplayAll = displayAll;
        changed = true;
    }
    for (int i = 0; i < qMin(mask.size(), 7); i++) {
        if (mask[i] != m_subRxAntMask[i]) {
            m_subRxAntMask[i] = mask[i];
            changed = true;
        }
    }
    if (changed) {
        emit subRxAntCfgChanged();
    }
}

void RadioState::setTxAntConfig(bool displayAll, const QVector<bool> &mask) {
    bool changed = false;
    if (displayAll != m_txDisplayAll) {
        m_txDisplayAll = displayAll;
        changed = true;
    }
    for (int i = 0; i < qMin(mask.size(), 3); i++) {
        if (mask[i] != m_txAntMask[i]) {
            m_txAntMask[i] = mask[i];
            changed = true;
        }
    }
    if (changed) {
        emit txAntCfgChanged();
    }
}

void RadioState::setLineOutLeft(int level) {
    level = qBound(0, level, 40);
    if (level != m_lineOutLeft) {
        m_lineOutLeft = level;
        emit lineOutChanged();
    }
}

void RadioState::setLineOutRight(int level) {
    level = qBound(0, level, 40);
    if (level != m_lineOutRight) {
        m_lineOutRight = level;
        emit lineOutChanged();
    }
}

void RadioState::setLineOutRightEqualsLeft(bool enabled) {
    if (enabled != m_lineOutRightEqualsLeft) {
        m_lineOutRightEqualsLeft = enabled;
        emit lineOutChanged();
    }
}

// Line In optimistic setters
void RadioState::setLineInSoundCard(int level) {
    level = qBound(0, level, 250);
    if (level != m_lineInSoundCard) {
        m_lineInSoundCard = level;
        emit lineInChanged();
    }
}

void RadioState::setLineInJack(int level) {
    level = qBound(0, level, 250);
    if (level != m_lineInJack) {
        m_lineInJack = level;
        emit lineInChanged();
    }
}

void RadioState::setLineInSource(int source) {
    if ((source == 0 || source == 1) && source != m_lineInSource) {
        m_lineInSource = source;
        emit lineInChanged();
    }
}

// Mic Input/Setup optimistic setters
void RadioState::setMicInput(int input) {
    if (input >= 0 && input <= 4 && input != m_micInput) {
        m_micInput = input;
        emit micInputChanged(m_micInput);
    }
}

void RadioState::setMicFrontPreamp(int preamp) {
    if (preamp >= 0 && preamp <= 2 && preamp != m_micFrontPreamp) {
        m_micFrontPreamp = preamp;
        emit micSetupChanged();
    }
}

void RadioState::setMicFrontBias(int bias) {
    if ((bias == 0 || bias == 1) && bias != m_micFrontBias) {
        m_micFrontBias = bias;
        emit micSetupChanged();
    }
}

void RadioState::setMicFrontButtons(int buttons) {
    if ((buttons == 0 || buttons == 1) && buttons != m_micFrontButtons) {
        m_micFrontButtons = buttons;
        emit micSetupChanged();
    }
}

void RadioState::setMicRearPreamp(int preamp) {
    if ((preamp == 0 || preamp == 1) && preamp != m_micRearPreamp) {
        m_micRearPreamp = preamp;
        emit micSetupChanged();
    }
}

void RadioState::setMicRearBias(int bias) {
    if ((bias == 0 || bias == 1) && bias != m_micRearBias) {
        m_micRearBias = bias;
        emit micSetupChanged();
    }
}

// Text Decode optimistic setters - Main RX
void RadioState::setTextDecodeMode(int mode) {
    mode = qBound(0, mode, 4);
    if (mode != m_textDecodeMode) {
        m_textDecodeMode = mode;
        emit textDecodeChanged();
    }
}

void RadioState::setTextDecodeThreshold(int threshold) {
    threshold = qBound(0, threshold, 9);
    if (threshold != m_textDecodeThreshold) {
        m_textDecodeThreshold = threshold;
        emit textDecodeChanged();
    }
}

void RadioState::setTextDecodeLines(int lines) {
    lines = qBound(1, lines, 10);
    if (lines != m_textDecodeLines) {
        m_textDecodeLines = lines;
        emit textDecodeChanged();
    }
}

// Text Decode optimistic setters - Sub RX
void RadioState::setTextDecodeModeB(int mode) {
    mode = qBound(0, mode, 4);
    if (mode != m_textDecodeModeB) {
        m_textDecodeModeB = mode;
        emit textDecodeBChanged();
    }
}

void RadioState::setTextDecodeThresholdB(int threshold) {
    threshold = qBound(0, threshold, 9);
    if (threshold != m_textDecodeThresholdB) {
        m_textDecodeThresholdB = threshold;
        emit textDecodeBChanged();
    }
}

void RadioState::setTextDecodeLinesB(int lines) {
    lines = qBound(1, lines, 10);
    if (lines != m_textDecodeLinesB) {
        m_textDecodeLinesB = lines;
        emit textDecodeBChanged();
    }
}

// VOX Gain optimistic setters
void RadioState::setVoxGainVoice(int gain) {
    gain = qBound(0, gain, 60);
    if (gain != m_voxGainVoice) {
        m_voxGainVoice = gain;
        emit voxGainChanged(0, gain);
    }
}

void RadioState::setVoxGainData(int gain) {
    gain = qBound(0, gain, 60);
    if (gain != m_voxGainData) {
        m_voxGainData = gain;
        emit voxGainChanged(1, gain);
    }
}

void RadioState::setAntiVox(int level) {
    level = qBound(0, level, 60);
    if (level != m_antiVox) {
        m_antiVox = level;
        emit antiVoxChanged(level);
    }
}

void RadioState::setEssbEnabled(bool enabled) {
    if (enabled != m_essbEnabled) {
        m_essbEnabled = enabled;
        emit essbChanged(m_essbEnabled, m_ssbTxBw);
    }
}

void RadioState::setSsbTxBw(int bw) {
    bw = qBound(30, bw, 45);
    if (bw != m_ssbTxBw) {
        m_ssbTxBw = bw;
        emit essbChanged(m_essbEnabled, m_ssbTxBw);
    }
}

// =============================================================================
// A/B Deduplication Helpers
// =============================================================================

void RadioState::handleIntPair(const QString &cmd, int prefixLen, int &member, int min, int max,
                               void (RadioState::*signal)(int)) {
    if (cmd.length() <= prefixLen)
        return;
    bool ok;
    int val = cmd.mid(prefixLen).toInt(&ok);
    if (ok && val >= min && val <= max && val != member) {
        member = val;
        emit(this->*signal)(val);
    }
}

void RadioState::handleBoolPair(const QString &cmd, int charPos, bool &member, void (RadioState::*signal)()) {
    if (cmd.length() <= charPos)
        return;
    bool enabled = (cmd.at(charPos) == '1');
    if (member != enabled) {
        member = enabled;
        emit(this->*signal)();
    }
}

void RadioState::handleBoolPairVal(const QString &cmd, int charPos, bool &member, void (RadioState::*signal)(bool)) {
    if (cmd.length() <= charPos)
        return;
    bool enabled = (cmd.at(charPos) == '1');
    if (member != enabled) {
        member = enabled;
        emit(this->*signal)(enabled);
    }
}

// =============================================================================
// Command Handler Registry
// =============================================================================

void RadioState::registerCommandHandlers() {
    // Register handlers in order - will be sorted by prefix length (longest first)
    // This ensures "MD$" is checked before "MD", "NB$" before "NB", etc.

    // Display commands (# prefix) - register longest first
    m_commandHandlers.append({"#HWFH", [this](const QString &c) { handleDisplayHWFH(c); }});
    m_commandHandlers.append({"#HDPM", [this](const QString &c) { handleDisplayHDPM(c); }});
    m_commandHandlers.append({"#HDSM", [this](const QString &c) { handleDisplayHDSM(c); }});
    m_commandHandlers.append({"#NBL$", [this](const QString &c) { handleDisplayNBL(c); }});
    // #REF$/#REF — deduplicated via handleIntPair (prefixLen 5 vs 4)
    m_commandHandlers.append({"#REF$", [this](const QString &c) {
                                  handleIntPair(c, 5, m_refLevelB, -200, 50, &RadioState::refLevelBChanged);
                              }});
    // #SPN$/#SPN — deduplicated via handleIntPair (prefixLen 5 vs 4, min 1)
    m_commandHandlers.append(
        {"#SPN$", [this](const QString &c) { handleIntPair(c, 5, m_spanHzB, 1, 999999, &RadioState::spanBChanged); }});
    m_commandHandlers.append({"#NB$", [this](const QString &c) { handleDisplayNB(c); }});
    // #MP$/#MP — deduplicated via handleBoolPairVal (charPos 4 vs 3)
    m_commandHandlers.append({"#MP$", [this](const QString &c) {
                                  handleBoolPairVal(c, 4, m_miniPanBEnabled, &RadioState::miniPanBEnabledChanged);
                              }});
    m_commandHandlers.append({"#REF", [this](const QString &c) {
                                  handleIntPair(c, 4, m_refLevel, -200, 50, &RadioState::refLevelChanged);
                              }});
    m_commandHandlers.append(
        {"#SPN", [this](const QString &c) { handleIntPair(c, 4, m_spanHz, 1, 999999, &RadioState::spanChanged); }});
    m_commandHandlers.append({"#SCL", [this](const QString &c) { handleDisplaySCL(c); }});
    m_commandHandlers.append({"#DPM", [this](const QString &c) { handleDisplayDPM(c); }});
    m_commandHandlers.append({"#DSM", [this](const QString &c) { handleDisplayDSM(c); }});
    m_commandHandlers.append({"#FPS", [this](const QString &c) { handleDisplayFPS(c); }});
    m_commandHandlers.append({"#WFC", [this](const QString &c) { handleDisplayWFC(c); }});
    m_commandHandlers.append({"#WFH", [this](const QString &c) { handleDisplayWFH(c); }});
    m_commandHandlers.append({"#AVG", [this](const QString &c) { handleDisplayAVG(c); }});
    m_commandHandlers.append({"#PKM", [this](const QString &c) { handleDisplayPKM(c); }});
    m_commandHandlers.append({"#FXT", [this](const QString &c) { handleDisplayFXT(c); }});
    m_commandHandlers.append({"#FXA", [this](const QString &c) { handleDisplayFXA(c); }});
    m_commandHandlers.append({"#FRZ", [this](const QString &c) { handleDisplayFRZ(c); }});
    m_commandHandlers.append({"#VFA", [this](const QString &c) { handleDisplayVFA(c); }});
    m_commandHandlers.append({"#VFB", [this](const QString &c) { handleDisplayVFB(c); }});
    m_commandHandlers.append({"#MP", [this](const QString &c) {
                                  handleBoolPairVal(c, 3, m_miniPanAEnabled, &RadioState::miniPanAEnabledChanged);
                              }});
    m_commandHandlers.append({"#AR", [this](const QString &c) { handleDisplayAR(c); }});

    // Multi-char commands with $ suffix (must come before their base commands)
    m_commandHandlers.append({"SIFP", [this](const QString &c) { handleSIFP(c); }});
    m_commandHandlers.append({"SIRC", [this](const QString &c) { handleSIRC(c); }});
    m_commandHandlers.append({"TD$", [this](const QString &c) { handleTDSub(c); }});
    m_commandHandlers.append({"TB$", [this](const QString &c) { handleTBSub(c); }});
    m_commandHandlers.append({"DR$", [this](const QString &c) { handleDRSub(c); }});
    m_commandHandlers.append({"DT$", [this](const QString &c) { handleDTSub(c); }});
    m_commandHandlers.append({"MD$", [this](const QString &c) { handleMDSub(c); }});
    // BW$, IS$, FP$ — deduplicated via handleIntPair (prefixLen 3)
    m_commandHandlers.append({"BW$", [this](const QString &c) {
                                  if (c.length() <= 3)
                                      return;
                                  bool ok;
                                  int bw = c.mid(3).toInt(&ok);
                                  if (ok) {
                                      int newBw = bw * 10;
                                      if (m_filterBandwidthB != newBw) {
                                          m_filterBandwidthB = newBw;
                                          emit filterBandwidthBChanged(m_filterBandwidthB);
                                      }
                                  }
                              }});
    m_commandHandlers.append({"IS$", [this](const QString &c) {
                                  handleIntPair(c, 3, m_ifShiftB, -99999, 99999, &RadioState::ifShiftBChanged);
                              }});
    m_commandHandlers.append({"FP$", [this](const QString &c) {
                                  handleIntPair(c, 3, m_filterPositionB, 1, 3, &RadioState::filterPositionBChanged);
                              }});
    // RG$ — strips leading dash before parsing
    m_commandHandlers.append({"RG$", [this](const QString &c) {
                                  if (c.length() <= 3)
                                      return;
                                  QString val = c.mid(3);
                                  if (val.startsWith('-'))
                                      val = val.mid(1);
                                  bool ok;
                                  int rg = val.toInt(&ok);
                                  if (ok && m_rfGainB != rg) {
                                      m_rfGainB = rg;
                                      emit rfGainBChanged(m_rfGainB);
                                  }
                              }});
    m_commandHandlers.append({"SQ$", [this](const QString &c) {
                                  handleIntPair(c, 3, m_squelchLevelB, -99999, 99999, &RadioState::squelchBChanged);
                              }});
    m_commandHandlers.append({"SM$", [this](const QString &c) { handleSMSub(c); }});
    m_commandHandlers.append({"NB$", [this](const QString &c) { handleNBSub(c); }});
    m_commandHandlers.append({"NR$", [this](const QString &c) { handleNRSub(c); }});
    m_commandHandlers.append({"PA$", [this](const QString &c) { handlePASub(c); }});
    m_commandHandlers.append({"RA$", [this](const QString &c) { handleRASub(c); }});
    m_commandHandlers.append({"GT$", [this](const QString &c) { handleGTSub(c); }});
    // NA$ — deduplicated via handleBoolPair
    m_commandHandlers.append(
        {"NA$", [this](const QString &c) { handleBoolPair(c, 3, m_autoNotchEnabledB, &RadioState::notchBChanged); }});
    m_commandHandlers.append({"NM$", [this](const QString &c) { handleNMSub(c); }});
    m_commandHandlers.append({"AP$", [this](const QString &c) { handleAPSub(c); }});
    // LK$ — deduplicated via handleBoolPairVal
    m_commandHandlers.append(
        {"LK$", [this](const QString &c) { handleBoolPairVal(c, 3, m_lockB, &RadioState::lockBChanged); }});
    // VT$ — deduplicated inline (takes .left(1), qBound)
    m_commandHandlers.append({"VT$", [this](const QString &c) {
                                  if (c.length() <= 3)
                                      return;
                                  bool ok;
                                  int step = c.mid(3).left(1).toInt(&ok);
                                  if (ok) {
                                      int ns = qBound(0, step, 5);
                                      if (ns != m_tuningStepB) {
                                          m_tuningStepB = ns;
                                          emit tuningStepBChanged(m_tuningStepB);
                                      }
                                  }
                              }});
    // AR$ — deduplicated inline (3-arg signal)
    m_commandHandlers.append({"AR$", [this](const QString &c) {
                                  if (c.length() <= 3)
                                      return;
                                  bool ok;
                                  int ar = c.mid(3).toInt(&ok);
                                  if (ok && ar >= 0 && ar <= 7 && ar != m_receiveAntennaSub) {
                                      m_receiveAntennaSub = ar;
                                      emit antennaChanged(m_selectedAntenna, m_receiveAntenna, m_receiveAntennaSub);
                                  }
                              }});
    // RO$ — inline (custom multi-arg signal)
    m_commandHandlers.append({"RO$", [this](const QString &c) {
                                  if (c.length() < 4)
                                      return;
                                  bool ok;
                                  int offset = c.mid(3).toInt(&ok);
                                  if (ok && offset != m_ritXitOffsetB) {
                                      m_ritXitOffsetB = offset;
                                      emit ritXitBChanged(m_ritEnabledB, m_ritXitOffsetB);
                                  }
                              }});
    // RT$ — inline (custom multi-arg signal)
    m_commandHandlers.append({"RT$", [this](const QString &c) {
                                  if (c.length() < 4 || (c.at(3) != '0' && c.at(3) != '1'))
                                      return;
                                  bool enabled = (c.at(3) == '1');
                                  if (enabled != m_ritEnabledB) {
                                      m_ritEnabledB = enabled;
                                      emit ritXitBChanged(m_ritEnabledB, m_ritXitOffsetB);
                                  }
                              }});

    // 3-char commands
    m_commandHandlers.append({"ACN", [this](const QString &c) { handleACN(c); }});
    m_commandHandlers.append({"ACM", [this](const QString &c) { handleACM(c); }});
    m_commandHandlers.append({"ACS", [this](const QString &c) { handleACS(c); }});
    m_commandHandlers.append({"ACT", [this](const QString &c) { handleACT(c); }});
    m_commandHandlers.append({"RV.", [this](const QString &c) { handleRV(c); }});

    // 2-char base commands
    m_commandHandlers.append({"FA", [this](const QString &c) { handleFA(c); }});
    m_commandHandlers.append({"FB", [this](const QString &c) { handleFB(c); }});
    m_commandHandlers.append({"FT", [this](const QString &c) { handleFT(c); }});
    // FP — deduplicated via handleIntPair
    m_commandHandlers.append({"FP", [this](const QString &c) {
                                  handleIntPair(c, 2, m_filterPosition, 1, 3, &RadioState::filterPositionChanged);
                              }});
    m_commandHandlers.append({"FX", [this](const QString &c) { handleFX(c); }});
    m_commandHandlers.append({"MD", [this](const QString &c) { handleMD(c); }});
    m_commandHandlers.append({"BL", [this](const QString &c) { handleBL(c); }});
    // BW — deduplicated inline (×10 multiplier)
    m_commandHandlers.append({"BW", [this](const QString &c) {
                                  if (c.length() <= 2)
                                      return;
                                  bool ok;
                                  int bw = c.mid(2).toInt(&ok);
                                  if (ok) {
                                      int newBw = bw * 10;
                                      if (m_filterBandwidth != newBw) {
                                          m_filterBandwidth = newBw;
                                          emit filterBandwidthChanged(m_filterBandwidth);
                                      }
                                  }
                              }});
    // IS — deduplicated via handleIntPair
    m_commandHandlers.append({"IS", [this](const QString &c) {
                                  handleIntPair(c, 2, m_ifShift, -99999, 99999, &RadioState::ifShiftChanged);
                              }});
    m_commandHandlers.append({"CW", [this](const QString &c) { handleCW(c); }});
    // RG — deduplicated inline (strips leading dash)
    m_commandHandlers.append({"RG", [this](const QString &c) {
                                  if (c.length() <= 2)
                                      return;
                                  QString val = c.mid(2);
                                  if (val.startsWith('-'))
                                      val = val.mid(1);
                                  bool ok;
                                  int rg = val.toInt(&ok);
                                  if (ok && m_rfGain != rg) {
                                      m_rfGain = rg;
                                      emit rfGainChanged(m_rfGain);
                                  }
                              }});
    // SQ — deduplicated via handleIntPair
    m_commandHandlers.append({"SQ", [this](const QString &c) {
                                  handleIntPair(c, 2, m_squelchLevel, -99999, 99999, &RadioState::squelchChanged);
                              }});
    m_commandHandlers.append({"MG", [this](const QString &c) { handleMG(c); }});
    m_commandHandlers.append({"ML", [this](const QString &c) { handleML(c); }});
    m_commandHandlers.append({"CP", [this](const QString &c) { handleCP(c); }});
    m_commandHandlers.append({"PC", [this](const QString &c) { handlePC(c); }});
    m_commandHandlers.append({"KS", [this](const QString &c) { handleKS(c); }});
    m_commandHandlers.append({"KP", [this](const QString &c) { handleKP(c); }});
    m_commandHandlers.append({"SM", [this](const QString &c) { handleSM(c); }});
    m_commandHandlers.append({"PO", [this](const QString &c) { handlePO(c); }});
    m_commandHandlers.append({"TM", [this](const QString &c) { handleTM(c); }});
    m_commandHandlers.append({"TX", [this](const QString &c) { handleTX(c); }});
    m_commandHandlers.append({"RX", [this](const QString &c) { handleRX(c); }});
    m_commandHandlers.append({"NB", [this](const QString &c) { handleNB(c); }});
    m_commandHandlers.append({"NR", [this](const QString &c) { handleNR(c); }});
    // NA — deduplicated via handleBoolPair
    m_commandHandlers.append(
        {"NA", [this](const QString &c) { handleBoolPair(c, 2, m_autoNotchEnabled, &RadioState::notchChanged); }});
    m_commandHandlers.append({"NM", [this](const QString &c) { handleNM(c); }});
    m_commandHandlers.append({"PA", [this](const QString &c) { handlePA(c); }});
    m_commandHandlers.append({"RA", [this](const QString &c) { handleRA(c); }});
    m_commandHandlers.append({"GT", [this](const QString &c) { handleGT(c); }});
    m_commandHandlers.append({"AP", [this](const QString &c) { handleAP(c); }});
    m_commandHandlers.append({"LN", [this](const QString &c) { handleLN(c); }});
    // LK — deduplicated via handleBoolPairVal
    m_commandHandlers.append(
        {"LK", [this](const QString &c) { handleBoolPairVal(c, 2, m_lockA, &RadioState::lockAChanged); }});
    m_commandHandlers.append({"LO", [this](const QString &c) { handleLO(c); }});
    m_commandHandlers.append({"LI", [this](const QString &c) { handleLI(c); }});
    // VT — deduplicated inline (takes .left(1), qBound)
    m_commandHandlers.append({"VT", [this](const QString &c) {
                                  if (c.length() <= 2)
                                      return;
                                  bool ok;
                                  int step = c.mid(2).left(1).toInt(&ok);
                                  if (ok) {
                                      int ns = qBound(0, step, 5);
                                      if (ns != m_tuningStep) {
                                          m_tuningStep = ns;
                                          emit tuningStepChanged(m_tuningStep);
                                      }
                                  }
                              }});
    m_commandHandlers.append({"VX", [this](const QString &c) { handleVX(c); }});
    m_commandHandlers.append({"VG", [this](const QString &c) { handleVG(c); }});
    m_commandHandlers.append({"VI", [this](const QString &c) { handleVI(c); }});
    m_commandHandlers.append({"MI", [this](const QString &c) { handleMI(c); }});
    m_commandHandlers.append({"MS", [this](const QString &c) { handleMS(c); }});
    m_commandHandlers.append({"MX", [this](const QString &c) { handleMX(c); }});
    m_commandHandlers.append({"MN", [this](const QString &c) { handleMN(c); }});
    m_commandHandlers.append({"ES", [this](const QString &c) { handleES(c); }});
    m_commandHandlers.append({"SD", [this](const QString &c) { handleSD(c); }});
    m_commandHandlers.append({"SB", [this](const QString &c) { handleSB(c); }});
    m_commandHandlers.append({"DV", [this](const QString &c) { handleDV(c); }});
    m_commandHandlers.append({"DR", [this](const QString &c) { handleDR(c); }});
    m_commandHandlers.append({"DT", [this](const QString &c) { handleDT(c); }});
    m_commandHandlers.append({"TS", [this](const QString &c) { handleTS(c); }});
    m_commandHandlers.append({"TB", [this](const QString &c) { handleTB(c); }});
    m_commandHandlers.append({"TD", [this](const QString &c) { handleTD(c); }});
    m_commandHandlers.append({"TE", [this](const QString &c) { handleTE(c); }});
    m_commandHandlers.append({"BS", [this](const QString &c) { handleBS(c); }});
    m_commandHandlers.append({"AN", [this](const QString &c) { handleAN(c); }});
    // AR — deduplicated inline (3-arg signal)
    m_commandHandlers.append({"AR", [this](const QString &c) {
                                  if (c.length() <= 2)
                                      return;
                                  bool ok;
                                  int ar = c.mid(2).toInt(&ok);
                                  if (ok && ar >= 0 && ar <= 7 && ar != m_receiveAntenna) {
                                      m_receiveAntenna = ar;
                                      emit antennaChanged(m_selectedAntenna, m_receiveAntenna, m_receiveAntennaSub);
                                  }
                              }});
    m_commandHandlers.append({"AT", [this](const QString &c) { handleAT(c); }});
    m_commandHandlers.append({"RT", [this](const QString &c) { handleRT(c); }});
    m_commandHandlers.append({"XT", [this](const QString &c) { handleXT(c); }});
    m_commandHandlers.append({"RO", [this](const QString &c) { handleRO(c); }});
    m_commandHandlers.append({"RE", [this](const QString &c) { handleRE(c); }});
    m_commandHandlers.append({"ID", [this](const QString &c) { handleID(c); }});
    m_commandHandlers.append({"OM", [this](const QString &c) { handleOM(c); }});
    m_commandHandlers.append({"ER", [this](const QString &c) { handleER(c); }});

    // Sort by prefix length (longest first) for correct matching
    std::sort(m_commandHandlers.begin(), m_commandHandlers.end(),
              [](const CommandEntry &a, const CommandEntry &b) { return a.prefix.length() > b.prefix.length(); });
}

// =============================================================================
// Individual Command Handlers - VFO/Frequency
// =============================================================================

void RadioState::handleFA(const QString &cmd) {
    if (cmd.length() <= 2)
        return;
    bool ok;
    quint64 freq = cmd.mid(2).toULongLong(&ok);
    if (ok && m_vfoA != freq) {
        m_vfoA = freq;
        m_frequency = freq;
        emit frequencyChanged(freq);
    }
}

void RadioState::handleFB(const QString &cmd) {
    if (cmd.length() <= 2)
        return;
    bool ok;
    quint64 freq = cmd.mid(2).toULongLong(&ok);
    if (ok && m_vfoB != freq) {
        m_vfoB = freq;
        emit frequencyBChanged(freq);
    }
}

void RadioState::handleFT(const QString &cmd) {
    if (cmd.length() <= 2)
        return;
    bool newSplit = (cmd.mid(2) == "1");
    if (newSplit != m_splitEnabled) {
        m_splitEnabled = newSplit;
        emit splitChanged(m_splitEnabled);
    }
}

// =============================================================================
// Individual Command Handlers - Mode
// =============================================================================

void RadioState::handleMD(const QString &cmd) {
    if (cmd.length() <= 2)
        return;
    bool ok;
    int modeCode = cmd.mid(2).toInt(&ok);
    if (ok) {
        Mode newMode = modeFromCode(modeCode);
        if (m_mode != newMode) {
            m_mode = newMode;
            emit modeChanged(m_mode);
            int currentDelay = delayForCurrentMode();
            if (currentDelay >= 0) {
                emit qskDelayChanged(currentDelay);
            }
        }
    }
}

void RadioState::handleMDSub(const QString &cmd) {
    if (cmd.length() <= 3)
        return;
    bool ok;
    int modeCode = cmd.mid(3).toInt(&ok);
    if (ok) {
        Mode newMode = modeFromCode(modeCode);
        if (m_modeB != newMode) {
            m_modeB = newMode;
            emit modeBChanged(m_modeB);
        }
    }
}

// =============================================================================
// Individual Command Handlers - Bandwidth/Filter
// =============================================================================

void RadioState::handleCW(const QString &cmd) {
    // CW pitch - but skip CW-R mode strings
    if (cmd.length() < 4 || cmd.startsWith("CW-"))
        return;
    bool ok;
    int pitchCode = cmd.mid(2).toInt(&ok);
    if (ok && pitchCode >= 25 && pitchCode <= 95) {
        int pitchHz = pitchCode * 10;
        if (pitchHz != m_cwPitch) {
            m_cwPitch = pitchHz;
            emit cwPitchChanged(m_cwPitch);
        }
    }
}

// =============================================================================
// Individual Command Handlers - Gain/Level
// =============================================================================

void RadioState::handleMG(const QString &cmd) {
    if (cmd.length() <= 2)
        return;
    bool ok;
    int gain = cmd.mid(2).toInt(&ok);
    if (ok && gain != m_micGain) {
        m_micGain = gain;
        emit micGainChanged(m_micGain);
    }
}

void RadioState::handleCP(const QString &cmd) {
    if (cmd.length() <= 2)
        return;
    bool ok;
    int comp = cmd.mid(2).toInt(&ok);
    if (ok && comp != m_compression) {
        m_compression = comp;
        emit compressionChanged(m_compression);
    }
}

void RadioState::handleML(const QString &cmd) {
    // Monitor Level - MLmnnn where m=mode (0=CW, 1=Data, 2=Voice), nnn=000-100
    if (cmd.length() < 5)
        return;
    bool ok;
    int mode = cmd.mid(2, 1).toInt(&ok);
    if (!ok || mode < 0 || mode > 2)
        return;

    int level = cmd.mid(3).toInt(&ok);
    if (!ok || level < 0 || level > 100)
        return;

    int *target = nullptr;
    switch (mode) {
    case 0:
        target = &m_monitorLevelCW;
        break;
    case 1:
        target = &m_monitorLevelData;
        break;
    case 2:
        target = &m_monitorLevelVoice;
        break;
    }
    if (target && *target != level) {
        *target = level;
        emit monitorLevelChanged(mode, level);
    }
}

void RadioState::handleBL(const QString &cmd) {
    // Balance - BLm±nn where m=mode (0=NOR, 1=BAL), ±nn=offset (-50 to +50)
    // Examples: BL0+00, BL0+02, BL0-03, BL1+00, BL1-01
    if (cmd.length() < 5)
        return;
    int mode = cmd[2].digitValue();
    if (mode < 0 || mode > 1)
        return;
    QChar sign = cmd[3];
    bool ok;
    int nn = cmd.mid(4).toInt(&ok);
    if (!ok)
        return;
    int offset = (sign == '-') ? -nn : nn;
    offset = qBound(-50, offset, 50);
    if (m_balanceMode != mode || m_balanceOffset != offset) {
        m_balanceMode = mode;
        m_balanceOffset = offset;
        emit balanceChanged(mode, offset);
    }
}

void RadioState::handleMX(const QString &cmd) {
    // Audio Mix routing - MXL.R where L and R are component strings
    // Format: "MX" followed by "left.right" e.g. "MXA.B", "MXAB.AB", "MXA.-A"
    // Components: A=main(0), B=sub(1), AB=main+sub(2), -A=neg main(3)
    if (cmd.length() < 5)
        return;

    QString body = cmd.mid(2); // e.g. "A.B", "AB.AB", "A.-A"
    int dotPos = body.indexOf('.');
    if (dotPos < 1 || dotPos >= body.length() - 1)
        return;

    QString leftStr = body.left(dotPos);
    QString rightStr = body.mid(dotPos + 1);

    // Map component string to MixSource value
    auto mapComponent = [](const QString &s) -> int {
        if (s == "A")
            return 0; // MixA
        if (s == "B")
            return 1; // MixB
        if (s == "AB")
            return 2; // MixAB
        if (s == "-A")
            return 3; // MixNegA
        return -1;
    };

    int left = mapComponent(leftStr);
    int right = mapComponent(rightStr);
    if (left < 0 || right < 0)
        return;

    if (m_audioMixLeft != left || m_audioMixRight != right) {
        m_audioMixLeft = left;
        m_audioMixRight = right;
        emit audioMixChanged(left, right);
    }
}

void RadioState::handlePC(const QString &cmd) {
    // Power Control - PCnnnr; where nnn=power value, r=L/H/X
    // L = QRP (0.1-10W): nnn is watts*10 (e.g., 099 = 9.9W)
    // H = QRO (1-110W): nnn is watts directly (e.g., 050 = 50W)
    // X = XVTR (0.1-10mW): nnn is mW*10
    if (cmd.length() < 6)
        return;

    bool ok;
    int powerRaw = cmd.mid(2, 3).toInt(&ok);
    if (!ok)
        return;

    QChar mode = cmd.at(5);
    double watts;
    bool qrp;

    if (mode == 'L') {
        // QRP mode: value is watts * 10
        watts = powerRaw / 10.0;
        qrp = true;
    } else if (mode == 'H') {
        // QRO mode: value is watts directly
        watts = powerRaw;
        qrp = false;
    } else {
        // Unknown mode (X for XVTR, etc.) - skip
        return;
    }

    bool changed = false;
    if (watts != m_rfPower) {
        m_rfPower = watts;
        changed = true;
    }
    if (qrp != m_isQrpMode) {
        m_isQrpMode = qrp;
        changed = true;
    }
    if (changed) {
        emit rfPowerChanged(m_rfPower, m_isQrpMode);
    }
}

void RadioState::handleKS(const QString &cmd) {
    if (cmd.length() <= 2)
        return;
    bool ok;
    int wpm = cmd.mid(2).toInt(&ok);
    if (ok && m_keyerSpeed != wpm) {
        m_keyerSpeed = wpm;
        emit keyerSpeedChanged(m_keyerSpeed);
    }
}

void RadioState::handleKP(const QString &cmd) {
    // KPionnn; where i=iambic(A/B), o=paddle(N/R), nnn=weight(090-125)
    if (cmd.length() < 7)
        return;
    QChar iambic = cmd[2];
    QChar paddle = cmd[3];
    bool ok;
    int weight = cmd.mid(4, 3).toInt(&ok);
    if (!ok)
        return;
    bool changed = false;
    if (m_iambicMode != iambic) {
        m_iambicMode = iambic;
        changed = true;
    }
    if (m_paddleOrientation != paddle) {
        m_paddleOrientation = paddle;
        changed = true;
    }
    if (m_keyingWeight != weight) {
        m_keyingWeight = weight;
        changed = true;
    }
    if (changed) {
        emit keyerPaddleChanged(m_iambicMode, m_paddleOrientation, m_keyingWeight);
    }
}

void RadioState::setIambicMode(QChar mode) {
    if (m_iambicMode != mode) {
        m_iambicMode = mode;
        emit keyerPaddleChanged(m_iambicMode, m_paddleOrientation, m_keyingWeight);
    }
}

void RadioState::setPaddleOrientation(QChar orientation) {
    if (m_paddleOrientation != orientation) {
        m_paddleOrientation = orientation;
        emit keyerPaddleChanged(m_iambicMode, m_paddleOrientation, m_keyingWeight);
    }
}

void RadioState::setKeyingWeight(int weight) {
    weight = qBound(90, weight, 125);
    if (m_keyingWeight != weight) {
        m_keyingWeight = weight;
        emit keyerPaddleChanged(m_iambicMode, m_paddleOrientation, m_keyingWeight);
    }
}

// =============================================================================
// Individual Command Handlers - Meters
// =============================================================================

void RadioState::handleSM(const QString &cmd) {
    if (cmd.length() <= 2)
        return;
    bool ok;
    int bars = cmd.mid(2).toInt(&ok);
    if (ok) {
        if (bars <= 18) {
            m_sMeter = bars / 2.0;
        } else {
            int dbAboveS9 = (bars - 18) * 3;
            m_sMeter = 9.0 + dbAboveS9 / 10.0;
        }
        emit sMeterChanged(m_sMeter);
    }
}

void RadioState::handleSMSub(const QString &cmd) {
    if (cmd.length() <= 3)
        return;
    bool ok;
    int bars = cmd.mid(3).toInt(&ok);
    if (ok) {
        if (bars <= 18) {
            m_sMeterB = bars / 2.0;
        } else {
            int dbAboveS9 = (bars - 18) * 3;
            m_sMeterB = 9.0 + dbAboveS9 / 10.0;
        }
        emit sMeterBChanged(m_sMeterB);
    }
}

void RadioState::handlePO(const QString &cmd) {
    if (cmd.length() <= 2)
        return;
    bool ok;
    int po = cmd.mid(2).toInt(&ok);
    if (ok && m_powerMeter != po) {
        m_powerMeter = po;
        emit powerMeterChanged(m_powerMeter);
    }
}

void RadioState::handleTM(const QString &cmd) {
    // TX Meter Data (TM) - TMaaabbbcccddd; (ALC, CMP, FWD, SWR) - 3-digit fields
    if (cmd.length() < 14)
        return;
    QString data = cmd.mid(2);
    if (data.length() < 12)
        return;

    bool ok1, ok2, ok3, ok4;
    int alc = data.mid(0, 3).toInt(&ok1);
    int cmp = data.mid(3, 3).toInt(&ok2);
    int fwd = data.mid(6, 3).toInt(&ok3);
    int swrRaw = data.mid(9, 3).toInt(&ok4);

    if (!ok1 || !ok2 || !ok3 || !ok4)
        return;

    m_alcMeter = alc;
    m_compressionDb = cmp;
    // FWD is watts in QRO, tenths in QRP
    m_forwardPower = m_isQrpMode ? fwd / 10.0 : fwd;
    m_swrMeter = swrRaw / 10.0; // SWR in 1/10th units

    emit txMeterChanged(m_alcMeter, m_compressionDb, m_forwardPower, m_swrMeter);
    emit swrChanged(m_swrMeter);
}

// =============================================================================
// Individual Command Handlers - TX/RX State
// =============================================================================

void RadioState::handleTX(const QString &cmd) {
    Q_UNUSED(cmd)
    if (!m_isTransmitting) {
        m_isTransmitting = true;
        emit transmitStateChanged(true);
    }
}

void RadioState::handleRX(const QString &cmd) {
    Q_UNUSED(cmd)
    if (m_isTransmitting) {
        m_isTransmitting = false;
        emit transmitStateChanged(false);
    }
}

// =============================================================================
// Individual Command Handlers - Processing (NB, NR, PA, RA, GT, NA, NM)
// =============================================================================

void RadioState::handleNB(const QString &cmd) {
    // NB - Noise Blanker Main: NBnnm or NBnnmf where nn=level, m=on/off, f=filter
    if (cmd.length() < 4)
        return;
    QString nbStr = cmd.mid(2);
    if (nbStr.length() < 3)
        return;

    bool ok1, ok2;
    int level = nbStr.left(2).toInt(&ok1);
    int enabled = nbStr.mid(2, 1).toInt(&ok2);
    if (!ok1 || !ok2)
        return;

    m_noiseBlankerLevel = qMin(level, 15);
    m_noiseBlankerEnabled = (enabled == 1);

    if (nbStr.length() >= 4) {
        bool ok3;
        int filter = nbStr.mid(3, 1).toInt(&ok3);
        if (ok3) {
            m_noiseBlankerFilterWidth = qMin(filter, 2);
        }
    }
    emit processingChanged();
}

void RadioState::handleNBSub(const QString &cmd) {
    // NB$ - Noise Blanker Sub
    if (cmd.length() < 5)
        return;
    QString nbStr = cmd.mid(3);
    if (nbStr.length() < 3)
        return;

    bool ok1, ok2;
    int level = nbStr.left(2).toInt(&ok1);
    int enabled = nbStr.mid(2, 1).toInt(&ok2);
    if (!ok1 || !ok2)
        return;

    m_noiseBlankerLevelB = qMin(level, 15);
    m_noiseBlankerEnabledB = (enabled == 1);

    if (nbStr.length() >= 4) {
        bool ok3;
        int filter = nbStr.mid(3, 1).toInt(&ok3);
        if (ok3) {
            m_noiseBlankerFilterWidthB = qMin(filter, 2);
        }
    }
    emit processingChangedB();
}

void RadioState::handleNR(const QString &cmd) {
    // NR - Noise Reduction Main
    if (cmd.length() < 3)
        return;
    QString nrStr = cmd.mid(2);
    if (nrStr.length() < 3)
        return;

    bool ok1, ok2;
    int level = nrStr.left(2).toInt(&ok1);
    int enabled = nrStr.right(1).toInt(&ok2);
    if (ok1 && ok2) {
        m_noiseReductionLevel = level;
        m_noiseReductionEnabled = (enabled == 1);
        emit processingChanged();
    }
}

void RadioState::handleNRSub(const QString &cmd) {
    // NR$ - Noise Reduction Sub
    if (cmd.length() < 4)
        return;
    QString nrStr = cmd.mid(3);
    if (nrStr.length() < 3)
        return;

    bool ok1, ok2;
    int level = nrStr.left(2).toInt(&ok1);
    int enabled = nrStr.right(1).toInt(&ok2);
    if (ok1 && ok2) {
        m_noiseReductionLevelB = level;
        m_noiseReductionEnabledB = (enabled == 1);
        emit processingChangedB();
    }
}

void RadioState::handlePA(const QString &cmd) {
    // PA - Preamp Main: PAnm where n=level, m=on/off
    if (cmd.length() < 4)
        return;
    QString paStr = cmd.mid(2);
    if (paStr.length() < 2)
        return;

    bool ok1, ok2;
    int level = paStr.left(1).toInt(&ok1);
    int enabled = paStr.mid(1, 1).toInt(&ok2);
    if (ok1 && ok2) {
        m_preamp = level;
        m_preampEnabled = (enabled == 1);
        emit processingChanged();
    }
}

void RadioState::handlePASub(const QString &cmd) {
    // PA$ - Preamp Sub
    if (cmd.length() < 5)
        return;
    QString paStr = cmd.mid(3);
    if (paStr.length() < 2)
        return;

    bool ok1, ok2;
    int level = paStr.left(1).toInt(&ok1);
    int enabled = paStr.mid(1, 1).toInt(&ok2);
    if (ok1 && ok2) {
        m_preampB = level;
        m_preampEnabledB = (enabled == 1);
        emit processingChangedB();
    }
}

void RadioState::handleRA(const QString &cmd) {
    // RA - Attenuator Main: RAnnotm where nn=level, m=on/off
    if (cmd.length() < 5)
        return;
    QString raStr = cmd.mid(2);
    if (raStr.length() < 3)
        return;

    bool ok1, ok2;
    int level = raStr.left(2).toInt(&ok1);
    int enabled = raStr.mid(2, 1).toInt(&ok2);
    if (ok1 && ok2) {
        m_attenuatorLevel = level;
        m_attenuatorEnabled = (enabled == 1);
        emit processingChanged();
    }
}

void RadioState::handleRASub(const QString &cmd) {
    // RA$ - Attenuator Sub
    if (cmd.length() < 6)
        return;
    QString raStr = cmd.mid(3);
    if (raStr.length() < 3)
        return;

    bool ok1, ok2;
    int level = raStr.left(2).toInt(&ok1);
    int enabled = raStr.mid(2, 1).toInt(&ok2);
    if (ok1 && ok2) {
        m_attenuatorLevelB = level;
        m_attenuatorEnabledB = (enabled == 1);
        emit processingChangedB();
    }
}

void RadioState::handleGT(const QString &cmd) {
    // GT - AGC Speed Main: GTn where n=0(off)/1(slow)/2(fast)
    if (cmd.length() <= 2)
        return;
    bool ok;
    int gt = cmd.mid(2).toInt(&ok);
    if (ok) {
        if (gt == 0)
            m_agcSpeed = AGC_Off;
        else if (gt == 1)
            m_agcSpeed = AGC_Slow;
        else if (gt == 2)
            m_agcSpeed = AGC_Fast;
        emit processingChanged();
    }
}

void RadioState::handleGTSub(const QString &cmd) {
    // GT$ - AGC Speed Sub
    if (cmd.length() < 4)
        return;
    bool ok;
    int gt = cmd.mid(3).toInt(&ok);
    if (ok) {
        if (gt == 0)
            m_agcSpeedB = AGC_Off;
        else if (gt == 1)
            m_agcSpeedB = AGC_Slow;
        else if (gt == 2)
            m_agcSpeedB = AGC_Fast;
        emit processingChangedB();
    }
}

void RadioState::handleNM(const QString &cmd) {
    // NM - Manual Notch Main: NMnnnnm or NMm
    if (cmd.length() < 3)
        return;
    QString data = cmd.mid(2);
    if (data.length() >= 5) {
        bool ok;
        int pitch = data.left(4).toInt(&ok);
        bool enabled = (data.at(4) == '1');
        bool changed = false;
        if (ok && pitch >= 150 && pitch <= 5000 && m_manualNotchPitch != pitch) {
            m_manualNotchPitch = pitch;
            changed = true;
        }
        if (m_manualNotchEnabled != enabled) {
            m_manualNotchEnabled = enabled;
            changed = true;
        }
        if (changed) {
            emit notchChanged();
        }
    } else if (data.length() >= 1) {
        bool enabled = (data.at(0) == '1');
        if (m_manualNotchEnabled != enabled) {
            m_manualNotchEnabled = enabled;
            emit notchChanged();
        }
    }
}

void RadioState::handleNMSub(const QString &cmd) {
    // NM$ - Manual Notch Sub
    if (cmd.length() < 4)
        return;
    QString data = cmd.mid(3);
    if (data.length() >= 5) {
        bool ok;
        int pitch = data.left(4).toInt(&ok);
        bool enabled = (data.at(4) == '1');
        bool changed = false;
        if (ok && pitch >= 150 && pitch <= 5000 && m_manualNotchPitchB != pitch) {
            m_manualNotchPitchB = pitch;
            changed = true;
        }
        if (m_manualNotchEnabledB != enabled) {
            m_manualNotchEnabledB = enabled;
            changed = true;
        }
        if (changed) {
            emit notchBChanged();
        }
    } else if (data.length() >= 1) {
        bool enabled = (data.at(0) == '1');
        if (m_manualNotchEnabledB != enabled) {
            m_manualNotchEnabledB = enabled;
            emit notchBChanged();
        }
    }
}

// =============================================================================
// Individual Command Handlers - Audio/Effects
// =============================================================================

void RadioState::handleFX(const QString &cmd) {
    // FX - Audio Effects: FXn where n=0(off)/1(delay)/2(pitch-map)
    if (cmd.length() < 3)
        return;
    bool ok;
    int fx = cmd.mid(2).toInt(&ok);
    if (ok && fx >= 0 && fx <= 2 && fx != m_afxMode) {
        m_afxMode = fx;
        emit afxModeChanged(m_afxMode);
    }
}

void RadioState::handleAP(const QString &cmd) {
    // AP - Audio Peak Filter Main: APmb where m=enabled, b=bandwidth
    if (cmd.length() < 4)
        return;
    bool ok;
    int m = cmd.mid(2, 1).toInt(&ok);
    if (!ok)
        return;
    int b = cmd.mid(3, 1).toInt(&ok);
    if (!ok)
        return;

    bool enabled = (m == 1);
    int bandwidth = qBound(0, b, 2);
    if (enabled != m_apfEnabled || bandwidth != m_apfBandwidth) {
        m_apfEnabled = enabled;
        m_apfBandwidth = bandwidth;
        emit apfChanged(m_apfEnabled, m_apfBandwidth);
    }
}

void RadioState::handleAPSub(const QString &cmd) {
    // AP$ - Audio Peak Filter Sub
    if (cmd.length() < 5)
        return;
    bool ok;
    int m = cmd.mid(3, 1).toInt(&ok);
    if (!ok)
        return;
    int b = cmd.mid(4, 1).toInt(&ok);
    if (!ok)
        return;

    bool enabled = (m == 1);
    int bandwidth = qBound(0, b, 2);
    if (enabled != m_apfEnabledB || bandwidth != m_apfBandwidthB) {
        m_apfEnabledB = enabled;
        m_apfBandwidthB = bandwidth;
        emit apfBChanged(m_apfEnabledB, m_apfBandwidthB);
    }
}

// =============================================================================
// Individual Command Handlers - VFO Control
// =============================================================================

void RadioState::handleLN(const QString &cmd) {
    // LN - VFO Link
    if (cmd.length() < 3)
        return;
    bool ok;
    int ln = cmd.mid(2).toInt(&ok);
    if (ok) {
        bool linked = (ln == 1);
        if (linked != m_vfoLink) {
            m_vfoLink = linked;
            emit vfoLinkChanged(m_vfoLink);
        }
    }
}

// =============================================================================
// Individual Command Handlers - VOX
// =============================================================================

void RadioState::handleVX(const QString &cmd) {
    // VX - VOX Enable: VXmn where m=mode (C/V/D), n=0/1
    if (cmd.length() < 4)
        return;
    QChar mode = cmd.at(2);
    bool enabled = (cmd.at(3) == '1');
    bool changed = false;
    if (mode == 'C' && m_voxCW != enabled) {
        m_voxCW = enabled;
        changed = true;
    } else if (mode == 'V' && m_voxVoice != enabled) {
        m_voxVoice = enabled;
        changed = true;
    } else if (mode == 'D' && m_voxData != enabled) {
        m_voxData = enabled;
        changed = true;
    }
    if (changed) {
        emit voxChanged(voxEnabled());
    }
}

void RadioState::handleVG(const QString &cmd) {
    // VG - VOX Gain: VGmnnn where m=V(voice)/D(data), nnn=000-060
    if (cmd.length() < 5)
        return;
    QChar modeChar = cmd.at(2);
    bool ok;
    int gain = cmd.mid(3, 3).toInt(&ok);
    if (ok && gain >= 0 && gain <= 60) {
        if (modeChar == 'V' && gain != m_voxGainVoice) {
            m_voxGainVoice = gain;
            emit voxGainChanged(0, gain);
        } else if (modeChar == 'D' && gain != m_voxGainData) {
            m_voxGainData = gain;
            emit voxGainChanged(1, gain);
        }
    }
}

void RadioState::handleVI(const QString &cmd) {
    // VI - Anti-VOX: VInnn where nnn=000-060
    if (cmd.length() < 5)
        return;
    bool ok;
    int level = cmd.mid(2, 3).toInt(&ok);
    if (ok && level >= 0 && level <= 60 && level != m_antiVox) {
        m_antiVox = level;
        emit antiVoxChanged(level);
    }
}

// =============================================================================
// Individual Command Handlers - Audio I/O
// =============================================================================

void RadioState::handleLO(const QString &cmd) {
    // LO - Line Out: LOlllrrrm where lll=left, rrr=right, m=mode
    if (cmd.length() < 9)
        return;
    bool okL, okR;
    int left = cmd.mid(2, 3).toInt(&okL);
    int right = cmd.mid(5, 3).toInt(&okR);
    int mode = cmd.mid(8, 1).toInt();

    if (okL && okR && left >= 0 && left <= 40 && right >= 0 && right <= 40) {
        bool changed = false;
        if (left != m_lineOutLeft) {
            m_lineOutLeft = left;
            changed = true;
        }
        if (right != m_lineOutRight) {
            m_lineOutRight = right;
            changed = true;
        }
        if ((mode == 1) != m_lineOutRightEqualsLeft) {
            m_lineOutRightEqualsLeft = (mode == 1);
            changed = true;
        }
        if (changed)
            emit lineOutChanged();
    }
}

void RadioState::handleLI(const QString &cmd) {
    // LI - Line In: LIuuullls where uuu=soundcard, lll=linein, s=source
    if (cmd.length() < 9)
        return;
    bool okS, okL;
    int soundCard = cmd.mid(2, 3).toInt(&okS);
    int lineIn = cmd.mid(5, 3).toInt(&okL);
    int source = cmd.mid(8, 1).toInt();

    if (okS && okL && soundCard >= 0 && soundCard <= 250 && lineIn >= 0 && lineIn <= 250 &&
        (source == 0 || source == 1)) {
        bool changed = false;
        if (soundCard != m_lineInSoundCard) {
            m_lineInSoundCard = soundCard;
            changed = true;
        }
        if (lineIn != m_lineInJack) {
            m_lineInJack = lineIn;
            changed = true;
        }
        if (source != m_lineInSource) {
            m_lineInSource = source;
            changed = true;
        }
        if (changed)
            emit lineInChanged();
    }
}

void RadioState::handleMI(const QString &cmd) {
    // MI - Mic Input Select
    if (cmd.length() < 3)
        return;
    int input = cmd.mid(2, 1).toInt();
    if (input >= 0 && input <= 4 && input != m_micInput) {
        m_micInput = input;
        emit micInputChanged(m_micInput);
    }
}

void RadioState::handleMS(const QString &cmd) {
    // MS - Mic Setup: MSabcde
    if (cmd.length() < 7)
        return;
    int frontPreamp = cmd.mid(2, 1).toInt();
    int frontBias = cmd.mid(3, 1).toInt();
    int frontButtons = cmd.mid(4, 1).toInt();
    int rearPreamp = cmd.mid(5, 1).toInt();
    int rearBias = cmd.mid(6, 1).toInt();

    bool changed = false;
    if (frontPreamp >= 0 && frontPreamp <= 2 && frontPreamp != m_micFrontPreamp) {
        m_micFrontPreamp = frontPreamp;
        changed = true;
    }
    if ((frontBias == 0 || frontBias == 1) && frontBias != m_micFrontBias) {
        m_micFrontBias = frontBias;
        changed = true;
    }
    if ((frontButtons == 0 || frontButtons == 1) && frontButtons != m_micFrontButtons) {
        m_micFrontButtons = frontButtons;
        changed = true;
    }
    if ((rearPreamp == 0 || rearPreamp == 1) && rearPreamp != m_micRearPreamp) {
        m_micRearPreamp = rearPreamp;
        changed = true;
    }
    if ((rearBias == 0 || rearBias == 1) && rearBias != m_micRearBias) {
        m_micRearBias = rearBias;
        changed = true;
    }
    if (changed)
        emit micSetupChanged();
}

void RadioState::handleES(const QString &cmd) {
    // ES - ESSB: ESnbb where n=0/1, bb=bandwidth
    if (cmd.length() < 4)
        return;
    bool ok;
    int modeVal = cmd.mid(2, 1).toInt(&ok);
    if (!ok || (modeVal != 0 && modeVal != 1))
        return;

    bool newEssb = (modeVal == 1);
    int newBw = m_ssbTxBw;

    if (cmd.length() >= 5) {
        newBw = cmd.mid(3, 2).toInt(&ok);
        if (!ok || newBw < 24 || newBw > 45) {
            newBw = m_ssbTxBw;
        }
    }

    bool changed = false;
    if (newEssb != m_essbEnabled) {
        m_essbEnabled = newEssb;
        changed = true;
    }
    if (newBw >= 24 && newBw <= 45 && newBw != m_ssbTxBw) {
        m_ssbTxBw = newBw;
        changed = true;
    }
    if (changed) {
        emit essbChanged(m_essbEnabled, m_ssbTxBw);
    }
}

// =============================================================================
// Individual Command Handlers - QSK/Delay
// =============================================================================

void RadioState::handleSD(const QString &cmd) {
    // SD - QSK/VOX Delay: SDxMzzz where x=QSK flag, M=mode, zzz=delay
    if (cmd.length() < 7)
        return;
    QChar qskFlag = cmd.at(2);
    QChar modeChar = cmd.at(3);
    bool ok;
    int delay = cmd.mid(4, 3).toInt(&ok);
    if (!ok)
        return;

    // Update QSK enabled state (only meaningful for CW mode)
    if (modeChar == 'C') {
        bool qskOn = (qskFlag == '1');
        if (qskOn != m_qskEnabled) {
            m_qskEnabled = qskOn;
            emit qskEnabledChanged(m_qskEnabled);
        }
    }

    bool isCurrentMode = false;
    if (modeChar == 'C') {
        if (m_qskDelayCW != delay) {
            m_qskDelayCW = delay;
            isCurrentMode = (m_mode == CW || m_mode == CW_R);
        }
    } else if (modeChar == 'V') {
        if (m_qskDelayVoice != delay) {
            m_qskDelayVoice = delay;
            isCurrentMode = (m_mode == LSB || m_mode == USB || m_mode == AM || m_mode == FM);
        }
    } else if (modeChar == 'D') {
        if (m_qskDelayData != delay) {
            m_qskDelayData = delay;
            isCurrentMode = (m_mode == DATA || m_mode == DATA_R);
        }
    }
    if (isCurrentMode) {
        emit qskDelayChanged(delay);
    }
}

// =============================================================================
// Individual Command Handlers - Control State
// =============================================================================

void RadioState::handleSB(const QString &cmd) {
    // SB - Sub Receiver: SB0=off, SB1=on, SB3=on (diversity)
    if (cmd.length() <= 2)
        return;
    bool newState = (cmd.mid(2) != "0");
    if (newState != m_subReceiverEnabled) {
        m_subReceiverEnabled = newState;
        emit subRxEnabledChanged(m_subReceiverEnabled);
    }
}

void RadioState::handleDV(const QString &cmd) {
    // DV - Diversity
    if (cmd.length() <= 2)
        return;
    bool newState = (cmd.mid(2) == "1");
    if (newState != m_diversityEnabled) {
        m_diversityEnabled = newState;
        emit diversityChanged(m_diversityEnabled);
    }
}

void RadioState::handleTS(const QString &cmd) {
    // TS - Test Mode
    if (cmd.length() < 3)
        return;
    bool enabled = (cmd.mid(2, 1) == "1");
    if (enabled != m_testMode) {
        m_testMode = enabled;
        emit testModeChanged(m_testMode);
    }
}

void RadioState::handleBS(const QString &cmd) {
    // BS - B SET
    if (cmd.length() < 3)
        return;
    bool enabled = (cmd.mid(2, 1) == "1");
    if (enabled != m_bSetEnabled) {
        m_bSetEnabled = enabled;
        emit bSetChanged(m_bSetEnabled);
    }
}

// =============================================================================
// Individual Command Handlers - Antenna
// =============================================================================

void RadioState::handleAN(const QString &cmd) {
    if (cmd.length() <= 2)
        return;
    bool ok;
    int an = cmd.mid(2).toInt(&ok);
    if (ok && an >= 1 && an <= 6 && an != m_selectedAntenna) {
        m_selectedAntenna = an;
        emit antennaChanged(m_selectedAntenna, m_receiveAntenna, m_receiveAntennaSub);
    }
}

void RadioState::handleAT(const QString &cmd) {
    if (cmd.length() < 3)
        return;
    bool ok;
    int at = cmd.mid(2).toInt(&ok);
    if (ok && at >= 0 && at <= 2 && at != m_atuMode) {
        m_atuMode = at;
        emit atuModeChanged(m_atuMode);
    }
}

void RadioState::handleACN(const QString &cmd) {
    if (cmd.length() < 4)
        return;
    bool ok;
    int index = cmd.mid(3, 1).toInt(&ok);
    if (ok && index >= 1 && index <= 7) {
        QString name = cmd.mid(4).trimmed();
        if (!name.isEmpty() && name != m_antennaNames.value(index)) {
            m_antennaNames[index] = name;
            emit antennaNameChanged(index, name);
        }
    }
}

void RadioState::handleACM(const QString &cmd) {
    if (cmd.length() < 11)
        return;
    bool displayAll = (cmd.at(3) == '1');
    bool changed = (displayAll != m_mainRxDisplayAll);
    m_mainRxDisplayAll = displayAll;
    for (int i = 0; i < 7; i++) {
        bool enabled = (cmd.at(4 + i) == '1');
        if (enabled != m_mainRxAntMask[i]) {
            m_mainRxAntMask[i] = enabled;
            changed = true;
        }
    }
    if (changed)
        emit mainRxAntCfgChanged();
}

void RadioState::handleACS(const QString &cmd) {
    if (cmd.length() < 11)
        return;
    bool displayAll = (cmd.at(3) == '1');
    bool changed = (displayAll != m_subRxDisplayAll);
    m_subRxDisplayAll = displayAll;
    for (int i = 0; i < 7; i++) {
        bool enabled = (cmd.at(4 + i) == '1');
        if (enabled != m_subRxAntMask[i]) {
            m_subRxAntMask[i] = enabled;
            changed = true;
        }
    }
    if (changed)
        emit subRxAntCfgChanged();
}

void RadioState::handleACT(const QString &cmd) {
    if (cmd.length() < 7)
        return;
    bool displayAll = (cmd.at(3) == '1');
    bool changed = (displayAll != m_txDisplayAll);
    m_txDisplayAll = displayAll;
    for (int i = 0; i < 3; i++) {
        bool enabled = (cmd.at(4 + i) == '1');
        if (enabled != m_txAntMask[i]) {
            m_txAntMask[i] = enabled;
            changed = true;
        }
    }
    if (changed)
        emit txAntCfgChanged();
}

// =============================================================================
// Individual Command Handlers - RIT/XIT
// =============================================================================

void RadioState::handleRT(const QString &cmd) {
    if (cmd.length() < 3 || (cmd.at(2) != '0' && cmd.at(2) != '1'))
        return;
    bool enabled = (cmd.at(2) == '1');
    if (enabled != m_ritEnabled) {
        m_ritEnabled = enabled;
        emit ritXitChanged(m_ritEnabled, m_xitEnabled, m_ritXitOffset);
    }
}

void RadioState::handleXT(const QString &cmd) {
    if (cmd.length() < 3 || (cmd.at(2) != '0' && cmd.at(2) != '1'))
        return;
    bool enabled = (cmd.at(2) == '1');
    if (enabled != m_xitEnabled) {
        m_xitEnabled = enabled;
        emit ritXitChanged(m_ritEnabled, m_xitEnabled, m_ritXitOffset);
    }
}

void RadioState::handleRO(const QString &cmd) {
    if (cmd.length() < 3)
        return;
    bool ok;
    int offset = cmd.mid(2).toInt(&ok);
    if (ok && offset != m_ritXitOffset) {
        m_ritXitOffset = offset;
        emit ritXitChanged(m_ritEnabled, m_xitEnabled, m_ritXitOffset);
    }
}

// =============================================================================
// Individual Command Handlers - Text Decode
// =============================================================================

void RadioState::handleTD(const QString &cmd) {
    if (cmd.length() < 5)
        return;
    int mode = cmd.mid(2, 1).toInt();
    int threshold = cmd.mid(3, 1).toInt();
    int lines = cmd.mid(4, 1).toInt();
    bool changed = false;
    if (mode != m_textDecodeMode) {
        m_textDecodeMode = mode;
        changed = true;
    }
    if (threshold != m_textDecodeThreshold) {
        m_textDecodeThreshold = threshold;
        changed = true;
    }
    if (lines != m_textDecodeLines && lines >= 1 && lines <= 9) {
        m_textDecodeLines = lines;
        changed = true;
    }
    if (changed)
        emit textDecodeChanged();
}

void RadioState::handleTDSub(const QString &cmd) {
    if (cmd.length() < 6)
        return;
    int mode = cmd.mid(3, 1).toInt();
    int threshold = cmd.mid(4, 1).toInt();
    int lines = cmd.mid(5, 1).toInt();
    bool changed = false;
    if (mode != m_textDecodeModeB) {
        m_textDecodeModeB = mode;
        changed = true;
    }
    if (threshold != m_textDecodeThresholdB) {
        m_textDecodeThresholdB = threshold;
        changed = true;
    }
    if (lines != m_textDecodeLinesB && lines >= 1 && lines <= 9) {
        m_textDecodeLinesB = lines;
        changed = true;
    }
    if (changed)
        emit textDecodeBChanged();
}

void RadioState::handleTB(const QString &cmd) {
    if (cmd.length() < 5)
        return;
    QString text = cmd.mid(5);
    if (text.endsWith(';'))
        text.chop(1);
    if (!text.isEmpty())
        emit textBufferReceived(text, false);
}

void RadioState::handleTBSub(const QString &cmd) {
    if (cmd.length() < 6)
        return;
    QString text = cmd.mid(6);
    if (text.endsWith(';'))
        text.chop(1);
    if (!text.isEmpty())
        emit textBufferReceived(text, true);
}

// =============================================================================
// Individual Command Handlers - Data Mode
// =============================================================================

void RadioState::handleDT(const QString &cmd) {
    if (cmd.length() < 3)
        return;
    bool ok;
    int subMode = cmd.mid(2).toInt(&ok);
    if (!ok || subMode < 0 || subMode > 3)
        return;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_dataSubModeOptimisticTime < 500)
        return;
    if (subMode != m_dataSubMode) {
        m_dataSubMode = subMode;
        emit dataSubModeChanged(m_dataSubMode);
    }
}

void RadioState::handleDTSub(const QString &cmd) {
    if (cmd.length() < 4)
        return;
    bool ok;
    int subMode = cmd.mid(3).toInt(&ok);
    if (!ok || subMode < 0 || subMode > 3)
        return;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_dataSubModeBOptimisticTime < 500)
        return;
    if (subMode != m_dataSubModeB) {
        m_dataSubModeB = subMode;
        emit dataSubModeBChanged(m_dataSubModeB);
    }
}

void RadioState::handleDR(const QString &cmd) {
    // DR0; or DR1;
    if (cmd.length() < 3)
        return;
    bool ok;
    int rate = cmd.mid(2).toInt(&ok);
    if (!ok || rate < 0 || rate > 1)
        return;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_dataRateOptimisticTime < 500)
        return;
    if (rate != m_dataRate) {
        m_dataRate = rate;
        emit dataRateChanged(m_dataRate);
    }
}

void RadioState::handleDRSub(const QString &cmd) {
    // DR$0; or DR$1;
    if (cmd.length() < 4)
        return;
    bool ok;
    int rate = cmd.mid(3).toInt(&ok);
    if (!ok || rate < 0 || rate > 1)
        return;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_dataRateBOptimisticTime < 500)
        return;
    if (rate != m_dataRateB) {
        m_dataRateB = rate;
        emit dataRateBChanged(m_dataRateB);
    }
}

// =============================================================================
// Individual Command Handlers - Equalizer
// =============================================================================

void RadioState::handleRE(const QString &cmd) {
    if (cmd.length() < 26)
        return;
    bool changed = false;
    for (int i = 0; i < 8; i++) {
        bool ok;
        int val = cmd.mid(2 + i * 3, 3).toInt(&ok);
        if (ok && val >= -16 && val <= 16 && val != m_rxEqBands[i]) {
            m_rxEqBands[i] = val;
            changed = true;
            emit rxEqBandChanged(i, val);
        }
    }
    if (changed)
        emit rxEqChanged();
}

void RadioState::handleTE(const QString &cmd) {
    if (cmd.length() < 26)
        return;
    bool changed = false;
    for (int i = 0; i < 8; i++) {
        bool ok;
        int val = cmd.mid(2 + i * 3, 3).toInt(&ok);
        if (ok && val >= -16 && val <= 16 && val != m_txEqBands[i]) {
            m_txEqBands[i] = val;
            changed = true;
            emit txEqBandChanged(i, val);
        }
    }
    if (changed)
        emit txEqChanged();
}

// =============================================================================
// Individual Command Handlers - Radio Info
// =============================================================================

void RadioState::handleID(const QString &cmd) {
    if (cmd.length() > 2)
        m_radioID = cmd.mid(2);
}

void RadioState::handleOM(const QString &cmd) {
    // OM format: 12-char string where each position indicates an option
    // Positions: 0=ATU, 1=PA, 2=XVTR, 3=SubRX, 4=HD, 5=Mini, 6=Linear, 7=KPA1500, 8=model marker
    if (cmd.length() <= 2)
        return;
    m_optionModules = cmd.mid(2).trimmed();
    if (m_optionModules.length() > 8) {
        bool hasS = m_optionModules.length() > 3 && m_optionModules[3] == 'S';
        bool hasH = m_optionModules.length() > 4 && m_optionModules[4] == 'H';
        bool has4 = m_optionModules.length() > 8 && m_optionModules[8] == '4';

        if (hasH && hasS && has4) {
            m_radioModel = "K4HD";
        } else if (hasS && has4) {
            m_radioModel = "K4D";
        } else if (has4) {
            m_radioModel = "K4";
        }
    }
}

void RadioState::handleRV(const QString &cmd) {
    // RV.COMPONENT-VERSION format (e.g., "RV.DDC0-00.65 (0:35)")
    if (cmd.length() <= 3)
        return;
    QString versionData = cmd.mid(3);
    int dashIndex = versionData.indexOf('-');
    if (dashIndex > 0) {
        QString component = versionData.left(dashIndex);
        QString version = versionData.mid(dashIndex + 1);
        m_firmwareVersions[component] = version;
    }
}

void RadioState::handleSIFP(const QString &cmd) {
    QString data = cmd.mid(4);
    // Parse VS (voltage)
    int vsIndex = data.indexOf("VS:");
    if (vsIndex >= 0) {
        int commaIndex = data.indexOf(',', vsIndex);
        QString vsStr =
            (commaIndex > vsIndex) ? data.mid(vsIndex + 3, commaIndex - vsIndex - 3) : data.mid(vsIndex + 3);
        bool ok;
        double voltage = vsStr.toDouble(&ok);
        if (ok && voltage != m_supplyVoltage) {
            m_supplyVoltage = voltage;
            emit supplyVoltageChanged(m_supplyVoltage);
        }
    }
    // Parse IS (current)
    int isIndex = data.indexOf("IS:");
    if (isIndex >= 0) {
        int commaIndex = data.indexOf(',', isIndex);
        QString isStr =
            (commaIndex > isIndex) ? data.mid(isIndex + 3, commaIndex - isIndex - 3) : data.mid(isIndex + 3);
        bool ok;
        double current = isStr.toDouble(&ok);
        if (ok && current != m_supplyCurrent) {
            m_supplyCurrent = current;
            emit supplyCurrentChanged(m_supplyCurrent);
        }
    }
}

void RadioState::handleSIRC(const QString &cmd) {
    Q_UNUSED(cmd)
}

void RadioState::handleMN(const QString &cmd) {
    if (cmd.length() < 3)
        return;
    bool ok;
    int bank = cmd.mid(2).toInt(&ok);
    if (ok && bank >= 1 && bank <= 4 && bank != m_messageBank) {
        m_messageBank = bank;
        emit messageBankChanged(m_messageBank);
    }
}

void RadioState::handleER(const QString &cmd) {
    int colonPos = cmd.indexOf(':');
    if (colonPos > 2) {
        bool ok;
        int errorCode = cmd.mid(2, colonPos - 2).toInt(&ok);
        if (ok)
            emit errorNotificationReceived(errorCode, cmd.mid(colonPos + 1));
    }
}

// =============================================================================
// Individual Command Handlers - Display (# prefix)
// =============================================================================

void RadioState::handleDisplaySCL(const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int scale = cmd.mid(4).toInt(&ok);
    if (ok && scale >= 10 && scale <= 150 && scale != m_scale) {
        m_scale = scale;
        emit scaleChanged(m_scale);
    }
}

void RadioState::handleDisplayDPM(const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int mode = cmd.mid(4).toInt(&ok);
    if (ok && mode >= 0 && mode <= 2 && mode != m_dualPanModeLcd) {
        m_dualPanModeLcd = mode;
        emit dualPanModeLcdChanged(m_dualPanModeLcd);
    }
}

void RadioState::handleDisplayHDPM(const QString &cmd) {
    if (cmd.length() <= 5)
        return;
    bool ok;
    int mode = cmd.mid(5).toInt(&ok);
    if (ok && mode >= 0 && mode <= 2 && mode != m_dualPanModeExt) {
        m_dualPanModeExt = mode;
        emit dualPanModeExtChanged(m_dualPanModeExt);
    }
}

void RadioState::handleDisplayDSM(const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int mode = cmd.mid(4).toInt(&ok);
    if (ok && (mode == 0 || mode == 1) && mode != m_displayModeLcd) {
        m_displayModeLcd = mode;
        emit displayModeLcdChanged(m_displayModeLcd);
    }
}

void RadioState::handleDisplayHDSM(const QString &cmd) {
    if (cmd.length() <= 5)
        return;
    bool ok;
    int mode = cmd.mid(5).toInt(&ok);
    if (ok && (mode == 0 || mode == 1) && mode != m_displayModeExt) {
        m_displayModeExt = mode;
        emit displayModeExtChanged(m_displayModeExt);
    }
}

void RadioState::handleDisplayFPS(const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int fps = cmd.mid(4).toInt(&ok);
    if (ok && fps >= 12 && fps <= 30 && fps != m_displayFps) {
        m_displayFps = fps;
        emit displayFpsChanged(m_displayFps);
    }
}

void RadioState::handleDisplayWFC(const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int color = cmd.mid(4).toInt(&ok);
    if (ok && color >= 0 && color <= 4 && color != m_waterfallColor) {
        m_waterfallColor = color;
        emit waterfallColorChanged(m_waterfallColor);
    }
}

void RadioState::handleDisplayWFH(const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int height = cmd.mid(4).toInt(&ok);
    if (ok && height >= 0 && height <= 100 && height != m_waterfallHeight) {
        m_waterfallHeight = height;
        emit waterfallHeightChanged(m_waterfallHeight);
    }
}

void RadioState::handleDisplayHWFH(const QString &cmd) {
    if (cmd.length() <= 5)
        return;
    bool ok;
    int height = cmd.mid(5).toInt(&ok);
    if (ok && height >= 0 && height <= 100 && height != m_waterfallHeightExt) {
        m_waterfallHeightExt = height;
        emit waterfallHeightExtChanged(m_waterfallHeightExt);
    }
}

void RadioState::handleDisplayAVG(const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int avg = cmd.mid(4).toInt(&ok);
    if (ok && avg >= 1 && avg <= 20 && avg != m_averaging) {
        m_averaging = avg;
        emit averagingChanged(m_averaging);
    }
}

void RadioState::handleDisplayPKM(const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int pkm = cmd.mid(4).toInt(&ok);
    if (ok && (pkm == 0 || pkm == 1) && pkm != m_peakMode) {
        m_peakMode = pkm;
        emit peakModeChanged(m_peakMode > 0);
    }
}

void RadioState::handleDisplayFXT(const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int fxt = cmd.mid(4).toInt(&ok);
    if (ok && (fxt == 0 || fxt == 1) && fxt != m_fixedTune) {
        m_fixedTune = fxt;
        emit fixedTuneChanged(m_fixedTune, m_fixedTuneMode);
    }
}

void RadioState::handleDisplayFXA(const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int fxa = cmd.mid(4).toInt(&ok);
    if (ok && fxa >= 0 && fxa <= 4 && fxa != m_fixedTuneMode) {
        m_fixedTuneMode = fxa;
        emit fixedTuneChanged(m_fixedTune, m_fixedTuneMode);
    }
}

void RadioState::handleDisplayFRZ(const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int frz = cmd.mid(4).toInt(&ok);
    if (ok && (frz == 0 || frz == 1) && frz != m_freeze) {
        m_freeze = frz;
        emit freezeChanged(m_freeze > 0);
    }
}

void RadioState::handleDisplayVFA(const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int vfa = cmd.mid(4).toInt(&ok);
    if (ok && vfa >= 0 && vfa <= 3 && vfa != m_vfoACursor) {
        m_vfoACursor = vfa;
        emit vfoACursorChanged(m_vfoACursor);
    }
}

void RadioState::handleDisplayVFB(const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int vfb = cmd.mid(4).toInt(&ok);
    if (ok && vfb >= 0 && vfb <= 3 && vfb != m_vfoBCursor) {
        m_vfoBCursor = vfb;
        emit vfoBCursorChanged(m_vfoBCursor);
    }
}

void RadioState::handleDisplayAR(const QString &cmd) {
    if (cmd.length() < 12)
        return;
    QChar mode = cmd.at(cmd.length() - 1);
    int newValue = (mode == 'A') ? 1 : 0;
    if (newValue != m_autoRefLevel) {
        m_autoRefLevel = newValue;
        emit autoRefLevelChanged(m_autoRefLevel > 0);
    }
}

void RadioState::handleDisplayNB(const QString &cmd) {
    if (cmd.length() <= 4)
        return;
    bool ok;
    int mode = cmd.mid(4).toInt(&ok);
    if (ok && mode >= 0 && mode <= 2 && mode != m_ddcNbMode) {
        m_ddcNbMode = mode;
        emit ddcNbModeChanged(m_ddcNbMode);
    }
}

void RadioState::handleDisplayNBL(const QString &cmd) {
    if (cmd.length() <= 5)
        return;
    bool ok;
    int level = cmd.mid(5).toInt(&ok);
    if (ok && level >= 0 && level <= 14 && level != m_ddcNbLevel) {
        m_ddcNbLevel = level;
        emit ddcNbLevelChanged(m_ddcNbLevel);
    }
}
