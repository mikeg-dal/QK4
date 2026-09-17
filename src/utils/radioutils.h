#ifndef RADIOUTILS_H
#define RADIOUTILS_H

#include <QString>
#include <QVector>
#include <QtGlobal>

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

/// Build an 8-band EQ CAT command string (e.g., "RE+00-02+04..." or "TE+00...").
/// prefix is "RE" (RX) or "TE" (TX), bands must have exactly 8 values in [-16, +16].
QString buildEqCommand(const QString &prefix, const QVector<int> &bands);

/// Convert SL tier (0-7) to Opus frame size in samples per channel at 12kHz.
/// SL0=240 (20ms), SL1-2=480 (40ms), SL3-5=720 (60ms), SL6-7=1440 (120ms).
/// Returns 720 (SL3 default) for out-of-range values.
int slTierToFrameSamples(int sl);

// --- RX jitter-buffer self-correction watermarks ---------------------------
// The AudioEngine RX queue can ratchet up a permanent latency backlog after a
// stall (e.g. PTT mic init / output-device rebuild). These watermarks let the
// queue trim itself back down. They are expressed as MULTIPLES OF THE CURRENT
// PACKET SIZE (one decoded K4 SL-bundle) — never fixed milliseconds — so the
// buffer scales with the operator's chosen SL tier: SL0's 20ms packets get a
// small buffer, SL7's 120ms packets get a proportionally larger one. A fixed-ms
// target would sit below one packet at high SL and drop every packet.
constexpr int JITTER_TARGET_PACKETS = 2;     // trim-to depth after a stall
constexpr int JITTER_HIGH_WATER_PACKETS = 5; // trigger; must exceed normal jitter (~1 pkt)

/// SL-scaled trim-to depth in bytes for the RX jitter buffer, given the current
/// decoded packet size in bytes. Returns 0 for non-positive input.
int jitterTargetBytes(int pktBytes);

/// SL-scaled high-water mark in bytes: once the RX queue exceeds this, it trims
/// back down to jitterTargetBytes(). Returns 0 for non-positive input.
int jitterHighWaterBytes(int pktBytes);

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

/// True if s is a strictly-valid dotted IPv4 address: exactly 4 dot-separated
/// octets, each parseable 0-255. Rejects "192.168.1", "192.168.100.500",
/// "1.2.3.4.5". (QHostAddress is NOT used here — it leniently accepts partial
/// forms like "192.168.1" as 192.168.0.1.)
bool isValidIpv4(const QString &s);

/// True if s is a valid host the K4 connection layer can use: a strict IPv4,
/// an IPv6 literal, or an RFC-1123 hostname (incl. mDNS ".local" names like
/// "k4.local", "K4-SN00045.local"). Trims input; empty -> false.
bool isValidHostOrIp(const QString &s);

} // namespace RadioUtils

#endif // RADIOUTILS_H
