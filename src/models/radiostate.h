#ifndef RADIOSTATE_H
#define RADIOSTATE_H

#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

#include "radiostate/antennastate.h"
#include "radiostate/audioeffectsstate.h"
#include "radiostate/datacontrolstate.h"
#include "radiostate/frequencyvfostate.h"
#include "radiostate/levelsstate.h"
#include "radiostate/modefilterstate.h"
#include "radiostate/powerstate.h"
#include "radiostate/processingstate.h"
#include "radiostate/qskcontrolstate.h"
#include "radiostate/rxtxmeterstate.h"
#include "radiostate/spectrumdisplaystate.h"
#include "radiostate/textdecodestate.h"
#include "radiostate/xvtrbandstate.h"

/**
 * @brief Central K4 state hub. Parses inbound CAT responses, stores every visible radio property,
 *        and emits fine-grained `*Changed` signals for the UI.
 *
 * Threading contract:
 * - `parseCATCommand()` is main-thread-only; this is enforced by `Q_ASSERT` in the implementation.
 *   Callers on other threads must marshal via `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`.
 *
 * `$` suffix convention (K4 protocol):
 * - Commands ending in `$` apply to VFO B / sub-receiver (e.g., `MD$`, `BW$`, `RO$`, `RT$`).
 * - Parsed-prefix order in the handler registry puts `$` variants before the base prefix so that
 *   `RO$` is not mis-matched as `RO`. See the handler-registration site in radiostate.cpp.
 *
 * `RO` vs `RO$` routing (see `memory/MEMORY.md` → "K4 RIT/XIT Offset Registers"):
 * - No split, RIT or XIT: offset lives in `RO` (VFO A).
 * - Split + XIT: offset lives in `RO$` (VFO B — the TX VFO when split).
 * - BSET + RIT: offset lives in `RO$`.
 *
 * New state fields: add a member + signal here, parse in `parseCATCommand()`, then add a test in
 * `tests/test_radiostate.cpp` (CONVENTIONS.md rule 6).
 */
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

    /**
     * @brief Parse a single CAT response from the K4 and emit `*Changed` signals for any fields
     *        whose value actually transitioned.
     *
     * Must be called on the main (GUI) thread — enforced by `Q_ASSERT(QThread::currentThread() ==
     * thread())`. Dispatches through the handler registry; the first prefix match wins. Idempotent
     * for unchanged values (no signal emitted if the new value equals the current one).
     */
    void parseCATCommand(const QString &command);

    // Frequency and VFO — backed by m_frequencyVfoState.
    quint64 frequency() const { return m_frequencyVfoState.frequency; }
    quint64 vfoA() const { return m_frequencyVfoState.vfoA; }
    quint64 vfoB() const { return m_frequencyVfoState.vfoB; }
    int tuningStep() const { return m_dataControlState.tuningStep; }
    int tuningStepB() const { return m_dataControlState.tuningStepB; }

    // Mode and filter — backed by m_modeFilterState.
    Mode mode() const { return static_cast<Mode>(m_modeFilterState.mode); }
    Mode modeB() const { return static_cast<Mode>(m_modeFilterState.modeB); }
    QString modeString() const;
    int filterBandwidth() const { return m_modeFilterState.filterBandwidth; }
    int filterBandwidthB() const { return m_modeFilterState.filterBandwidthB; }
    int filterPosition() const { return m_modeFilterState.filterPosition; }
    int filterPositionB() const { return m_modeFilterState.filterPositionB; }
    int ifShift() const { return m_modeFilterState.ifShift; }
    int shiftHz() const { return m_modeFilterState.ifShift * 10; }
    int ifShiftB() const { return m_modeFilterState.ifShiftB; }
    int shiftBHz() const { return m_modeFilterState.ifShiftB * 10; }
    int cwPitch() const { return m_modeFilterState.cwPitch; }
    int keyerSpeed() const { return m_modeFilterState.keyerSpeed; }

    // Keyer paddle (KP) — backed by m_modeFilterState.
    QChar iambicMode() const { return m_modeFilterState.iambicMode; }
    QChar paddleOrientation() const { return m_modeFilterState.paddleOrientation; }
    int keyingWeight() const { return m_modeFilterState.keyingWeight; }

    // Power and levels
    double rfPower() const { return m_levelsState.rfPower; }
    LevelsState::PowerRange powerRange() const { return m_levelsState.powerRange; }
    bool isQrpMode() const { return m_levelsState.powerRange == LevelsState::PowerRange::Qrp; }
    bool isXvtrPowerMode() const { return m_levelsState.powerRange == LevelsState::PowerRange::Xvtr; }
    int micGain() const { return m_levelsState.micGain; }
    int compression() const { return m_levelsState.compression; }
    int rfGain() const { return m_levelsState.rfGain; }
    int squelchLevel() const { return m_levelsState.squelchLevel; }
    int rfGainB() const { return m_levelsState.rfGainB; }
    int squelchLevelB() const { return m_levelsState.squelchLevelB; }

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
    void setSsnrLevel(int level);
    void setSsnrLevelB(int level);

    // Meters — backed by m_rxTxMeterState.
    double sMeter() const { return m_rxTxMeterState.sMeter; }
    double sMeterB() const { return m_rxTxMeterState.sMeterB; }

    double swrMeter() const { return m_rxTxMeterState.swrMeter; }

    // TX Meter data (TM command)
    int alcMeter() const { return m_rxTxMeterState.alcMeter; }
    int compressionDb() const { return m_rxTxMeterState.compressionDb; }
    double forwardPower() const { return m_rxTxMeterState.forwardPower; }

    // Power supply info (SIFP command)
    double supplyVoltage() const { return m_rxTxMeterState.supplyVoltage; }
    double supplyCurrent() const { return m_rxTxMeterState.supplyCurrent; }

    // PA drain current (SIRF LM field, parsed from centi-amps to amps)
    double paDrainCurrent() const { return m_rxTxMeterState.paDrainCurrent; }

    // Control states
    bool isTransmitting() const { return m_rxTxMeterState.isTransmitting; }
    bool subReceiverEnabled() const { return m_rxTxMeterState.subReceiverEnabled == 1; }
    bool diversityEnabled() const { return m_rxTxMeterState.diversityEnabled == 1; }
    bool splitEnabled() const { return m_frequencyVfoState.splitEnabled; }

    // Processing (NB/NR/PA/RA/GT + NA/NM) — backed by m_processingState.
    int noiseBlankerLevel() const { return m_processingState.noiseBlankerLevel; }
    bool noiseBlankerEnabled() const { return m_processingState.noiseBlankerEnabled; }
    int noiseBlankerFilterWidth() const { return m_processingState.noiseBlankerFilterWidth; }
    int noiseReductionLevel() const { return m_processingState.noiseReductionLevel; }
    bool noiseReductionEnabled() const { return m_processingState.noiseReductionEnabled; }
    int ssnrLevel() const { return m_processingState.ssnrLevel; }
    bool ssnrEnabled() const { return m_processingState.ssnrEnabled; }

    bool autoNotchEnabled() const { return m_processingState.autoNotchEnabled; }
    bool manualNotchEnabled() const { return m_processingState.manualNotchEnabled; }
    int manualNotchPitch() const { return m_processingState.manualNotchPitch; }

    bool autoNotchEnabledB() const { return m_processingState.autoNotchEnabledB; }
    bool manualNotchEnabledB() const { return m_processingState.manualNotchEnabledB; }
    int manualNotchPitchB() const { return m_processingState.manualNotchPitchB; }

    // Optimistic setters for notch pitch (radio doesn't echo these commands)
    void setManualNotchPitch(int pitch);
    void setManualNotchPitchB(int pitch);

    int preamp() const { return m_processingState.preamp; }
    bool preampEnabled() const { return m_processingState.preampEnabled; }
    int attenuatorLevel() const { return m_processingState.attenuatorLevel; }
    bool attenuatorEnabled() const { return m_processingState.attenuatorEnabled; }
    AGCSpeed agcSpeed() const { return static_cast<AGCSpeed>(m_processingState.agcSpeed); }

    int noiseBlankerLevelB() const { return m_processingState.noiseBlankerLevelB; }
    bool noiseBlankerEnabledB() const { return m_processingState.noiseBlankerEnabledB; }
    int noiseBlankerFilterWidthB() const { return m_processingState.noiseBlankerFilterWidthB; }
    int noiseReductionLevelB() const { return m_processingState.noiseReductionLevelB; }
    bool noiseReductionEnabledB() const { return m_processingState.noiseReductionEnabledB; }
    int ssnrLevelB() const { return m_processingState.ssnrLevelB; }
    bool ssnrEnabledB() const { return m_processingState.ssnrEnabledB; }
    int preampB() const { return m_processingState.preampB; }
    bool preampEnabledB() const { return m_processingState.preampEnabledB; }
    int attenuatorLevelB() const { return m_processingState.attenuatorLevelB; }
    bool attenuatorEnabledB() const { return m_processingState.attenuatorEnabledB; }
    AGCSpeed agcSpeedB() const { return static_cast<AGCSpeed>(m_processingState.agcSpeedB); }

    // Radio info
    QString radioID() const { return m_rxTxMeterState.radioID; }
    QString radioModel() const { return m_rxTxMeterState.radioModel; }
    QString optionModules() const { return m_rxTxMeterState.optionModules; }
    QMap<QString, QString> firmwareVersions() const { return m_rxTxMeterState.firmwareVersions; }

    // Antenna — backed by m_antennaState (see src/models/radiostate/antennastate.h).
    int txAntenna() const { return m_antennaState.selectedAntenna; }
    int rxAntennaMain() const { return m_antennaState.receiveAntenna; }
    int rxAntennaSub() const { return m_antennaState.receiveAntennaSub; }
    QString antennaName(int index) const {
        return m_antennaState.antennaNames.value(index, QString("ANT%1").arg(index));
    }
    QString txAntennaName() const { return antennaName(m_antennaState.selectedAntenna); }
    QString rxAntennaMainName() const { return antennaName(m_antennaState.receiveAntenna); }
    QString rxAntennaSubName() const { return antennaName(m_antennaState.receiveAntennaSub); }

    // RIT/XIT — backed by m_frequencyVfoState.
    bool ritEnabled() const { return m_frequencyVfoState.ritEnabled; }
    bool xitEnabled() const { return m_frequencyVfoState.xitEnabled; }
    int ritXitOffset() const { return m_frequencyVfoState.ritXitOffset; }
    bool ritEnabledB() const { return m_frequencyVfoState.ritEnabledB; }
    int ritXitOffsetB() const { return m_frequencyVfoState.ritXitOffsetB; }

    // Message bank (MN)
    int messageBank() const { return m_rxTxMeterState.messageBank; }

    // VOX
    bool voxCW() const { return m_audioEffectsState.voxCW; }
    bool voxVoice() const { return m_audioEffectsState.voxVoice; }
    bool voxData() const { return m_audioEffectsState.voxData; }
    bool voxEnabled() const {
        return m_audioEffectsState.voxCW || m_audioEffectsState.voxVoice || m_audioEffectsState.voxData;
    }

    // QSK (full break-in)
    bool qskEnabled() const { return m_qskControlState.qskEnabled; }

    // K4 remote power state — std::nullopt until the first PS echo arrives,
    // then true (PS1) / false (PS0). See radiostate/powerstate.h.
    std::optional<bool> isPoweredOn() const { return m_powerState.isOn(); }

    // PA / Lower-PA temperatures in °C from the SIRF PT / LT fields. Sentinel -1
    // means "not yet seen" — UI should render the disconnected placeholder.
    int paTemperatureC() const { return m_rxTxMeterState.paTemperatureC; }
    int lpaTemperatureC() const { return m_rxTxMeterState.lpaTemperatureC; }

    // TEST mode (TX test)
    bool testMode() const { return m_rxTxMeterState.testMode; }

    // ATU mode (0=not installed, 1=bypass, 2=auto)
    int atuMode() const { return m_antennaState.atuMode; }

    // B SET (Target B) - controls whether feature menu commands target Sub RX
    // State is tracked internally (toggled when SW44 is sent)
    bool bSetEnabled() const { return m_rxTxMeterState.bSetEnabled; }
    void setBSetEnabled(bool enabled);
    void toggleBSet() { setBSetEnabled(!m_rxTxMeterState.bSetEnabled); }

    // Streaming Latency (SL command)
    int streamingLatency() const { return m_dataControlState.streamingLatency; }

    // Audio Effects (FX command)
    int afxMode() const { return m_audioEffectsState.afxMode; } // 0=off, 1=delay, 2=pitch-map

    // Audio Peak Filter (AP/AP$ commands, CW mode only)
    bool apfEnabled() const { return m_audioEffectsState.apfEnabled; }      // Main RX
    int apfBandwidth() const { return m_audioEffectsState.apfBandwidth; }   // Main RX: 0=30Hz, 1=50Hz, 2=150Hz
    bool apfEnabledB() const { return m_audioEffectsState.apfEnabledB; }    // Sub RX
    int apfBandwidthB() const { return m_audioEffectsState.apfBandwidthB; } // Sub RX: 0=30Hz, 1=50Hz, 2=150Hz

    // VFO Lock (LK/LK$ commands)
    bool lockA() const { return m_frequencyVfoState.lockA; }
    bool lockB() const { return m_frequencyVfoState.lockB; }

    // VFO Link (LN command)
    bool vfoLink() const { return m_frequencyVfoState.vfoLink; }

    // Monitor Level (ML command) - sidetone/speech monitor
    // mode: 0=CW, 1=AF data, 2=voice
    int monitorLevelCW() const { return m_audioEffectsState.monitorLevelCW; }
    int monitorLevelData() const { return m_audioEffectsState.monitorLevelData; }
    int monitorLevelVoice() const { return m_audioEffectsState.monitorLevelVoice; }
    int monitorLevelForCurrentMode() const;
    // Returns the ML mode code (0/1/2) for the current operating mode
    int monitorModeCode() const;

    // Audio mix routing (MX command) - how main/sub maps to L/R when SUB is on
    int audioMixLeft() const { return m_audioEffectsState.audioMixLeft; }   // MixSource left
    int audioMixRight() const { return m_audioEffectsState.audioMixRight; } // MixSource right

    // Audio balance (BL command) - MAIN/SUB balance
    int balanceMode() const { return m_audioEffectsState.balanceMode; }     // 0=NOR, 1=BAL
    int balanceOffset() const { return m_audioEffectsState.balanceOffset; } // -50 to +50

    // Optimistic setter for balance (radio doesn't echo BL SET commands)
    void setBalance(int mode, int offset);

    // Optimistic setter for monitor level
    void setMonitorLevel(int mode, int level);

    // Returns VOX state for current operating mode
    bool voxForCurrentMode() const;

    // QSK/VOX Delay (in 10ms increments)
    int qskDelayCW() const { return m_qskControlState.qskDelayCW; }
    int qskDelayVoice() const { return m_qskControlState.qskDelayVoice; }
    int qskDelayData() const { return m_qskControlState.qskDelayData; }
    // Returns delay for current operating mode (in 10ms increments)
    int delayForCurrentMode() const;

    // Optimistic setter for QSK/VOX delay (in 10ms increments, 0-255)
    void setDelayForCurrentMode(int delay);

    // Panadapter REF level (Main)
    int refLevel() const { return m_spectrumDisplayState.refLevel; }
    void setRefLevel(int level);

    // Panadapter scale (Main, from #SCL command, 10-150)
    // Higher values = more compressed display (signals appear weaker)
    // Lower values = more expanded display (signals appear stronger)
    int scale() const { return m_spectrumDisplayState.scale; }
    void setScale(int scale);

    // Panadapter span (Main, from #SPN command, in Hz)
    int spanHz() const { return m_spectrumDisplayState.spanHz; }
    void setSpanHz(int spanHz);

    // Panadapter REF level (Sub)
    int refLevelB() const { return m_spectrumDisplayState.refLevelB; }
    void setRefLevelB(int level);

    // Panadapter span (Sub, from #SPN$ command, in Hz)
    int spanHzB() const { return m_spectrumDisplayState.spanHzB; }
    void setSpanHzB(int spanHz);

    // Mini-Pan enabled state (tracked via #MP / #MP$ CAT commands)
    bool miniPanAEnabled() const { return m_spectrumDisplayState.miniPanAEnabled; }
    bool miniPanBEnabled() const { return m_spectrumDisplayState.miniPanBEnabled; }

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
    int dualPanModeLcd() const { return m_spectrumDisplayState.dualPanModeLcd; }
    int dualPanModeExt() const { return m_spectrumDisplayState.dualPanModeExt; }
    int displayModeLcd() const { return m_spectrumDisplayState.displayModeLcd; }
    int displayModeExt() const { return m_spectrumDisplayState.displayModeExt; }
    int displayFps() const { return m_spectrumDisplayState.displayFps; }
    int waterfallColor() const { return m_spectrumDisplayState.waterfallColor; }
    int waterfallHeight() const { return m_spectrumDisplayState.waterfallHeight; }       // #WFHxx
    int waterfallHeightExt() const { return m_spectrumDisplayState.waterfallHeightExt; } // #HWFHxx
    int averaging() const { return m_spectrumDisplayState.averaging; }
    bool peakMode() const { return m_spectrumDisplayState.peakMode > 0; }
    int fixedTune() const { return m_spectrumDisplayState.fixedTune; } // #FXT
    int fixedTuneMode() const { return m_spectrumDisplayState.fixedTuneMode; }
    bool freeze() const { return m_spectrumDisplayState.freeze > 0; }
    int vfoACursor() const { return m_spectrumDisplayState.vfoACursor; }
    int vfoBCursor() const { return m_spectrumDisplayState.vfoBCursor; }
    bool autoRefLevel() const { return m_spectrumDisplayState.autoRefLevel > 0; }
    int ddcNbMode() const { return m_spectrumDisplayState.ddcNbMode; }
    int ddcNbLevel() const { return m_spectrumDisplayState.ddcNbLevel; }

    // Data sub-mode (DT command): 0=DATA-A, 1=AFSK-A, 2=FSK-D, 3=PSK-D
    int dataSubMode() const { return m_dataControlState.dataSubMode; }
    int dataSubModeB() const { return m_dataControlState.dataSubModeB; }

    // Data rate (DR command): 0=slower (RTTY45/PSK31), 1=faster (RTTY75/PSK63)
    int dataRate() const { return m_dataControlState.dataRate; }
    int dataRateB() const { return m_dataControlState.dataRateB; }

    // RX Graphic Equalizer (RE command) - 8 bands, -16 to +16 dB
    // Bands: 100, 200, 400, 800, 1200, 1600, 2400, 3200 Hz
    // Note: Main RX and Sub RX share the same EQ settings
    int rxEqBand(int index) const { return (index >= 0 && index < 8) ? m_audioEffectsState.rxEqBands[index] : 0; }
    QVector<int> rxEqBands() const {
        return QVector<int>(m_audioEffectsState.rxEqBands, m_audioEffectsState.rxEqBands + 8);
    }

    // Optimistic setter for RX EQ bands (radio doesn't echo)
    void setRxEqBand(int index, int dB);
    void setRxEqBands(const QVector<int> &bands);

    // TX Graphic Equalizer (TE command) - 8 bands, -16 to +16 dB
    // Bands: 100, 200, 400, 800, 1200, 1600, 2400, 3200 Hz
    int txEqBand(int index) const { return (index >= 0 && index < 8) ? m_audioEffectsState.txEqBands[index] : 0; }
    QVector<int> txEqBands() const {
        return QVector<int>(m_audioEffectsState.txEqBands, m_audioEffectsState.txEqBands + 8);
    }

    // Optimistic setter for TX EQ bands (radio doesn't echo)
    void setTxEqBand(int index, int dB);
    void setTxEqBands(const QVector<int> &bands);

    // Antenna Configuration Masks (ACM/ACS/ACT) — backed by m_antennaState.
    bool mainRxDisplayAll() const { return m_antennaState.mainRxDisplayAll; }
    bool mainRxAntEnabled(int index) const {
        return (index >= 0 && index < 7) ? m_antennaState.mainRxAntMask[index] : false;
    }
    QVector<bool> mainRxAntMask() const {
        return QVector<bool>(m_antennaState.mainRxAntMask, m_antennaState.mainRxAntMask + 7);
    }

    bool subRxDisplayAll() const { return m_antennaState.subRxDisplayAll; }
    bool subRxAntEnabled(int index) const {
        return (index >= 0 && index < 7) ? m_antennaState.subRxAntMask[index] : false;
    }
    QVector<bool> subRxAntMask() const {
        return QVector<bool>(m_antennaState.subRxAntMask, m_antennaState.subRxAntMask + 7);
    }

    bool txDisplayAll() const { return m_antennaState.txDisplayAll; }
    bool txAntEnabled(int index) const { return (index >= 0 && index < 3) ? m_antennaState.txAntMask[index] : false; }
    QVector<bool> txAntMask() const { return QVector<bool>(m_antennaState.txAntMask, m_antennaState.txAntMask + 3); }

    // Optimistic setters for antenna config (radio doesn't echo)
    void setMainRxAntConfig(bool displayAll, const QVector<bool> &mask);
    void setSubRxAntConfig(bool displayAll, const QVector<bool> &mask);
    void setTxAntConfig(bool displayAll, const QVector<bool> &mask);

    // Line Out levels (LO command)
    int lineOutLeft() const { return m_audioEffectsState.lineOutLeft; }
    int lineOutRight() const { return m_audioEffectsState.lineOutRight; }
    bool lineOutRightEqualsLeft() const { return m_audioEffectsState.lineOutRightEqualsLeft; }

    // Optimistic setters for Line Out
    void setLineOutLeft(int level);
    void setLineOutRight(int level);
    void setLineOutRightEqualsLeft(bool enabled);

    // Line In levels and source (LI command)
    int lineInSoundCard() const { return m_audioEffectsState.lineInSoundCard; }
    int lineInJack() const { return m_audioEffectsState.lineInJack; }
    int lineInSource() const { return m_audioEffectsState.lineInSource; } // 0=SoundCard, 1=LineInJack

    // Optimistic setters for Line In
    void setLineInSoundCard(int level);
    void setLineInJack(int level);
    void setLineInSource(int source);

    // Mic Input (MI command) - 0=front, 1=rear, 2=line in, 3=front+line in, 4=rear+line in
    int micInput() const { return m_audioEffectsState.micInput; }

    // Mic Setup (MS command) - preamp, bias, buttons configuration
    int micFrontPreamp() const { return m_audioEffectsState.micFrontPreamp; }   // 0=0dB, 1=10dB, 2=20dB
    int micFrontBias() const { return m_audioEffectsState.micFrontBias; }       // 0=OFF, 1=ON
    int micFrontButtons() const { return m_audioEffectsState.micFrontButtons; } // 0=disabled, 1=UP/DN enabled
    int micRearPreamp() const { return m_audioEffectsState.micRearPreamp; }     // 0=0dB, 1=14dB
    int micRearBias() const { return m_audioEffectsState.micRearBias; }         // 0=OFF, 1=ON

    // VOX Gain (VG command) - per mode (0=voice, 1=data)
    int voxGainVoice() const { return m_audioEffectsState.voxGainVoice; } // 0-60
    int voxGainData() const { return m_audioEffectsState.voxGainData; }   // 0-60
    int voxGainForCurrentMode() const {
        return (mode() == DATA || mode() == DATA_R) ? m_audioEffectsState.voxGainData
                                                    : m_audioEffectsState.voxGainVoice;
    }

    // Anti-VOX (VI command) - voice modes only
    int antiVox() const { return m_audioEffectsState.antiVox; } // 0-60

    // ESSB and SSB TX Bandwidth (ES command)
    bool essbEnabled() const { return m_audioEffectsState.essbEnabled; } // 0=SSB, 1=ESSB
    int ssbTxBw() const { return m_audioEffectsState.ssbTxBw; }          // 30-45 (3.0-4.5 kHz in 100Hz units)

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

    // Text Decode (TD$ command) - Main RX. Backed by m_textDecodeState (see
    // models/radiostate/textdecodestate.h).
    int textDecodeMode() const { return m_textDecodeState.textDecodeMode; }           // 0=off, 2-4=CW WPM
    int textDecodeThreshold() const { return m_textDecodeState.textDecodeThreshold; } // 0=AUTO, 1-9
    int textDecodeLines() const { return m_textDecodeState.textDecodeLines; }         // 1-10 lines

    // Text Decode (TD$$ command) - Sub RX
    int textDecodeModeB() const { return m_textDecodeState.textDecodeModeB; }
    int textDecodeThresholdB() const { return m_textDecodeState.textDecodeThresholdB; }
    int textDecodeLinesB() const { return m_textDecodeState.textDecodeLinesB; }

    // XVTR per-band config — backed by m_xvtrBandState.
    const QVector<XvtrBandConfig> &xvtrBands() const { return m_xvtrBandState.bands; }
    int xvtrBandSelect() const { return m_xvtrBandState.currentSelect; }

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

    // Introspection for tests. parseCATCommand() iterates m_commandHandlers in
    // registration order and first-match-wins, so the registration order must be
    // "shadow-safe": if prefix X is a proper prefix of prefix Y, Y must be
    // registered before X. See tests/test_radiostate_registry.cpp.
    QStringList registeredCommandPrefixes() const;

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

    void transmitStateChanged(bool transmitting);
    // Emitted when PC echo lands. `value` is in W for Qrp/Qro and mW for Xvtr —
    // UI consumers should check `range` before formatting (it dictates both the
    // unit suffix and the decimal precision).
    void rfPowerChanged(double value, LevelsState::PowerRange range);
    void supplyVoltageChanged(double volts);
    void supplyCurrentChanged(double amps);
    void paDrainCurrentChanged(double amps);
    void paTemperatureChanged(int celsius);  // SIRF PT field (final-PA heatsink temp)
    void lpaTemperatureChanged(int celsius); // SIRF LT field (Lower-PA heatsink temp)
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
    void powerStateChanged(bool on);                                 // PS0/PS1 — K4 remote power state
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

    // Streaming latency
    void streamingLatencyChanged(int tier); // SL: 0-7

    // Error/notification messages from K4 (ERxx: format)
    void errorNotificationReceived(int errorCode, const QString &message);

    // XVTR per-band config (XvtrBandState). xvtrBandsChanged fires when any
    // band's mode/RF/IF/offset value changes; xvtrBandSelectChanged fires when
    // the K4's current-band pointer (XVN / ME0086) moves.
    void xvtrBandsChanged();
    void xvtrBandSelectChanged(int band);

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
    void rxEqChanged(); // Any EQ band value changed

    // TX Graphic Equalizer
    void txEqChanged(); // Any EQ band value changed

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
    // Frequency / VFO / split / RIT/XIT state — see radiostate/frequencyvfostate.h.
    FrequencyVfoState m_frequencyVfoState;
    // Data-mode + tuning step + streaming latency — see radiostate/datacontrolstate.h.
    DataControlState m_dataControlState;

    // Mode / filter / CW pitch / keyer — see radiostate/modefilterstate.h.
    ModeFilterState m_modeFilterState;

    // Power, mic gain, compression, RF gain, squelch — see radiostate/levelsstate.h.
    LevelsState m_levelsState;
    // Keyer speed / iambic / paddle / weight live on m_modeFilterState.

    // Meters, TX/RX transition, control toggles (SB/DV/TS/BS), message bank,
    // supply voltage/current, and radio identity (ID/OM/RV.) all live on
    // m_rxTxMeterState. See models/radiostate/rxtxmeterstate.h.
    RxTxMeterState m_rxTxMeterState;
    // splitEnabled lives on m_frequencyVfoState.

    // Processing state (NB/NR/PA/RA/GT/NA/NM) — see models/radiostate/processingstate.h.
    ProcessingState m_processingState;

    // Antenna state — see models/radiostate/antennastate.h.
    AntennaState m_antennaState;

    // Audio pipeline state (FX/AP/VX/VG/VI/ES/RE/TE/LO/LI/MI/MS/ML/MX/BL) —
    // see models/radiostate/audioeffectsstate.h.
    AudioEffectsState m_audioEffectsState;

    // RIT/XIT state lives on m_frequencyVfoState.

    // Message bank lives on m_rxTxMeterState.

    // VOX flags / gain / anti-VOX live on m_audioEffectsState.

    // QSK (full break-in) - extracted from SD command x flag
    // QSK (full break-in) state and per-mode delays — see radiostate/qskcontrolstate.h.
    QskControlState m_qskControlState;

    // K4 remote power state — see radiostate/powerstate.h.
    PowerState m_powerState;

    // TEST / B SET live on m_rxTxMeterState.

    // QSK/VOX Delay per mode (in 10ms increments)

    // Streaming Latency (SL command)
    // m_streamingLatency lives on m_dataControlState.

    // Audio effects / APF / mix routing / balance / monitor level all live on
    // m_audioEffectsState.

    // VFO Link (LN command)

    // VFO Lock (LK/LK$ commands)
    // VFO link and per-VFO lock (LN / LK / LK$) live on m_frequencyVfoState.

    // Panadapter / display state (#REF, #SPN, #SCL, #MP, #DPM, #DSM, #FPS,
    // #WFC, #WFH, #AVG, #PKM, #FXT, #FXA, #FRZ, #VFA, #VFB, #AR, #NB$,
    // #NBL$, plus EXT variants) — see models/radiostate/spectrumdisplaystate.h.
    SpectrumDisplayState m_spectrumDisplayState;

    // Radio identity (ID/OM/RV.) lives on m_rxTxMeterState.

    // Data sub-mode + rate + optimistic cooldown timestamps live on m_dataControlState.

    // RX/TX graphic EQ, Line In/Out levels, Mic Input/Setup, VOX gain/anti-VOX,
    // and ESSB state all live on m_audioEffectsState (declared above).

    // Antenna config masks live on m_antennaState (declared above).

    // Text Decode (TD / TD$ / TD$$ + TB / TB$). See
    // models/radiostate/textdecodestate.h for the field layout and the handler
    // functions that mutate it.
    TextDecodeState m_textDecodeState;

    // XVTR per-band config (XVN/XVM/XVR/XVI/XVO). See xvtrbandstate.h.
    XvtrBandState m_xvtrBandState;

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
    void handleNB(const QString &cmd);     // Noise Blanker Main
    void handleNBSub(const QString &cmd);  // Noise Blanker Sub (NB$)
    void handleNR(const QString &cmd);     // Noise Reduction Main
    void handleNRSub(const QString &cmd);  // Noise Reduction Sub (NR$)
    void handleNRS(const QString &cmd);    // Spectral-subtraction NR Main (NRS)
    void handleNRSSub(const QString &cmd); // Spectral-subtraction NR Sub (NRS$)
    void handlePA(const QString &cmd);     // Preamp Main
    void handlePASub(const QString &cmd);  // Preamp Sub (PA$)
    void handleRA(const QString &cmd);     // Attenuator Main
    void handleRASub(const QString &cmd);  // Attenuator Sub (RA$)
    void handleGT(const QString &cmd);     // AGC Speed Main
    void handleGTSub(const QString &cmd);  // AGC Speed Sub (GT$)
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
    void handleSIRF(const QString &cmd); // RF Deck Status (PA drain current)

    void handleSL(const QString &cmd); // Streaming Latency
    void handleMN(const QString &cmd); // Message Bank
    void handleER(const QString &cmd); // Error notifications

    // Display commands (# prefix) — all handlers live in SpectrumDisplayHandlers
    // (see models/radiostate/spectrumdisplaystate.{h,cpp}). The registry inline
    // lambdas in registerCommandHandlers() forward directly into the namespace.
};

#endif // RADIOSTATE_H
