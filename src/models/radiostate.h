#ifndef RADIOSTATE_H
#define RADIOSTATE_H

#include <QMap>
#include <QObject>
#include <QString>
#include <QVector>
#include <functional>

class RadioState : public QObject {
    Q_OBJECT

public:
    enum Mode { Unknown = 0, LSB = 1, USB = 2, CW = 3, FM = 4, AM = 5, DATA = 6, CW_R = 7, DATA_R = 9 };
    Q_ENUM(Mode)

    enum AGCSpeed { AGC_Off = 0, AGC_Slow = 1, AGC_Fast = 2 };
    Q_ENUM(AGCSpeed)

    explicit RadioState(QObject *parent = nullptr);

    // Reset all state to initial values (used on disconnect for clean reconnect)
    void reset();

    // Parse a CAT command response and update state
    void parseCATCommand(const QString &command);

    // Frequency and VFO
    quint64 frequency() const { return m_frequency; }
    quint64 vfoA() const { return m_vfoA; }
    quint64 vfoB() const { return m_vfoB; }
    int tuningStep() const { return m_tuningStep; }
    int tuningStepB() const { return m_tuningStepB; }

    // Mode and filter
    Mode mode() const { return m_mode; }
    Mode modeB() const { return m_modeB; }
    QString modeString() const;
    int filterBandwidth() const { return m_filterBandwidth; }
    int filterBandwidthB() const { return m_filterBandwidthB; }
    int filterPosition() const { return m_filterPosition; }
    int filterPositionB() const { return m_filterPositionB; }
    int ifShift() const { return m_ifShift; }
    int shiftHz() const { return m_ifShift * 10; } // Convert raw IS value to Hz (IS0050 = 500 Hz)
    int ifShiftB() const { return m_ifShiftB; }
    int shiftBHz() const { return m_ifShiftB * 10; } // Sub RX IF shift in Hz
    int cwPitch() const { return m_cwPitch; }
    int keyerSpeed() const { return m_keyerSpeed; }

    // Keyer Paddle settings (KP command)
    QChar iambicMode() const { return m_iambicMode; }               // A or B
    QChar paddleOrientation() const { return m_paddleOrientation; } // N(ormal) or R(everse)
    int keyingWeight() const { return m_keyingWeight; }             // 090-125 (ratio * 100)

    // Power and levels
    double rfPower() const { return m_rfPower; }
    bool isQrpMode() const { return m_isQrpMode; }
    int micGain() const { return m_micGain; }
    int compression() const { return m_compression; }
    int rfGain() const { return m_rfGain; }
    int squelchLevel() const { return m_squelchLevel; }
    int rfGainB() const { return m_rfGainB; }
    int squelchLevelB() const { return m_squelchLevelB; }

    // Optimistic setters for scroll wheel updates (radio doesn't echo these commands)
    void setKeyerSpeed(int wpm);
    void setCwPitch(int pitchHz);
    void setRfPower(double watts);
    void setFilterBandwidth(int bwHz);
    void setIfShift(int shift);
    void setFilterBandwidthB(int bwHz);
    void setIfShiftB(int shift);
    void setRfGain(int gain);
    void setSquelchLevel(int level);
    void setRfGainB(int gain);
    void setSquelchLevelB(int level);
    void setMicGain(int gain);
    void setCompression(int level);

    // Optimistic setters for keyer paddle (KP command)
    void setIambicMode(QChar mode);
    void setPaddleOrientation(QChar orientation);
    void setKeyingWeight(int weight);

    // Optimistic setters for NB/NR (radio doesn't echo these commands)
    void setNoiseBlankerLevel(int level);
    void setNoiseBlankerLevelB(int level);
    void setNoiseBlankerFilter(int filter);
    void setNoiseBlankerFilterB(int filter);
    void setNoiseReductionLevel(int level);
    void setNoiseReductionLevelB(int level);

    // Meters
    double sMeter() const { return m_sMeter; }
    double sMeterB() const { return m_sMeterB; }
    int powerMeter() const { return m_powerMeter; }
    double swrMeter() const { return m_swrMeter; }

    // TX Meter data (TM command)
    int alcMeter() const { return m_alcMeter; }
    int compressionDb() const { return m_compressionDb; }
    double forwardPower() const { return m_forwardPower; }

    // Power supply info (SIFP command)
    double supplyVoltage() const { return m_supplyVoltage; }
    double supplyCurrent() const { return m_supplyCurrent; }

    // Control states
    bool isTransmitting() const { return m_isTransmitting; }
    bool subReceiverEnabled() const { return m_subReceiverEnabled; }
    bool diversityEnabled() const { return m_diversityEnabled; }
    bool splitEnabled() const { return m_splitEnabled; }

    // Processing - Main RX
    int noiseBlankerLevel() const { return m_noiseBlankerLevel; }
    bool noiseBlankerEnabled() const { return m_noiseBlankerEnabled; }
    int noiseBlankerFilterWidth() const { return m_noiseBlankerFilterWidth; } // 0=NONE, 1=NARROW, 2=WIDE
    int noiseReductionLevel() const { return m_noiseReductionLevel; }
    bool noiseReductionEnabled() const { return m_noiseReductionEnabled; }

    // Notch filter - Main RX
    bool autoNotchEnabled() const { return m_autoNotchEnabled; }
    bool manualNotchEnabled() const { return m_manualNotchEnabled; }
    int manualNotchPitch() const { return m_manualNotchPitch; }

    // Notch filter - Sub RX
    bool autoNotchEnabledB() const { return m_autoNotchEnabledB; }
    bool manualNotchEnabledB() const { return m_manualNotchEnabledB; }
    int manualNotchPitchB() const { return m_manualNotchPitchB; }

    // Optimistic setters for notch pitch (radio doesn't echo these commands)
    void setManualNotchPitch(int pitch);
    void setManualNotchPitchB(int pitch);

    int preamp() const { return m_preamp; }
    bool preampEnabled() const { return m_preampEnabled; }
    int attenuatorLevel() const { return m_attenuatorLevel; }
    bool attenuatorEnabled() const { return m_attenuatorEnabled; }
    AGCSpeed agcSpeed() const { return m_agcSpeed; }

    // Processing - Sub RX
    int noiseBlankerLevelB() const { return m_noiseBlankerLevelB; }
    bool noiseBlankerEnabledB() const { return m_noiseBlankerEnabledB; }
    int noiseBlankerFilterWidthB() const { return m_noiseBlankerFilterWidthB; } // 0=NONE, 1=NARROW, 2=WIDE
    int noiseReductionLevelB() const { return m_noiseReductionLevelB; }
    bool noiseReductionEnabledB() const { return m_noiseReductionEnabledB; }
    int preampB() const { return m_preampB; }
    bool preampEnabledB() const { return m_preampEnabledB; }
    int attenuatorLevelB() const { return m_attenuatorLevelB; }
    bool attenuatorEnabledB() const { return m_attenuatorEnabledB; }
    AGCSpeed agcSpeedB() const { return m_agcSpeedB; }

