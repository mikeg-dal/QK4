#include "catframes.h"

#include <QChar>
#include <QString>
#include <QtGlobal>

namespace {
int k4ModeDigit(RadioState::Mode mode) {
    return (mode == RadioState::Unknown) ? 2 : static_cast<int>(mode);
}
} // namespace

namespace CatFrames {

QByteArray frequencyA(quint64 hz) {
    return QString("FA%1;").arg(hz, 11, 10, QChar('0')).toUtf8();
}

QByteArray frequencyB(quint64 hz) {
    return QString("FB%1;").arg(hz, 11, 10, QChar('0')).toUtf8();
}

QByteArray modeA(RadioState::Mode m) {
    return QString("MD%1;").arg(k4ModeDigit(m)).toUtf8();
}

QByteArray modeB(RadioState::Mode m) {
    return QString("MD$%1;").arg(k4ModeDigit(m)).toUtf8();
}

QByteArray ptt(bool transmitting) {
    return QString("TQ%1;").arg(transmitting ? 1 : 0).toUtf8();
}

QByteArray split(bool enabled) {
    return QString("FT%1;").arg(enabled ? 1 : 0).toUtf8();
}

QByteArray subReceiver(bool enabled) {
    return QString("SB%1;").arg(enabled ? 1 : 0).toUtf8();
}

QByteArray ritOffset(int offset) {
    return QString("RO%1%2;").arg(offset >= 0 ? "+" : "-").arg(qAbs(offset), 4, 10, QChar('0')).toUtf8();
}

QByteArray ritEnabled(bool en) {
    return QString("RT%1;").arg(en ? 1 : 0).toUtf8();
}

QByteArray xitEnabled(bool en) {
    return QString("XT%1;").arg(en ? 1 : 0).toUtf8();
}

QByteArray rfPower(double watts) {
    return QString("PC%1;").arg(static_cast<int>(watts), 3, 10, QChar('0')).toUtf8();
}

QByteArray rfPowerExtended(double watts, bool qrp) {
    return QString("PCX%1%2;").arg(static_cast<int>(watts), 3, 10, QChar('0')).arg(qrp ? "L" : "H").toUtf8();
}

QByteArray filterBandwidth(int bwHz) {
    return QString("BW%1;").arg(bwHz, 4, 10, QChar('0')).toUtf8();
}

QByteArray filterWidthExtended(int bwHz) {
    return QString("FW%1;").arg(bwHz, 8, 10, QChar('0')).toUtf8();
}

QByteArray keyerSpeed(int wpm) {
    return QString("KS%1;").arg(wpm, 3, 10, QChar('0')).toUtf8();
}

QByteArray setNoiseBlanker(int level, bool on) {
    return QString("NB%1%2;").arg(qBound(0, level, 15), 2, 10, QChar('0')).arg(on ? 1 : 0).toUtf8();
}

QByteArray setRfPower(int value, bool qrp) {
    // Matches what QK4's own UI sends (sidecontrolscrollcontroller.cpp) and what the radio echoes
    // back, PC045H. The QRP range is reported in tenths, so a request in watts is scaled.
    const int raw = qrp ? qBound(0, value * 10, 100) : qBound(0, value, 110);
    return QString("PC%1%2;").arg(raw, 3, 10, QChar('0')).arg(qrp ? "L" : "H").toUtf8();
}

QByteArray setFilterBandwidth(int bwHz) {
    // The K4 takes 10-Hz units: BW0280 is 2800 Hz. Sending Hz directly asks for ten times the
    // width, which the radio ignores as out of range.
    return QString("BW%1;").arg(qBound(0, bwHz / 10, 9999), 4, 10, QChar('0')).toUtf8();
}

QByteArray noiseBlanker(bool on) {
    return QString("NB%1;").arg(on ? 1 : 0).toUtf8();
}

QByteArray noiseReduction(bool on) {
    return QString("NR%1;").arg(on ? 1 : 0).toUtf8();
}

QByteArray agcSpeed(int agc) {
    return QString("GT%1;").arg(agc, 3, 10, QChar('0')).toUtf8();
}

QByteArray vox(bool on) {
    return QString("VX%1;").arg(on ? 1 : 0).toUtf8();
}

QByteArray diversity(bool on) {
    return QString("DV%1;").arg(on ? 1 : 0).toUtf8();
}

QByteArray dataSubMode(int subMode) {
    return QString("DT%1;").arg(subMode).toUtf8();
}

QByteArray aiMode(int level) {
    return QString("AI%1;").arg(level).toUtf8();
}

QByteArray sMeterMain(double sMeter) {
    // WHY: Inverse of handleSM() in rxtxmeterstate.cpp:32-46. K4 SM format is the
    // raw bar count: 0..18 = S0..S9 (2 bars per S-unit), 19+ encodes dB-over-S9
    // (each unit = 10 dB). The old "sMeter*3" math saturated at 21 for anything
    // >= S7, causing MacLoggerDX's meter to read full-scale on normal noise floor.
    int bars;
    if (sMeter <= 9.0) {
        bars = qBound(0, static_cast<int>(sMeter * 2.0 + 0.5), 18);
    } else {
        bars = qBound(18, static_cast<int>(18 + (sMeter - 9.0) + 0.5), 30);
    }
    return QString("SM%1;").arg(bars, 4, 10, QChar('0')).toUtf8();
}

QByteArray txMeter(int alc, int compression, double fwdPower, double swr, bool qrp) {
    // WHY: K4 TM format is TMaaabbbcccddd; — 4 fields × 3 digits.
    // aaa=ALC, bbb=compression, ccc=forward power (×10 in QRP mode), ddd=SWR×10.
    // Matches the inverse of handleTM() in rxtxmeterstate.cpp:69-93.
    const int fwdField = qBound(0, static_cast<int>(qrp ? fwdPower * 10.0 : fwdPower), 999);
    const int swrField = qBound(0, static_cast<int>(swr * 10.0 + 0.5), 999);
    return QString("TM%1%2%3%4;")
        .arg(qBound(0, alc, 999), 3, 10, QChar('0'))
        .arg(qBound(0, compression, 999), 3, 10, QChar('0'))
        .arg(fwdField, 3, 10, QChar('0'))
        .arg(swrField, 3, 10, QChar('0'))
        .toUtf8();
}

QByteArray ifFrame(const RadioState &state) {
    // WHY: IF command - K4 basic radio information (38 chars total).
    // Per K4 CAT spec (K3-compat, K31 extended) — byte-exact layout:
    //   IF[freq:11]     [+/-][offset:4][r][x] 00[t][m]0[s][p][b][d]1 ;
    // where r=RIT, x=XIT, t=TX, m=mode (1 digit), s=scan, p=split,
    // b=band-change flag, d=data submode; the space, 00, 0, 1, and trailing
    // space are fixed literals required for K2/K3 parser compatibility.
    const int offsetRaw = state.ritXitOffset();

    QString r;
    r.reserve(38);
    r += "IF";
    r += QString::number(state.frequency()).rightJustified(11, '0');
    r += "     ";
    r += (offsetRaw >= 0 ? '+' : '-');
    r += QString::number(qAbs(offsetRaw)).rightJustified(4, '0');
    r += (state.ritEnabled() ? '1' : '0');
    r += (state.xitEnabled() ? '1' : '0');
    r += ' ';
    r += "00";
    r += (state.isTransmitting() ? '1' : '0');
    r += QString::number(k4ModeDigit(state.mode()));
    r += '0';
    r += '0';
    r += (state.splitEnabled() ? '1' : '0');
    r += '0';
    r += '0';
    r += "1 ;";
    return r.toUtf8();
}

} // namespace CatFrames
