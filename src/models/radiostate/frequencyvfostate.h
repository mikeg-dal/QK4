#ifndef MODELS_RADIOSTATE_FREQUENCYVFOSTATE_H
#define MODELS_RADIOSTATE_FREQUENCYVFOSTATE_H

#include "reportedflag.h"

#include <QString>
#include <QStringView>
#include <QtGlobal>

#include <limits>
#include <optional>

class RadioState;

// VFO A/B frequency, split state, and RIT/XIT offset registers.
// Handles FA, FB, FT, RT, RT$, XT, RO, RO$ — the CAT commands that
// together determine "what frequency is the K4 tuned to right now?"
//
// See memory/MEMORY.md → K4 RIT/XIT Offset Registers for the K4's
// quirky routing of XIT offset through RO$ when split is on.
struct FrequencyVfoState {
    quint64 frequency = 0; // Convenience alias for vfoA (legacy)
    quint64 vfoA = 0;
    quint64 vfoB = 0;

    ReportedFlag splitEnabled; // FT

    // RIT / XIT state + offsets. Main RX on RO, Sub RX / split-XIT on RO$.
    // RO / RO$ carry ±9999 Hz, so RitXitOffsetUnknown can never arrive on the wire.
    static constexpr int RitXitOffsetUnknown = (std::numeric_limits<int>::min)();
    ReportedFlag ritEnabled;
    ReportedFlag xitEnabled;
    int ritXitOffset = RitXitOffsetUnknown;
    ReportedFlag ritEnabledB;
    int ritXitOffsetB = RitXitOffsetUnknown;

    // VFO link (LN) and per-VFO lock (LK / LK$).
    ReportedFlag vfoLink;
    ReportedFlag lockA;
    ReportedFlag lockB;

    void reset();
};

namespace FrequencyVfoHandlers {
/// The frequency in Hz that an FA/FB argument (the digits after "FA"/"FB", no ';') sets, read the
/// way the K4 reads it (Programmer's Reference D12, FA): 1-2 digits are MHz, 3-5 kHz, 6-11 Hz, so
/// FA14; is 14 MHz and FA7100; 7.1 MHz. The radio itself always reports 11 digits in Hz. nullopt
/// for anything but 1-11 ASCII digits. No band check: transverter frequencies pass through.
std::optional<quint64> frequencyFromCatDigits(QStringView digits);

void handleFA(FrequencyVfoState &state, RadioState &owner, const QString &cmd);
void handleFB(FrequencyVfoState &state, RadioState &owner, const QString &cmd);
void handleFT(FrequencyVfoState &state, RadioState &owner, const QString &cmd);
void handleRT(FrequencyVfoState &state, RadioState &owner, const QString &cmd);
void handleRTSub(FrequencyVfoState &state, RadioState &owner, const QString &cmd);
void handleXT(FrequencyVfoState &state, RadioState &owner, const QString &cmd);
void handleRO(FrequencyVfoState &state, RadioState &owner, const QString &cmd);
void handleROSub(FrequencyVfoState &state, RadioState &owner, const QString &cmd);
void handleLN(FrequencyVfoState &state, RadioState &owner, const QString &cmd);
void handleLK(FrequencyVfoState &state, RadioState &owner, const QString &cmd);
void handleLKSub(FrequencyVfoState &state, RadioState &owner, const QString &cmd);

} // namespace FrequencyVfoHandlers

#endif // MODELS_RADIOSTATE_FREQUENCYVFOSTATE_H