    // Radio info
    QString radioID() const { return m_radioID; }
    QString radioModel() const { return m_radioModel; }
    QString optionModules() const { return m_optionModules; }
    QMap<QString, QString> firmwareVersions() const { return m_firmwareVersions; }

    // Antenna
    int txAntenna() const { return m_selectedAntenna; }
    int rxAntennaMain() const { return m_receiveAntenna; }
    int rxAntennaSub() const { return m_receiveAntennaSub; }
    QString antennaName(int index) const { return m_antennaNames.value(index, QString("ANT%1").arg(index)); }
    QString txAntennaName() const { return antennaName(m_selectedAntenna); }
    QString rxAntennaMainName() const { return antennaName(m_receiveAntenna); }
    QString rxAntennaSubName() const { return antennaName(m_receiveAntennaSub); }

    // RIT/XIT
    bool ritEnabled() const { return m_ritEnabled; }
    bool xitEnabled() const { return m_xitEnabled; }
    int ritXitOffset() const { return m_ritXitOffset; }
    bool ritEnabledB() const { return m_ritEnabledB; }
    int ritXitOffsetB() const { return m_ritXitOffsetB; }

    // Message bank
    int messageBank() const { return m_messageBank; }

    // VOX
    bool voxCW() const { return m_voxCW; }
    bool voxVoice() const { return m_voxVoice; }
    bool voxData() const { return m_voxData; }
    bool voxEnabled() const { return m_voxCW || m_voxVoice || m_voxData; }

    // QSK (full break-in)
    bool qskEnabled() const { return m_qskEnabled; }

    // TEST mode (TX test)
    bool testMode() const { return m_testMode; }

    // ATU mode (0=not installed, 1=bypass, 2=auto)
    int atuMode() const { return m_atuMode; }

    // B SET (Target B) - controls whether feature menu commands target Sub RX
    // State is tracked internally (toggled when SW44 is sent)
    bool bSetEnabled() const { return m_bSetEnabled; }
    void setBSetEnabled(bool enabled) {
        if (enabled != m_bSetEnabled) {
            m_bSetEnabled = enabled;
            emit bSetChanged(m_bSetEnabled);
        }
    }
    void toggleBSet() { setBSetEnabled(!m_bSetEnabled); }

    // Audio Effects (FX command)
    int afxMode() const { return m_afxMode; } // 0=off, 1=delay, 2=pitch-map

    // Audio Peak Filter (AP/AP$ commands, CW mode only)
    bool apfEnabled() const { return m_apfEnabled; }      // Main RX
    int apfBandwidth() const { return m_apfBandwidth; }   // Main RX: 0=30Hz, 1=50Hz, 2=150Hz
    bool apfEnabledB() const { return m_apfEnabledB; }    // Sub RX
    int apfBandwidthB() const { return m_apfBandwidthB; } // Sub RX: 0=30Hz, 1=50Hz, 2=150Hz

    // VFO Lock (LK/LK$ commands)
    bool lockA() const { return m_lockA; }
    bool lockB() const { return m_lockB; }

    // VFO Link (LN command)
    bool vfoLink() const { return m_vfoLink; }

    // Monitor Level (ML command) - sidetone/speech monitor
    // mode: 0=CW, 1=AF data, 2=voice
    int monitorLevelCW() const { return m_monitorLevelCW; }
    int monitorLevelData() const { return m_monitorLevelData; }
    int monitorLevelVoice() const { return m_monitorLevelVoice; }
    int monitorLevelForCurrentMode() const {
        switch (m_mode) {
        case CW:
        case CW_R:
            return m_monitorLevelCW;
        case DATA:
        case DATA_R:
            return m_monitorLevelData;
        default: // LSB, USB, AM, FM = Voice modes
            return m_monitorLevelVoice;
        }
    }
    // Returns the ML mode code (0/1/2) for the current operating mode
    int monitorModeCode() const {
        switch (m_mode) {
        case CW:
        case CW_R:
            return 0;
        case DATA:
        case DATA_R:
            return 1;
        default: // LSB, USB, AM, FM = Voice modes
            return 2;
        }
    }

    // Audio mix routing (MX command) - how main/sub maps to L/R when SUB is on
    int audioMixLeft() const { return m_audioMixLeft; }   // MixSource value for left output
    int audioMixRight() const { return m_audioMixRight; } // MixSource value for right output

    // Audio balance (BL command) - MAIN/SUB balance
    int balanceMode() const { return m_balanceMode; }     // 0=NOR, 1=BAL
    int balanceOffset() const { return m_balanceOffset; } // -50 to +50

    // Optimistic setter for balance (radio doesn't echo BL SET commands)
    void setBalance(int mode, int offset);

    // Optimistic setter for monitor level
    void setMonitorLevel(int mode, int level);

    // Returns VOX state for current operating mode
    bool voxForCurrentMode() const {
        switch (m_mode) {
        case CW:
        case CW_R:
            return m_voxCW;
        case DATA:
        case DATA_R:
            return m_voxData;
        default: // LSB, USB, AM, FM = Voice modes
            return m_voxVoice;
        }
    }

    // QSK/VOX Delay (in 10ms increments)
    int qskDelayCW() const { return m_qskDelayCW; }
    int qskDelayVoice() const { return m_qskDelayVoice; }
    int qskDelayData() const { return m_qskDelayData; }
    // Returns delay for current operating mode (in 10ms increments)
    int delayForCurrentMode() const {
        switch (m_mode) {
        case CW:
        case CW_R:
            return m_qskDelayCW;
        case DATA:
        case DATA_R:
            return m_qskDelayData;
        default: // LSB, USB, AM, FM = Voice modes
            return m_qskDelayVoice;
        }
    }

    // Optimistic setters for QSK/VOX delay (in 10ms increments, 0-255)
    void setDelayForCurrentMode(int delay) {
        delay = qBound(0, delay, 255);
        switch (m_mode) {
        case CW:
        case CW_R:
            if (m_qskDelayCW != delay) {
                m_qskDelayCW = delay;
                emit qskDelayChanged(delay);
            }
            break;
        case DATA:
        case DATA_R:
            if (m_qskDelayData != delay) {
                m_qskDelayData = delay;
                emit qskDelayChanged(delay);
            }
            break;
        default: // LSB, USB, AM, FM = Voice modes
            if (m_qskDelayVoice != delay) {
                m_qskDelayVoice = delay;
                emit qskDelayChanged(delay);
            }
            break;
        }
    }

    // Panadapter REF level (Main)
    int refLevel() const { return m_refLevel; }

    // Setter for optimistic ref level updates (Main RX)
    void setRefLevel(int level) {
        if (level != m_refLevel) {
            m_refLevel = level;
            emit refLevelChanged(m_refLevel);
        }
    }

    // Panadapter scale (Main, from #SCL command, 10-150)
    // Higher values = more compressed display (signals appear weaker)
    // Lower values = more expanded display (signals appear stronger)
    int scale() const { return m_scale; }

