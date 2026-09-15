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

**Headline:** QK4 is complete for a *digital-mode* client (WSJT-X works end to end — receive,
decode, transmit, and a full FT8 QSO). It is not usable by a **CW** client at all, and it reports
several values it does not actually track.

**Three defects found while writing this document** — see §6. They are real and currently shipped.

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

**Layer 2: an explicit CAT passthrough, for everything else.** For K4 features with no TCI
equivalent at all, a passthrough is the pragmatic answer — but it should be *deliberate*, not a
default-forward:

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

These are in shipped code, found by comparing the implementation against the spec.

### 6.1 `AGC_MODE` reports a value the spec does not define

QK4 answers `agc_mode:0,med;`. The spec lists exactly three: **`normal`, `fast`, `off`**. `med` is
not among them. A client matching on the documented vocabulary will fail to parse it.

`RadioState::AGCSpeed` exists and `CatFrames::agcSpeed` (`GT`) exists, so the fix is a real
mapping onto the three legal names rather than a hardcoded string. **Should be fixed before any
non-WSJT-X client is attached.**

### 6.2 `SQL_LEVEL` reports a value outside the spec range

QK4 answers `sql_level:0,20;`. The spec defines squelch threshold as **dBm, −140…0**. `20` is
positive and therefore out of range in either interpretation. It is a placeholder that was never
reconciled with the units.

### 6.3 The snapshot models two RIT/XIT offsets where the radio has one

`TciRadioSnapshot` carries independent `ritOffsetHz` and `xitOffsetHz`. `RadioState` correctly
carries a single `ritXitOffset()`. Harmless today because both report 0, but it bakes a wrong
model into the struct: whoever wires these up will naturally populate two fields from one source
and then report an XIT offset that cannot be true. **Collapse to one field with two enables, to
match both the radio and `RadioState`.**

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

### 7.2 Sensors are acknowledged but never sent

QK4 echoes `RX_SENSORS_ENABLE` and `TX_SENSORS_ENABLE` so clients do not wait, then sends no data.
**QK4 already has everything needed:**

| Sensor | Data source |
|---|---|
| `RX_SENSORS` / `RX_CHANNEL_SENSORS` | `RadioState::sMeter*`, `CatFrames::sMeterMain` |
| `TX_SENSORS` (mic, power, peak, SWR) | `RadioState::txMeterChanged(alc, compression, fwdPower, swr)`, `swrMeter()` |

This is the cheapest high-value gap remaining: no new radio commands, no risk of moving the radio,
and it makes QK4 useful to amplifier controllers and panadapter clients. `RX_SENSORS` is
deprecated in 2.0 in favour of `RX_CHANNEL_SENSORS`; implement both.

### 7.3 Spots and IQ not implemented

`SPOT`, `SPOT_DELETE`, `SPOT_CLEAR`, `CLICKED_ON_SPOT`, `RX_CLICKED_ON_SPOT` — QK4 has a DX cluster
and a panadapter, so the plumbing exists; this is UI integration work, not radio work.
`IQ_START`/`IQ_STOP`/`IQ_SAMPLERATE` would require raw IQ, which the K4 link does not carry in the
form TCI expects. `iq_samplerate` is *announced* in the burst but no IQ stream is ever sent.

---

## 8. Recommended order of work

Ordered by value per unit of risk. Anything touching the radio needs a K4 on the bench.

1. **Fix §6.1 and §6.2** (`agc_mode`, `sql_level`) — wrong values on the wire today, no radio risk.
2. **Collapse the RIT/XIT offset model** (§6.3) — no radio risk, prevents a wrong model spreading.
3. **Implement the sensors** (§7.2) — high value, read-only, data already in `RadioState`.
4. **Wire the snapshot to `RadioState`** for the fields already reported as constants (NB, NR,
   filter, squelch) — read-only, makes existing replies truthful.
5. **`CW_MACROS` + speed via a new `CatFrames::cwText`** (§5.2) — the largest capability gap.
6. **The ready-to-wire SETs** (`RIT_ENABLE`, `XIT_ENABLE`, `RX_NB_ENABLE`, `RX_NR_ENABLE`,
   `CW_KEYER_SPEED`) — builders exist; bench one at a time.
7. **`DRIVE`/`TUNE_DRIVE`** — needs the percent↔watt decision first.
8. **Explicit opt-in CAT passthrough** (§5.2 layer 2), default off.
9. **Sub RX** — unlocks `trx_count:2` and the whole `*_B` family at once.
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
