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
| Bidirectional control (4.2) | 42 | 6 SET + 24 reported | Reporting is real, not constants — §3.2 |
| Unidirectional control (4.3) | 21 | 8 | Audio stream config + start/stop |
| Notification (4.4) | 11 | 5 | Sensors implemented — §7.2 |
| New in 2.0 (4.5) | 2 | 1 | `RX_CHANNEL_SENSORS` done; `VFO_LOCK` absent |
| CW (3.2) | 9 | 3 | `CW_MACROS`, `CW_MACROS_STOP`, speed. **Verified on air — see §5** |

A fourth defect — **no transmit-status message was ever sent** — was found later with TR4W as the
client and is covered in §11, along with an audit of everything a server must send unprompted.

**Headline:** QK4 is complete for a *digital-mode* client (WSJT-X works end to end — receive,
decode, transmit, and a full FT8 QSO), reports **two receivers** with independent mode, filter,
AGC, RIT, volume and S-meter, and now **keys CW** for a logger — QLog sends its macros through
QK4 to the radio and they key correctly, chunking and prosigns included (§5, §12.7).

**Everything below the protocol tables has been verified against a live K4**, not just unit-tested
— see §12 for what the radio actually confirmed.

**Three defects were found while writing this document and are now fixed** — see §6.

---

## 2. Initialization commands (spec 4.1) — complete

| Spec | QK4 | Depth | Notes |
|---|---|---|---|
| `VFO_LIMITS` | ✅ | Static | `100000,54000000` — K4 HF/6 m range |
| `IF_LIMITS` | ✅ | Static | `-48000,48000` |
| `TRX_COUNT` | ✅ | Static `2` | Main and Sub, both fully addressable. See §4.2 |
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
| `VFO` | ✅ | `CatFrames::frequencyA` / `frequencyB` (`FA`/`FB`) | Both receivers, and channel 1 of receiver 0 |
| `DDS` | ✅ | `CatFrames::frequencyA` | Alias for a receiver's own VFO |
| `MODULATION` | ✅ | `CatFrames::modeA` / `modeB` (`MD`/`MD$`) | Per receiver, for the 10 announced modes |
| `TRX` | ✅ | PTT gate + TX audio | Receiver 0 only — one transmitter. **arg3 ignored**, §7.1 |
| `SPLIT_ENABLE` | ✅ | `CatFrames::split` (`FT`) | Edge-triggered only, by design |
| `RX_CHANNEL_ENABLE` | ✅ | `CatFrames::subReceiver` (`SB`) | Sub RX on/off. `rx_enable` accepted as a synonym, §4.2 |

These six are what a digital-mode client needs, and they are bench-verified against a live K4
through a completed FT8 QSO.

### 3.2 Answered read-only — QK4 reports, the radio does not move

Phase 8a. A SET-shaped command gets the current value back, never a false acknowledgement.

`RIT_ENABLE`, `XIT_ENABLE`, `RIT_OFFSET`, `XIT_OFFSET`, `RX_FILTER_BAND`, `DRIVE`, `TUNE_DRIVE`,
`AGC_MODE`, `LOCK`, `SQL_ENABLE`, `SQL_LEVEL`, `MUTE`, `VOLUME`, `RX_NB_ENABLE`, `RX_NR_ENABLE`,
`RX_ANF_ENABLE`, `RX_APF_ENABLE`

**Almost all of these now report the actual radio and broadcast on change**, per receiver:
`RIT_ENABLE`, `XIT_ENABLE`, `RIT_OFFSET`, `XIT_OFFSET`, `AGC_MODE`, `DRIVE`, `TUNE_DRIVE`,
`RX_FILTER_BAND`, `RX_NB_ENABLE`, `RX_NR_ENABLE`, `RX_ANF_ENABLE`, `RX_APF_ENABLE`,
`RX_NF_ENABLE`, `RX_VOLUME`, `LOCK`, `SQL_ENABLE`, plus the globals `MIC_LEVEL` and
`CW_KEYER_SPEED` / `CW_MACROS_SPEED`.