    // Setter for optimistic scale updates (UI updates immediately)
    void setScale(int scale) {
        if (scale >= 10 && scale <= 150 && scale != m_scale) {
            m_scale = scale;
            emit scaleChanged(m_scale);
        }
    }

    // Panadapter span (Main, from #SPN command, in Hz)
    int spanHz() const { return m_spanHz; }

    // Setter for optimistic span updates (UI updates immediately, like K4Mobile)
    void setSpanHz(int spanHz) {
        if (spanHz > 0 && spanHz != m_spanHz) {
            m_spanHz = spanHz;
            emit spanChanged(m_spanHz);
        }
    }

    // Panadapter REF level (Sub)
    int refLevelB() const { return m_refLevelB; }

    // Setter for optimistic ref level updates (Sub RX)
    void setRefLevelB(int level) {
        if (level != m_refLevelB) {
            m_refLevelB = level;
            emit refLevelBChanged(m_refLevelB);
        }
    }

    // Panadapter span (Sub, from #SPN$ command, in Hz)
    int spanHzB() const { return m_spanHzB; }

    // Setter for optimistic span updates (Sub RX)
    void setSpanHzB(int spanHz) {
        if (spanHz > 0 && spanHz != m_spanHzB) {
            m_spanHzB = spanHz;
            emit spanBChanged(m_spanHzB);
        }
    }

    // Mini-Pan enabled state (tracked via #MP / #MP$ CAT commands)
    bool miniPanAEnabled() const { return m_miniPanAEnabled; }
    bool miniPanBEnabled() const { return m_miniPanBEnabled; }

    // Mini-Pan state setters (called optimistically when sending CAT commands)
    void setMiniPanAEnabled(bool enabled);
    void setMiniPanBEnabled(bool enabled);

    // Waterfall height setters (for optimistic updates)
    void setWaterfallHeight(int percent);
    void setWaterfallHeightExt(int percent);

    // Averaging setter (for optimistic updates)
    void setAveraging(int value);

    // Display state (tracked via # prefixed display commands)
    // LCD/EXT getters - default to LCD for backwards compatibility
    int dualPanModeLcd() const { return m_dualPanModeLcd; }
    int dualPanModeExt() const { return m_dualPanModeExt; }
    int displayModeLcd() const { return m_displayModeLcd; }
    int displayModeExt() const { return m_displayModeExt; }
    int displayFps() const { return m_displayFps; }
    int waterfallColor() const { return m_waterfallColor; }
    int waterfallHeight() const { return m_waterfallHeight; }       // LCD: #WFHxx (0-100%)
    int waterfallHeightExt() const { return m_waterfallHeightExt; } // EXT: #HWFHxx (0-100%)
    int averaging() const { return m_averaging; }
    bool peakMode() const { return m_peakMode > 0; }
    int fixedTune() const { return m_fixedTune; }         // #FXT: 0=track, 1=fixed
    int fixedTuneMode() const { return m_fixedTuneMode; } // #FXA: 0-4
    bool freeze() const { return m_freeze > 0; }
    int vfoACursor() const { return m_vfoACursor; }
    int vfoBCursor() const { return m_vfoBCursor; }
    bool autoRefLevel() const { return m_autoRefLevel > 0; }
    int ddcNbMode() const { return m_ddcNbMode; }   // #NB$: 0=off, 1=on, 2=auto
    int ddcNbLevel() const { return m_ddcNbLevel; } // #NBL$: 0-14

    // Data sub-mode (DT command): 0=DATA-A, 1=AFSK-A, 2=FSK-D, 3=PSK-D
    int dataSubMode() const { return m_dataSubMode; }
    int dataSubModeB() const { return m_dataSubModeB; }

    // Data rate (DR command): 0=slower (RTTY45/PSK31), 1=faster (RTTY75/PSK63)
    int dataRate() const { return m_dataRate; }
    int dataRateB() const { return m_dataRateB; }

    // RX Graphic Equalizer (RE command) - 8 bands, -16 to +16 dB
    // Bands: 100, 200, 400, 800, 1200, 1600, 2400, 3200 Hz
    // Note: Main RX and Sub RX share the same EQ settings
    int rxEqBand(int index) const { return (index >= 0 && index < 8) ? m_rxEqBands[index] : 0; }
    QVector<int> rxEqBands() const { return QVector<int>(m_rxEqBands, m_rxEqBands + 8); }

    // Optimistic setter for RX EQ bands (radio doesn't echo)
    void setRxEqBand(int index, int dB);
    void setRxEqBands(const QVector<int> &bands);

    // TX Graphic Equalizer (TE command) - 8 bands, -16 to +16 dB
    // Bands: 100, 200, 400, 800, 1200, 1600, 2400, 3200 Hz
    int txEqBand(int index) const { return (index >= 0 && index < 8) ? m_txEqBands[index] : 0; }
    QVector<int> txEqBands() const { return QVector<int>(m_txEqBands, m_txEqBands + 8); }

    // Optimistic setter for TX EQ bands (radio doesn't echo)
    void setTxEqBand(int index, int dB);
    void setTxEqBands(const QVector<int> &bands);

    // Antenna Configuration Masks (ACM/ACS/ACT commands)
    // ACM - Main RX antenna access mask
    bool mainRxDisplayAll() const { return m_mainRxDisplayAll; }
    bool mainRxAntEnabled(int index) const { return (index >= 0 && index < 7) ? m_mainRxAntMask[index] : false; }
    QVector<bool> mainRxAntMask() const { return QVector<bool>(m_mainRxAntMask, m_mainRxAntMask + 7); }

    // ACS - Sub RX antenna access mask
    bool subRxDisplayAll() const { return m_subRxDisplayAll; }
    bool subRxAntEnabled(int index) const { return (index >= 0 && index < 7) ? m_subRxAntMask[index] : false; }
    QVector<bool> subRxAntMask() const { return QVector<bool>(m_subRxAntMask, m_subRxAntMask + 7); }

    // ACT - TX antenna access mask
    bool txDisplayAll() const { return m_txDisplayAll; }
    bool txAntEnabled(int index) const { return (index >= 0 && index < 3) ? m_txAntMask[index] : false; }
    QVector<bool> txAntMask() const { return QVector<bool>(m_txAntMask, m_txAntMask + 3); }

    // Optimistic setters for antenna config (radio doesn't echo)
    void setMainRxAntConfig(bool displayAll, const QVector<bool> &mask);
    void setSubRxAntConfig(bool displayAll, const QVector<bool> &mask);
    void setTxAntConfig(bool displayAll, const QVector<bool> &mask);

    // Line Out levels (LO command)
    int lineOutLeft() const { return m_lineOutLeft; }
    int lineOutRight() const { return m_lineOutRight; }
    bool lineOutRightEqualsLeft() const { return m_lineOutRightEqualsLeft; }

    // Optimistic setters for Line Out
    void setLineOutLeft(int level);
    void setLineOutRight(int level);
    void setLineOutRightEqualsLeft(bool enabled);

