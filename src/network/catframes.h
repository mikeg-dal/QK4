#pragma once

#include "models/radiostate.h"

#include "models/radiostate/levelsstate.h"
#include <QByteArray>
#include <QList>
#include <QString>
#include <QVector>

namespace CatFrames {

QByteArray frequencyA(quint64 hz);
QByteArray frequencyB(quint64 hz);

QByteArray modeA(RadioState::Mode m);
QByteArray modeB(RadioState::Mode m);

QByteArray ptt(bool transmitting);
QByteArray split(bool enabled);

// Sub receiver on/off. SB0 is off and SB1 is on; the radio also reports SB3, which is the sub
// receiver on with its mini-pan running - the K4 requires it for the Sub mini-pan and moves between
// SB1 and SB3 itself when #MP$ toggles. This builder never sends 3 - the mini-pan is driven by #MP$,
// not SB - but RadioState treats SB3 as enabled. Diversity is DV, not SB.
QByteArray subReceiver(bool enabled);
QByteArray ritOffset(int offset);
QByteArray ritEnabled(bool en);
QByteArray xitEnabled(bool en);
// PC / PCX replies. Both take the PowerRange because the wire encoding depends on it: the K4
// reports the QRP and XVTR ranges in TENTHS, so a value in watts (or mW) must be scaled by 10.
// Passing a bool could never express the XVTR range at all. See CAT-005.
QByteArray rfPower(double value, LevelsState::PowerRange range);
QByteArray rfPowerExtended(double value, LevelsState::PowerRange range);
QByteArray filterWidthExtended(int bwHz);
// KSnnn. CLAMPED to the K4's documented range (manual: "from 8 to 100 WPM"), because the CW macro
// grammar reaches this with ARITHMETIC - each '>' adds 5 WPM with no upper bound of its own - and
// an out-of-range KS is a command the radio will not act on.
QByteArray keyerSpeed(int wpm);

// CW text as one or more KY commands.
//
// A LIST, unlike every other builder here, because the K4 takes at most 60 characters per KY and a
// contest exchange is routinely longer. Splitting is the K4's constraint, so it belongs with the
// K4's command rather than in the caller. Not padded: the padding other Elecraft drivers use
// guards against a short KY following a keyer abort, which is a flow QK4 does not have.
//
// `wait` selects the KYW form on the LAST chunk, which delays the radio's processing of following
// host commands until the message has been sent. Pass it when a KS follows, which is the use the
// manual names. Do NOT pass it otherwise: it stalls everything QK4 sends afterwards - polling
// included - for the duration of the message.
//
// Prosigns arrive in TCI's |XX| form and are translated here, because their spelling is a K4 fact.
QList<QByteArray> cwText(const QString &text, bool wait = false);

// Will a KY sent in this mode key anything?
//
// The manual calls KY "CW/DATA Message Text": the radio sends it as Morse in CW and CW-REVERSE, and
// as data in the DATA modes. In SSB, AM and FM it is DISCARDED IN SILENCE - no keying, no error, no
// response of any kind. Confirmed on a K4: with the radio in LSB, a correctly formed KY produced
// nothing at all.
//
// A K4 fact, so it lives beside the command it describes. QK4 does NOT change the mode on the
// strength of it - that would be a surprising side effect of a text command - it only says so in
// the log, which is the only place the silence can be explained.
bool modeKeysCwText(RadioState::Mode mode);

// Aborts a message in progress: KY<0x04>;RX;. One frame carrying two commands, which is how the
// radio is given it. Bench-confirmed on Elecraft hardware (TR4W's TK4Radio.StopCW).
QByteArray cwAbort();
// NBnnm: nn is the level 00-15, m is on/off. The level is preserved by the caller, because TCI's
// RX_NB_ENABLE is on/off only and must not silently move the level. The radio uses the same form
// in both directions, so this is also the reply to a CAT client's `NB;`.
// NBnnm, or NBnnmf when filterWidth is given (0-2). Pass filterWidth < 0 to leave it off the
// command and change only the level and the on/off flag.
QByteArray setNoiseBlanker(int level, bool on, int filterWidth = -1);

// #NB$n - the panadapter (DDC) noise blanker: 0 off, 1 on, 2 auto (follows the receiver NB).
// Not the receiver noise blanker, which is NB / NB$. Out-of-range modes are clamped.
QByteArray ddcNoiseBlanker(int mode);

// #AVGnn - panadapter averaging, 01..20 frames (K4 reference: SET/RESP #AVGnn;). Clamped.
QByteArray averaging(int frames);

// LKn / LK$n - VFO tuning lock, per VFO.
QByteArray setVfoLock(bool locked, bool subVfo);

// BWnnnn in 10-Hz units, which is the inverse of what RadioState's handleBW parses. The radio uses
// the same form in both directions, so this is also the reply to a CAT client's `BW;`.
QByteArray setFilterBandwidth(int bwHz);

// ACMzabcdefg; / ACSzabcdefg; / ACTzabc; - antenna configuration from the ANT CFG popups.
// z is display-all, then one enable digit per antenna: `count` of them (7 for RX, 3 for TX), with
// any missing from `mask` sent as disabled.
QByteArray setAntennaConfig(const char *prefix, bool displayAll, const QVector<bool> &mask, int count);

// MEnnnn.vvvv - set a K4 MENU item to an absolute value. MenuController already sends the
// relative forms (ME0069.+, .-, ./); this is the absolute one.
QByteArray setMenuValue(int menuId, int value);

// PCnnnr: nnn is watts in QRO and tenths in QRP and XVTR (PC100L is 10.0 W), r is the range letter.
// `value` is in the range's own unit: W for QRO and QRP, mW for XVTR.
// NOT the PCX form below - that one is the extended QUERY, and the radio ignores it as a set.
QByteArray setRfPower(double value, LevelsState::PowerRange range);

// NRnnm: nn is the level, m is on/off - the radio's form in both directions (NR050 is level 05, off).
QByteArray noiseReduction(int level, bool on);
// GTn, one digit: 0 off, 1 slow, 2 fast - the radio's form in both directions (GT2 at R40).
QByteArray agcSpeed(int agc);
// SQnnn - the radio's form (SQ010 at R40); subRx() gives SQ$nnn. Clamped to 0-40.
QByteArray squelch(int level);
// RG-nn - the radio's form (RG-00 at R40); subRx() gives RG$-nn. RadioState stores the magnitude.
QByteArray rfGain(int magnitude);
// The Sub RX form of a two-letter main frame: `$` after the prefix (RT0; -> RT$0;). Every Sub RX
// reply measured on a K4D at R40 has this shape: RO$+0000, RT$0, GT$2, NB$0301, NR$050, BW$0020, DT$0.
QByteArray subRx(const QByteArray &mainFrame);
// VXmn: the radio keeps VOX per mode group (C = CW and direct data, V = voice, D = AF data) and
// answers a bare `VX;` with the group of the CURRENT mode - measured on a K4D at R40, in DATA: VXD0
// while VXC1 was on.
QByteArray vox(const RadioState &rs);
QByteArray diversity(bool on);
QByteArray dataSubMode(int subMode);
QByteArray aiMode(int level);

QByteArray sMeterMain(double sMeter);
// SMH±nnn / SMH$±nnn: signal level in dBm, a sign and three digits - the radio's form (SMH-110,
// SMH$-122 from a K4D, 2026-10-30).
QByteArray signalLevelDbm(int dbm, bool sub);

QByteArray txMeter(int alc, int compression, double fwdPower, double swr, bool qrp);

QByteArray ifFrame(const RadioState &state);

// KP<iambic><paddle><weight>; - iambic A/B, paddle N/R, weight 090-125 (hundredths). The K4 takes all
// three fields even when only one changes, so a field not yet reported by the radio (null QChar,
// negative weight) is sent as its default: A, N, 100.
QByteArray keyerPaddle(QChar iambic, QChar paddle, int weight);

} // namespace CatFrames
