#include "radiostate.h"
#include <QDateTime>
#include <QDebug>
#include <QThread>
#include <algorithm>

RadioState::RadioState(QObject *parent) : QObject(parent) {
    registerCommandHandlers();
}

void RadioState::reset() {
    // Frequency / VFO / RIT/XIT state — subsystem struct handles split + offsets too.
    m_frequencyVfoState.reset();
    // Data-mode + tuning step + streaming latency.
    m_dataControlState.reset();

    // Mode / filter / CW pitch / keyer config — subsystem struct.
    m_modeFilterState.reset();

    // Power, mic gain, compression, RF gain, squelch.
    m_levelsState.reset();
    // Keyer speed / iambic / paddle / weight reset via m_modeFilterState.reset() above.

    // Meters / TX state / control toggles / supply / radio identity / message bank.
    m_rxTxMeterState.reset();
    // splitEnabled reset via m_frequencyVfoState.reset() above.

    // Processing state (NB/NR/PA/RA/GT + NA/NM for both VFOs).
    m_processingState.reset();

    // XVTR per-band configs (12 slots).
    m_xvtrBandState.reset();

    // Antenna
    m_antennaState.reset();

    // RIT/XIT state reset via m_frequencyVfoState.reset() above.

    // Audio pipeline state (VOX/FX/AP/mix/balance/ML/LO/LI/MI/MS/RE/TE/VG/VI/ES).
    m_audioEffectsState.reset();

    // QSK (full break-in) + per-mode delays. (TEST/B SET live on m_rxTxMeterState.)
    m_qskControlState.reset();

    // K4 remote power state — back to "unknown" so the indicator goes neutral
    // until the next session sends a PS query.
    m_powerState.reset();

    // VFO link + lock A/B reset via m_frequencyVfoState.reset() above.

    // Panadapter / display state (REF, SPN, SCL, MP, DPM, DSM, FPS, WFC, WFH,
    // AVG, PKM, FXT, FXA, FRZ, VFA, VFB, AR, NB$, NBL$ + EXT variants).
    m_spectrumDisplayState.reset();

    // Radio identity cleared by m_rxTxMeterState.reset().

    // Data-mode + rate + optimistic cooldowns reset via m_dataControlState.reset().

    // EQ bands / Line In/Out / Mic Input/Setup / VOX Gain / Anti-VOX / ESSB
    // are reset via m_audioEffectsState.reset() above.

    // Antenna config masks reset as part of m_antennaState.reset() above.

    // Streaming latency reset via m_dataControlState.reset().

    // Text Decode
    m_textDecodeState.reset();
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
}

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
    return modeToString(mode());
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
    // For DATA / DATA-R modes, show the sub-mode instead.
    const Mode m = mode();
    if (m == DATA || m == DATA_R)
        return dataSubModeToString(m_dataControlState.dataSubMode);
    return modeToString(m);
}

QString RadioState::modeStringFullB() const {
    const Mode m = modeB();
    if (m == DATA || m == DATA_R)
        return dataSubModeToString(m_dataControlState.dataSubModeB);
    return modeToString(m);
}

// Mode/filter/CW pitch/keyer optimistic setters — delegate to ModeFilterHandlers.
void RadioState::setKeyerSpeed(int wpm) {
    ModeFilterHandlers::setKeyerSpeed(m_modeFilterState, *this, wpm);
}
void RadioState::setCwPitch(int pitchHz) {
    ModeFilterHandlers::setCwPitch(m_modeFilterState, *this, pitchHz);
}

void RadioState::setRfPower(double watts) {
    LevelsHandlers::setRfPower(m_levelsState, *this, watts);
}

void RadioState::setFilterBandwidth(int bwHz) {
    ModeFilterHandlers::setFilterBandwidth(m_modeFilterState, *this, bwHz);
}
void RadioState::setIfShift(int shift) {
    ModeFilterHandlers::setIfShift(m_modeFilterState, *this, shift);
}
void RadioState::setFilterBandwidthB(int bwHz) {
    ModeFilterHandlers::setFilterBandwidthB(m_modeFilterState, *this, bwHz);
}
void RadioState::setIfShiftB(int shift) {
    ModeFilterHandlers::setIfShiftB(m_modeFilterState, *this, shift);
}

void RadioState::setRfGain(int gain) {
    LevelsHandlers::setRfGain(m_levelsState, *this, gain);
}

void RadioState::setSquelchLevel(int level) {
    LevelsHandlers::setSquelchLevel(m_levelsState, *this, level);
}

void RadioState::setRfGainB(int gain) {
    LevelsHandlers::setRfGainB(m_levelsState, *this, gain);
}

void RadioState::setSquelchLevelB(int level) {
    LevelsHandlers::setSquelchLevelB(m_levelsState, *this, level);
}

void RadioState::setMicGain(int gain) {
    LevelsHandlers::setMicGain(m_levelsState, *this, gain);
}

void RadioState::setCompression(int level) {
    LevelsHandlers::setCompression(m_levelsState, *this, level);
}

void RadioState::setBalance(int mode, int offset) {
    AudioEffectsHandlers::setBalance(m_audioEffectsState, *this, mode, offset);
}

void RadioState::setMonitorLevel(int mode, int level) {
    AudioEffectsHandlers::setMonitorLevel(m_audioEffectsState, *this, mode, level);
}