    // Line In levels and source (LI command)
    int lineInSoundCard() const { return m_lineInSoundCard; }
    int lineInJack() const { return m_lineInJack; }
    int lineInSource() const { return m_lineInSource; } // 0=SoundCard, 1=LineInJack

    // Optimistic setters for Line In
    void setLineInSoundCard(int level);
    void setLineInJack(int level);
    void setLineInSource(int source);

    // Mic Input (MI command) - 0=front, 1=rear, 2=line in, 3=front+line in, 4=rear+line in
    int micInput() const { return m_micInput; }

    // Mic Setup (MS command) - preamp, bias, buttons configuration
    int micFrontPreamp() const { return m_micFrontPreamp; }   // 0=0dB, 1=10dB, 2=20dB
    int micFrontBias() const { return m_micFrontBias; }       // 0=OFF, 1=ON
    int micFrontButtons() const { return m_micFrontButtons; } // 0=disabled, 1=UP/DN enabled
    int micRearPreamp() const { return m_micRearPreamp; }     // 0=0dB, 1=14dB
    int micRearBias() const { return m_micRearBias; }         // 0=OFF, 1=ON

    // VOX Gain (VG command) - per mode (0=voice, 1=data)
    int voxGainVoice() const { return m_voxGainVoice; } // 0-60
    int voxGainData() const { return m_voxGainData; }   // 0-60
    int voxGainForCurrentMode() const { return (m_mode == DATA || m_mode == DATA_R) ? m_voxGainData : m_voxGainVoice; }

    // Anti-VOX (VI command) - voice modes only
    int antiVox() const { return m_antiVox; } // 0-60

    // ESSB and SSB TX Bandwidth (ES command)
    bool essbEnabled() const { return m_essbEnabled; } // 0=SSB, 1=ESSB
    int ssbTxBw() const { return m_ssbTxBw; }          // 30-45 (3.0-4.5 kHz in 100Hz units)

    // Optimistic setters for VOX Gain/Anti-VOX/ESSB
    void setVoxGainVoice(int gain);
    void setVoxGainData(int gain);
    void setAntiVox(int level);
    void setEssbEnabled(bool enabled);
    void setSsbTxBw(int bw);

    // Optimistic setters for Mic Input/Setup
    void setMicInput(int input);
    void setMicFrontPreamp(int preamp);
    void setMicFrontBias(int bias);
    void setMicFrontButtons(int buttons);
    void setMicRearPreamp(int preamp);
    void setMicRearBias(int bias);

    // Text Decode (TD$ command) - Main RX
    int textDecodeMode() const { return m_textDecodeMode; }           // 0=off, 2-4=CW WPM ranges, 1=DATA/SSB on
    int textDecodeThreshold() const { return m_textDecodeThreshold; } // 0=AUTO, 1-9 (CW only)
    int textDecodeLines() const { return m_textDecodeLines; }         // 1-10 lines

    // Text Decode (TD$$ command) - Sub RX
    int textDecodeModeB() const { return m_textDecodeModeB; }
    int textDecodeThresholdB() const { return m_textDecodeThresholdB; }
    int textDecodeLinesB() const { return m_textDecodeLinesB; }

    // Optimistic setters for Text Decode
    void setTextDecodeMode(int mode);
    void setTextDecodeThreshold(int threshold);
    void setTextDecodeLines(int lines);
    void setTextDecodeModeB(int mode);
    void setTextDecodeThresholdB(int threshold);
    void setTextDecodeLinesB(int lines);

    // Optimistic setters for data sub-mode (radio doesn't echo DT SET commands)
    void setDataSubMode(int subMode);
    void setDataSubModeB(int subMode);

    // Optimistic setters for data rate
    void setDataRate(int rate);
    void setDataRateB(int rate);

    // Full mode string including data sub-mode (DATA-A, AFSK, FSK, PSK)
    QString modeStringFull() const;  // Main RX mode with sub-mode
    QString modeStringFullB() const; // Sub RX mode with sub-mode

    // Static helpers
    static Mode modeFromCode(int code);
    static QString modeToString(Mode mode);
    static QString dataSubModeToString(int subMode); // 0=DATA, 1=AFSK, 2=FSK, 3=PSK

signals:
    void frequencyChanged(quint64 freq);
    void frequencyBChanged(quint64 freq);
    void modeChanged(Mode mode);
    void modeBChanged(Mode mode);
    void filterBandwidthChanged(int bw);
    void filterBandwidthBChanged(int bw);
    void filterPositionChanged(int position);  // Filter position VFO A (1-3)
    void filterPositionBChanged(int position); // Filter position VFO B (1-3)
    void ifShiftChanged(int shiftHz);
    void ifShiftBChanged(int shiftHz);
    void cwPitchChanged(int pitchHz);
    void tuningStepChanged(int step);  // VFO A tuning rate (0-5)
    void tuningStepBChanged(int step); // VFO B tuning rate (0-5)
    void sMeterChanged(double value);
    void sMeterBChanged(double value);
    void powerMeterChanged(int watts);
    void transmitStateChanged(bool transmitting);
    void rfPowerChanged(double watts, bool isQrp);
    void supplyVoltageChanged(double volts);
    void supplyCurrentChanged(double amps);
    void swrChanged(double swr);
    void txMeterChanged(int alc, int compression, double fwdPower, double swr);
    void splitChanged(bool enabled);
    void subRxEnabledChanged(bool enabled); // Sub RX on/off (SB command)
    void diversityChanged(bool enabled);    // Diversity on/off (DV command)
    void antennaChanged(int txAnt, int rxAntMain, int rxAntSub);
    void antennaNameChanged(int index, const QString &name);
    void ritXitChanged(bool ritEnabled, bool xitEnabled, int offset);
    void ritXitBChanged(bool ritEnabled, int offset);
    void messageBankChanged(int bank);
    void processingChanged();         // NB, NR, PA, RA, GT changes for Main RX
    void processingChangedB();        // NB, NR, PA, RA, GT changes for Sub RX
    void refLevelChanged(int level);  // Panadapter reference level (#REF command)
    void scaleChanged(int scale);     // Panadapter scale (#SCL command, 10-150) - GLOBAL, applies to both
    void spanChanged(int spanHz);     // Panadapter span (#SPN command)
    void refLevelBChanged(int level); // Sub RX panadapter reference level (#REF$ command)
    void spanBChanged(int spanHz);    // Sub RX panadapter span (#SPN$ command)
    void keyerSpeedChanged(int wpm);  // CW keyer speed
    void keyerPaddleChanged(QChar iambic, QChar paddle, int weight); // KP keyer paddle settings
    void qskDelayChanged(int delay);                                 // QSK/VOX delay in 10ms increments
    void rfGainChanged(int gain);                                    // RF gain
    void squelchChanged(int level);                                  // Squelch level
    void rfGainBChanged(int gain);                                   // RF gain Sub RX
    void squelchBChanged(int level);                                 // Squelch Sub RX
    void micGainChanged(int gain);                                   // Mic gain (0-80)
    void compressionChanged(int level);                              // Speech compression (0-30, SSB only)
    void voxChanged(bool enabled);                                   // VOX state (any mode)
    void qskEnabledChanged(bool enabled);                            // QSK (full break-in) state
    void testModeChanged(bool enabled);                              // TX test mode state
    void atuModeChanged(int mode);                                   // ATU mode (1=bypass, 2=auto)
    void bSetChanged(bool enabled);                                  // B SET (Target B) state
    void notchChanged();                                             // Manual notch state/pitch changed (Main RX)
    void notchBChanged();                                            // Manual notch state/pitch changed (Sub RX)
    void miniPanAEnabledChanged(bool enabled);                       // Mini-Pan A state (#MP command)
    void miniPanBEnabledChanged(bool enabled);                       // Mini-Pan B state (#MP$ command)

