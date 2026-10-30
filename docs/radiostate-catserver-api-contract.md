# RadioState → CAT Server API Contract

This is a living contract. It lists the parts of `RadioState` that QK4's native CAT server reads,
so a change to `RadioState` does not silently change what WSJT-X, MacLoggerDX, RumLogNG or N1MM
see. Three files consume it:

- `src/network/catserver.cpp` — answers GETs from the cache, forwards SETs, gates PTT.
- `src/network/catframes.cpp` — builds the reply strings. `CatFrames::ifFrame` and
  `CatFrames::vox` take a `const RadioState&` and read getters themselves.
- `src/network/catpushbroadcaster.cpp` — the auto-information (`AI`) push. It listens to
  `RadioState` signals and reads getters when one fires.

It began as the frozen API for the April 2026 RadioState subsystem split. The split is done. The
rules below still apply to any change to `RadioState`.

## Tests

- `tests/test_catserver.cpp` (`CatServerTests`) — drives the server over a real TCP loopback socket.
- `tests/test_catpushbroadcaster.cpp` (`CatPushBroadcasterTests`) — the AI push.

CI builds both through the `qk4_tests` aggregate target and runs them with `ctest`
(`.github/workflows/ci-checks.yml`). Not every row below has a test through the server. At the
time of writing these replies have none: `OM`, `RVM`/`RVD`, the `KS;` GET, `SM`, `TM`, `FW` and
`DT` with a stored value. A change to those rows is not caught by CI.

## Getters read by CatServer and CatFrames

| Getter | Return type | Reply it feeds |
|---|---|---|
| `firmwareVersions()` | `QMap<QString,QString>` | `RVM` → `RVM<FP>;`, `RVD` → `RVD<DSP>;` (default `01.00`) |
| `frequency()` | `quint64` | `FA`, `IF` |
| `vfoB()` | `quint64` | `FB` |
| `mode()` | `RadioState::Mode` | `MD`, `IF`, `VX` (picks the mode group) |
| `modeB()` | `RadioState::Mode` | `MD$` |
| `isTransmitting()` | `bool` | `TQ`, `IF`, and `TX/;` (the toggle reads it) |
| `splitEnabled()` | `bool` | `FT`, `IF` |
| `ritEnabled()` / `xitEnabled()` | `bool` | `RT` / `XT`, `IF` |
| `ritXitOffset()` | `int` | `RO`, `IF` (offset field) |
| `rfPower()` + `powerRange()` | `double`, `LevelsState::PowerRange` | `PC`, `PCX` (range letter as the radio sends it) |
| `agcSpeed()` | `RadioState::AGCSpeed` | `GT` |
| `keyerSpeed()` | `int` | `KS` |
| `noiseBlankerLevel()` / `noiseBlankerEnabled()` / `noiseBlankerFilterWidth()` | `int`, `bool`, `int` | `NB` (`NB0301`) |
| `noiseReductionLevel()` / `noiseReductionEnabled()` | `int`, `bool` | `NR` (`NR050`) |
| `voxCW()` / `voxVoice()` / `voxData()` (via `CatFrames::vox`) | `bool` | `VX` — the current mode's group, as the radio answers it (`VXD0`) |
| `filterBandwidth()` | `int` | `BW` (no answer while −1), `FW` |
| `dataSubMode()` | `int` | `DT` (no answer while −1) |
| `optionModules()` | `QString` | `OM` (`OM AP----------;` while empty) |
| `diversityEnabled()` | `bool` | `DV` |
| `sMeter()` | `double` | `SM` |
| `sMeterDbm()` / `sMeterDbmB()` | `std::optional<int>` | `SMH` / `SMH$` (`SMH-110`; no answer while empty) |
| `alcMeter()`, `compressionDb()`, `forwardPower()`, `swrMeter()`, `isQrpMode()` | `int`, `int`, `double`, `double`, `bool` | `TM` |
| `squelchLevel()` / `squelchLevelB()` | `int` | `SQ` / `SQ$` (no answer while −1) |
| `rfGain()` / `rfGainB()` | `int` | `RG` / `RG$` (`RG-00`; no answer while unset) |
| `ritXitOffsetB()`, `ritEnabledB()`, `agcSpeedB()`, the `…B()` NB and NR getters, `filterBandwidthB()`, `dataSubModeB()` | | `RO$`, `RT$`, `GT$`, `NB$`, `NR$`, `BW$`, `DT$` (main frame with `$` after the prefix) |

