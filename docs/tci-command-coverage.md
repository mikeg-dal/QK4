# TCI Command Coverage and Gaps

What QK4's TCI server implements, measured against **TCI Protocol Ver. 2.0** (Expert Electronics,
12 January 2024), and how deep each command actually reaches toward the K4.

Source spec: `TCI Protocol.pdf` from the TCI-Keyer repository. Companion to
`docs/tci-server-design.md`, which covers the wire format and the phased build.

**Three separate questions are kept apart throughout, because conflating them hides the real gaps:**

1. **Does QK4 answer the command?** — the TCI surface.
2. **Does it reach the radio?** — whether a SET actually moves the K4.
3. **Can the K4 even do it?** — hardware limits that no amount of QK4 work removes.

A command can be answered and still be a lie. The read-only group (phase 8a) answers from QK4's
snapshot and deliberately does not touch the radio; a client cannot tell the difference from the
reply alone, which is why column 3 exists.

---

## 1. Summary

| Category | Spec commands | QK4 implements | Notes |
|---|---:|---:|---|
| Initialization (4.1) | 9 | 9 | Complete |
| Bidirectional control (4.2) | 42 | 5 SET + 26 read-only | 11 absent |
| Unidirectional control (4.3) | 21 | 8 | Audio stream config + start/stop |
| Notification (4.4) | 11 | 2 (echo only) | No sensor data is actually sent |
| New in 2.0 (4.5) | 2 | 0 | `VFO_LOCK`, `RX_CHANNEL_SENSORS` |
| CW (3.2) | 9 | 0 | **The largest single gap — see §5** |

A fourth defect — **no transmit-status message was ever sent** — was found later with TR4W as the
client and is covered in §11, along with an audit of everything a server must send unprompted.

**Headline:** QK4 is complete for a *digital-mode* client (WSJT-X works end to end — receive,
decode, transmit, and a full FT8 QSO). It is not usable by a **CW** client at all, and it reports
several values it does not actually track.

**Three defects were found while writing this document and are now fixed** — see §6.

---

## 2. Initialization commands (spec 4.1) — complete

| Spec | QK4 | Depth | Notes |
|---|---|---|---|
| `VFO_LIMITS` | ✅ | Static | `100000,54000000` — K4 HF/6 m range |
| `IF_LIMITS` | ✅ | Static | `-48000,48000` |
| `TRX_COUNT` | ✅ | Static `1` | K4 has Main+Sub; scoped to Main. See §4.2 |
| `CHANNEL_COUNT` | ✅ | Static `2` | **Sent as `channels_count` (plural)** — see §6.4 |
| `DEVICE` | ✅ | Static | `QK4` |
| `RECEIVE_ONLY` | ✅ | Static `false` | |
| `MODULATIONS_LIST` | ✅ | Static | 11 modes, all mapped to K4 modes |
| `PROTOCOL` | ✅ | Static | Announces `ExpertSDR3,1.5`, not 2.0. Deliberate — see §6.5 |
| `READY` | ✅ | — | Sent last, after `START` |

QK4's init burst is 42 commands, larger than this table, because it also seeds current state
(`vfo`, `modulation`, `drive`, …). That matches what a real ExpertSDR3 server does; clients cache
the burst and act on it.

---

## 3. Bidirectional control (spec 4.2)

### 3.1 Implemented as real SETs — these move the radio

| Spec | QK4 | K4 path | Depth |
|---|---|---|---|
| `VFO` | ✅ | `CatFrames::frequencyA` / `frequencyB` (`FA`/`FB`) | Full, both channels |
| `DDS` | ✅ | `CatFrames::frequencyA` | Full. Treated as an alias for the RX VFO |
| `MODULATION` | ✅ | `CatFrames::modeA` (`MD`) | Full for the 11 announced modes |
| `TRX` | ✅ | PTT gate + TX audio | See §3.4 — **arg3 ignored** |
| `SPLIT_ENABLE` | ✅ | `CatFrames::split` (`FT`) | Edge-triggered only, by design |

These five are what a digital-mode client needs, and they are bench-verified against a live K4
through a completed FT8 QSO.

### 3.2 Answered read-only — QK4 reports, the radio does not move

Phase 8a. A SET-shaped command gets the current value back, never a false acknowledgement.

`RIT_ENABLE`, `XIT_ENABLE`, `RIT_OFFSET`, `XIT_OFFSET`, `RX_FILTER_BAND`, `DRIVE`, `TUNE_DRIVE`,
`AGC_MODE`, `LOCK`, `SQL_ENABLE`, `SQL_LEVEL`, `MUTE`, `VOLUME`, `RX_NB_ENABLE`, `RX_NR_ENABLE`,
`RX_ANF_ENABLE`, `RX_APF_ENABLE`