// Processing optimistic setters — delegate to ProcessingHandlers namespace.
void RadioState::setNoiseBlankerLevel(int level) {
    ProcessingHandlers::setNoiseBlankerLevel(m_processingState, *this, level);
}
void RadioState::setNoiseBlankerLevelB(int level) {
    ProcessingHandlers::setNoiseBlankerLevelB(m_processingState, *this, level);
}
void RadioState::setNoiseBlankerFilter(int filter) {
    ProcessingHandlers::setNoiseBlankerFilter(m_processingState, *this, filter);
}
void RadioState::setNoiseBlankerFilterB(int filter) {
    ProcessingHandlers::setNoiseBlankerFilterB(m_processingState, *this, filter);
}
void RadioState::setNoiseReductionLevel(int level) {
    ProcessingHandlers::setNoiseReductionLevel(m_processingState, *this, level);
}
void RadioState::setNoiseReductionLevelB(int level) {
    ProcessingHandlers::setNoiseReductionLevelB(m_processingState, *this, level);
}
void RadioState::setSsnrLevel(int level) {
    ProcessingHandlers::setSsnrLevel(m_processingState, *this, level);
}
void RadioState::setSsnrLevelB(int level) {
    ProcessingHandlers::setSsnrLevelB(m_processingState, *this, level);
}
void RadioState::setManualNotchPitch(int pitch) {
    ProcessingHandlers::setManualNotchPitch(m_processingState, *this, pitch);
}
void RadioState::setManualNotchPitchB(int pitch) {
    ProcessingHandlers::setManualNotchPitchB(m_processingState, *this, pitch);
}

// Data mode optimistic setters — delegate to DataControlHandlers namespace.
void RadioState::setDataSubMode(int subMode) {
    DataControlHandlers::setDataSubMode(m_dataControlState, *this, subMode);
}
void RadioState::setDataSubModeB(int subMode) {
    DataControlHandlers::setDataSubModeB(m_dataControlState, *this, subMode);
}
void RadioState::setDataRate(int rate) {
    DataControlHandlers::setDataRate(m_dataControlState, *this, rate);
}
void RadioState::setDataRateB(int rate) {
    DataControlHandlers::setDataRateB(m_dataControlState, *this, rate);
}

void RadioState::setMiniPanAEnabled(bool enabled) {
    SpectrumDisplayHandlers::setMiniPanAEnabled(m_spectrumDisplayState, *this, enabled);
}

void RadioState::setMiniPanBEnabled(bool enabled) {
    SpectrumDisplayHandlers::setMiniPanBEnabled(m_spectrumDisplayState, *this, enabled);
}

void RadioState::setWaterfallHeight(int percent) {
    SpectrumDisplayHandlers::setWaterfallHeight(m_spectrumDisplayState, *this, percent);
}

void RadioState::setWaterfallHeightExt(int percent) {
    SpectrumDisplayHandlers::setWaterfallHeightExt(m_spectrumDisplayState, *this, percent);
}

void RadioState::setAveraging(int value) {
    SpectrumDisplayHandlers::setAveraging(m_spectrumDisplayState, *this, value);
}

void RadioState::setRefLevel(int level) {
    SpectrumDisplayHandlers::setRefLevel(m_spectrumDisplayState, *this, level);
}

void RadioState::setScale(int scale) {
    SpectrumDisplayHandlers::setScale(m_spectrumDisplayState, *this, scale);
}

void RadioState::setSpanHz(int spanHz) {
    SpectrumDisplayHandlers::setSpanHz(m_spectrumDisplayState, *this, spanHz);
}

void RadioState::setRefLevelB(int level) {
    SpectrumDisplayHandlers::setRefLevelB(m_spectrumDisplayState, *this, level);
}

void RadioState::setSpanHzB(int spanHz) {
    SpectrumDisplayHandlers::setSpanHzB(m_spectrumDisplayState, *this, spanHz);
}

void RadioState::setRxEqBand(int index, int dB) {
    AudioEffectsHandlers::setRxEqBand(m_audioEffectsState, *this, index, dB);
}

void RadioState::setRxEqBands(const QVector<int> &bands) {
    AudioEffectsHandlers::setRxEqBands(m_audioEffectsState, *this, bands);
}

void RadioState::setTxEqBand(int index, int dB) {
    AudioEffectsHandlers::setTxEqBand(m_audioEffectsState, *this, index, dB);
}

void RadioState::setTxEqBands(const QVector<int> &bands) {
    AudioEffectsHandlers::setTxEqBands(m_audioEffectsState, *this, bands);
}

// Antenna config setters — delegate to AntennaHandlers namespace.
void RadioState::setMainRxAntConfig(bool displayAll, const QVector<bool> &mask) {
    AntennaHandlers::setMainRxAntConfig(m_antennaState, *this, displayAll, mask);
}
void RadioState::setSubRxAntConfig(bool displayAll, const QVector<bool> &mask) {
    AntennaHandlers::setSubRxAntConfig(m_antennaState, *this, displayAll, mask);
}
void RadioState::setTxAntConfig(bool displayAll, const QVector<bool> &mask) {
    AntennaHandlers::setTxAntConfig(m_antennaState, *this, displayAll, mask);
}

void RadioState::setLineOutLeft(int level) {
    AudioEffectsHandlers::setLineOutLeft(m_audioEffectsState, *this, level);
}

void RadioState::setLineOutRight(int level) {
    AudioEffectsHandlers::setLineOutRight(m_audioEffectsState, *this, level);
}

void RadioState::setLineOutRightEqualsLeft(bool enabled) {
    AudioEffectsHandlers::setLineOutRightEqualsLeft(m_audioEffectsState, *this, enabled);
}

// Line In optimistic setters
void RadioState::setLineInSoundCard(int level) {
    AudioEffectsHandlers::setLineInSoundCard(m_audioEffectsState, *this, level);
}

void RadioState::setLineInJack(int level) {
    AudioEffectsHandlers::setLineInJack(m_audioEffectsState, *this, level);
}

void RadioState::setLineInSource(int source) {
    AudioEffectsHandlers::setLineInSource(m_audioEffectsState, *this, source);
}

// Mic Input/Setup optimistic setters
void RadioState::setMicInput(int input) {
    AudioEffectsHandlers::setMicInput(m_audioEffectsState, *this, input);
}

void RadioState::setMicFrontPreamp(int preamp) {
    AudioEffectsHandlers::setMicFrontPreamp(m_audioEffectsState, *this, preamp);
}

void RadioState::setMicFrontBias(int bias) {
    AudioEffectsHandlers::setMicFrontBias(m_audioEffectsState, *this, bias);
}

