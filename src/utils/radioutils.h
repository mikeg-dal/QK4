#ifndef RADIOUTILS_H
#define RADIOUTILS_H

#include "models/radiostate/levelsstate.h"

#include <QString>
#include <QVector>
#include <QtGlobal>

#include <optional>

/**
 * @brief Shared radio utility functions and constants.
 *
 * Centralizes frequency, band, and span calculations used by
 * MainWindow, SpectrumController, and HardwareController.
 * No duplicated static functions — all callers use this namespace.
 */
namespace RadioUtils {

// K4 span range: 5 kHz to 368 kHz
// UP (zoom out): +1 kHz until 144 kHz, then +4 kHz until 368 kHz
// DOWN (zoom in): -4 kHz until 140 kHz, then -1 kHz until 5 kHz
constexpr int SPAN_MIN = 5000;
constexpr int SPAN_MAX = 368000;
constexpr int SPAN_THRESHOLD_UP = 144000;
constexpr int SPAN_THRESHOLD_DOWN = 140000;

// K4 keyer speed range. The manual gives "from 8 to 100 WPM" and the KS command documents the
// same, so a speed outside it cannot come from the radio.
constexpr int KEYER_WPM_MIN = 8;
constexpr int KEYER_WPM_MAX = 100;

/// Dit unit length in ms for a keyer speed, clamped to the K4's range.
///
/// WHY this is shared rather than written out at each site: the places that derive an element
/// length from the speed — the local iambic keyer's element clock and the sidetone's PCM block
/// length — have to agree exactly. They did not: the
/// sidetone clamped to 5-60 WPM while the keyer clamped not at all, so above 60 WPM the sidetone
/// built longer blocks than the keyer's element interval and fell further behind on every element,
/// with no recovery until keying stopped. One conversion, one clamp, no drift.
inline int ditMsForWpm(int wpm) {
    return 1200 / qBound(KEYER_WPM_MIN, wpm, KEYER_WPM_MAX);
}

// K4 limits from the Programmer's Reference rev. D5, in the unit RadioState stores. Every control
// that steps one of these values and every builder that sends it uses these, never a copy.
// CW: "CWnn; where nn is sidetone pitch x10 Hz (25-95)".
constexpr int CW_PITCH_MIN_HZ = 250;
constexpr int CW_PITCH_MAX_HZ = 950;
constexpr int CW_PITCH_STEP_HZ = 10;
// SQ / SQ$: "nnn is 000-040".
constexpr int SQUELCH_MAX = 40;
// MG: "<xxx> is 000-080".
constexpr int MIC_GAIN_MAX = 80;
// CP: "nnn is 000-030".
constexpr int COMPRESSION_MAX = 30;
// RG / RG$: "nn is 00 to 60 (-0 to -60 dB)". RadioState stores the magnitude.
constexpr int RF_GAIN_MAX = 60;
// SD: "delay in 10-ms increments (000-255 = 0.00 to 2.55 seconds)".
constexpr int QSK_DELAY_MAX = 255;

/// `current` moved by `steps` and clamped to [min, max], or nullopt when nothing must be sent:
/// `current` is negative (the radio has not reported the value yet; every such field starts at a
/// negative sentinel, and stepping from it sends a value the operator never chose), or the clamped
/// result equals `current` (already at the limit).
std::optional<int> stepWithin(int current, int steps, int min, int max);

/// Keyer speed after `steps` WPM, within KEYER_WPM_MIN..KEYER_WPM_MAX. nullopt as stepWithin().
std::optional<int> nextKeyerSpeed(int currentWpm, int steps);

/// CW pitch after `steps` x CW_PITCH_STEP_HZ, within CW_PITCH_MIN_HZ..CW_PITCH_MAX_HZ. nullopt as
/// stepWithin().
std::optional<int> nextCwPitch(int currentHz, int steps);

/// Squelch after `steps`, within 0..SQUELCH_MAX. nullopt as stepWithin().
std::optional<int> nextSquelch(int current, int steps);

// PC power limits (Programmer's Reference rev. D5): "L = low power (QRP, 0.1-10.0 W) ... H = high
// power (QRO, 1-110 W) ... X = milliwatt range (for XVTRs, 0.1-10.0 mW)". QRP and XVTR are in tenths
// of their unit, as the PC command carries them; QRO is in whole watts.
constexpr int RF_POWER_TENTHS_MIN = 1;
constexpr int RF_POWER_TENTHS_MAX = 100;
constexpr int RF_POWER_QRO_MIN_W = 1;
constexpr int RF_POWER_QRO_MAX_W = 110;

/// RF power as the K4 holds it: `value` in the range's own unit (W for QRP and QRO, mW for XVTR).
struct RfPower {
    double value;
    LevelsState::PowerRange range;
};

/// RF power after `steps` knob ticks, stepping within the range the radio reported:
///  - QRP steps 0.1 W down to 0.1 W; past 10.0 W it moves to 11 W QRO.
///  - QRO steps 1 W within 1-110 W. It moves to 10.0 W QRP only when it crosses down from above
///    10 W, so a QRO setting of 1-10 W that the radio reported stays QRO.
///  - XVTR steps 0.1 mW within 0.1-10.0 mW and never leaves XVTR: the range belongs to the
///    transverter band, and a value in watts there would ask for a different power entirely.
/// nullopt when the power is not known yet (negative sentinel) or the tick changes nothing.
std::optional<RfPower> nextRfPower(double value, LevelsState::PowerRange range, int steps);

/// Convert VT tuning step index (0-5) to Hz.
/// Returns 1000 Hz for out-of-range values.
int tuningStepToHz(int step);

/// VT tuning step for a frequency-display digit, counted from the right (0 = 1 Hz digit).
/// Digits 0-4 (1 Hz to 10 kHz) map to VT0-VT4; any other digit returns -1 because the K4 has no
/// tuning step above 10 kHz (VT5 wraps back to 100 Hz).
int tuningStepForDigit(int digitFromRight);

/// Convert frequency (Hz) to K4 band number (0=160m ... 10=6m, 16=XVTR).
/// Returns -1 for out-of-band frequencies.
int getBandFromFrequency(quint64 freq);

/// Get next span step up (zoom out) from current span in Hz.
/// Clamps at SPAN_MAX.
int getNextSpanUp(int currentSpan);

/// Get next span step down (zoom in) from current span in Hz.
/// Clamps at SPAN_MIN.
int getNextSpanDown(int currentSpan);

/// Span after one zoom step: zooming in narrows the span (getNextSpanDown), zooming out widens it
/// (getNextSpanUp). The one place that decides which way a + or - span control goes.
int spanAfterZoom(int currentSpan, bool zoomIn);

/// Next K4 filter preset when cycling like the front panel: 1 -> 3 -> 2 -> 1.
/// Returns -1 for anything outside 1-3 (preset not known yet).
int nextFilterPreset(int currentPreset);

/// Next panadapter (DDC) noise blanker mode (#NB$) when the DISPLAY menu's NB key is tapped:
/// OFF (0) -> ON (1) -> AUTO (2) -> OFF. A mode not reported yet (-1) is taken as OFF.
int nextDdcNoiseBlankerMode(int currentMode);

/// Filter as the K4 holds it: bandwidth and IF shift (centre), both in decahertz.
struct FilterBwIs {
    int bwDah;
    int isDah;
};

/// Limits for filterAfterEdgeStep(), in decahertz. isLocked: the mode fixes IS (FSK-D, PSK-D).
struct FilterEdgeLimits {
    int bwMinDah;
    int bwMaxDah;
    int isMinDah;
    int isMaxDah;
    bool isLocked;
};

/// One HI or LO scroll tick (`steps` x 20 Hz) applied to a filter while the OPPOSITE edge stays put.
/// The moved edge is recomputed from the fixed edge and the clamped bandwidth, and IS is derived from
/// those two edges. Returns nullopt when the tick changes nothing or when honouring the IS limit would
/// move the fixed edge or take LO below 0 Hz. With isLocked, only the bandwidth changes and IS is returned unchanged.
std::optional<FilterBwIs> filterAfterEdgeStep(FilterBwIs current, bool adjustHi, int steps,
                                              const FilterEdgeLimits &limits);

/// Build an 8-band EQ CAT command string (e.g., "RE+00-02+04..." or "TE+00...").
/// prefix is "RE" (RX) or "TE" (TX), bands must have exactly 8 values in [-16, +16].
QString buildEqCommand(const QString &prefix, const QVector<int> &bands);

/// Convert SL tier (0-7) to Opus frame size in samples per channel at 12kHz.
/// SL0=240 (20ms), SL1-2=480 (40ms), SL3-5=720 (60ms), SL6-7=1440 (120ms).
/// Returns 720 (SL3 default) for out-of-range values.
int slTierToFrameSamples(int sl);

// --- K4 panadapter fixed-tune display mode ---------------------------------
// The K4 encodes the panadapter tuning mode across two CAT params: #FXT
// (0=track, 1=fixed) and #FXA (0-4). WARNING: Elecraft uses TWO naming
// systems and they do NOT line up intuitively. The programmer's reference
// names #FXA by behavior; the front panel shows different labels. We mirror
// the front-panel labels (what the user sees on the radio), verified against
// hardware 2026-05-28:
//   #FXA  reference name       front-panel label (what QK4 shows)
//   ----  -------------------  --------------------------------
//    0    FULL SPAN            FIXED1
//    1    HALF SPAN            FIXED2
//    2    SLIDE EDGE           SLIDE1
//    3    STATIC               STATIC
//    4    SLIDE NEAR EDGE      SLIDE2
//   (#FXT0 = TRACK, regardless of #FXA)
// WHY centralized: the inbound parse and the outbound command builder must
// agree, or the displayed mode desyncs from the radio (the historical
// SLIDE1<->FIXED2 bug, caused by mis-associating the two naming systems).
// One mapping here makes divergence impossible.
enum class FixedTuneMode { Track = 0, Slide1, Slide2, Fixed1, Fixed2, Static };

/// Decode raw #FXT/#FXA into a FixedTuneMode. fxt==0 is always Track (FXA
/// ignored); unknown fxa values fall back to Track.
FixedTuneMode fixedTuneModeFromCat(int fxt, int fxa);

/// Build the CAT command that sets the K4 to `mode`, setting both #FXT and
/// #FXA deterministically (no reliance on the radio's prior FXA). Does NOT
/// include a read-back GET; callers append "#FXA;#FXT;" when they need the
/// radio to confirm (the K4 does not echo these SETs at AI4).
QString fixedTuneSetCommand(FixedTuneMode mode);

/// Snap an absolute frequency DOWN to the nearest multiple of stepHz, zeroing the
/// digits below the current tuning step (e.g. 7277380 @ 100 Hz -> 7277300). No-op
/// when already aligned or stepHz <= 0. Used by every tune path so dial/scroll/drag
/// land on the same step boundary the dimmed-digit display indicates.
qint64 snapFreqToStep(qint64 freq, int stepHz);

/// Frequency a wheel turn of `steps` over a VFO's frequency display tunes that VFO to, or 0 when the
/// turn must not tune. 0 when the VFO is locked (LK / LK$): the K4's lock holds only its own knob -
/// the Programmer's Reference gives FA/FB no lock exception - so honouring it is QK4's job, as the
/// panadapter and KPOD paths already do. Also 0 when the result is not a positive frequency.
qint64 vfoWheelTuneTarget(quint64 currentHz, int steps, int stepHz, bool locked);

/// The VFOs one CAT command would move, as a mask of VfoA and VfoB. Recognises the K4's frequency
/// SETs (Programmer's Reference rev. D5): FAn / FBn, UP / DN (VFO A) and UPB / DNB (VFO B),
/// VOsnnnnn (A) and VO$snnnnn (B), and ABn (0 A>B, 1 B>A, 2 swap, 3 all A>B, 4 all B>A, 5 all
/// swap). A GET (FA;) moves nothing. The trailing ';' is optional. SWnn front-panel key
/// emulations are not counted: those are the radio's own keys and the radio applies its own rules.
enum VfoMask { VfoNone = 0, VfoA = 1, VfoB = 2 };

/// Whether QK4 may send a TD / TD$ SET for a receiver in K4 mode `mdCode` (the MD digit: 3 CW,
/// 7 CW-R, 6 DATA, 9 DATA-R). TD's argument means something different per mode (Programmer's
/// Reference D5): text decode in CW and DATA, but in SSB "encode mode (2-tone): 0 = 2-TONE off,
/// 1 = on", and nothing defined in AM/FM. So TD is a decode command only in CW and DATA; in any other
/// mode a TD1 would arm the two-tone generator and the next PTT would transmit two tones.
bool textDecodeSetAllowed(int mdCode);

/// Whether the K4's APF (audio peaking filter, AP / AP$) exists in K4 mode `mdCode`: CW and CW-R only.
/// In every other mode the K4's RX row shows the key as "N/A" and it does nothing.
bool apfAvailable(int mdCode);
int vfosMovedByCat(const QString &command);

/// True when `command` would move a VFO that is locked (LK for A, LK$ for B, each on its own). Every
/// path that sends a frequency change - QK4's own controls, TCI clients and CAT-server clients - asks
/// this first. Honouring the lock is QK4's job: the K4 documents no lock exception for these SETs.
bool catMovesLockedVfo(const QString &command, bool lockA, bool lockB);

/// GT AGC speed (RadioState::AGCSpeed: 0 off, 1 slow, 2 fast) as the K4 labels it: "AGC", "AGC-S",
/// "AGC-F". Anything else, including the not-yet-received sentinel, reads as off.
QString agcLabel(int agcSpeed);

/// True when the GT AGC speed is slow or fast, i.e. AGC is on.
bool agcActive(int agcSpeed);

/// ES TX bandwidth (100 Hz units) as a button label, e.g. 28 -> "2.8k".
QString txBandwidthLabel(int bwHundredsHz);

/// Convert a Celsius reading to Fahrenheit, rounded to the nearest degree.
/// Display-only: warning/critical thresholds stay in Celsius so a given physical
/// temperature keeps its colour whichever unit is shown.
int celsiusToFahrenheit(int celsius);

/// True if s is a strictly-valid dotted IPv4 address: exactly 4 dot-separated
/// octets, each parseable 0-255, none with a leading zero. Rejects "192.168.1",
/// "192.168.100.500", "1.2.3.4.5", "010.0.0.1". (QHostAddress is NOT used here —
/// it leniently accepts partial forms like "192.168.1" as 192.168.0.1.)
bool isValidIpv4(const QString &s);

/// True if s is a valid host the K4 connection layer can use: a strict IPv4,
/// an IPv6 literal, or an RFC-1123 hostname (incl. mDNS ".local" names like
/// "k4.local", "K4-SN00045.local"). Trims input; empty -> false.
bool isValidHostOrIp(const QString &s);

} // namespace RadioUtils

#endif // RADIOUTILS_H