**Most of these report a hardcoded constant, not the radio.** The snapshot carries only
frequency, mode and split from `RadioState`; everything else is a struct default. So
`rx_nb_enable:0,false;` is QK4 saying "I don't model this", not "the K4's noise blanker is off" —
even though `RadioState` *does* track `noiseBlankerEnabled()`. Closing that is snapshot wiring,
not new protocol work.

**QK4 already has the K4 builders for most of the missing SETs:**

| TCI SET | Existing builder | K4 cmd | Blocker |
|---|---|---|---|
| `DRIVE` / `TUNE_DRIVE` | `rfPower` / `rfPowerExtended` | `PC` | Units: TCI is 0–100 %, K4 is watts. Needs a mapping decision |
| `RIT_ENABLE` | `ritEnabled` | `RT` | None — ready to wire |
| `XIT_ENABLE` | `xitEnabled` | `XT` | None — ready to wire |
| `RIT_OFFSET` | `ritOffset` | `RO` | **Shared with XIT — see §4.1** |
| `XIT_OFFSET` | `ritOffset` | `RO` | **Same control. Cannot be independent** |
| `AGC_MODE` | `agcSpeed` | `GT` | Vocabulary mismatch — see §6.1 |
| `RX_NB_ENABLE` | `noiseBlanker` | `NB` | None — ready to wire |
| `RX_NR_ENABLE` | `noiseReduction` | `NR` | None — ready to wire |
| `RX_FILTER_BAND` | `filterBandwidth` / `filterWidthExtended` | `BW`/`FW` | TCI passes low+high edges; K4 takes a width. Lossy both ways |
| `CW_KEYER_SPEED` | `keyerSpeed` | `KS` | None — ready to wire |
| `LOCK` | *none* | `LK` | No builder |

Deliberately deferred rather than shipped: each one moves real hardware and none has been
bench-tested. Four defects reached the air during phases 4–6 and every one was found by the radio,
not by a test.

### 3.3 Not implemented at all

| Spec | Why | K4 support |
|---|---|---|
| `START` / `STOP` | Device start/stop is meaningless here; QK4 owns the K4 link | n/a |
| `IF` | IF filter tuning within the panorama | Possible via `FW`/`BW`, no mapping designed |
| `TUNE` | Tune-mode keying | K4 supports it; **no `CatFrames` builder** |
| `RX_CHANNEL_ENABLE` | Enables VFO B as a second RX | K4 Sub RX — out of scope while `trx_count:1` |
| `AGC_GAIN` | AGC threshold in dB | K4 has AGC controls; no builder |
| `RX_NB_PARAM` | NB threshold + pulse width | `RadioState` tracks `noiseBlankerLevel`; no builder |
| `RX_BIN_ENABLE` | Binaural/pseudo-stereo | K4 has no equivalent |
| `RX_ANC_ENABLE` | Adaptive noise cancellation | No K4 equivalent |
| `RX_DSE_ENABLE` | Digital surround for CW | No K4 equivalent |
| `RX_NF_ENABLE` | Notch filter module | K4 has notch; no builder |
| `RX_MUTE`, `RX_VOLUME`, `RX_BALANCE` | Per-receiver audio | **Local to QK4's audio stack, never CAT** |
| `MON_VOLUME`, `MON_ENABLE` | TX monitor | Local audio, or K4 `MON` |
| `DIGL_OFFSET`, `DIGU_OFFSET` | Data-mode carrier offsets | K4 has `DT`/data sub-modes; no mapping |

---

## 4. K4 hardware and architecture limits

These are gaps that cannot be closed in QK4. Implementing the TCI command would require lying to
the client.

### 4.1 XIT has no independent offset — confirmed

The single clearest example. TCI models RIT and XIT as **two offsets**:

```
RIT_OFFSET:0,500;      XIT_OFFSET:0,-350;
```

The K4 has **one shared offset register** with two independent enables:

| K4 command | Meaning |
|---|---|
| `RO±nnnn;` | THE offset — one register, used by whichever of RIT/XIT is on |
| `RT1/0;` | RIT on/off |
| `XT1/0;` | XIT on/off |

QK4's own model already reflects this correctly: `RadioState::ritXitOffset()` is a single value,
and the change signal is `ritXitChanged(bool ritEnabled, bool xitEnabled, int offset)` — one
offset, two flags.

**Consequence:** `RIT_OFFSET` and `XIT_OFFSET` can be *reported* separately but cannot be *set*
separately. Writing one necessarily moves the other. Recommended handling: implement both SETs
against `RO`, document that they are aliases, and never pretend otherwise.

**This is also a latent bug in QK4 today** — see §6.3.

### 4.2 Sub receiver is out of scope, not absent