Only **`SQL_LEVEL`** and **`MUTE`** are still constants. `SQL_LEVEL` is blocked on units (§6.2);
`MUTE` is not modelled for TCI.

A note on why the broadcasts matter as much as the replies: every one of these was answerable on
request *before* it was broadcast on change, and that gap is a bug in itself — §11.3. `DRIVE` is
the worked example: it answered queries correctly while a client watched the power knob move and
saw nothing.

**These used to be hardcoded constants.** The snapshot once carried only frequency, mode and split
from `RadioState`, so `rx_nb_enable:0,false;` meant "I don't model this" rather than "the K4's
noise blanker is off" — a reply a client cannot distinguish from a measurement. They are now wired
through, per receiver, and broadcast on change.

**QK4 already has the K4 builders for most of the missing SETs:**

| TCI SET | Existing builder | K4 cmd | Blocker |
|---|---|---|---|
| `DRIVE` / `TUNE_DRIVE` | `rfPower` / `rfPowerExtended` | `PC` | **Reporting done** (§4.4). The SET is still not wired |
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
| `AGC_GAIN` | AGC threshold in dB | K4 has AGC controls; no builder |
| `RX_NB_PARAM` | NB threshold + pulse width | `RadioState` tracks `noiseBlankerLevel`; no builder |
| `RX_BIN_ENABLE` | Binaural/pseudo-stereo | K4 has no equivalent |
| `RX_ANC_ENABLE` | Adaptive noise cancellation | No K4 equivalent |
| `RX_DSE_ENABLE` | Digital surround for CW | No K4 equivalent |
| `RX_NF_ENABLE` | Notch filter module | K4 has notch; no builder |
| `RX_MUTE`, `RX_BALANCE` | Per-receiver audio | **Local to QK4's audio stack, never CAT** |
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

### 4.2 Sub receiver — IMPLEMENTED as receiver 1

QK4 declares **`trx_count:2`**. Receiver 0 is the Main RX, receiver 1 the Sub RX, each reporting
its own frequency, mode, filter, AGC, RIT, squelch, DSP flags, volume and S-meter from
`RadioState`'s `*B` getters.

**It had to be a receiver, not a channel, and that was not obvious.** TCI has two axes carrying
different things:

| Axis | Carries |
|---|---|
| **channel** (A/B in a receiver) | frequency and audio only — `vfo`, `rx_channel_enable`, `rx_volume`, `rx_balance`, `vfo_lock`, `rx_channel_sensors` |
| **trx** (receiver) | everything else — `modulation`, `rx_filter_band`, `agc_mode`, `rit`/`xit`, `sql`, the DSP flags, `lock` |

`modulation` takes no channel argument. So the first implementation, which modelled the Sub RX as
channel 1 of receiver 0, left VFO B's mode with **nowhere to live** — and the same for its filter,
AGC and RIT. The bug surfaced as "VFO B's frequency appears but its mode does not". ExpertSDR3's
channels A/B are two tuning points inside one receiver's passband; the K4's Sub RX is an
independent receiver.

Channel 1 of receiver 0 keeps its other job, the **split transmit VFO**. On a K4 that is the same
VFO B the Sub RX tunes, so both mappings resolve to `CatFrames::frequencyB` and point at one
register — which is correct, not a collision.

`rx_channel_enable` and `rx_enable` are both accepted and both broadcast, because clients disagree
about how a second receiver is addressed. Only the main receiver transmits: `trx:1` is declined
explicitly and `tx_enable` reports true only for receiver 0.

### 4.3 Units and vocabulary mismatches

