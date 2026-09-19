# audio/

RX and TX audio pipeline. Owned by `controllers/audiocontroller.cpp`.

## Files

- `audioengine.{cpp,h}` — `QAudioSink` + `QAudioSource` with a jitter buffer (10ms feed timer, ~40ms prebuffer). Handles volume, balance, mix routing.
- `opusdecoder.{cpp,h}` — Opus decode wrapper. K4 sends 12kHz stereo (left=Main, right=Sub). All four encode modes normalise by one rule, `native × K4_GAIN_BOOST / full_scale`; the per-mode scales were measured 2026-09-19, not tuned by ear. See `docs/k4-protocol-quirks.md` §11.
- `opusencoder.{cpp,h}` — Opus encode wrapper for TX. 12kHz mono; frame size reconfigured per K4 SL tier. `encodeFloat()` is what the TX path uses — Opus takes float natively, so the Opus modes never quantise.
- `rawaudioformat.h` — header-only, Qt-free. Builds the EM0 (S32LE, 24-bit) and EM1 (S16LE) TX wire payloads from a captured **Float32** mono frame. Holds `EM0_FULL_SCALE` (2^23 — EM0 is 24-bit in a 32-bit container), which `OpusDecoder::NORMALIZE_K4_RAW` is derived from so the two directions of EM0 cannot drift apart. See the WHY block for AUD-003 and the 2026-09-19 calibration.
- `audiodeviceselection.h` — header-only, Qt-free. Decides which device a stream belongs on when the device list changes. Follows the system default when nothing is pinned, and claims a PINNED device the moment it reappears — a pinned device absent at startup falls back silently, and without this the stream never returned to it.
- `audiodecimator.h` — header-only, Qt-free. Decimates a captured mic stream to the K4's 12 kHz. The factor comes from the rate the DEVICE reports, not one we demand: asking for a fixed 48 kHz produced zero bytes from an AirPods Pro while Qt reported the format as supported. See its WHY block (INT-005).
- `sidetonegenerator.{cpp,h}` — Real-time CW sidetone synthesis at 48kHz.

## Threading

- `AudioEngine` lives on `AudioController::m_audioThread` (moveToThread at construction).
- `OpusDecoder` has main-thread affinity but is only called from the IO-thread lambda wired to `Protocol::audioDataReady` — effectively single-threaded on the IO thread.
- `OpusEncoder` has main-thread affinity; called from the audio thread via queued signals.
- `SidetoneGenerator` lives on `HardwareController::m_sidetoneThread`.

## RX path

K4 → Protocol → OpusDecoder → AudioEngine::enqueueAudio → QAudioSink → speakers.

## TX path

Microphone → QAudioSource (at the device's own rate) → AudioDecimator → AudioEngine → OpusEncoder →
ConnectionController::sendAudio → K4.

`QAudioDevice::isFormatSupported()` cannot be trusted as proof a device will deliver: it returns true
for rates some devices produce nothing at. A keyed transmitter receiving no mic data for 500 ms is
reported under `qk4.audio` — that silence used to be indistinguishable from a quiet room.

The path stays Float32 end to end and quantises exactly once, at the wire format: EM0 to 24 bits, EM1 to 16,
and the Opus modes not at all. Quantising earlier (which is what it used to do, right after mic gain) cost
bits in proportion to the attenuation and was audible as tonal distortion on quiet audio.
`QT_LOGGING_RULES="qk4.audio.tx.debug=true"` reports what each TX frame actually put on the wire —
the only observer QK4 has for a format the radio alone reads.

## See also

- `docs/k4-protocol-quirks.md` → "`SL` (streaming latency) is not echoed" — the SL tier → frame
  bundling map, and why the tier cannot be read back from the radio.
