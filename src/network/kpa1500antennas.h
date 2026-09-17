#ifndef KPA1500ANTENNAS_H
#define KPA1500ANTENNAS_H

#include <QString>
#include <QVector>

// KPA1500 antenna numbering (firmware 3.00+), with no socket or widget in it so it can be tested.
//
// The amp has two physical connectors, ANT1 and ANT2, but numbers antennas 1 through 32 per band.
// Antenna 1 is always connector ANT1 and antenna 2 always ANT2; antennas 3-32 are "sub-antennas"
// the operator routes through either connector. Which are enabled is the operator's configuration
// on the amp - QK4 only reads it (^AEbbALL;) and never sends an ^AE SET.
//
// Syntax source: KPA1500 Programming Reference V3 (firmware 03.0), ^AE and ^AN.
namespace Kpa1500Antennas {

constexpr int kMaxAntenna = 32;

// Connector for each antenna 1-32 from the 32 characters that follow "^AEbbALL": 'D' disabled -> 0,
// '1' -> 1, '2' -> 2. Index 0 is antenna 1. Empty when the input is not exactly 32 valid characters.
QVector<int> parseEnableMap(const QString &map32);

// Label for the active antenna: "ANT1" and "ANT2" for antennas 1 and 2, "ANT<connector>:<antenna>"
// for a sub-antenna ("ANT1:5"), or "ANT:<antenna>" while its connector is not known yet. Empty for
// an antenna number outside 1-32.
QString label(int antenna, int connector);

} // namespace Kpa1500Antennas

#endif // KPA1500ANTENNAS_H