| TCI | Range/vocabulary | K4 | Mismatch |
|---|---|---|---|
| `DRIVE` | 0–100 | `PC` in watts/mW by range | **Resolved — reported as-is, §4.4** |
| `VOLUME`, `RX_VOLUME` | −60…0 dB | QK4 audio stack, linear | Not CAT at all |
| `SQL_LEVEL` | −140…0 dBm | K4 squelch 0–29 | Scale conversion required |
| `AGC_MODE` | `normal`/`fast`/`off` | `GT` numeric | Enum mapping required |
| `AGC_GAIN` | −20…120 dB | K4 AGC controls | No direct equivalent |

### 4.4 `DRIVE`: report the power QK4 already displays

Found by reducing power on the radio and watching a client sit at `100` — `drive` was never
populated from `RadioState` and never broadcast, so it reported the struct default forever.

`RadioState::rfPower()` is the value, and it is **already in the units the operator sees**: watts
in QRP and QRO, mW in XVTR, with `powerRange()` saying which. `SideControlPanel::setPower()` does
nothing but choose the unit label and the decimal places — there is no normalisation anywhere in
QK4 to reuse, and none worth inventing.

So `DRIVE` reports that value directly, clamped to the protocol's 0–100. The K4 front panel, QK4's
own PWR button and a TCI client then all show **the same number**.

An earlier attempt scaled it to a percentage of the active range (QRO against nominal 100 W, QRP
against 10 W, XVTR against 10 mW). That is arguably the tidier reading of a field the spec
documents as "output power value from 0 to 100", and in QRP it is genuinely more informative — 10 W
there is *full* power and reports as `10` under the simple rule. It was dropped anyway, because it
made a client read `82` while the radio said 90 W. **Agreement with the radio in front of the
operator beats theoretical tidiness**, and it means no conversion table has to be kept in step with
the K4's ranges.

The one place the simple rule loses: QRO runs to 110 W while the protocol field stops at 100, so
101–110 W all report `100`. Nothing fixes that without normalising.

`TUNE_DRIVE` tracks `DRIVE`. The K4 has no separate tune-power setting, and reporting an
independent value would invent a control the radio does not have.

**Reported for receiver 0 only.** Drive belongs to the transmitter and the K4 has exactly one, so
`drive:1,…` would describe a second power control that does not exist. A drive query addressed to
receiver 1 is recognised and deliberately unanswered.

**Reporting only.** A client sending `drive:0,50;` still does not change power — that SET is in §8.

### 4.5 `RX_VOLUME`: QK4's mix, not the rig's AF gain

A TCI client is listening to **QK4's audio stream**, not to the rig's speaker, so the level that
means anything to it is QK4's Main/Sub mix. The K4's AF gain describes a speaker the client cannot
hear, and QK4 has no other use for it — sourcing `rx_volume` from `AG` would have meant adding a
CAT poll purely to answer one field.

The conversion is **exact, not fitted**: QK4's sliders are 0–100 scaled to a linear amplitude gain
of 0.0–1.0, and `dB = 20·log₁₀(gain)` is the definition of that ratio in dB. Gain 1.0 → 0 dB,
0.5 → −6 dB, silence clamps to the −60 the protocol documents as "no sound".

It reads the **applied gain**, not the slider position, because the two disagree: in BAL mode the
sub slider drives the L/R balance offset and leaves sub volume alone. The gain is what the
listener hears.

QK4 deliberately does **not** parse the radio's `AG`/`AG$` at all. It was written and then removed:
nothing consumes it once `rx_volume` reports QK4's mix, and carrying a CAT parser with no consumer
on the chance a future change might want it is how a codebase accumulates weight.

Separately, and untouched: `catserver.cpp` answers an `AG;` query with a hardcoded `AG000;`,
telling every CAT client on port 9299 that the audio is muted whatever the radio is doing. `SQ;`
has the same shape. Both are pre-existing, unrelated to TCI, and raised with the maintainer rather
than fixed here.

### 4.6 Commands with no K4 concept