void RadioState::setMicFrontButtons(int buttons) {
    AudioEffectsHandlers::setMicFrontButtons(m_audioEffectsState, *this, buttons);
}

void RadioState::setMicRearPreamp(int preamp) {
    AudioEffectsHandlers::setMicRearPreamp(m_audioEffectsState, *this, preamp);
}

void RadioState::setMicRearBias(int bias) {
    AudioEffectsHandlers::setMicRearBias(m_audioEffectsState, *this, bias);
}

// Text Decode optimistic setters — delegate into the TextDecodeHandlers
// namespace (see models/radiostate/textdecodestate.cpp). Public API shape
// preserved; the struct holds the state and the handler fns mutate it.
void RadioState::setTextDecodeMode(int mode) {
    TextDecodeHandlers::setMode(m_textDecodeState, *this, mode);
}
void RadioState::setTextDecodeThreshold(int threshold) {
    TextDecodeHandlers::setThreshold(m_textDecodeState, *this, threshold);
}
void RadioState::setTextDecodeLines(int lines) {
    TextDecodeHandlers::setLines(m_textDecodeState, *this, lines);
}
void RadioState::setTextDecodeModeB(int mode) {
    TextDecodeHandlers::setModeB(m_textDecodeState, *this, mode);
}
void RadioState::setTextDecodeThresholdB(int threshold) {
    TextDecodeHandlers::setThresholdB(m_textDecodeState, *this, threshold);
}
void RadioState::setTextDecodeLinesB(int lines) {
    TextDecodeHandlers::setLinesB(m_textDecodeState, *this, lines);
}

// VOX Gain / Anti-VOX / ESSB optimistic setters — delegate into AudioEffectsHandlers.
void RadioState::setVoxGainVoice(int gain) {
    AudioEffectsHandlers::setVoxGainVoice(m_audioEffectsState, *this, gain);
}

void RadioState::setVoxGainData(int gain) {
    AudioEffectsHandlers::setVoxGainData(m_audioEffectsState, *this, gain);
}

void RadioState::setAntiVox(int level) {
    AudioEffectsHandlers::setAntiVox(m_audioEffectsState, *this, level);
}

void RadioState::setEssbEnabled(bool enabled) {
    AudioEffectsHandlers::setEssbEnabled(m_audioEffectsState, *this, enabled);
}