    // Display state signals (separate LCD and EXT)
    void dualPanModeLcdChanged(int mode);        // #DPM: LCD 0=A, 1=B, 2=Dual
    void dualPanModeExtChanged(int mode);        // #HDPM: EXT 0=A, 1=B, 2=Dual
    void displayModeLcdChanged(int mode);        // #DSM: LCD 0=spectrum, 1=spectrum+waterfall
    void displayModeExtChanged(int mode);        // #HDSM: EXT 0=spectrum, 1=spectrum+waterfall
    void displayFpsChanged(int fps);             // #FPS: Display frame rate 12-30
    void waterfallColorChanged(int color);       // #WFC: 0-4
    void waterfallHeightChanged(int percent);    // #WFHxx: LCD 0-100%
    void waterfallHeightExtChanged(int percent); // #HWFHxx: EXT 0-100%
    void averagingChanged(int value);            // #AVG: 1-20
    void peakModeChanged(bool enabled);          // #PKM: 0/1
    void fixedTuneChanged(int fxt, int fxa);     // #FXT + #FXA combined
    void freezeChanged(bool enabled);            // #FRZ: 0/1
    void vfoACursorChanged(int mode);            // #VFA: 0-3
    void vfoBCursorChanged(int mode);            // #VFB: 0-3
    void autoRefLevelChanged(bool enabled);      // #AR: A/M (GLOBAL - affects both VFOs)
    void ddcNbModeChanged(int mode);             // #NB$: 0=off, 1=on, 2=auto
    void ddcNbLevelChanged(int level);           // #NBL$: 0-14
    void dataSubModeChanged(int subMode);        // DT: 0=DATA-A, 1=AFSK-A, 2=FSK-D, 3=PSK-D
    void dataSubModeBChanged(int subMode);       // DT$: Sub RX data sub-mode
    void dataRateChanged(int rate);              // DR: 0=slower (RTTY45/PSK31), 1=faster (RTTY75/PSK63)
    void dataRateBChanged(int rate);             // DR$: Sub RX data rate

    // Error/notification messages from K4 (ERxx: format)
    void errorNotificationReceived(int errorCode, const QString &message);

    // Audio effects and processing
    void afxModeChanged(int mode);                 // FX: 0=off, 1=delay, 2=pitch-map
    void apfChanged(bool enabled, int width);      // AP: Main RX APF (0=30Hz, 1=50Hz, 2=150Hz)
    void apfBChanged(bool enabled, int width);     // AP$: Sub RX APF (0=30Hz, 1=50Hz, 2=150Hz)
    void vfoLinkChanged(bool linked);              // LN: VFOs linked
    void lockAChanged(bool locked);                // LK: VFO A lock state
    void lockBChanged(bool locked);                // LK$: VFO B lock state
    void monitorLevelChanged(int mode, int level); // ML: Monitor level (0=CW, 1=Data, 2=Voice)
    void audioMixChanged(int left, int right);     // MX: Audio mix routing (MixSource values)
    void balanceChanged(int mode, int offset);     // BL: Balance (mode 0=NOR/1=BAL, offset -50 to +50)

    // RX Graphic Equalizer
    void rxEqChanged();                      // Any EQ band value changed
    void rxEqBandChanged(int index, int dB); // Specific band changed

    // TX Graphic Equalizer
    void txEqChanged();                      // Any EQ band value changed
    void txEqBandChanged(int index, int dB); // Specific band changed

    // Antenna Configuration Masks
    void mainRxAntCfgChanged(); // ACM command received/changed
    void subRxAntCfgChanged();  // ACS command received/changed
    void txAntCfgChanged();     // ACT command received/changed

    // Line Out
    void lineOutChanged(); // LO command - left/right level or mode changed

    // Line In
    void lineInChanged(); // LI command - sound card/line in jack level or source changed

    // Mic Input/Setup
    void micInputChanged(int input); // MI command - mic input source changed
    void micSetupChanged();          // MS command - mic config changed

    // VOX Gain/Anti-VOX/ESSB
    void voxGainChanged(int mode, int gain); // VG: mode 0=voice, 1=data
    void antiVoxChanged(int level);          // VI: anti-vox level
    void essbChanged(bool enabled, int bw);  // ES: ESSB state and bandwidth

    // Text Decode
    void textDecodeChanged();                                   // TD$ command - Main RX settings changed
    void textDecodeBChanged();                                  // TD$$ command - Sub RX settings changed
    void textBufferReceived(const QString &text, bool isSubRx); // TB$ decoded text

private:
    // Frequency and VFO
    quint64 m_frequency = 0;
    quint64 m_vfoA = 0;
    quint64 m_vfoB = 0;
    int m_tuningStep = -1;
    int m_tuningStepB = -1;

    // Mode and filter
    Mode m_mode = Unknown;
    Mode m_modeB = Unknown;
    int m_filterBandwidth = -1;
    int m_filterBandwidthB = -1;
    int m_filterPosition = -1;
    int m_filterPositionB = -1;
    int m_ifShift = -1;  // IF shift position (0-99, 50=centered) - init to -1 to ensure first emit
    int m_ifShiftB = -1; // Sub RX IF shift
    int m_cwPitch = -1;  // Init to -1 to ensure first emit

    // Power and levels
    double m_rfPower = -1.0; // Init to invalid to ensure first emit
    bool m_isQrpMode = false;
    int m_micGain = -1;        // Init to invalid to ensure first emit (0-80)
    int m_compression = -1;    // Init to invalid to ensure first emit (0-30, SSB only)
    int m_rfGain = -999;       // Init to invalid to ensure first emit
    int m_squelchLevel = -1;   // Init to invalid to ensure first emit
    int m_rfGainB = -999;      // Sub RX RF gain
    int m_squelchLevelB = -1;  // Sub RX squelch
    int m_keyerSpeed = -1;     // WPM - init to -1 to ensure first emit
    QChar m_iambicMode;        // A or B (null = not yet received)
    QChar m_paddleOrientation; // N(ormal) or R(everse) (null = not yet received)
    int m_keyingWeight = -1;   // 090-125 (ratio * 100), -1 = not yet received