`RX_BIN_ENABLE` (binaural), `RX_ANC_ENABLE` (adaptive noise cancellation), `RX_DSE_ENABLE`
(digital surround for CW), `LINE_OUT_RECORDER_*` (server-side recording), `APP_FOCUS` /
`SET_IN_FOCUS` (ExpertSDR3 window management). These should stay unimplemented; silence is the
protocol's defined response.

---

## 5. CW — implemented for the commands a logger uses

**`CW_MACROS` and `CW_MACROS_STOP` are implemented and verified on air.** QLog drives them through
QK4 to the radio; see §12.7 for what the bench confirmed.

| Spec | Purpose | QK4 |
|---|---|---|
| `CW_MACROS:trx,text` | Send arbitrary CW text | ✅ |
| `CW_MACROS_STOP` | Abort transmission | ✅ confirmed on air — cuts mid-message |
| `CW_MACROS_SPEED` | Speed | ✅ reported and settable (`KS`) |
| `CW_MSG:trx,prefix,callsign,suffix` | Structured message with editable callsign | ❌ by decision, §5.3 |
| `CW_MSG:text` | Correct a callsign mid-transmission | ❌ by decision, §5.3 |
| `CW_TERMINAL:bool` | Stay in TX between macros | ❌ |
| `CW_MACROS_EMPTY` | Server→client: queue drained | ❌ |
| `CALLSIGN_SEND:call` | Server→client: final callsign as sent | ❌ by decision, §5.3 |
| `CW_MACROS_SPEED_UP` / `_DOWN` / `_DELAY` | Speed stepping and timing | ❌ |
| `KEYER:trx,state,ms` | Straight-key state with element timing | ❌ by decision, §5.3 |

### 5.0 What a real client actually sends

Captured from QLog on the bench, once QK4 started logging commands it does not process:

```
cw_macros:0,CQ CQ CQ DE NY4I NY4I NY4I K
cw_macros:0,QRZ?
cw_macros:0,DE NY4I GE OM TNX FER CALL UR RST 599 599 NAME TOM TOM QTH CLEARWATER ...
```

Six `cw_macros` and **nothing else** — no `trx` to key first, no `cw_msg`, no `keyer`,
no `cw_terminal`. The server is expected to do the whole job: key, send, unkey. That single
observation is what set the scope of this section.

Note the name is misleading: `CW_MACROS` does **not** invoke a stored macro on the radio. It
carries the already-expanded text. The macro is the logger's concept, resolved before it reaches
the wire.

### 5.1 The character collisions — the reason this is not a passthrough

TCI's macro grammar and the K4's `KY` command use the same characters for different things, and two
of the K4's meanings are destructive:

| Char | TCI means | K4 `KY` means |
|---|---|---|
| `>` `<` | raise / lower speed by 5 WPM | **`<` enters TX TEST mode** until `>` returns to TX NORM |
| `\|` | prosign bracket, `\|SK\|` | **terminates TX** in FSK/PSK |
| `*` | escaped `;` (TCI reserves `:` `,` `;` and carries them as `^` `~` `*`) | **the SK prosign** |
| `@` | — | **terminates the CW message**, truncating it |

Forwarding TCI macro text into `KY` unprocessed would take the transmitter off the air on any
message containing a speed marker. All of it is consumed or stripped.

`*` is the trap: it means opposite things at the two ends. Unescaping (`*`→`;`) MUST happen before
prosign translation (`|SK|`→`*`), or a genuine SK would become a semicolon and truncate the CAT
command carrying it. Pinned by `tests/test_cwmacro.cpp`.

### 5.2 How it is built

Two units, keeping the rule the design doc states — *the TCI layer never spells a K4 command*:

- **`src/network/cwmacro.{h,cpp}`** knows TCI's grammar. Undoes the escaping, resolves `>` and `<`
  into speed-homogeneous segments, leaves prosigns in `|XX|` form.
- **`CatFrames::cwText()`** knows the K4. Spells the prosigns from the manual's table
  (`(`=KN, `+`=AR, `=`=BT, `%`=AS, `*`=SK, `!`=VE), strips what the radio would act on, chunks,
  and frames as `KY*[text];`.

