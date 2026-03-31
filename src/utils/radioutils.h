#ifndef RADIOUTILS_H
#define RADIOUTILS_H

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

/// Convert frequency (Hz) to K4 band number (0=160m ... 10=6m, 16=XVTR).
/// Returns -1 for out-of-band frequencies.
int getBandFromFrequency(quint64 freq);

/// Get next span step up (zoom out) from current span in Hz.
/// Clamps at SPAN_MAX.
int getNextSpanUp(int currentSpan);

/// Get next span step down (zoom in) from current span in Hz.
/// Clamps at SPAN_MIN.
int getNextSpanDown(int currentSpan);

} // namespace RadioUtils

#endif // RADIOUTILS_H
