#ifndef AUDIO_MICRATE_H
#define AUDIO_MICRATE_H

#include <QString>

#include "audio/audiodecimator.h"
#include "audio/audioresampler.h"

// What to TELL THE OPERATOR when a microphone cannot be captured from.
//
// AudioDecimator and AudioResampler own the rule - exact multiples of 12 kHz are decimated, any
// other rate a device reports is resampled - and this owns the wording, so the sentence an operator
// reads is written once and tested.
//
// WHY THIS EXISTS AT ALL. A microphone QK4 cannot use is not a quiet degradation - it is total.
// setupAudioInput() refuses to open it, no mic data is captured, and because a local PTT press keys
// the K4 by STREAMING AUDIO rather than by sending TX; (TransmitOwner::Route::StreamedFromHere),
// no audio means the radio never keys. The operator presses PTT and nothing whatever happens.
//
// That was reported from the field (#180, and a Mac Studio whose display microphone ran at
// 44.1 kHz) when only multiples of 12 kHz were accepted. The refusal was logged, but only to the
// log, which nobody sees unless they start the app from a terminal. Detection was never the gap;
// saying so where the operator is looking was.
//
// Pure functions, so the wording is unit-testable with no audio device. Qt Core only, no widgets.
namespace MicRate {

// The same rule setupAudioInput() applies: decimated if it can be, resampled otherwise.
inline bool isUsable(int sampleRate) {
    return AudioDecimator::isSupportedRate(sampleRate) ||
           AudioResampler::canConvert(sampleRate, AudioDecimator::OUTPUT_RATE);
}

// Empty when QK4 can capture at this rate. Otherwise a sentence naming the rate, what it breaks,
// and what to do - in that order, because the operator wants the last part most.
inline QString problem(int sampleRate) {
    if (isUsable(sampleRate))
        return QString();

    if (sampleRate <= 0) {
        return QStringLiteral("This microphone did not report a usable sample rate, so QK4 cannot "
                              "capture from it and transmit audio would be silent. Choose a "
                              "different microphone.");
    }

    // Named explicitly: an operator about to change a rate in Audio MIDI Setup or Windows Sound
    // needs the number they have and the number to pick, not the arithmetic.
    return QStringLiteral("This microphone runs at %1 Hz, which QK4 cannot convert for the radio, so "
                          "transmit audio would be silent and PTT would appear to do nothing. Set "
                          "the device to 48000 Hz and restart QK4, or choose a different "
                          "microphone.")
        .arg(sampleRate);
}

// The other ways setupAudioInput()/openMic() end with no microphone. Each was already a qCWarning;
// on screen for the same reason as problem() - from the operator's chair every one of them is a PTT
// button that does nothing.
inline QString noMicrophone() {
    return QStringLiteral("QK4 cannot find a microphone, so nothing is being transmitted. Connect "
                          "one, or choose one in Options > Audio Input.");
}

// `deviceName` may be empty if the device vanished between opening and starting.
inline QString micWouldNotOpen(const QString &deviceName) {
    const QString which = deviceName.isEmpty() ? QStringLiteral("The microphone")
                                               : QStringLiteral("The microphone \"%1\"").arg(deviceName);
    return which + QStringLiteral(" would not open, so nothing is being transmitted. Choose a different "
                                  "microphone in Options > Audio Input.");
}

// Shown when the microphone is open and transmitting but delivering nothing at all.
//
// WHY THIS NAMES A RESTART, which is otherwise a terrible thing to tell an operator: QK4 cannot
// recover from the most likely cause. Qt snapshots a device's preferredFormat() when it first
// enumerates devices and never refreshes it for a format-only change - macOS fires no device-list
// notification for one, so nothing invalidates the cache. Measured 2026-10-18: a process that
// started while a device was at 16 kHz kept reporting 16 kHz for its whole life after the device
// moved to 8 kHz, while a process started afterwards read 8 kHz immediately.
//
// So QK4 cannot see the new rate, cannot re-open at it, and cannot resample from a rate it is being
// told wrongly. A restart genuinely is the fix, and saying so is better than an operator pressing a
// dead PTT and concluding the radio is broken.
//
// BEFORE ANYONE TRIES TO DETECT IT PROPERLY - the obvious approaches were tried and measured, and
// none of them work. This is documented Qt behaviour, not a bug to route around:
//
//   - QMediaDevices::audioInputsChanged() is the signal Qt's own documentation points you at. It
//     does NOT fire for a format-only change. Zero events across two bench sessions and a 30-second
//     probe while the rate was being changed.
//   - Re-enumerating does not help either, which is the part that looks like it should. Qt caches
//     the device LIST and rebuilds it only when the backend notifies; with no notification,
//     QMediaDevices::audioInputs() hands back the same stale QAudioDevice objects. The probe called
//     it every two seconds for thirty seconds and read the old rate every time, while a process
//     started afterwards read the new one immediately.
//   - Re-opening the QAudioSource is therefore pointless: setupAudioInput() reads preferredFormat()
//     from that same cached device and would re-open at the rate we already know is wrong.
//
// What remains is platform-specific: kAudioDevicePropertyNominalSampleRate via
// AudioObjectAddPropertyListener on macOS, IMMNotificationClient on Windows, PipeWire or PulseAudio
// on Linux. Three implementations, none reachable from a unit test, in the transmit path, to save a
// restart after an action almost nobody takes mid-session. Judged not worth it on 2026-10-18 - the
// failure is no longer silent, which was the actual complaint.
//
// Qt says as much itself: QAudioDevice "retains its properties throughout its lifetime, even if the
// physical device is modified" (doc.qt.io/qt-6/qaudiodevice.html), and the only refresh Qt offers is
// QMediaDevices::audioInputsChanged (doc.qt.io/qt-6/qmediadevices.html), which a format-only change
// does not raise.
//
// The other cause is INT-005's: a Bluetooth headset that cannot switch to HFP while a stereo
// playback stream is open on it. Both are named because from the operator's chair they are
// identical - a PTT button that does nothing.
inline QString micDeliveringNothing() {
    return QStringLiteral("Your microphone is not producing any audio, so nothing is being "
                          "transmitted. If you changed its sample rate, restart QK4 to pick up the "
                          "new rate. If it is a Bluetooth headset, it cannot provide a microphone "
                          "while also playing audio - choose a different device for playback.");
}

} // namespace MicRate

#endif // AUDIO_MICRATE_H
