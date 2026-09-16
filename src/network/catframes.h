#pragma once

#include "models/radiostate.h"

#include <QByteArray>

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
QByteArray keyerSpeed(int wpm);
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