The K4 has a full Sub RX and `RadioState` models it throughout (`filterBandwidthB`,
`squelchLevelB`, `agcSpeedB`, `ritEnabledB`, …). QK4 declares `trx_count:1` and refuses any
command addressing receiver 1. That is a *scoping* choice from the design phase ("let's just focus
on the main VFO for now"), not a hardware limit. `RX_CHANNEL_ENABLE`, `RX_CHANNEL_SENSORS` and the
`*_B` half of everything else unlock together whenever that scope changes.

### 4.3 Units and vocabulary mismatches

| TCI | Range/vocabulary | K4 | Mismatch |
|---|---|---|---|
| `DRIVE` | 0–100 (%) | `PC` in watts | Needs a percent↔watt mapping; K4 also has a QRP range |
| `VOLUME`, `RX_VOLUME` | −60…0 dB | QK4 audio stack, linear | Not CAT at all |
| `SQL_LEVEL` | −140…0 dBm | K4 squelch 0–29 | Scale conversion required |
| `AGC_MODE` | `normal`/`fast`/`off` | `GT` numeric | Enum mapping required |
| `AGC_GAIN` | −20…120 dB | K4 AGC controls | No direct equivalent |

### 4.4 Commands with no K4 concept

`RX_BIN_ENABLE` (binaural), `RX_ANC_ENABLE` (adaptive noise cancellation), `RX_DSE_ENABLE`
(digital surround for CW), `LINE_OUT_RECORDER_*` (server-side recording), `APP_FOCUS` /
`SET_IN_FOCUS` (ExpertSDR3 window management). These should stay unimplemented; silence is the
protocol's defined response.

---

## 5. CW — the largest gap, and the case for CAT passthrough

**QK4 implements none of the TCI CW command set.** Verified: no `KY`, no CW-text path, nothing
matching `cwText`/`sendCW`/`cwMessage` anywhere in `src/`.

| Spec | Purpose | QK4 |
|---|---|---|
| `CW_MACROS:trx,text` | Send arbitrary CW text | ❌ |
| `CW_MSG:trx,prefix,callsign,suffix` | Structured message with editable callsign | ❌ |
| `CW_MSG:text` | Correct a callsign mid-transmission | ❌ |
| `CW_TERMINAL:bool` | Stay in TX between macros | ❌ |
| `CW_MACROS_STOP` | Abort transmission | ❌ |
| `CW_MACROS_EMPTY` | Server→client: queue drained | ❌ |
| `CALLSIGN_SEND:call` | Server→client: final callsign as sent | ❌ |
| `CW_MACROS_SPEED` / `_UP` / `_DOWN` / `_DELAY` | Speed and timing | ❌ (builder exists: `keyerSpeed`) |
| `KEYER:trx,state,ms` | Straight-key state with element timing | ❌ |

The K4 sends CW by CAT with its **`KY` command**. QK4 has no builder for it — `CatFrames` has 24
builders and CW text is not among them. `keyerSpeed` (`KS`) is the only CW-adjacent one.

### 5.1 Why passthrough is the right shape here

QK4 already does exactly this on the CAT server. `catserver.cpp` handles the prefixes it knows
specially and then falls through:

```cpp
// SET commands (have args) - forward to real K4
qCDebug(netCat) << "   forwarding SET to K4:" << cmd;
emit catCommandReceived(cmd);
```

A default-forward path already exists and is proven. The TCI side has no equivalent — an
unrecognised TCI command is silence.

### 5.2 Recommended approach — two layers, not one

**Layer 1: a `CatFrames::cwText()` builder.** CW-by-CAT is a *known, structured* K4 capability,
not an escape hatch. It belongs with the other 24 builders, keeping the rule the design doc
already states: *the TCI layer never spells a K4 command.* `CW_MACROS` then maps to it directly,
and `CW_MACROS_SPEED` to the existing `keyerSpeed`.

This covers the common case (a logger sending exchange macros) and needs no new protocol concept.

**Layer 2: an explicit CAT passthrough — DEFERRED TO PHASE 2.** For K4 features with no TCI
equivalent at all (the whole of §10.2), a passthrough is the pragmatic answer. It is deliberately
held back to its own phase because it is the one change that hands an external program unmediated
control of the radio. When it is built, it should be *deliberate*, not a default-forward:

- **Do not** copy CatServer's fall-through. On a CAT server the client is *already* speaking K4 and
  a raw forward is honest. A TCI client is speaking TCI; silently forwarding an unrecognised TCI
  command name as a K4 string would forward garbage.
- **Do** expose it under a distinct command name so intent is unambiguous, and require the client
  to opt in by spelling a real K4 command.
- Gate it behind a setting, default **off**. It hands an external program unmediated control of the
  radio, which is a different trust level from the curated command set.

### 5.3 What CW support does *not* need

`CW_MSG`'s editable-callsign protocol and `KEYER`'s element timing exist because ExpertSDR3
generates CW in its own DSP. The K4 generates CW in firmware from `KY`. Attempting to reproduce
the mid-flight callsign-correction semantics over a `KY` buffer is likely to produce exactly the
"drunken sailor" timing the spec describes trying to avoid. **Recommendation: implement
`CW_MACROS` and the speed commands; leave `CW_MSG`, `KEYER` and `CALLSIGN_SEND` unimplemented**
and document why. Partial CW support that keys correctly beats full coverage that stutters.

---

## 6. Defects found while writing this document

Found by comparing the implementation against the spec. **§6.1–6.3 are fixed** (commit
`419a53f`), verified against a live K4 with `scripts/tciclient.py --audit`. §6.4–6.6 are recorded
deviations, not bugs.

### 6.1 `AGC_MODE` reported a value the spec does not define — FIXED

QK4 answered `agc_mode:0,med;`. The spec lists exactly three: **`normal`, `fast`, `off`**. `med`
is not among them, so a client matching the documented vocabulary could not parse it.

Now mapped from `RadioState::AGCSpeed`, which has exactly the three states the spec wants:
`AGC_Off` → `off`, `AGC_Fast` → `fast`, `AGC_Slow` → `normal`. Pinned by
`agcModeIsAlwaysOneOfTheThreeSpecValues`, which asserts the vocabulary rather than one literal —
a pinned literal is what let the wrong value ship in the first place.

### 6.2 `SQL_LEVEL` reported a value outside the spec range — FIXED

QK4 answered `sql_level:0,20;`. The spec defines the squelch threshold as **dBm, −140…0**, so a
positive number is out of range on any reading.

Now reports **−140**. Deliberately *not* a mapping from the K4's `SQ` scale: `SQ` is an arbitrary
integer and QK4 does not know what it means in dBm, so any conversion would be invented data.
−140 is "opens on anything" — in range, and the least misleading claim available. A truthful
mapping needs K4 threshold data QK4 does not currently have. Pinned by
`squelchLevelStaysInsideTheSpecRange`.

### 6.3 The snapshot modelled two RIT/XIT offsets where the radio has one — FIXED

`TciRadioSnapshot` carried independent `ritOffsetHz` and `xitOffsetHz` where `RadioState`
correctly carries a single `ritXitOffset()`. Harmless while both read 0, but it baked a wrong
model into the struct for whoever wired it up.

Collapsed to one `ritXitOffsetHz`, reported for both TCI commands, and `rit`/`xit`/the offset are
now published from `RadioState` rather than left at struct defaults. The live radio shows exactly
why this matters: `RO+0095` with `RT0` and `XT0` — one register holding a 95 Hz offset while both
enables are off. Pinned by `ritAndXitReportTheSameOffset`.

### 6.4 `CHANNEL_COUNT` is sent as `channels_count` — deliberate, documented here

The published spec says `CHANNEL_COUNT` (singular). QK4 sends **`channels_count`** (plural)
because the reference parser aborts on the singular form. This is a knowing deviation from the
PDF in favour of what real implementations accept, pinned by
`burstUsesThePluralChannelsCount`. Recorded so it is never "fixed" back.

### 6.5 `PROTOCOL` announces 1.5, not 2.0

QK4 sends `protocol:ExpertSDR3,1.5;`. WSJT-X string-matches this and **halves transmit amplitude**
when it does not match. It is a compatibility choice, not an oversight, and it means clients will
not offer 2.0-only features (`VFO_LOCK`, `RX_CHANNEL_SENSORS`). Revisit only with a client that
needs 2.0 and tolerates the string.

### 6.6 `MIC_LEVEL` is not in the published spec

QK4 sends and answers `mic_level`, which does **not** appear anywhere in the 2.0 command list. It
was taken from a captured AetherSDR session — an undocumented ExpertSDR3 extension. Keep it (real
clients expect it), but do not treat the PDF as its authority.

---

## 7. Other notable gaps

### 7.1 `TRX` ignores arg3 (signal source)

Spec 2.0 `TRX:trx,state,source` takes a third argument: `tci`, `mic1`, `mic2`, `micPC`, `ecoder2`.
QK4 parses args 1–2 and ignores arg3, always routing TCI audio when PTT comes from TCI. Benign for
WSJT-X, but a client sending `trx:0,true,mic1` expecting a *microphone* transmission gets a TCI
one. Worth honouring once more clients are attached.

### 7.2 Sensors — IMPLEMENTED

QK4 used to echo `RX_SENSORS_ENABLE` and `TX_SENSORS_ENABLE` and then send nothing. It now
reports:

| Sensor | Source | Notes |
|---|---|---|
| `RX_SENSORS` | `RadioState::sMeter` | Deprecated in 2.0, still sent — older clients know only this |
| `RX_CHANNEL_SENSORS` | `sMeter`, `sMeterB` | Channel B only while the Sub RX is on |
| `TX_SENSORS` | `forwardPower`, `swrMeter` | Five arguments always; see the mic caveat below |

**Design: telemetry is not state.** Sensors live in `TciSensorReadings`, deliberately *outside*
`TciRadioSnapshot`. The snapshot is slow state broadcast on change; meters move continuously, so
running them through the same diff would either flood every client or need an arbitrary change
threshold. Readings are stored as they arrive and emitted on a timer, per subscriber, at the
interval that client asked for. Subscriptions are **per client and per direction** — asking for TX
readings never delivers RX levels.

The interval is clamped to the spec's 30–1000 ms rather than refused: a client asking for 1 ms
means "as fast as you can", and without the clamp it can turn the server into a packet generator
(measured: 500 readings in 500 ms).

**dBm conversion.** `RadioState` carries the K4's S-meter in its own encoding — 0–9 is S0–S9, and
anything stronger is `9.0 + dBoverS9/10`, so S9+20 arrives as `11.0`, not `29`. Converting that
with `sUnitDbm()` would clamp it to S9 and silently discard every strong signal. `SpectrumScale::
dbmForSMeterReading()` handles it, in the one place that already owns the S-unit convention.

**Two fields QK4 cannot fill honestly**, both recorded in the code rather than faked:

- **`TX_SENSORS` mic level.** The spec wants a calibrated microphone level in dBm. The K4 reports
  ALC deflection, which is a drive indicator, not a level. Reports the floor rather than passing a
  scaled ALC reading off as a measurement.
- **`TX_SENSORS` peak power.** The K4 reports *one* forward-power figure. Peak is reported equal to
  RMS; synthesising a peak by holding a maximum here would be a measurement QK4 never made.

Power and SWR are real, which is what an amplifier or band-decoder client actually needs.

### 7.3 Spots and IQ not implemented

`SPOT`, `SPOT_DELETE`, `SPOT_CLEAR`, `CLICKED_ON_SPOT`, `RX_CLICKED_ON_SPOT` — QK4 has a DX cluster
and a panadapter, so the plumbing exists; this is UI integration work, not radio work.
`IQ_START`/`IQ_STOP`/`IQ_SAMPLERATE` would require raw IQ, which the K4 link does not carry in the
form TCI expects. `iq_samplerate` is *announced* in the burst but no IQ stream is ever sent.

---

## 8. Order of work

Ordered by value per unit of risk. A K4 in **TX Test mode** transmits nothing whatever it is sent,
which is what makes the radio-touching items benchable at all.

### Done

- ~~**Fix §6.1 and §6.2**~~ (`agc_mode`, `sql_level`) — wrong values on the wire. Fixed in
  `419a53f`, verified live.
- ~~**Collapse the RIT/XIT offset model**~~ (§6.3) — fixed in the same commit, and `rit`, `xit`
  and the offset now publish from `RadioState` instead of struct defaults.
- ~~**Sub RX / VFO B**~~ — `rx_channel_enable`, and Sub audio in the right channel.
- ~~**`trx` broadcast**~~ (§11.4) and ~~`tx_frequency`~~ (§11.5).
- ~~**Implement the sensors**~~ (§7.2) — `rx_sensors`, `rx_channel_sensors`, `tx_sensors`.

### Next

1. **Wire the remaining snapshot fields to `RadioState`** (NB, NR, ANF, APF, filter, squelch) —
   read-only; makes replies that are currently constants truthful. See §10.1.
2. **`CW_MACROS` + speed, via a new `CatFrames::cwText`** (§5.2 layer 1) — the largest
   *capability* gap, and the one that makes QK4 usable to a CW client at all.
3. **The ready-to-wire SETs** (`RIT_ENABLE`, `XIT_ENABLE`, `RX_NB_ENABLE`, `RX_NR_ENABLE`,
   `CW_KEYER_SPEED`) — builders exist; bench one at a time.
4. **`DRIVE`/`TUNE_DRIVE`** — needs the percent↔watt decision first.

### Phase 2

5. **Explicit opt-in CAT passthrough** (§5.2 layer 2), default off. Held to its own phase: it is
   the one change that hands an external program unmediated control of the radio, and §10.3 sets
   its proper scope — the categories TCI has no vocabulary for, never the commands it does.
---

## 9. How to reproduce this audit

`scripts/tciclient.py` is a dependency-free TCI client that walks the inventory in this document
against a running QK4 and reports what is actually answered. It is a **bench tool, not a CI step**
— it is meant to be pointed at the real application with a real radio attached.

```
python3 scripts/tciclient.py --selftest    # offline: handshake, framing, no radio, no QK4
python3 scripts/tciclient.py --audit       # read-only conformance sweep against a running QK4
python3 scripts/tciclient.py               # interactive; prints everything the server sends
```

**Safety posture, because this runs against a live transmitter:** the audit sends only reads and
never sets a frequency, mode, split or power level. Interactive mode refuses any command that
would key the transmitter unless `--allow-tx` is given. The bare `trx;` read is answered by QK4
without keying (pinned by `doesNotKeyOnAMalformedBoolean`) and can still be skipped with
`--no-trx`.

It is deliberately a **second, independent implementation** of RFC 6455 rather than a wrapper
around the same library QK4 uses — the same library on both ends agrees with itself even when
both are wrong. It also exercises `TciController`'s thread hop, which the unit tests cannot: they
drive `TciServer` directly on one thread.

### Measured result (2026-09-15, QK4 at `8942e1e`, radio disconnected)

```
init burst: 42 commands, 41 distinct names
readable commands answered: 35/55
every reply agrees with the init burst

value checks:
    drive            0,100   OK
    tune_drive       0,100   OK
    volume           0.0     OK
    agc_mode         med     NOT a spec value (normal/fast/off)
    sql_level        20.0    OUT OF RANGE (spec: -140..0 dBm)
```

The two value failures are §6.1 and §6.2, found independently by the tool after being found by
reading the spec — which is the point of having both.

**Caveat on "answered":** 35/55 measures the TCI surface, not truthfulness. Most of those 35 are
the read-only group reporting struct defaults rather than the radio (§3.2). The audit cannot tell
the difference, and neither can a client — that is exactly why §3.2 exists and why the count above
should not be read as 64% feature coverage.
---

## 10. The other direction: QK4 capabilities with no TCI representation

Sections 2–7 ask "what does the spec define that QK4 lacks". This section asks the inverse: **what
does QK4 already model that a TCI client cannot reach.** It matters for two reasons — it is where
a QK4-aware client could be given more than ExpertSDR3 offers, and it sets the boundary of what a
CAT passthrough (§5.2, now phase 2) would be *for*.

`RadioState` exposes roughly 180 getters. Almost all of them are invisible over TCI.

### 10.1 Already modelled, TCI has a command, QK4 does not wire it

These are pure wiring — the data and the K4 builder both exist.

| Capability | QK4 has | TCI command |
|---|---|---|
| Noise blanker on/off + level | `noiseBlankerEnabled/Level`, `CatFrames::noiseBlanker` | `RX_NB_ENABLE`, `RX_NB_PARAM` |
| Noise reduction on/off + level | `noiseReductionEnabled/Level`, `CatFrames::noiseReduction` | `RX_NR_ENABLE` |
| Auto-notch | `autoNotchEnabled` | `RX_ANF_ENABLE` |
| Manual notch + pitch | `manualNotchEnabled`, `manualNotchPitch` | `RX_NF_ENABLE` |
| APF + bandwidth | `apfEnabled`, `apfBandwidth` | `RX_APF_ENABLE` |
| Squelch level | `squelchLevel` | `SQL_LEVEL` (needs a dBm mapping — see §6.2) |
| Filter bandwidth | `filterBandwidth`, `CatFrames::filterBandwidth` | `RX_FILTER_BAND` |
| RF power | `rfPower`, `CatFrames::rfPower` | `DRIVE`, `TUNE_DRIVE` |
| CW keyer speed | `keyerSpeed`, `CatFrames::keyerSpeed` | `CW_KEYER_SPEED`, `CW_MACROS_SPEED` |
| VFO lock | `lockA`, `lockB` | `LOCK`, `VFO_LOCK` |
| S-meter (both receivers) | `sMeter`, `sMeterB` | `RX_SENSORS`, `RX_CHANNEL_SENSORS` |
| ALC / SWR / forward power | `alcMeter`, `swrMeter`, `forwardPower` | `TX_SENSORS` |
| Mic gain | `micGain` | `MIC_LEVEL` (undocumented extension — §6.6) |

### 10.2 Modelled by QK4, no TCI command exists at all

This is the interesting half. A TCI client can never see any of it, however complete the
implementation becomes, because the protocol has no vocabulary for it.

**Antenna and front end**
`rxAntennaMain`, `rxAntennaSub`, `txAntenna` (+ names), `preamp`/`preampEnabled`,
`attenuatorEnabled`/`attenuatorLevel`, `rfGain`, `atuMode`, `xvtrBandSelect`, `isXvtrPowerMode`.
TCI has `AGC_GAIN` and nothing else here. Antenna switching and ATU state are completely absent
from the protocol.

**Elecraft-specific DSP**
`ssnrEnabled`/`ssnrLevel` (Elecraft's noise reduction), `afxMode`, `essbEnabled`, `ssbTxBw`,
`ifShift`, `filterPosition`, `diversityEnabled` (+ `CatFrames::diversity`). Diversity reception in
particular has no TCI concept — it is a two-receiver mode the protocol cannot describe.

**Built-in text decode**
`textDecodeMode`, `textDecodeLines`, `textDecodeThreshold` (and `*B` for the sub receiver). The K4
decodes CW/RTTY/PSK in firmware and QK4 surfaces it. TCI has no decoded-text channel in either
direction — the nearest thing is the CW *transmit* macro set. A QK4-aware client could be handed
this; a stock TCI client can never ask for it.

**CW and keying detail**
`cwPitch`, `keyingWeight`, `qskEnabled`, `qskDelayCW/Data/Voice`, `messageBank`,
`monitorLevelCW/Data/Voice`, `monitorModeCode`, `delayForCurrentMode`. TCI's CW surface is macro
text plus speed; the K4's keying character is not expressible.

**Voice and TX shaping**
`voxEnabled`, `voxGainVoice/Data`, `antiVox`, `compression`/`compressionDb`, `micInput`,
`micFrontBias/Preamp/Buttons`, `micRearBias/Preamp`. TCI has `MON_*` and nothing else.

**Radio health telemetry**
`paTemperatureC`, `lpaTemperatureC`, `supplyVoltage`, `supplyCurrent`, `paDrainCurrent`,
`optionModules`, `radioID`, `radioModel`, `testMode`. `TX_SENSORS` carries mic level, power, peak
power and SWR — no temperatures, no voltages. QK4 receives all of it continuously (the `SIFP`,
`SICP`, `SID` telemetry frames).

**Panadapter and display**
`refLevel`, `spanHz`, `averaging`, `peakMode`, `waterfallColor`, `waterfallHeight`, `displayFps`,
`displayModeLcd/Ext`, `dualPanMode*`, `miniPanAEnabled/B`, `mainRxDisplayAll`, `subRxDisplayAll`.
TCI treats the panorama as the *server's* UI (`SPOT`, `CLICKED_ON_SPOT`, `SET_IN_FOCUS`) and has
no way to describe or control a client-side one.

**Tuning behaviour**
`tuningStep`, `vfoLink`, `freeze`, `fixedTune`/`fixedTuneMode`, `bSetEnabled`, `vfoACursor`.

**Audio routing**
`lineInSource`, `lineInJack`, `lineInSoundCard`, `lineOutLeft/Right`, `audioMixLeft/Right`,
`balanceMode`, `balanceOffset`, `streamingLatency`. TCI's `RX_VOLUME`/`RX_BALANCE`/`MUTE` cover a
fraction; the K4's routing matrix does not fit.

### 10.3 What follows from this

1. **TCI is a lowest-common-denominator protocol.** It was designed around ExpertSDR3's feature
   set, and the K4 has a large surface outside it. Full TCI coverage is therefore a *ceiling*, not
   a measure of how well QK4 controls the radio — QK4's own CAT server on 9299 will always reach
   further.
2. **This is the real argument for the phase 2 passthrough.** Not "some commands are unimplemented"
   — §3.2 shows most of those are wiring — but that entire categories above have no TCI
   vocabulary and never will. An escape hatch is the only way a client reaches antenna switching,
   diversity, text decode or PA telemetry.
3. **It also bounds the passthrough.** Anything in §10.1 should be a *proper* TCI command, not a
   passthrough, because the protocol already has a word for it. Passthrough is for §10.2 only.
   A passthrough used where a real command exists is how a protocol rots.
---

## 11. Server-to-client obligations — what QK4 must send unprompted

Sections 2–7 audit what QK4 answers when **asked**. This section audits what the protocol requires
a server to send when **nobody asked** — the half a read-only sweep like `--audit` can never
measure, because the tool only ever sees replies to its own queries.

This distinction is not academic. It is where the TR4W transmit-indicator bug lived: every command
involved was "implemented", `--audit` reported it answered, and the status message still never
arrived.

### 11.1 The rule

Spec §3.1 makes the server a **synchroniser**, not a request/response service:

> When a parameter change occurs in the ExpertSDR3 (server) program, the server notifies all
> connected clients, i.e., clients do not need to poll the server constantly, any change of state
> will be sent in time to all clients. If the client sends a new state, the server will set it to
> itself, as well as send it to all clients.

Two obligations follow, and QK4 was violating both for `trx`:

1. **A change of state is broadcast, whatever caused it** — including changes the radio made on
   its own, with no client involved.
2. **A change requested by one client is sent to _all_ clients**, not just the requester.

### 11.2 Initialization — complete

All nine (§2) are sent on connect, `ready;` last. QK4's burst is larger than the required set
because it also seeds current state, which is what real servers do.

### 11.3 Broadcast on change

| Command | Broadcast? | Notes |
|---|---|---|
| `vfo` (both channels) | ✅ | |
| `dds` | ✅ | |
| `tx_frequency` | ✅ | Added — see §11.5 |
| `modulation` | ✅ | |
| `split_enable` | ✅ | |
| `rx_channel_enable` | ✅ | Sub RX |
| `trx` | ✅ | **Fixed — see §11.4** |
| `rit_enable`, `xit_enable`, `rit_offset`, `xit_offset` | ✅ | |
| `agc_mode` | ✅ | |
| Everything else in §3.2 | n/a | Reported from constants, so nothing ever changes. **When those are wired to `RadioState` they must gain a broadcast in the same commit** — a value that can change and is not broadcast is this same bug again. |

### 11.4 The `trx` defect — FIXED

**Symptom, found with TR4W as the client:** QK4 never sent a transmit-status message. TR4W's ON
indicator stayed dark while the radio transmitted. TR4W needed no change — it already handles
`trx` — QK4 simply never sent it.

**Cause.** `setSnapshot` carried the transmit flag over from the previous snapshot
*unconditionally*, so a transmit state arriving from `RadioState` could never produce a broadcast.
`TciController` compounded it by never populating the field and never subscribing to
`transmitStateChanged`. A transmit begun by the microphone, a footswitch, another CAT client, or
the radio's own keying was invisible to every TCI client.

That carry-over was itself a fix for the opposite defect in phase 5: the K4 keys only once TX
audio starts arriving, so `RadioState` lags a TCI client by whole packets, and broadcasting the
radio's `false` mid-transmission made WSJT-X stop sending audio. The first fix was correct about
the problem and too broad in the remedy.

**The rule now:** ownership decides whose transmit state wins.

- **A TCI client holds PTT** → its own assertion wins; the lagging radio value is ignored.
- **Nobody holds PTT** → the radio is the only truth there is, and a change is broadcast.

Both directions are pinned, and both were revert-tested:
`broadcastsTransmitStartedByTheRadioItself` fails against the old unconditional carry-over (no
message at all — the reported bug), and `aRadioStateUpdateStillDoesNotUnkeyTheOwningClient` fails
if the ownership guard is removed (the phase 5 defect returns).

**A second instance of the same class, also fixed:** `setPtt` confirmed a key with
`sendText(clientId, …)` — a **unicast to the requester**. With two clients attached (WSJT-X and a
logger, say), the second never learned the transmitter had been keyed. Now broadcast, which still
includes the requester and so remains the echo WSJT-X waits for.

### 11.5 `tx_frequency` — ADDED

`TX_FREQUENCY` is server-to-client only: **the protocol defines no read form**, so a client that
wants the transmit frequency cannot ask for it and only ever learns by being told. QK4 already
computes the value (`txChannelHz()`); it simply never sent it. Now seeded in the burst and
broadcast whenever it changes.

It matters most under split, which is exactly when it differs from the receive VFO, and it is what
an amplifier or band-decoder client needs.

### 11.6 Still missing, and why

| Message | QK4 has the data? | Status |
|---|---|---|
| `rx_sensors`, `rx_channel_sensors` | Yes | **Implemented** — §7.2 |
| `tx_sensors` | Partly | **Implemented**; mic level and peak power cannot be measured — §7.2 |
| `tx_enable` on band change | Yes — frequency is tracked | Sent at connect only. The spec says also "when the band is changed, in case transmitter permission was changed" |
| `vfo_lock` | **Yes** — `lockA`, `lockB` | Not sent. TCI 2.0 |
| `tx_footswitch` | **No** | The K4 reports that it is transmitting, not *what* keyed it. Cannot be sent honestly |
| `clicked_on_spot`, `rx_clicked_on_spot` | Partly | Needs panadapter/DX-cluster UI integration, not radio work |
| `app_focus` | Yes | About the server's own window; low value for a headless-ish control app |
| `cw_macros_empty`, `callsign_send` | No | CW, §5 |

With the sensors done, the largest remaining omissions are `tx_enable` on band change and
`vfo_lock` — both small, both read-only.

### 11.7 Why `--audit` cannot catch this class

`scripts/tciclient.py --audit` sends reads and checks replies. Every gap in this section is
invisible to it: a message nobody asked for cannot appear in a reply. `tx_frequency` and
`tx_footswitch` have no read form at all, so they can *never* show up in an audit sweep.

Catching these needs the other kind of test — connect, change something on the radio, and assert a
message arrives unprompted. `broadcastsTransmitStartedByTheRadioItself` and
`announcesTheTransmitFrequency` are that shape, and `tests/test_tcisensors.cpp` is an entire suite
of it: nothing in that file is ever a reply to a query.

**`scripts/tcimonitor.py` covers the interactive half.** It subscribes to the sensors and renders
whatever arrives, live. If a meter is not moving on screen, the server is not sending it — which
is the check that no amount of `--audit` can perform. It sends exactly two commands, both
subscriptions, and never a SET, so it is safe to leave running against a live radio.