**Chunking**: 60 characters — the manual's documented maximum, bench-confirmed — splitting on a
word boundary and carrying the space to the **start** of the next chunk, because the radio trims
trailing spaces and a chunk ending on a real word gap would lose it, keying `NY4I NY4I` as
`NY4INY4I`. **Not padded.**

An earlier version chunked at 22 and padded to 22, having read TR4W's `CWFrameRule(22, True)` as a
maximum. Its 22 is a MINIMUM — padding short commands so they are not swallowed after the keyer
abort TR4W sends before every message — and QK4 sends no such abort, so neither the length nor the
padding ever applied here. Read as a maximum it made QK4 send five commands where two would do.

**`KYW`** (the wait flag) only where a `KS` follows, which is the use the manual names. It stalls
every later command QK4 sends — polling included — until the message has been keyed, so a macro
with no speed markers pays nothing for it. Speed is restored afterwards only if the macro moved it.

**`CatFrames::keyerSpeed` clamps to 8..100**, the documented `KS` range. It was unreachable with
bad input before this, but `>` arithmetic has no upper bound of its own.

**Layer 2: an explicit CAT passthrough — STILL DEFERRED TO PHASE 2.** For K4 features with no TCI
equivalent at all (§10.2). Held back because it is the one change that hands an external program
unmediated control of the radio. When built it should be *deliberate*, not a default-forward:

- **Do not** copy CatServer's fall-through. On a CAT server the client is *already* speaking K4 and
  a raw forward is honest. A TCI client is speaking TCI; forwarding an unrecognised TCI command
  name as a K4 string would forward garbage.
- **Do** expose it under a distinct command name, and require the client to spell a real K4 command.
- Gate it behind a setting, default **off**.

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

**This held.** QLog asked for none of them, and the implemented subset keys a full CQ and exchange
correctly. If a client does want them it now says so: an unimplemented command is reported in
QK4's log rather than dropped in silence (§5.0).

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

### Done, and verified on a live K4

- ~~`agc_mode` and `sql_level` reporting illegal values~~ (§6.1, §6.2).
- ~~The RIT/XIT offset model~~ (§6.3) — one register, two enables, as the radio has it.
- ~~**Sub RX**~~ — as **receiver 1** (§4.2), with its own mode, filter, AGC, RIT, volume and
  S-meter, and its audio in the right channel.
- ~~`trx` broadcast on radio-driven transmit~~ (§11.4) and ~~`tx_frequency`~~ (§11.5).
- ~~**Sensors**~~ (§7.2) — `rx_sensors`, `rx_channel_sensors`, `tx_sensors`.
- ~~`drive`~~ (§4.4), ~~`rx_filter_band`~~, ~~`cw_keyer_speed`~~, ~~`rx_nf_enable`~~,
  ~~`mic_level`~~, ~~`rx_volume`~~ (§4.5) — all reporting the radio instead of constants, and all
  broadcasting on change.
- ~~**All eight SETs**~~ — rit/xit enable and offset, NB, NR, AGC, filter band and drive, each
  confirmed on the radio as a round trip (§12.6).
- ~~FM reported as `nfm`~~ — the K4 has one FM mode; `nfm` is no longer advertised, still accepted.

### Next

1. ~~**`CW_MACROS` + a `CatFrames::cwText`**~~ — done and verified on air (§5, §12.7).
2. **The SETs still missing a builder** — `mute`, `sql_enable`/`sql_level`, `lock`,
   `rx_anf_enable`, `rx_apf_enable`, `rx_nf_enable`. Each needs a `CatFrames` builder in the
   K4 set form, which is where the last three bugs lived; bench each with `tcitester.py`.
3. **Sub-receiver SETs** — the K4 forms are $-suffixed (RT$, GT$, NB$, NR$, BW$) and have no
   builders. Reporting for receiver 1 already works.