Replies that read no getter: `K2`/`K3`, `PS`, `FR0`, `ID017`, `TB000`, `SB0` (constants), and `AI`,
which reads the client's own subscription from `CatPushBroadcaster::clientAiMode`.

`AG` / `AG$` read no getter. The radio does not report AF gain when the knob moves, so a stored
value would be stale. CatServer asks the radio instead (`radioQueryRequested`, answered through
`onRadioResponse`). #134.

`TX;`, `TX/;` and `RX;` are never forwarded. CatServer emits `pttRequested`, and MainWindow hands it
to `TransmitController` (see `docs/k4-protocol-quirks.md` §4). Every other SET is emitted as
`catCommandReceived`; MainWindow sends it to the K4 and applies it to `RadioState` with
`parseCATCommand`.

## Signals and getters used by CatPushBroadcaster

`CatPushBroadcaster` connects to these `RadioState` signals:

| Signal | Pushed to AI subscribers |
|---|---|
| `frequencyChanged` / `frequencyBChanged` | `FA` / `FB` |
| `modeChanged` | `MD`, then `IF` (loggers read mode from `IF`); recomputes the `VX` cache without pushing |
| `modeBChanged` | `MD$` |
| `transmitStateChanged` | `TQ`, then `IF` |
| `splitChanged` | `FT` |
| `ritXitChanged` | `RO`, `RT`, `XT` |
| `rfPowerChanged` | `PC` |
| `filterBandwidthChanged` | `BW` |
| `keyerSpeedChanged` | `KS` |
| `voxChanged` | `VX` for the current mode, only when that reply changed |
| `diversityChanged` | `DV` |
| `dataSubModeChanged` | `DT` |
| `processingChanged` | `NB`, `NR`, `GT` — each only when its frame changed; the first fire seeds the cache |

On a signal it reads `noiseBlankerLevel()`, `noiseBlankerEnabled()`, `noiseBlankerFilterWidth()`,
`noiseReductionLevel()`, `noiseReductionEnabled()`, `agcSpeed()`, and through `CatFrames::ifFrame` /
`CatFrames::vox` the getters marked `IF` and `VX` above. A client at AI0 gets nothing.

## Rules

1. **Keep every getter above on `RadioState`.** If its state lives in a subsystem struct, the getter
   stays as a thin delegator.
2. **Keep the signatures.** A type change (`int` → `qint64`, a different enum) changes a reply's
   format. Check the matching `CatFrames` builder.
3. **Keep the semantics.** `VX` answers for the current mode's group only; it must not become an
   any-of rollup. Sentinels (−1) mean "no answer yet"; replacing one with 0 makes a real zero and
   "unknown" look the same.
4. **Keep the signal set and arguments** that `CatPushBroadcaster` connects to, and emit only on a
   real change. A signal that fires without a change becomes a push to every AI client.
5. Before committing a change to any row, run
   `ctest --test-dir build -R "CatServerTests|CatPushBroadcasterTests" --output-on-failure`.

## Revalidation

Refresh the tables when these commands print something they don't list:

```bash
# getters read by CatServer and CatPushBroadcaster
grep -ohE 'm_radioState->[a-zA-Z]+\(' src/network/catserver.cpp src/network/catpushbroadcaster.cpp | sort -u
# getters read inside CatFrames (ifFrame, vox)
grep -ohE '\b(rs|state)\.[a-zA-Z]+\(' src/network/catframes.cpp | sort -u
# RadioState signals the broadcaster connects to
grep -oE 'RadioState::[a-zA-Z]+Changed' src/network/catpushbroadcaster.cpp | sort -u
```
