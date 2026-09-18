#pragma once

#include "models/radiostate.h"

#include <QByteArray>
#include <QList>
#include <QString>

namespace CatFrames {

QByteArray frequencyA(quint64 hz);
QByteArray frequencyB(quint64 hz);

QByteArray modeA(RadioState::Mode m);
QByteArray modeB(RadioState::Mode m);

QByteArray ptt(bool transmitting);
QByteArray split(bool enabled);

// Sub receiver on/off. SB0 is off and SB1 is on; the radio also reports SB3, which is the sub
// receiver on in diversity mode. This builder never sends 3 - turning diversity on is a separate
// decision from turning the sub receiver on - but a radio already in diversity reports SB3 and
// RadioState treats that as enabled.
QByteArray subReceiver(bool enabled);
QByteArray ritOffset(int offset);
QByteArray ritEnabled(bool en);
QByteArray xitEnabled(bool en);
QByteArray rfPower(double watts);
QByteArray rfPowerExtended(double watts, bool qrp);
QByteArray filterBandwidth(int bwHz);
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
// WHY THESE TWO EXIST ALONGSIDE noiseBlanker() AND filterBandwidth() BELOW.
//
// The pair below build REPLIES to a CAT client (catserver.cpp, catpushbroadcaster.cpp). They have
// never been used to send anything TO the radio, and neither is in the K4's actual command form:
// noiseBlanker() emits "NB1;" where the K4 wants NBnnm, and filterBandwidth() emits the width in
// Hz where the K4 wants 10-Hz units. Both are pre-existing reply-direction bugs, reported
// separately rather than changed here - altering them would change what every existing CAT client
// on port 9299 is told.
//
// These two are for SENDING, and match what QK4's own UI already sends by hand
// (sidecontrolscrollcontroller.cpp divides the bandwidth by 10; featuremenucontroller.cpp uses the
// NB/ toggle).

// NBnnm: nn is the level 00-15, m is on/off. The level is preserved by the caller, because TCI's
// RX_NB_ENABLE is on/off only and must not silently move the level.
// NBnnm, or NBnnmf when filterWidth is given (0-2). Pass filterWidth < 0 to leave it off the
// command and change only the level and the on/off flag.
QByteArray setNoiseBlanker(int level, bool on, int filterWidth = -1);

// LKn / LK$n - VFO tuning lock, per VFO.
QByteArray setVfoLock(bool locked, bool subVfo);

// BWnnnn in 10-Hz units, which is the inverse of what RadioState's handleBW parses.
QByteArray setFilterBandwidth(int bwHz);

// MEnnnn.vvvv - set a K4 MENU item to an absolute value. MenuController already sends the
// relative forms (ME0069.+, .-, ./); this is the absolute one.
QByteArray setMenuValue(int menuId, int value);

// PCnnnr: nnn is watts in QRO and watts*10 in QRP (PC100L is 10.0 W), r is the range letter.
// NOT the PCX form below - that one is the extended QUERY, and the radio ignores it as a set.
QByteArray setRfPower(int value, bool qrp);

QByteArray noiseBlanker(bool on);
QByteArray noiseReduction(bool on);
QByteArray agcSpeed(int agc);
QByteArray vox(bool on);
QByteArray diversity(bool on);
QByteArray dataSubMode(int subMode);
QByteArray aiMode(int level);

QByteArray sMeterMain(double sMeter);

QByteArray txMeter(int alc, int compression, double fwdPower, double swr, bool qrp);

QByteArray ifFrame(const RadioState &state);

} // namespace CatFrames
