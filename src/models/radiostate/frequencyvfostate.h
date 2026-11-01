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
// Handles FA, FB, BN, BN$, FT, RT, RT$, XT, RO, RO$ — the CAT commands that
// together determine "what frequency is the K4 tuned to right now?"
//
// See docs/k4-protocol-quirks.md §2 for the K4's routing of the XIT offset
// through RO$ when split is on.
struct FrequencyVfoState {
    quint64 vfoA = 0;
    quint64 vfoB = 0;

    // BN / BN$ — the band the radio says each VFO is on (Programmer's Reference D12, BN$: "nn is
    // the band number: 00 = 160 m ...10 = 6 m"; XVTR bands per RadioUtils::isXvtrBandNumber).
    // BandUnknown until the radio reports one; it never sends a negative band.
    static constexpr int BandUnknown = -1;
    static constexpr int BandNumberMax = 25; // highest number D12 gives a meaning (BN$: "16-25")
    int bandNumber = BandUnknown;
    int bandNumberB = BandUnknown;

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
void handleBN(FrequencyVfoState &state, RadioState &owner, const QString &cmd);
void handleBNSub(FrequencyVfoState &state, RadioState &owner, const QString &cmd);
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
