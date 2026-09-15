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