4. **`SQL_LEVEL`** — needs a decided K4-to-dBm scale before it can report anything true (§6.2).
5. **`VFO_LOCK`** and **`tx_enable` on band change** — small, read-only, both tracked already
   (§11.6).

### Phase 2

6. **Explicit opt-in CAT passthrough** (§5.2 layer 2), default off. Held to its own phase: it is
   the one change that hands an external program unmediated control of the radio, and §10.3 sets
   its proper scope — the categories TCI has no vocabulary for, never the commands it does.

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

**Caveat on "answered":** the count measures the TCI surface, not truthfulness. At the time of
that run most of the answers were struct defaults rather than the radio, and **an audit sweep
cannot tell the difference — neither can a client.** That is why §3.2 tracks which values are real
and why a count like this should never be read as a feature-coverage percentage. Most of those
values are now wired to `RadioState`; §12 records what a live radio confirmed.
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
| `drive`, `tune_drive` | ✅ | **Fixed** — see §4.4 |
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

**`tcimonitor.py` covers the interactive half** — it lives in the
[ny4i/utilities](https://github.com/ny4i/utilities) repository, since it is a plain TCI client and
works against any TCI server rather than being QK4-specific. It subscribes to the sensors and renders
whatever arrives, live. If a meter is not moving on screen, the server is not sending it — which
is the check that no amount of `--audit` can perform. It sends exactly two commands, both
subscriptions, and never a SET, so it is safe to leave running against a live radio.
---

## 12. What the radio actually confirmed

Everything in §§2–11 describes intent. This section records what a **live K4** confirmed, because
the difference between the two is where every defect in this feature has lived. Four reached the
air during phases 4–6, and every one was found by the radio rather than by a test.

Captured from a real init burst and a live sensor subscription, 2026-09-16, radio in **TX Test
mode** (transmits nothing whatever it is sent, which is what made the transmit-side items
benchable at all).

### 12.1 Two receivers, genuinely independent

```
RX0 Main  14.073.930   digu   agc normal  filter -1000..+4000  vol  -5 dB
RX1 Sub   14.072.900   cw     agc fast    filter   +30..+1030  vol -60 dB
```

Different mode, AGC, filter and volume on each receiver, simultaneously. That single line is the
proof the §4.2 restructure was necessary and correct: under the channel model, **none** of
`modulation`, `agc_mode` or `rx_filter_band` could have carried a second value at all.

S-meters confirmed independent too — Main −118 dBm and Sub −121 dBm in one sample, −97/−118 in
another, tracking separately.

### 12.2 Values that are real, not constants

| Reported | Live value | Cross-check |
|---|---|---|
| `drive:0` | `45` | tracked the power knob; radio sent `PC045H` |
| `cw_keyer_speed` | `21` | the K4's `KS` |
| `mic_level` | `35` | `RadioState::micGain` |
| `rx_filter_band:0` | `30,1030` | a real 1 kHz CW filter, not the old 100/2800 default |
| `agc_mode` | `fast` | and a legal spec value, unlike the `med` it used to send |
| `modulations_list` | no `nfm` | the K4 has one FM mode |
| `split_enable:0` | `true` | which is why `tx_frequency` correctly followed VFO B |

### 12.3 Audio

101 consecutive `RX_AUDIO` frames, **every one with differing left and right channels** (mean
|L| 0.0195 against |R| 0.0496). L is Main, R is Sub, exactly as §4.2 intends — and measurably not
a duplicated mono stream, which is what it was before.

### 12.4 Sensors

Subscribed at a 100 ms interval: 30 readings in 3 s of each type, silent before subscribing and
silent again after unsubscribing. Both spellings of the sub-receiver level arrive
(`rx_channel_sensors:1,0,…` and `rx_channel_sensors:0,1,…`) and agree.

### 12.5 Transmit state — confirmed with TR4W

The defect that started §11.4: a transmit begun anywhere other than a TCI client was invisible, so
TR4W's ON indicator stayed dark while the radio transmitted. Confirmed fixed against the radio with
TR4W as the client — a transmit the client did not ask for now reaches it, which is the whole
point of the ownership rule in §11.4.

### 12.6 Every SET, confirmed on the radio

`tcitester.py` drives each SET as a round trip — read the current value, send a new one, wait for
the server to **broadcast** the change, restore the original. The broadcast is the point: a reply
proves nothing, because the server answers with the value it holds either way, so a SET that never
reached the radio replies identically to one that did.

**All pass:** `rit_enable`, `xit_enable`, `rit_offset`, `xit_offset`, `rx_nb_enable`,
`rx_nr_enable`, `agc_mode`, `rx_filter_band`, `vfo`, `dds`, `modulation`, `split_enable`,
`rx_channel_enable`, `drive`, `tune_drive`.

Getting there took three rounds and found three bugs of one kind, none of which any unit test
could have caught, because all three were about the **bytes on the wire**:

| Command | Was sent | The K4 wants |
|---|---|---|
| `rx_nb_enable` | `NB1;` | `NBnnm` — nn level, m on/off |
| `rx_filter_band` | width in Hz | `BWnnnn` in **10-Hz units** |
| `drive` | `PCX045H;` | `PC045H;` — `PCX` is the extended *query* |
| `tune_drive` | `PC…` — the operating power | `ME0069.nnnn` — a menu item, not a command |

All three came from `CatFrames` builders that had **never sent anything to a radio**. Their only
callers were `catserver.cpp` and `catpushbroadcaster.cpp`, both formatting *replies to a CAT
client*, and none of the three is in the K4's set form. The TCI SET path was the first outbound
user of any of them, which is why this surfaced now rather than years ago. QK4's own UI has always
sent these correctly by hand.

The fix added separate `setNoiseBlanker`, `setFilterBandwidth` and `setRfPower` builders rather
than changing the existing ones, so nothing a port-9299 client sees has changed. The reply-side
bugs are real and are reported to the maintainer — see §4.5.

**`tune_drive` took a fourth round and is the most instructive.** It first shared `drive`'s
handler, so asking for *tune* power sent `PC` and changed the **operating** power — the wrong
control entirely, found by watching the radio. The K4 has no tune-power command: it is **menu item
69, "TUNE LP", 1–50 W**, set with the absolute form `ME0069.0020`.

It was then briefly made report-only, reporting a remembered value. That was worse than stale: the
tester's read-change-restore cycle would have written back a number the radio never had, silently
clobbering a front-panel setting. It now reads menu 69 from `MenuModel`, which costs no extra
traffic — the radio pushes the `MEDF` definitions at connect and individual `ME` updates
afterwards.

Confirmed **in both directions**: a TCI client can set it, and a change made on the radio's front
panel appears in `tcimonitor.py` within a second. That is the first menu-backed control to work
over TCI, and the pattern generalises to anything else the K4 keeps in its menu.

A fourth "failure" was the tester itself: it compared the last argument of `rx_filter_band`, but
only the **width** survives a round trip, so a correct result looked wrong. Recorded because a
measurement sharing a bug with the thing it measures is the failure mode this whole section exists
to guard against.

### 12.7 CW, confirmed on the radio

**Raw evidence: [cw-by-cat-bench-log.md](cw-by-cat-bench-log.md)** — the commands, the wire traces
and the full 36-second transmit timeline, kept verbatim. What follows is the summary.

Every claim in §5 was checked on air, not just unit-tested. QLog drove the TCI path; `k4kytest.py`
(utilities repo) drove the radio directly on port 9200, with no QK4 or TCI in between, to separate
"the radio does this" from "QK4 does this".

| What | Result |
|---|---|
| `cw_macros` from QLog, plain text | Keys correctly. QLog sends ONE command; QK4 does the chunking |
| `>TU >599 004 \|SK\|` | `KS026` → `KYW TU` → `KS031` → `KYW 599 004 *` → `KS021`. Speed stepped audibly, SK clean, speed restored |
| `\|SK\|` → `*` | Keyed as the SK prosign, not as an asterisk |
| 28-char macro, 2 chunks | No run-together at the seam — the leading-space trick works |
| 60 characters in one `KY` | Keyed in full |
| 68 characters in one `KY` | Keyed in full — 8 PAST the documented limit. Undefined behaviour that happens to work on this firmware; not something to build on |
| 4 chunks back-to-back, no `KYW` | **Keyed as one continuous message, no interruption, nothing dropped** |

**The last row is the one that decided a design question.** The worry was that consecutive `KY`
commands might overflow the radio's buffer and silently drop text — the `KY;` → `KY1;` (buffer
full) query exists precisely because that is possible — and the proposed guard was `KYW` on every
chunk but the last, making the sends self-throttling. The radio settles it: four 22-character
commands sent back-to-back with no flag keyed seamlessly. So `KYW` stays reserved for the case the
manual names, a following `KS`, and ordinary macros pay nothing for it. This is also what TR4W does,
which is the corroboration rather than the reason.