    // Meters
    double m_sMeter = 0.0;
    double m_sMeterB = 0.0;
    int m_powerMeter = 0;
    double m_swrMeter = 1.0;
    int m_alcMeter = 0;
    int m_compressionDb = 0;
    double m_forwardPower = 0.0;

    // Power supply (from SIFP)
    double m_supplyVoltage = 0.0;
    double m_supplyCurrent = 0.0;

    // Control states
    bool m_isTransmitting = false;
    bool m_subReceiverEnabled = false;
    bool m_diversityEnabled = false;
    bool m_splitEnabled = false;

    // Processing - Main RX
    int m_noiseBlankerLevel = 0;
    bool m_noiseBlankerEnabled = false;
    int m_noiseBlankerFilterWidth = 0;
    int m_noiseReductionLevel = 0;
    bool m_noiseReductionEnabled = false;

    // Notch filter
    bool m_autoNotchEnabled = false;
    bool m_manualNotchEnabled = false;
    int m_manualNotchPitch = 1000; // 150-5000 Hz, default 1000

    // Notch filter - Sub RX
    bool m_autoNotchEnabledB = false;
    bool m_manualNotchEnabledB = false;
    int m_manualNotchPitchB = 1000; // 150-5000 Hz, default 1000

    int m_preamp = 0;
    bool m_preampEnabled = false;
    int m_attenuatorLevel = 0;
    bool m_attenuatorEnabled = false;
    AGCSpeed m_agcSpeed = AGC_Slow;

    // Processing - Sub RX
    int m_noiseBlankerLevelB = 0;
    bool m_noiseBlankerEnabledB = false;
    int m_noiseBlankerFilterWidthB = 0; // 0=NONE, 1=NARROW, 2=WIDE
    int m_noiseReductionLevelB = 0;
    bool m_noiseReductionEnabledB = false;
    int m_preampB = 0;
    bool m_preampEnabledB = false;
    int m_attenuatorLevelB = 0;
    bool m_attenuatorEnabledB = false;
    AGCSpeed m_agcSpeedB = AGC_Slow;

    // Antenna
    int m_selectedAntenna = -1;
    int m_receiveAntenna = -1;
    int m_receiveAntennaSub = -1;
    int m_atuMode = -1;
    QMap<int, QString> m_antennaNames;

    // RIT/XIT
    bool m_ritEnabled = false;
    bool m_xitEnabled = false;
    int m_ritXitOffset = 0;
    bool m_ritEnabledB = false;
    int m_ritXitOffsetB = 0;

    // Message bank
    int m_messageBank = -1;

    // VOX
    bool m_voxCW = false;
    bool m_voxVoice = false;
    bool m_voxData = false;

    // QSK (full break-in) - extracted from SD command x flag
    bool m_qskEnabled = false;

    // TEST mode (TX test)
    bool m_testMode = false;

    // B SET (Target B) - feature menu commands target Sub RX when enabled
    bool m_bSetEnabled = false;

    // QSK/VOX Delay per mode (in 10ms increments)
    int m_qskDelayCW = -1;
    int m_qskDelayVoice = -1;
    int m_qskDelayData = -1;

    // Audio effects (FX command)
    int m_afxMode = 0; // 0=off, 1=delay, 2=pitch-map

    // Audio Peak Filter (AP/AP$ commands, CW mode only)
    bool m_apfEnabled = false;  // Main RX
    int m_apfBandwidth = 0;     // Main RX: 0=30Hz, 1=50Hz, 2=150Hz
    bool m_apfEnabledB = false; // Sub RX
    int m_apfBandwidthB = 0;    // Sub RX: 0=30Hz, 1=50Hz, 2=150Hz

    // VFO Link (LN command)
    bool m_vfoLink = false;

    // VFO Lock (LK/LK$ commands)
    bool m_lockA = false;
    bool m_lockB = false;

    // Audio mix routing (MX command) - default A.B (main left, sub right)
    // Values are MixSource enum: 0=A(main), 1=B(sub), 2=AB(main+sub), 3=-A(neg main)
    int m_audioMixLeft = -1;
    int m_audioMixRight = -1;

    // Audio balance (BL command) - MAIN/SUB balance
    int m_balanceMode = -1;
    int m_balanceOffset = -99; // sentinel outside valid range (-50 to +50)

    // Monitor Level (ML command) - sidetone/speech monitor (0-100)
    int m_monitorLevelCW = -1;    // CW sidetone level
    int m_monitorLevelData = -1;  // Data monitor level
    int m_monitorLevelVoice = -1; // Voice monitor level

    // Panadapter REF level (Main)
    int m_refLevel = -110; // Default -110 dBm

    // Panadapter scale (GLOBAL, from #SCL command, applies to both panadapters)
    int m_scale = -1; // 10-150, init to -1 to ensure first emit

    // Panadapter span (Main, from #SPN command)
    int m_spanHz = 0; // Init to 0 to ensure first emit

    // Panadapter REF level (Sub)
    int m_refLevelB = -110; // Default -110 dBm

    // Panadapter span (Sub, from #SPN$ command)
    int m_spanHzB = 0; // Init to 0 to ensure first emit

    // Radio info
    QString m_radioID;
    QString m_radioModel;
    QString m_optionModules;
    QMap<QString, QString> m_firmwareVersions;

    // Mini-Pan enabled state (tracked via #MP / #MP$ CAT commands)
    bool m_miniPanAEnabled = false;
    bool m_miniPanBEnabled = false;

    // Display state (from # prefixed display commands)
    // Initial values are -1 to ensure first update triggers signal
    // Separate LCD (#DPM, #DSM) and EXT (#HDPM, #HDSM) state
    int m_dualPanModeLcd = -1;     // #DPM: LCD 0=A, 1=B, 2=Dual
    int m_dualPanModeExt = -1;     // #HDPM: EXT 0=A, 1=B, 2=Dual
    int m_displayModeLcd = -1;     // #DSM: LCD 0=spectrum, 1=spectrum+waterfall
    int m_displayModeExt = -1;     // #HDSM: EXT 0=spectrum, 1=spectrum+waterfall
    int m_displayFps = 30;         // #FPS: Display frame rate 12-30 (default 30)
    int m_waterfallColor = -1;     // #WFC: 0-4
    int m_waterfallHeight = 50;    // #WFHxx: LCD waterfall height 0-100% (default 50%)
    int m_waterfallHeightExt = 50; // #HWFHxx: EXT waterfall height 0-100% (default 50%)
    int m_averaging = -1;          // #AVG: 1-20
    int m_peakMode = -1;           // #PKM: 0/1 (int for -1 init)
    int m_fixedTune = -1;          // #FXT: 0=track, 1=fixed
    int m_fixedTuneMode = -1;      // #FXA: 0-4
    int m_freeze = -1;             // #FRZ: 0/1 (int for -1 init)
    int m_vfoACursor = -1;         // #VFA: 0=OFF, 1=ON, 2=AUTO, 3=HIDE
    int m_vfoBCursor = -1;         // #VFB: 0-3
    int m_autoRefLevel = -1;       // #AR: A=auto, M=manual (GLOBAL - affects both VFOs)
    int m_ddcNbMode = -1;          // #NB$: 0=off, 1=on, 2=auto
    int m_ddcNbLevel = -1;         // #NBL$: 0-14