void RadioState::setSsbTxBw(int bw) {
    AudioEffectsHandlers::setSsbTxBw(m_audioEffectsState, *this, bw);
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
//
// WHY a registry and not a giant switch:
//   1. K4 CAT prefixes are variable-length (2–5 chars: `FA`, `MD$`, `#REF$`). A switch on the
//      leading two characters would mis-route `MD$` into the `MD` branch — `$`-suffixed sub-RX
//      variants are indistinguishable from the base command at the first two bytes.
//   2. Prefix matching lets us keep the sub-RX variant (`$` suffix) alongside its base command
//      in code, which makes it obvious they share parsing logic (many use `handleIntPair()`).
//   3. The list is sorted longest-first so the dispatcher's first-match-wins rule correctly
//      prefers `RO$` over `RO` and `#REF$` over `#REF`.
//
// Dispatch is O(handlers) linear scan — with ~200 handlers and a single CAT stream, this has
// never been the bottleneck and keeping the list readable (grouped by subsystem) matters more
// than O(log n) matching.
//
// Pure-data and state fields that use `handleIntPair` consolidate the base and sub-RX variants
// into one call site; handlers that differ between main/sub (e.g., `MD` vs `MD$` often need to
// emit different signals) stay as separate lambdas.

QStringList RadioState::registeredCommandPrefixes() const {
    QStringList prefixes;
    prefixes.reserve(m_commandHandlers.size());
    for (const auto &entry : m_commandHandlers)
        prefixes.append(entry.prefix);
    return prefixes;
}

void RadioState::registerCommandHandlers() {
    // Register handlers in order - will be sorted by prefix length (longest first)
    // This ensures "MD$" is checked before "MD", "NB$" before "NB", etc.

    // Display commands (# prefix) - register longest first. All handlers
    // delegate to SpectrumDisplayHandlers (see radiostate/spectrumdisplaystate.cpp).
    auto spectrumHandler = [this](void (*fn)(SpectrumDisplayState &, RadioState &, const QString &)) {
        return [this, fn](const QString &c) { fn(m_spectrumDisplayState, *this, c); };
    };
    m_commandHandlers.append({"#HWFH", spectrumHandler(&SpectrumDisplayHandlers::handleHWFH)});
    m_commandHandlers.append({"#HDPM", spectrumHandler(&SpectrumDisplayHandlers::handleHDPM)});
    m_commandHandlers.append({"#HDSM", spectrumHandler(&SpectrumDisplayHandlers::handleHDSM)});
    m_commandHandlers.append({"#NBL$", spectrumHandler(&SpectrumDisplayHandlers::handleNBL)});
    m_commandHandlers.append({"#REF$", spectrumHandler(&SpectrumDisplayHandlers::handleREFSub)});
    m_commandHandlers.append({"#SPN$", spectrumHandler(&SpectrumDisplayHandlers::handleSPNSub)});
    m_commandHandlers.append({"#NB$", spectrumHandler(&SpectrumDisplayHandlers::handleNB)});
    m_commandHandlers.append({"#MP$", spectrumHandler(&SpectrumDisplayHandlers::handleMPSub)});
    m_commandHandlers.append({"#REF", spectrumHandler(&SpectrumDisplayHandlers::handleREF)});
    m_commandHandlers.append({"#SPN", spectrumHandler(&SpectrumDisplayHandlers::handleSPN)});
    m_commandHandlers.append({"#SCL", spectrumHandler(&SpectrumDisplayHandlers::handleSCL)});
    m_commandHandlers.append({"#DPM", spectrumHandler(&SpectrumDisplayHandlers::handleDPM)});
    m_commandHandlers.append({"#DSM", spectrumHandler(&SpectrumDisplayHandlers::handleDSM)});
    m_commandHandlers.append({"#FPS", spectrumHandler(&SpectrumDisplayHandlers::handleFPS)});
    m_commandHandlers.append({"#WFC", spectrumHandler(&SpectrumDisplayHandlers::handleWFC)});
    m_commandHandlers.append({"#WFH", spectrumHandler(&SpectrumDisplayHandlers::handleWFH)});
    m_commandHandlers.append({"#AVG", spectrumHandler(&SpectrumDisplayHandlers::handleAVG)});
    m_commandHandlers.append({"#PKM", spectrumHandler(&SpectrumDisplayHandlers::handlePKM)});
    m_commandHandlers.append({"#FXT", spectrumHandler(&SpectrumDisplayHandlers::handleFXT)});
    m_commandHandlers.append({"#FXA", spectrumHandler(&SpectrumDisplayHandlers::handleFXA)});
    m_commandHandlers.append({"#FRZ", spectrumHandler(&SpectrumDisplayHandlers::handleFRZ)});
    m_commandHandlers.append({"#VFA", spectrumHandler(&SpectrumDisplayHandlers::handleVFA)});
    m_commandHandlers.append({"#VFB", spectrumHandler(&SpectrumDisplayHandlers::handleVFB)});
    m_commandHandlers.append({"#MP", spectrumHandler(&SpectrumDisplayHandlers::handleMP)});
    m_commandHandlers.append({"#AR", spectrumHandler(&SpectrumDisplayHandlers::handleAR)});

    // Multi-char commands with $ suffix (must come before their base commands)
    m_commandHandlers.append({"SIFP", [this](const QString &c) { handleSIFP(c); }});
    m_commandHandlers.append({"SIRF", [this](const QString &c) { handleSIRF(c); }});

    m_commandHandlers.append({"TD$", [this](const QString &c) { handleTDSub(c); }});
    m_commandHandlers.append({"TB$", [this](const QString &c) { handleTBSub(c); }});
    m_commandHandlers.append({"DR$", [this](const QString &c) { handleDRSub(c); }});
    m_commandHandlers.append({"DT$", [this](const QString &c) { handleDTSub(c); }});
    m_commandHandlers.append({"MD$", [this](const QString &c) { handleMDSub(c); }});
    // BW$, IS$, FP$ — deduplicated via handleIntPair / ModeFilterHandlers.
    m_commandHandlers.append(
        {"BW$", [this](const QString &c) { ModeFilterHandlers::handleBWSub(m_modeFilterState, *this, c); }});
    m_commandHandlers.append({"IS$", [this](const QString &c) {
                                  handleIntPair(c, 3, m_modeFilterState.ifShiftB, -99999, 99999,
                                                &RadioState::ifShiftBChanged);
                              }});
    m_commandHandlers.append({"FP$", [this](const QString &c) {
                                  handleIntPair(c, 3, m_modeFilterState.filterPositionB, 1, 3,
                                                &RadioState::filterPositionBChanged);
                              }});
    // RG$ — strips leading dash before parsing.
    // WHY: K4 reports RF gain as an attenuation in the range -0..-60 dB
    // ("RG-030;" = -30 dB). Throughout the codebase (SideControlPanel UI,
    // MainWindow scroll handler, RadioState::setRfGain setter) RF gain is
    // carried as a positive magnitude 0..60; the minus sign is re-added only
    // at the display / CAT dispatch boundary. The handler uses qAbs() to
    // make that convention explicit. See levelsstate.cpp:80 for the canonical
    // version of this WHY and test_radiostate.cpp:1702 for the regression test.
    m_commandHandlers.append(
        {"RG$", [this](const QString &c) { LevelsHandlers::handleRGSub(m_levelsState, *this, c); }});
    m_commandHandlers.append(
        {"SQ$", [this](const QString &c) { LevelsHandlers::handleSQSub(m_levelsState, *this, c); }});
    m_commandHandlers.append({"SM$", [this](const QString &c) { handleSMSub(c); }});
    m_commandHandlers.append({"NB$", [this](const QString &c) { handleNBSub(c); }});
    m_commandHandlers.append({"NR$", [this](const QString &c) { handleNRSub(c); }});
    m_commandHandlers.append({"NRS$", [this](const QString &c) { handleNRSSub(c); }});
    m_commandHandlers.append({"PA$", [this](const QString &c) { handlePASub(c); }});
    m_commandHandlers.append({"RA$", [this](const QString &c) { handleRASub(c); }});
    m_commandHandlers.append({"GT$", [this](const QString &c) { handleGTSub(c); }});
    // NA$ — deduplicated via handleBoolPair
    m_commandHandlers.append({"NA$", [this](const QString &c) {
                                  handleBoolPair(c, 3, m_processingState.autoNotchEnabledB, &RadioState::notchBChanged);
                              }});
    m_commandHandlers.append({"NM$", [this](const QString &c) { handleNMSub(c); }});
    m_commandHandlers.append({"AP$", [this](const QString &c) { handleAPSub(c); }});
    m_commandHandlers.append(
        {"LK$", [this](const QString &c) { FrequencyVfoHandlers::handleLKSub(m_frequencyVfoState, *this, c); }});
    // VT$ — deduplicated inline (takes .left(1), qBound)
    m_commandHandlers.append(
        {"VT$", [this](const QString &c) { DataControlHandlers::handleVTSub(m_dataControlState, *this, c); }});
    // AR$ — deduplicated inline (3-arg signal)
    m_commandHandlers.append(
        {"AR$", [this](const QString &c) { AntennaHandlers::handleARSub(m_antennaState, *this, c); }});
    // RO$ — inline (custom multi-arg signal).
    // WHY a separate RO$ handler (RIT/XIT offset routing on the K4):
    //   - No split, RIT or XIT active  → offset lives in `RO`  (VFO A, handled below)
    //   - Split + XIT                  → offset lives in `RO$` (VFO B, the TX VFO when split)
    //   - BSET + RIT                   → offset lives in `RO$` (VFO B)
    // We update `m_ritXitOffsetB` and emit the `B`-variant signal so the VFO B display tracks.
    // See `~/.claude/projects/.../memory/MEMORY.md` → "K4 RIT/XIT Offset Registers".
    m_commandHandlers.append(
        {"RO$", [this](const QString &c) { FrequencyVfoHandlers::handleROSub(m_frequencyVfoState, *this, c); }});
    m_commandHandlers.append(
        {"RT$", [this](const QString &c) { FrequencyVfoHandlers::handleRTSub(m_frequencyVfoState, *this, c); }});

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
    // FP — deduplicated via handleIntPair.
    m_commandHandlers.append({"FP", [this](const QString &c) {
                                  handleIntPair(c, 2, m_modeFilterState.filterPosition, 1, 3,
                                                &RadioState::filterPositionChanged);
                              }});
    m_commandHandlers.append({"FX", [this](const QString &c) { handleFX(c); }});
    m_commandHandlers.append({"MD", [this](const QString &c) { handleMD(c); }});
    m_commandHandlers.append({"BL", [this](const QString &c) { handleBL(c); }});
    // BW — deduplicated via ModeFilterHandlers (×10 multiplier).
    m_commandHandlers.append(
        {"BW", [this](const QString &c) { ModeFilterHandlers::handleBW(m_modeFilterState, *this, c); }});
    // IS — deduplicated via handleIntPair.
    m_commandHandlers.append({"IS", [this](const QString &c) {
                                  handleIntPair(c, 2, m_modeFilterState.ifShift, -99999, 99999,
                                                &RadioState::ifShiftChanged);
                              }});
    m_commandHandlers.append({"CW", [this](const QString &c) { handleCW(c); }});
    m_commandHandlers.append({"RG", [this](const QString &c) { LevelsHandlers::handleRG(m_levelsState, *this, c); }});
    m_commandHandlers.append({"SQ", [this](const QString &c) { LevelsHandlers::handleSQ(m_levelsState, *this, c); }});
    m_commandHandlers.append({"MG", [this](const QString &c) { handleMG(c); }});
    m_commandHandlers.append({"ML", [this](const QString &c) { handleML(c); }});
    m_commandHandlers.append({"CP", [this](const QString &c) { handleCP(c); }});
    m_commandHandlers.append({"PC", [this](const QString &c) { handlePC(c); }});
    m_commandHandlers.append({"PS", [this](const QString &c) { PowerHandlers::handlePS(m_powerState, *this, c); }});
    m_commandHandlers.append({"KS", [this](const QString &c) { handleKS(c); }});
    m_commandHandlers.append({"KP", [this](const QString &c) { handleKP(c); }});
    m_commandHandlers.append({"SM", [this](const QString &c) { handleSM(c); }});
    m_commandHandlers.append({"PO", [this](const QString &c) { handlePO(c); }});
    m_commandHandlers.append({"TM", [this](const QString &c) { handleTM(c); }});
    m_commandHandlers.append({"TX", [this](const QString &c) { handleTX(c); }});
    m_commandHandlers.append({"RX", [this](const QString &c) { handleRX(c); }});
    m_commandHandlers.append({"NB", [this](const QString &c) { handleNB(c); }});
    m_commandHandlers.append({"NR", [this](const QString &c) { handleNR(c); }});
    m_commandHandlers.append({"NRS", [this](const QString &c) { handleNRS(c); }});
    // NA — deduplicated via handleBoolPair
    m_commandHandlers.append({"NA", [this](const QString &c) {
                                  handleBoolPair(c, 2, m_processingState.autoNotchEnabled, &RadioState::notchChanged);
                              }});
    m_commandHandlers.append({"NM", [this](const QString &c) { handleNM(c); }});
    m_commandHandlers.append({"PA", [this](const QString &c) { handlePA(c); }});
    m_commandHandlers.append({"RA", [this](const QString &c) { handleRA(c); }});
    m_commandHandlers.append({"GT", [this](const QString &c) { handleGT(c); }});
    m_commandHandlers.append({"AP", [this](const QString &c) { handleAP(c); }});
    m_commandHandlers.append({"LN", [this](const QString &c) { handleLN(c); }});
    // LK — deduplicated via handleBoolPairVal
    m_commandHandlers.append(
        {"LK", [this](const QString &c) { FrequencyVfoHandlers::handleLK(m_frequencyVfoState, *this, c); }});
    m_commandHandlers.append({"LO", [this](const QString &c) { handleLO(c); }});
    m_commandHandlers.append({"LI", [this](const QString &c) { handleLI(c); }});
    // VT — deduplicated inline (takes .left(1), qBound)
    m_commandHandlers.append(
        {"VT", [this](const QString &c) { DataControlHandlers::handleVT(m_dataControlState, *this, c); }});
    m_commandHandlers.append({"VX", [this](const QString &c) { handleVX(c); }});
    m_commandHandlers.append({"VG", [this](const QString &c) { handleVG(c); }});
    m_commandHandlers.append({"VI", [this](const QString &c) { handleVI(c); }});
    m_commandHandlers.append({"MI", [this](const QString &c) { handleMI(c); }});
    m_commandHandlers.append({"MS", [this](const QString &c) { handleMS(c); }});
    m_commandHandlers.append({"MX", [this](const QString &c) { handleMX(c); }});
    m_commandHandlers.append({"MN", [this](const QString &c) { handleMN(c); }});
    m_commandHandlers.append({"ES", [this](const QString &c) { handleES(c); }});
    m_commandHandlers.append({"SD", [this](const QString &c) { handleSD(c); }});
    m_commandHandlers.append({"SL", [this](const QString &c) { handleSL(c); }});
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
    m_commandHandlers.append({"AR", [this](const QString &c) { AntennaHandlers::handleAR(m_antennaState, *this, c); }});
    m_commandHandlers.append({"AT", [this](const QString &c) { handleAT(c); }});
    m_commandHandlers.append({"RT", [this](const QString &c) { handleRT(c); }});
    m_commandHandlers.append({"XT", [this](const QString &c) { handleXT(c); }});
    m_commandHandlers.append({"RO", [this](const QString &c) { handleRO(c); }});
    m_commandHandlers.append({"RE", [this](const QString &c) { handleRE(c); }});
    m_commandHandlers.append({"ID", [this](const QString &c) { handleID(c); }});
    m_commandHandlers.append({"OM", [this](const QString &c) { handleOM(c); }});
    m_commandHandlers.append({"ER", [this](const QString &c) { handleER(c); }});

    // XVTR per-band config — XvtrBandHandlers populates m_xvtrBandState.
    m_commandHandlers.append(
        {"XVN", [this](const QString &c) { XvtrBandHandlers::handleXVN(m_xvtrBandState, *this, c); }});
    m_commandHandlers.append(
        {"XVM", [this](const QString &c) { XvtrBandHandlers::handleXVM(m_xvtrBandState, *this, c); }});
    m_commandHandlers.append(
        {"XVR", [this](const QString &c) { XvtrBandHandlers::handleXVR(m_xvtrBandState, *this, c); }});
    m_commandHandlers.append(
        {"XVI", [this](const QString &c) { XvtrBandHandlers::handleXVI(m_xvtrBandState, *this, c); }});
    m_commandHandlers.append(
        {"XVO", [this](const QString &c) { XvtrBandHandlers::handleXVO(m_xvtrBandState, *this, c); }});

    // Sort by prefix length (longest first) for correct matching
    std::sort(m_commandHandlers.begin(), m_commandHandlers.end(),
              [](const CommandEntry &a, const CommandEntry &b) { return a.prefix.length() > b.prefix.length(); });
}

// =============================================================================
// Individual Command Handlers - VFO/Frequency
// =============================================================================

// Frequency / VFO CAT handlers — delegate to FrequencyVfoHandlers namespace.
void RadioState::handleFA(const QString &cmd) {
    FrequencyVfoHandlers::handleFA(m_frequencyVfoState, *this, cmd);
}
void RadioState::handleFB(const QString &cmd) {
    FrequencyVfoHandlers::handleFB(m_frequencyVfoState, *this, cmd);
}
void RadioState::handleFT(const QString &cmd) {
    FrequencyVfoHandlers::handleFT(m_frequencyVfoState, *this, cmd);
}

// =============================================================================
// Individual Command Handlers - Mode
// =============================================================================

void RadioState::handleMD(const QString &cmd) {
    ModeFilterHandlers::handleMD(m_modeFilterState, *this, cmd);
}
void RadioState::handleMDSub(const QString &cmd) {
    ModeFilterHandlers::handleMDSub(m_modeFilterState, *this, cmd);
}

// =============================================================================
// Individual Command Handlers - Bandwidth/Filter
// =============================================================================

void RadioState::handleCW(const QString &cmd) {
    ModeFilterHandlers::handleCW(m_modeFilterState, *this, cmd);
}

// =============================================================================
// Individual Command Handlers - Gain/Level
// =============================================================================

void RadioState::handleMG(const QString &cmd) {
    LevelsHandlers::handleMG(m_levelsState, *this, cmd);
}

void RadioState::handleCP(const QString &cmd) {
    LevelsHandlers::handleCP(m_levelsState, *this, cmd);
}

void RadioState::handleML(const QString &cmd) {
    AudioEffectsHandlers::handleML(m_audioEffectsState, *this, cmd);
}

void RadioState::handleBL(const QString &cmd) {
    AudioEffectsHandlers::handleBL(m_audioEffectsState, *this, cmd);
}

void RadioState::handleMX(const QString &cmd) {
    AudioEffectsHandlers::handleMX(m_audioEffectsState, *this, cmd);
}

void RadioState::handlePC(const QString &cmd) {
    LevelsHandlers::handlePC(m_levelsState, *this, cmd);
}

void RadioState::handleKS(const QString &cmd) {
    ModeFilterHandlers::handleKS(m_modeFilterState, *this, cmd);
}
void RadioState::handleKP(const QString &cmd) {
    ModeFilterHandlers::handleKP(m_modeFilterState, *this, cmd);
}

void RadioState::setIambicMode(QChar mode) {
    ModeFilterHandlers::setIambicMode(m_modeFilterState, *this, mode);
}
void RadioState::setPaddleOrientation(QChar orientation) {
    ModeFilterHandlers::setPaddleOrientation(m_modeFilterState, *this, orientation);
}
void RadioState::setKeyingWeight(int weight) {
    ModeFilterHandlers::setKeyingWeight(m_modeFilterState, *this, weight);
}

// =============================================================================
// Individual Command Handlers - Meters
// =============================================================================

void RadioState::handleSM(const QString &cmd) {
    RxTxMeterHandlers::handleSM(m_rxTxMeterState, *this, cmd);
}

void RadioState::handleSMSub(const QString &cmd) {
    RxTxMeterHandlers::handleSMSub(m_rxTxMeterState, *this, cmd);
}

void RadioState::handlePO(const QString &cmd) {
    RxTxMeterHandlers::handlePO(m_rxTxMeterState, *this, cmd);
}

void RadioState::handleTM(const QString &cmd) {
    RxTxMeterHandlers::handleTM(m_rxTxMeterState, *this, cmd);
}

// =============================================================================
// Individual Command Handlers - TX/RX State
// =============================================================================

void RadioState::handleTX(const QString &cmd) {
    RxTxMeterHandlers::handleTX(m_rxTxMeterState, *this, cmd);
}

void RadioState::handleRX(const QString &cmd) {
    RxTxMeterHandlers::handleRX(m_rxTxMeterState, *this, cmd);
}

// =============================================================================
// Individual Command Handlers - Processing (NB, NR, PA, RA, GT, NA, NM)
// =============================================================================

// WHY: Handlers in this block (NB/NB$, NR/NR$, PA/PA$, RA/RA$, GT/GT$)
// all emit the cross-cutting `processingChanged()` / `processingChangedB()`
// rollup signal. Compute proposed values first, short-circuit if nothing
// differs, and only then mutate + emit — keeps the rollup idempotent so
// redundant CAT echoes don't trigger spurious UI repaints.

// Processing CAT handlers — delegate to ProcessingHandlers namespace.
void RadioState::handleNB(const QString &cmd) {
    ProcessingHandlers::handleNB(m_processingState, *this, cmd);
}
void RadioState::handleNBSub(const QString &cmd) {
    ProcessingHandlers::handleNBSub(m_processingState, *this, cmd);
}
void RadioState::handleNR(const QString &cmd) {
    ProcessingHandlers::handleNR(m_processingState, *this, cmd);
}
void RadioState::handleNRSub(const QString &cmd) {
    ProcessingHandlers::handleNRSub(m_processingState, *this, cmd);
}
void RadioState::handleNRS(const QString &cmd) {
    ProcessingHandlers::handleNRS(m_processingState, *this, cmd);
}
void RadioState::handleNRSSub(const QString &cmd) {
    ProcessingHandlers::handleNRSSub(m_processingState, *this, cmd);
}
void RadioState::handlePA(const QString &cmd) {
    ProcessingHandlers::handlePA(m_processingState, *this, cmd);
}
void RadioState::handlePASub(const QString &cmd) {
    ProcessingHandlers::handlePASub(m_processingState, *this, cmd);
}
void RadioState::handleRA(const QString &cmd) {
    ProcessingHandlers::handleRA(m_processingState, *this, cmd);
}
void RadioState::handleRASub(const QString &cmd) {
    ProcessingHandlers::handleRASub(m_processingState, *this, cmd);
}
void RadioState::handleGT(const QString &cmd) {
    ProcessingHandlers::handleGT(m_processingState, *this, cmd);
}
void RadioState::handleGTSub(const QString &cmd) {
    ProcessingHandlers::handleGTSub(m_processingState, *this, cmd);
}
void RadioState::handleNM(const QString &cmd) {
    ProcessingHandlers::handleNM(m_processingState, *this, cmd);
}
void RadioState::handleNMSub(const QString &cmd) {
    ProcessingHandlers::handleNMSub(m_processingState, *this, cmd);
}

// =============================================================================
// Individual Command Handlers - Audio/Effects
// =============================================================================

void RadioState::handleFX(const QString &cmd) {
    AudioEffectsHandlers::handleFX(m_audioEffectsState, *this, cmd);
}

void RadioState::handleAP(const QString &cmd) {
    AudioEffectsHandlers::handleAP(m_audioEffectsState, *this, cmd);
}

void RadioState::handleAPSub(const QString &cmd) {
    AudioEffectsHandlers::handleAPSub(m_audioEffectsState, *this, cmd);
}

// =============================================================================
// Individual Command Handlers - VFO Control
// =============================================================================

void RadioState::handleLN(const QString &cmd) {
    FrequencyVfoHandlers::handleLN(m_frequencyVfoState, *this, cmd);
}

// =============================================================================
// Individual Command Handlers - VOX
// =============================================================================

void RadioState::handleVX(const QString &cmd) {
    AudioEffectsHandlers::handleVX(m_audioEffectsState, *this, cmd);
}

void RadioState::handleVG(const QString &cmd) {
    AudioEffectsHandlers::handleVG(m_audioEffectsState, *this, cmd);
}

void RadioState::handleVI(const QString &cmd) {
    AudioEffectsHandlers::handleVI(m_audioEffectsState, *this, cmd);
}

// =============================================================================
// Individual Command Handlers - Audio I/O
// =============================================================================

void RadioState::handleLO(const QString &cmd) {
    AudioEffectsHandlers::handleLO(m_audioEffectsState, *this, cmd);
}

void RadioState::handleLI(const QString &cmd) {
    AudioEffectsHandlers::handleLI(m_audioEffectsState, *this, cmd);
}

void RadioState::handleMI(const QString &cmd) {
    AudioEffectsHandlers::handleMI(m_audioEffectsState, *this, cmd);
}

void RadioState::handleMS(const QString &cmd) {
    AudioEffectsHandlers::handleMS(m_audioEffectsState, *this, cmd);
}

void RadioState::handleES(const QString &cmd) {
    AudioEffectsHandlers::handleES(m_audioEffectsState, *this, cmd);
}

// =============================================================================
// Individual Command Handlers - QSK/Delay
// =============================================================================

void RadioState::handleSD(const QString &cmd) {
    QskHandlers::handleSD(m_qskControlState, *this, cmd);
}

// =============================================================================
// Individual Command Handlers - Control State
// =============================================================================

void RadioState::handleSL(const QString &cmd) {
    DataControlHandlers::handleSL(m_dataControlState, *this, cmd);
}

void RadioState::handleSB(const QString &cmd) {
    RxTxMeterHandlers::handleSB(m_rxTxMeterState, *this, cmd);
}

void RadioState::handleDV(const QString &cmd) {
    RxTxMeterHandlers::handleDV(m_rxTxMeterState, *this, cmd);
}

void RadioState::handleTS(const QString &cmd) {
    RxTxMeterHandlers::handleTS(m_rxTxMeterState, *this, cmd);
}

void RadioState::handleBS(const QString &cmd) {
    RxTxMeterHandlers::handleBS(m_rxTxMeterState, *this, cmd);
}

void RadioState::setBSetEnabled(bool enabled) {
    RxTxMeterHandlers::setBSetEnabled(m_rxTxMeterState, *this, enabled);
}

// =============================================================================
// Individual Command Handlers - Antenna
// =============================================================================

// Antenna CAT handlers — delegate to AntennaHandlers namespace.
void RadioState::handleAN(const QString &cmd) {
    AntennaHandlers::handleAN(m_antennaState, *this, cmd);
}
void RadioState::handleAT(const QString &cmd) {
    AntennaHandlers::handleAT(m_antennaState, *this, cmd);
}
void RadioState::handleACN(const QString &cmd) {
    AntennaHandlers::handleACN(m_antennaState, *this, cmd);
}
void RadioState::handleACM(const QString &cmd) {
    AntennaHandlers::handleACM(m_antennaState, *this, cmd);
}
void RadioState::handleACS(const QString &cmd) {
    AntennaHandlers::handleACS(m_antennaState, *this, cmd);
}
void RadioState::handleACT(const QString &cmd) {
    AntennaHandlers::handleACT(m_antennaState, *this, cmd);
}

// =============================================================================
// Individual Command Handlers - RIT/XIT
// =============================================================================

// RIT / XIT CAT handlers — delegate to FrequencyVfoHandlers namespace.
void RadioState::handleRT(const QString &cmd) {
    FrequencyVfoHandlers::handleRT(m_frequencyVfoState, *this, cmd);
}
void RadioState::handleXT(const QString &cmd) {
    FrequencyVfoHandlers::handleXT(m_frequencyVfoState, *this, cmd);
}
void RadioState::handleRO(const QString &cmd) {
    FrequencyVfoHandlers::handleRO(m_frequencyVfoState, *this, cmd);
}

// =============================================================================
// Individual Command Handlers - Text Decode
// =============================================================================

// Text decode CAT handlers — delegate to TextDecodeHandlers namespace.
void RadioState::handleTD(const QString &cmd) {
    TextDecodeHandlers::handleTD(m_textDecodeState, *this, cmd);
}
void RadioState::handleTDSub(const QString &cmd) {
    TextDecodeHandlers::handleTDSub(m_textDecodeState, *this, cmd);
}
void RadioState::handleTB(const QString &cmd) {
    TextDecodeHandlers::handleTB(*this, cmd);
}
void RadioState::handleTBSub(const QString &cmd) {
    TextDecodeHandlers::handleTBSub(*this, cmd);
}

// =============================================================================
// Individual Command Handlers - Data Mode
// =============================================================================

void RadioState::handleDT(const QString &cmd) {
    DataControlHandlers::handleDT(m_dataControlState, *this, cmd);
}
void RadioState::handleDTSub(const QString &cmd) {
    DataControlHandlers::handleDTSub(m_dataControlState, *this, cmd);
}
void RadioState::handleDR(const QString &cmd) {
    DataControlHandlers::handleDR(m_dataControlState, *this, cmd);
}
void RadioState::handleDRSub(const QString &cmd) {
    DataControlHandlers::handleDRSub(m_dataControlState, *this, cmd);
}

// =============================================================================
// Individual Command Handlers - Equalizer
// =============================================================================

void RadioState::handleRE(const QString &cmd) {
    AudioEffectsHandlers::handleRE(m_audioEffectsState, *this, cmd);
}

void RadioState::handleTE(const QString &cmd) {
    AudioEffectsHandlers::handleTE(m_audioEffectsState, *this, cmd);
}

// =============================================================================
// Individual Command Handlers - Radio Info
// =============================================================================

void RadioState::handleID(const QString &cmd) {
    RxTxMeterHandlers::handleID(m_rxTxMeterState, *this, cmd);
}

void RadioState::handleOM(const QString &cmd) {
    RxTxMeterHandlers::handleOM(m_rxTxMeterState, *this, cmd);
}

void RadioState::handleRV(const QString &cmd) {
    RxTxMeterHandlers::handleRV(m_rxTxMeterState, *this, cmd);
}

void RadioState::handleSIFP(const QString &cmd) {
    RxTxMeterHandlers::handleSIFP(m_rxTxMeterState, *this, cmd);
}

void RadioState::handleSIRF(const QString &cmd) {
    RxTxMeterHandlers::handleSIRF(m_rxTxMeterState, *this, cmd);
}

void RadioState::handleMN(const QString &cmd) {
    RxTxMeterHandlers::handleMN(m_rxTxMeterState, *this, cmd);
}

void RadioState::handleER(const QString &cmd) {
    RxTxMeterHandlers::handleER(m_rxTxMeterState, *this, cmd);
}

// Mode-dispatched accessors/setters.
// The K4 partitions several settings (monitor level, VOX, QSK/VOX delay) into
// CW / DATA / Voice buckets; these helpers select the right bucket based on
// the current operating mode so callers don't reimplement the switch.
int RadioState::monitorLevelForCurrentMode() const {
    switch (mode()) {
    case CW:
    case CW_R:
        return m_audioEffectsState.monitorLevelCW;
    case DATA:
    case DATA_R:
        return m_audioEffectsState.monitorLevelData;
    default:
        return m_audioEffectsState.monitorLevelVoice;
    }
}

int RadioState::monitorModeCode() const {
    switch (mode()) {
    case CW:
    case CW_R:
        return 0;
    case DATA:
    case DATA_R:
        return 1;
    default:
        return 2;
    }
}

bool RadioState::voxForCurrentMode() const {
    switch (mode()) {
    case CW:
    case CW_R:
        return m_audioEffectsState.voxCW;
    case DATA:
    case DATA_R:
        return m_audioEffectsState.voxData;
    default:
        return m_audioEffectsState.voxVoice;
    }
}

int RadioState::delayForCurrentMode() const {
    switch (mode()) {
    case CW:
    case CW_R:
        return m_qskControlState.qskDelayCW;
    case DATA:
    case DATA_R:
        return m_qskControlState.qskDelayData;
    default:
        return m_qskControlState.qskDelayVoice;
    }
}

void RadioState::setDelayForCurrentMode(int delay) {
    delay = qBound(0, delay, 255);
    int *target = nullptr;
    switch (mode()) {
    case CW:
    case CW_R:
        target = &m_qskControlState.qskDelayCW;
        break;
    case DATA:
    case DATA_R:
        target = &m_qskControlState.qskDelayData;
        break;
    default:
        target = &m_qskControlState.qskDelayVoice;
        break;
    }
    if (*target != delay) {
        *target = delay;
        emit qskDelayChanged(delay);
    }
}