The timeline supplied a second argument against the guard that nobody had thought of. That message
**transmitted for 35.9 seconds**. `KYW` delays the radio's processing of every following host
command until the current one has been keyed, so the proposed throttle would have blocked QK4's
polling, meter reads and any operator-triggered CAT for the whole 36 seconds — a long blackout
bought to prevent a failure the radio had just demonstrated it does not have.

Also visible in that run, and worth knowing before it is mistaken for a defect: `TQ` toggles
between 0 and 1 around fourteen times during the message, roughly 110-130 ms each. That is QSK
dropping to receive between words, not the message breaking up.

**`KYW` is load-bearing**, and that was established by trying to do without it. The same command
sequence sent with the wait flags stepped the speed correctly; sent without them, "the speed never
changed" — every `KS` was processed on arrival, so the whole message keyed at whatever the last one
set. It follows that the appealing simplification of always ending with a plain `KY ` is wrong: when
a macro ends away from base, the restore is a following `KS` and would land early. See the bench log.

**The chunk length is 60 — the documented maximum — and unpadded.** It was 22 with padding, on the
strength of TR4W's `CWFrameRule(22, True)`; that 22 is a MINIMUM guarding a keyer-abort flow QK4
does not have, and taking it for a maximum made QK4 send five commands where two would do. Any
value below 60 needs a reason and there is not one: the manual says "0 to 60 characters", 60 keyed
in full here, and so did 68.