    // Data sub-mode (DT command): 0=DATA-A, 1=AFSK-A, 2=FSK-D, 3=PSK-D
    int m_dataSubMode = -1;  // Main RX
    int m_dataSubModeB = -1; // Sub RX

    // Data rate (DR command): 0=slower (RTTY45/PSK31), 1=faster (RTTY75/PSK63)
    int m_dataRate = -1;  // Main RX
    int m_dataRateB = -1; // Sub RX

    // Timestamps for optimistic update cooldown (ignore echoes briefly after sending)
    qint64 m_dataSubModeOptimisticTime = 0;
    qint64 m_dataSubModeBOptimisticTime = 0;
    qint64 m_dataRateOptimisticTime = 0;
    qint64 m_dataRateBOptimisticTime = 0;

    // RX Graphic Equalizer (8 bands: 100, 200, 400, 800, 1200, 1600, 2400, 3200 Hz)
    // Range: -16 to +16 dB, init to 0 (flat)
    int m_rxEqBands[8] = {0, 0, 0, 0, 0, 0, 0, 0};

    // TX Graphic Equalizer (8 bands: 100, 200, 400, 800, 1200, 1600, 2400, 3200 Hz)
    // Range: -16 to +16 dB, init to 0 (flat)
    int m_txEqBands[8] = {0, 0, 0, 0, 0, 0, 0, 0};

    // Antenna Configuration Masks (ACM/ACS/ACT commands)
    // ACM - Main RX: z=displayAll, a-g = ANT1, ANT2, ANT3, RX1, RX2, =TX ANT, =OPP TX ANT
    bool m_mainRxDisplayAll = true;
    bool m_mainRxAntMask[7] = {false, false, false, false, false, false, false};

    // ACS - Sub RX: same format as ACM
    bool m_subRxDisplayAll = true;
    bool m_subRxAntMask[7] = {false, false, false, false, false, false, false};

    // ACT - TX: z=displayAll, a-c = TX ANT1, TX ANT2, TX ANT3
    bool m_txDisplayAll = true;
    bool m_txAntMask[3] = {false, false, false};

    // Line Out levels (LO command)
    int m_lineOutLeft = -1;  // 0-40, init to -1 to ensure first emit
    int m_lineOutRight = -1; // 0-40
    bool m_lineOutRightEqualsLeft = false;

    // Line In levels and source (LI command)
    int m_lineInSoundCard = -1; // 0-250, init to -1 to ensure first emit
    int m_lineInJack = -1;      // 0-250
    int m_lineInSource = -1;    // 0=SoundCard, 1=LineInJack

    // Mic Input (MI command)
    int m_micInput = -1; // 0=front, 1=rear, 2=line in, 3=front+line, 4=rear+line

    // Mic Setup (MS command)
    int m_micFrontPreamp = -1;  // 0=0dB, 1=10dB, 2=20dB
    int m_micFrontBias = -1;    // 0=OFF, 1=ON
    int m_micFrontButtons = -1; // 0=disabled, 1=UP/DN enabled
    int m_micRearPreamp = -1;   // 0=0dB, 1=14dB
    int m_micRearBias = -1;     // 0=OFF, 1=ON

    // VOX Gain (VG command)
    int m_voxGainVoice = -1; // 0-60
    int m_voxGainData = -1;  // 0-60

    // Anti-VOX (VI command)
    int m_antiVox = -1; // 0-60

    // ESSB (ES command)
    bool m_essbEnabled = false; // 0=SSB, 1=ESSB
    int m_ssbTxBw = -1;         // 30-45 (3.0-4.5 kHz in 100Hz units)

    // Text Decode (TD$ command) - Main RX
    int m_textDecodeMode = -1;      // 0=off, 2=8-45WPM, 3=8-60WPM, 4=8-90WPM, 1=DATA/SSB on
    int m_textDecodeThreshold = -1; // 0=AUTO, 1-9 (CW only)
    int m_textDecodeLines = -1;     // 1-10 lines

    // Text Decode (TD$$ command) - Sub RX
    int m_textDecodeModeB = -1;
    int m_textDecodeThresholdB = -1;
    int m_textDecodeLinesB = -1;

    // =========================================================================
    // Command Handler Registry
    // =========================================================================
    // Handler function type: takes command string (already trimmed, no trailing ;)
    using CommandHandler = std::function<void(const QString &)>;

    // Registry entry: prefix to match and handler function
    struct CommandEntry {
        QString prefix;
        CommandHandler handler;
    };

    // Sorted list of command handlers (longest prefix first for correct matching)
    QVector<CommandEntry> m_commandHandlers;

    // Initialize command handler registry (called from constructor)
    void registerCommandHandlers();

    // =========================================================================
    // A/B Deduplication Helpers
    // =========================================================================

    // Parse int from cmd at prefixLen, store in member if changed & in [min,max], emit signal
    void handleIntPair(const QString &cmd, int prefixLen, int &member, int min, int max,
                       void (RadioState::*signal)(int));

    // Parse bool from cmd character at charPos, store in member if changed, emit void signal
    void handleBoolPair(const QString &cmd, int charPos, bool &member, void (RadioState::*signal)());

    // Parse bool from cmd character at charPos, store in member if changed, emit signal(bool)
    void handleBoolPairVal(const QString &cmd, int charPos, bool &member, void (RadioState::*signal)(bool));

    // =========================================================================
    // Individual Command Handlers (grouped by function)
    // =========================================================================

    // VFO/Frequency commands
    void handleFA(const QString &cmd); // VFO A frequency
    void handleFB(const QString &cmd); // VFO B frequency
    void handleFT(const QString &cmd); // Split TX/RX

    // Mode commands
    void handleMD(const QString &cmd);    // Mode VFO A
    void handleMDSub(const QString &cmd); // Mode VFO B (MD$)

    // Bandwidth/Filter commands
    void handleBW(const QString &cmd);    // Bandwidth VFO A
    void handleBWSub(const QString &cmd); // Bandwidth VFO B (BW$)
    void handleIS(const QString &cmd);    // IF Shift VFO A
    void handleISSub(const QString &cmd); // IF Shift VFO B (IS$)
    void handleCW(const QString &cmd);    // CW pitch
    // FP/FP$, BW/BW$, IS/IS$ — handled inline via handleIntPair in registerCommandHandlers()

    // Gain/Level commands
    // RG/RG$, SQ/SQ$ — handled inline via handleIntPair in registerCommandHandlers()
    void handleMG(const QString &cmd); // Mic Gain
    void handleCP(const QString &cmd); // Compression
    void handleML(const QString &cmd); // Monitor Level
    void handlePC(const QString &cmd); // Power Control
    void handleKS(const QString &cmd); // Keyer Speed
    void handleKP(const QString &cmd); // Keyer Paddle (iambic/paddle/weight)

