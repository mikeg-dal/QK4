#ifndef MODELS_RADIOSTATE_PROCESSINGSTATE_H
#define MODELS_RADIOSTATE_PROCESSINGSTATE_H

#include "reportedflag.h"

#include <QString>

class RadioState;

// Per-VFO signal-processing state: NB / NR / PA / RA / GT (AGC) plus the
// automatic-notch (NA) and manual-notch (NM) subsystems. Mirrors what the
// K4 front panel groups under "RX DSP" + preamp/attenuator controls.
//
// Main-RX and Sub-RX state are kept together on this struct rather than
// split further because (a) the rollup signal processingChanged /
// processingChangedB fires for any change to either's NB/NR/PA/RA/GT
// bundle, so they already share the emit boundary, and (b) the handler
// pairs (handleNB / handleNBSub etc.) are structurally identical so
// keeping them colocated makes the dedup opportunity obvious.
//
// agcSpeed is stored as int to avoid a circular include with radiostate.h
// (RadioState::AGCSpeed is defined inside RadioState). 0=Off 1=Slow 2=Fast
// matches the AGCSpeed enum — see the getter in radiostate.h which casts.
//
// Every field is unknown until the radio reports it (ints hold Unknown, flags are ReportedFlag — see
// reportedflag.h), so the radio's first report always emits. The RadioState getters read unknown
// as the values QK4 has always shown before a first report: off, level 0, notch 1000 Hz, AGC Slow.
struct ProcessingState {
    // Every int below is ≥ 0 on the wire.
    static constexpr int Unknown = -1;
    static constexpr int ManualNotchPitchDefault = 1000; // 150-5000 Hz

    // Main RX
    int noiseBlankerLevel = Unknown;
    ReportedFlag noiseBlankerEnabled;
    int noiseBlankerFilterWidth = Unknown; // 0=NONE, 1=NARROW, 2=WIDE
    int noiseReductionLevel = Unknown;
    ReportedFlag noiseReductionEnabled;
    int ssnrLevel = Unknown; // Spectral-subtraction NR (LMS NR's peer; mutually exclusive on K4)
    ReportedFlag ssnrEnabled;
    ReportedFlag autoNotchEnabled;
    ReportedFlag manualNotchEnabled;
    int manualNotchPitch = Unknown;
    int preamp = Unknown;
    ReportedFlag preampEnabled;
    int attenuatorLevel = Unknown;
    ReportedFlag attenuatorEnabled;
    int agcSpeed = Unknown;

    // Sub RX
    int noiseBlankerLevelB = Unknown;
    ReportedFlag noiseBlankerEnabledB;
    int noiseBlankerFilterWidthB = Unknown;
    int noiseReductionLevelB = Unknown;
    ReportedFlag noiseReductionEnabledB;
    int ssnrLevelB = Unknown;
    ReportedFlag ssnrEnabledB;
    ReportedFlag autoNotchEnabledB;
    ReportedFlag manualNotchEnabledB;
    int manualNotchPitchB = Unknown;
    int preampB = Unknown;
    ReportedFlag preampEnabledB;
    int attenuatorLevelB = Unknown;
    ReportedFlag attenuatorEnabledB;
    int agcSpeedB = Unknown;

    void reset();
};

namespace ProcessingHandlers {

// CAT handlers.
void handleNB(ProcessingState &state, RadioState &owner, const QString &cmd);
void handleNBSub(ProcessingState &state, RadioState &owner, const QString &cmd);
void handleNR(ProcessingState &state, RadioState &owner, const QString &cmd);
void handleNRSub(ProcessingState &state, RadioState &owner, const QString &cmd);
void handleNRS(ProcessingState &state, RadioState &owner, const QString &cmd);
void handleNRSSub(ProcessingState &state, RadioState &owner, const QString &cmd);
void handlePA(ProcessingState &state, RadioState &owner, const QString &cmd);
void handlePASub(ProcessingState &state, RadioState &owner, const QString &cmd);
void handleRA(ProcessingState &state, RadioState &owner, const QString &cmd);
void handleRASub(ProcessingState &state, RadioState &owner, const QString &cmd);
void handleGT(ProcessingState &state, RadioState &owner, const QString &cmd);
void handleGTSub(ProcessingState &state, RadioState &owner, const QString &cmd);
void handleNM(ProcessingState &state, RadioState &owner, const QString &cmd);
void handleNMSub(ProcessingState &state, RadioState &owner, const QString &cmd);

// Optimistic setters.
void setNoiseBlankerLevel(ProcessingState &state, RadioState &owner, int level);
void setNoiseBlankerLevelB(ProcessingState &state, RadioState &owner, int level);
void setNoiseBlankerFilter(ProcessingState &state, RadioState &owner, int filter);
void setNoiseBlankerFilterB(ProcessingState &state, RadioState &owner, int filter);
void setNoiseReductionLevel(ProcessingState &state, RadioState &owner, int level);
void setNoiseReductionLevelB(ProcessingState &state, RadioState &owner, int level);
void setSsnrLevel(ProcessingState &state, RadioState &owner, int level);
void setSsnrLevelB(ProcessingState &state, RadioState &owner, int level);
void setManualNotchPitch(ProcessingState &state, RadioState &owner, int pitch);
void setManualNotchPitchB(ProcessingState &state, RadioState &owner, int pitch);

} // namespace ProcessingHandlers

#endif // MODELS_RADIOSTATE_PROCESSINGSTATE_H