QLog's 28-character CQ is now one command instead of two; its 99-character exchange two instead of
five. If a macro sent immediately after `cw_macros_stop` is ever heard to vanish, padding short
commands is the known remedy — that is the one flow where the swallow hazard could still exist.

### 12.8 Still unverified

- **The report-only group** — `mute`, `sql_enable`, `sql_level`, `lock`, `rx_anf_enable`,
  `rx_apf_enable`, `rx_nf_enable`. They answer but cannot be set, because no `CatFrames` builder
  exists. §8.
- **SETs addressed to the sub receiver.** Refused by design: the K4's sub forms are `$`-suffixed
  and have no builders yet.

### 12.9 How to repeat it

Start QK4 with the TCI server enabled, connect the radio, then, from
[ny4i/utilities](https://github.com/ny4i/utilities):

- **`tcimonitor.py`** — every reported field on one screen, updating live. If a value is not moving
  there, the server is not sending it, which is the check `--audit` structurally cannot perform
  (§11.7). Read-only, so it is safe to leave running.
- **`tcitester.py --all`** — drives every SET and restores each afterwards. Writes, so it is a
  separate tool; that separation is what keeps the monitor provably safe.

Also update §8 as things land. A coverage document that is wrong about coverage is worse than
none, and this one had drifted twice before anyone noticed.