    // Meter commands
    void handleSM(const QString &cmd);    // S-Meter Main (complex conversion)
    void handleSMSub(const QString &cmd); // S-Meter Sub (SM$)
    void handlePO(const QString &cmd);    // Power Output
    void handleTM(const QString &cmd);    // TX Meter

    // TX/RX state
    void handleTX(const QString &cmd); // Transmit
    void handleRX(const QString &cmd); // Receive

    // Processing commands (NB, NR, PA, RA, GT, NA, NM)
    void handleNB(const QString &cmd);    // Noise Blanker Main
    void handleNBSub(const QString &cmd); // Noise Blanker Sub (NB$)
    void handleNR(const QString &cmd);    // Noise Reduction Main
    void handleNRSub(const QString &cmd); // Noise Reduction Sub (NR$)
    void handlePA(const QString &cmd);    // Preamp Main
    void handlePASub(const QString &cmd); // Preamp Sub (PA$)
    void handleRA(const QString &cmd);    // Attenuator Main
    void handleRASub(const QString &cmd); // Attenuator Sub (RA$)
    void handleGT(const QString &cmd);    // AGC Speed Main
    void handleGTSub(const QString &cmd); // AGC Speed Sub (GT$)
    // NA/NA$, NM/NM$ — NA handled inline via handleBoolPair; NM stays (complex)
    void handleNM(const QString &cmd);    // Manual Notch Main
    void handleNMSub(const QString &cmd); // Manual Notch Sub (NM$)

    // Balance and audio mix commands
    void handleBL(const QString &cmd); // Audio Balance (BL)
    void handleMX(const QString &cmd); // Audio Mix routing (MX)

    // Audio/Effects commands
    void handleFX(const QString &cmd);    // Audio Effects
    void handleAP(const QString &cmd);    // Audio Peak Filter Main (complex — multi-field)
    void handleAPSub(const QString &cmd); // Audio Peak Filter Sub (AP$)

    // VFO control commands
    void handleLN(const QString &cmd); // VFO Link
    // LK/LK$ — handled inline via handleBoolPair
    // VT/VT$ — handled inline via handleIntPair

    // VOX commands
    void handleVX(const QString &cmd); // VOX enable
    void handleVG(const QString &cmd); // VOX Gain
    void handleVI(const QString &cmd); // Anti-VOX

    // Audio I/O commands
    void handleLO(const QString &cmd); // Line Out
    void handleLI(const QString &cmd); // Line In
    void handleMI(const QString &cmd); // Mic Input
    void handleMS(const QString &cmd); // Mic Setup
    void handleES(const QString &cmd); // ESSB

    // QSK/Delay commands
    void handleSD(const QString &cmd); // QSK/VOX Delay

    // Control state commands
    void handleSB(const QString &cmd); // Sub Receiver
    void handleDV(const QString &cmd); // Diversity
    void handleTS(const QString &cmd); // Test Mode
    void handleBS(const QString &cmd); // B SET

    // Antenna commands
    void handleAN(const QString &cmd); // TX Antenna
    // AR/AR$ — handled inline via handleIntPair
    void handleAT(const QString &cmd);  // ATU Mode
    void handleACN(const QString &cmd); // Antenna Names
    void handleACM(const QString &cmd); // Main RX Antenna Config
    void handleACS(const QString &cmd); // Sub RX Antenna Config
    void handleACT(const QString &cmd); // TX Antenna Config

    // RIT/XIT commands
    void handleRT(const QString &cmd); // RIT
    void handleXT(const QString &cmd); // XIT
    void handleRO(const QString &cmd); // RIT/XIT Offset
    // RO$ — handled inline via lambda (same structure but different signal)
    // RT$ — handled inline via handleBoolPair

    // Text decode commands
    void handleTD(const QString &cmd);    // Text Decode Main
    void handleTDSub(const QString &cmd); // Text Decode Sub (TD$)
    void handleTB(const QString &cmd);    // Text Buffer Main
    void handleTBSub(const QString &cmd); // Text Buffer Sub (TB$)

    // Data mode commands
    void handleDT(const QString &cmd);    // Data Sub-Mode Main
    void handleDTSub(const QString &cmd); // Data Sub-Mode Sub (DT$)
    void handleDR(const QString &cmd);    // Data Rate Main
    void handleDRSub(const QString &cmd); // Data Rate Sub (DR$)

    // Equalizer commands
    void handleRE(const QString &cmd); // RX EQ
    void handleTE(const QString &cmd); // TX EQ

    // Radio info commands
    void handleID(const QString &cmd);   // Radio ID
    void handleOM(const QString &cmd);   // Option Modules
    void handleRV(const QString &cmd);   // Firmware Version (RV.)
    void handleSIFP(const QString &cmd); // Power Supply Info
    void handleSIRC(const QString &cmd); // SIRC status
    void handleMN(const QString &cmd);   // Message Bank
    void handleER(const QString &cmd);   // Error notifications

    // Display commands (# prefix)
    // #REF/#REF$ — handled inline via handleIntPair
    void handleDisplaySCL(const QString &cmd); // #SCL - Scale
    // #SPN/#SPN$ — handled inline via handleIntPair
    // #MP/#MP$ — handled inline via handleBoolPair
    void handleDisplayDPM(const QString &cmd);  // #DPM - Dual Pan Mode LCD
    void handleDisplayHDPM(const QString &cmd); // #HDPM - Dual Pan Mode EXT
    void handleDisplayDSM(const QString &cmd);  // #DSM - Display Mode LCD
    void handleDisplayHDSM(const QString &cmd); // #HDSM - Display Mode EXT
    void handleDisplayFPS(const QString &cmd);  // #FPS - Frame Rate
    void handleDisplayWFC(const QString &cmd);  // #WFC - Waterfall Color
    void handleDisplayWFH(const QString &cmd);  // #WFH - Waterfall Height LCD
    void handleDisplayHWFH(const QString &cmd); // #HWFH - Waterfall Height EXT
    void handleDisplayAVG(const QString &cmd);  // #AVG - Averaging
    void handleDisplayPKM(const QString &cmd);  // #PKM - Peak Mode
    void handleDisplayFXT(const QString &cmd);  // #FXT - Fixed Tune
    void handleDisplayFXA(const QString &cmd);  // #FXA - Fixed Tune Mode
    void handleDisplayFRZ(const QString &cmd);  // #FRZ - Freeze
    void handleDisplayVFA(const QString &cmd);  // #VFA - VFO A Cursor
    void handleDisplayVFB(const QString &cmd);  // #VFB - VFO B Cursor
    void handleDisplayAR(const QString &cmd);   // #AR - Auto Ref Level
    void handleDisplayNB(const QString &cmd);   // #NB$ - DDC NB Mode
    void handleDisplayNBL(const QString &cmd);  // #NBL$ - DDC NB Level
};

#endif // RADIOSTATE_H
