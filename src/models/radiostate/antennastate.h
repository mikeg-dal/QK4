#ifndef MODELS_RADIOSTATE_ANTENNASTATE_H
#define MODELS_RADIOSTATE_ANTENNASTATE_H

#include "reportedflag.h"

#include <QMap>
#include <QString>
#include <QVector>

class RadioState;

// Antenna state: TX antenna selection (AN), RX antenna selection per VFO
// (AR/AR$), ATU mode (AT), per-index antenna names (ACN), and the three
// antenna config masks (ACM/ACS/ACT) that determine which antennas appear
// in the menu popups.
//
// Plain struct — follows Pattern C (see PATTERNS.md → Subsystem State).
struct AntennaState {
    int selectedAntenna = -1;        // AN (1-6)
    int receiveAntenna = -1;         // AR (0-7)
    int receiveAntennaSub = -1;      // AR$ (0-7)
    int atuMode = -1;                // AT (0=not installed, 1=bypass, 2=auto)
    QMap<int, QString> antennaNames; // ACN: antenna 1-7 display names

    // ACM — Main RX mask: which antennas to offer in the main RX popup
    // displayAll=true lets the user pick from all; false limits to masked. Unknown until the radio
    // reports it (the getters read unknown as true), so the first ACM/ACS/ACT always emits.
    ReportedFlag mainRxDisplayAll;
    bool mainRxAntMask[7] = {false, false, false, false, false, false, false};

    // ACS — Sub RX mask
    ReportedFlag subRxDisplayAll;
    bool subRxAntMask[7] = {false, false, false, false, false, false, false};

    // ACT — TX mask (only 3 TX antennas on the K4)
    ReportedFlag txDisplayAll;
    bool txAntMask[3] = {false, false, false};

    void reset();
};

namespace AntennaHandlers {

// CAT handlers.
void handleAN(AntennaState &state, RadioState &owner, const QString &cmd);
void handleAR(AntennaState &state, RadioState &owner, const QString &cmd);    // AR (2-char prefix)
void handleARSub(AntennaState &state, RadioState &owner, const QString &cmd); // AR$
void handleAT(AntennaState &state, RadioState &owner, const QString &cmd);
void handleACN(AntennaState &state, RadioState &owner, const QString &cmd);
void handleACM(AntennaState &state, RadioState &owner, const QString &cmd);
void handleACS(AntennaState &state, RadioState &owner, const QString &cmd);
void handleACT(AntennaState &state, RadioState &owner, const QString &cmd);

// Optimistic config setters (called from AntennaConfigController after user edits).
void setMainRxAntConfig(AntennaState &state, RadioState &owner, bool displayAll, const QVector<bool> &mask);
void setSubRxAntConfig(AntennaState &state, RadioState &owner, bool displayAll, const QVector<bool> &mask);
void setTxAntConfig(AntennaState &state, RadioState &owner, bool displayAll, const QVector<bool> &mask);

} // namespace AntennaHandlers

#endif // MODELS_RADIOSTATE_ANTENNASTATE_H
