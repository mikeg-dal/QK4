# TCI Command Coverage and Gaps

What QK4's TCI server implements, measured against **TCI Protocol Ver. 2.0** (Expert Electronics,
12 January 2024), and how deep each command actually reaches toward the K4.

Source spec: `TCI Protocol.pdf` from the TCI-Keyer repository. Companion to
`docs/tci-server-design.md`, a dated design record of the wire format and the phased build.

**How to read this file.** §1–§5, §7, §8, §10 and §11.1–§11.3 and §11.6 describe the code as it is
(checked 2026-10-29). §6, §9's measured result, §11.4–§11.5 and §12 are a dated bench log from
2026-09-15 to 09-17 and keep the numbers measured then. When this file and the code disagree, the
code wins. The places to check are `TciServer::initBurst()` / `receiverBurst()` and
`TciServer::answerReadOnly()` / `applySet()` (`src/network/tciserverstate.cpp`), the dispatch in
`TciServer::onTextMessageReceived()` (`src/network/tciserver.cpp`), and the SET wiring in
`TciController::wireCatSets()` and `publishSnapshot()` (`src/controllers/tcicontroller.cpp`).

**Three separate questions are kept apart throughout, because conflating them hides the real gaps:**

1. **Does QK4 answer the command?** — the TCI surface.
2. **Does it reach the radio?** — whether a SET actually moves the K4.
3. **Can the K4 even do it?** — hardware limits that no amount of QK4 work removes.

A command can be answered and still be a lie. Every reply, SET or not, carries the value in QK4's
snapshot, never the value requested. A client cannot tell from the reply alone whether a SET reached
the radio, which is why column 3 exists and why §3.2 lists the commands that are answered but
never move the radio.

---

## 1. Summary

| Category | Spec commands | QK4 | Notes |
|---|---:|---|---|
| Initialization (4.1) | 9 | all 9 | Complete — §2 |
| Bidirectional control (4.2) | 42 | SETs in §3.1, answered-only in §3.2 | Reporting comes from the radio, not constants, except `sql_*`, `mute` and `volume` |
| Unidirectional control (4.3) | 21 | audio stream config, `audio_start`/`audio_stop`, sensor enables | |
| Notification (4.4) | 11 | `trx`, `tx_frequency`, sensors | Sensors — §7.2 |
| New in 2.0 (4.5) | 2 | both | `RX_CHANNEL_SENSORS` and `VFO_LOCK` (reported, broadcast and settable) |
| CW (3.2) | 9 | `CW_MACROS`, `CW_MACROS_STOP`, speed | **Verified on air — see §5** |

**Headline:** QK4 is complete for a *digital-mode* client (WSJT-X works end to end — receive,
decode, transmit, and a full FT8 QSO), reports **two receivers** with independent mode, filter,
AGC, RIT, volume and S-meter, and **keys CW** for a logger — QLog sends its macros through QK4 to
the radio and they key correctly, chunking and prosigns included (§5, §12.7).

§12 records what a live K4 confirmed, and §12.8 what it has not.

---

## 2. Initialization commands (spec 4.1) — complete

| Spec | QK4 | Depth | Notes |
|---|---|---|---|
| `VFO_LIMITS` | ✅ | Static | `100000,54000000` — K4 HF/6 m range |
| `IF_LIMITS` | ✅ | Static | `-48000,48000` |
| `TRX_COUNT` | ✅ | Static `2` | Main and Sub, both fully addressable. See §4.2 |
| `CHANNEL_COUNT` | ✅ | Static `2` | **Sent as `channels_count` (plural)** — see §6.4 |
| `DEVICE` | ✅ | Static | `QK4 <version>` (`deviceIdentity()` in `tciserver_internal.h`) |
| `RECEIVE_ONLY` | ✅ | Static `false` | |
| `MODULATIONS_LIST` | ✅ | Static | 10 modes, all mapped to K4 modes (`kModulationsList`); no `nfm` |
| `PROTOCOL` | ✅ | Static | Announces `ExpertSDR3,1.5`, not 2.0. Deliberate — see §6.5 |
| `READY` | ✅ | — | Sent last, after `START` |

QK4's init burst is larger than this table, because it also seeds current state for both
receivers (`vfo`, `modulation`, `drive`, …). That matches what a real ExpertSDR3 server does;
clients cache the burst and act on it. The contents are `TciServer::initBurst()` plus
`receiverBurst()` for each receiver; it was 76 commands on 2026-10-29. To count it against a
running QK4: `python3 scripts/tciclient.py --audit` prints `init burst: N commands` first.

---

## 3. Bidirectional control (spec 4.2)

### 3.1 Implemented as real SETs — these move the radio

Each SET goes TciServer → a `set…Requested` signal → `TciController` on the main thread →
`CatFrames` → `ConnectionController::sendCAT`, with a queued `RadioState::parseCATCommand` of the
same string (`TciController::applyCat`). The reply always carries the value the snapshot holds.

| Spec | K4 path | Receivers | Notes |
|---|---|---|---|
| `VFO` | `CatFrames::frequencyA` / `frequencyB` (`FA`/`FB`) | 0, 1, and channel 1 of receiver 0 | Channel 1 of receiver 0 is VFO B |
| `DDS` | `CatFrames::frequencyA` / `frequencyB` | 0, 1 | Alias for a receiver's own VFO |
| `MODULATION` | `CatFrames::modeA` / `modeB` (`MD`/`MD$`) | 0, 1 | The 10 announced modes; `nfm` accepted as FM; unknown refused |
| `TRX` | `TransmitController::engage(TciClient, StreamedFromHere)` / `release` | 0 only | One transmitter. **arg3 ignored**, §7.1 |
| `SPLIT_ENABLE` | `CatFrames::split` (`FT`) | 0 | Edge-triggered only, by design |
| `RX_CHANNEL_ENABLE` / `RX_ENABLE` | `CatFrames::subReceiver` (`SB`) | Sub RX | Both spellings, §4.2 |
| `RIT_ENABLE`, `XIT_ENABLE` | `CatFrames::ritEnabled` / `xitEnabled` (`RT`/`XT`) | 0 | |
| `RIT_OFFSET`, `XIT_OFFSET` | `CatFrames::ritOffset` (`RO`) | 0 | One register; either name moves both, §4.1 |
| `RX_NB_ENABLE` | `CatFrames::setNoiseBlanker` (`NB`) | 0 | Keeps the current level |
| `RX_NB_PARAM` | `CatFrames::setNoiseBlanker` (`NB`) | 0 | Level and filter width; keeps on/off |
| `RX_NR_ENABLE` | `CatFrames::noiseReduction` (`NR`) | 0 | Keeps the current level |
| `AGC_MODE` | `CatFrames::agcSpeed` (`GT`) | 0 | `off`/`normal`/`fast` only; anything else refused, §6.1 |
| `AGC_GAIN` | `CatFrames::setMenuValue` (menu 10) | 0 | Raw 2–8, clamped; one radio-wide setting |
| `RX_FILTER_BAND` | `CatFrames::setFilterBandwidth` (`BW`) | 0 | Width only (`high − low`); `high > low` required |
| `DRIVE` | `CatFrames::setRfPower` (`PC`) | 0 | Range letter from `powerRange()`, §4.4 |
| `TUNE_DRIVE` | `CatFrames::setMenuValue` (menu 69, "TUNE LP") | 0 | 1–50 W, clamped; never `PC` |
| `VFO_LOCK` | `CatFrames::setVfoLock` (`LK`/`LK$`) | 0, 1 | Channel 1 of receiver 0 addresses VFO B |
| `CW_KEYER_SPEED`, `CW_MACROS_SPEED` | `CatFrames::keyerSpeed` (`KS`) | global | Clamped 8–100 |
| `CW_MACROS`, `CW_MACROS_STOP` | `CatFrames::cwText` (`KY`) / `cwAbort` | global | §5 |

SETs in the per-receiver group (`applySet`) are acted on for receiver 0 only. The K4's Sub forms
are `$`-suffixed (`RT$`, `GT$`, `NB$`, `NR$`, `BW$`) and `CatFrames` has no set builders for them,
so a SET addressed to receiver 1 is answered and not acted on.

### 3.2 Answered, never moves the radio

These are answered from the snapshot. A SET-shaped command gets the current value back and changes
nothing.

| Command | What it reports |
|---|---|
| `LOCK` | The receiver's VFO lock (`lockA`/`lockB`). Settable only as `VFO_LOCK` |
| `RX_ANF_ENABLE`, `RX_APF_ENABLE`, `RX_NF_ENABLE` | The radio's auto-notch, APF and manual notch, per receiver, broadcast on change. No set builder |
| `RX_VOLUME` | QK4's Main/Sub mix in dB, §4.5. Channel A only |
| `MIC_LEVEL` | `RadioState::micGain()` (K4 `MG`, 0–80), §6.6 |
| `TX_ENABLE` | `true` for receiver 0, `false` for receiver 1. Sent in the burst; answered to a SET too |
| `SQL_ENABLE` | Always `false`. `publishSnapshot` does not fill it |
| `SQL_LEVEL` | Always −140 dBm, §6.2 |
| `MUTE` | Always `false`. Not modelled for TCI |
| `VOLUME` | Always `0` |

Receiver 1 always reports `xit_enable` false: `publishSnapshot` does not set it for the Sub.

A note on why broadcasts matter as much as replies: every one of the reported values was answerable
on request *before* it was broadcast on change, and that gap is a bug in itself — §11.3. `DRIVE` is
the worked example: it answered queries correctly while a client watched the power knob move and
saw nothing.

### 3.3 Not implemented at all

| Spec | Why | K4 support |
|---|---|---|
| `START` / `STOP` | Device start/stop is meaningless here; QK4 owns the K4 link | n/a |
| `IF` | IF filter tuning within the panorama | Possible via `FW`/`BW`, no mapping designed |
| `TUNE` | Tune-mode keying | K4 supports it; **no `CatFrames` builder** |
| `RX_BIN_ENABLE` | Binaural/pseudo-stereo | K4 has no equivalent |
| `RX_ANC_ENABLE` | Adaptive noise cancellation | No K4 equivalent |
| `RX_DSE_ENABLE` | Digital surround for CW | No K4 equivalent |
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
its own frequency, mode, filter, AGC, RIT, DSP flags, volume and S-meter from `RadioState`'s `*B`
getters. (Squelch is reported per receiver but is a constant for both — §3.2.)

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
| `SQL_LEVEL` | −140…0 dBm | K4 `SQ` integer scale (range unconfirmed: this file said 0–29, `tciradiostate.h` says 0–40) | Scale conversion required |
| `AGC_MODE` | `normal`/`fast`/`off` | `GT` numeric | Enum mapping required |
| `AGC_GAIN` | −20…120 dB | K4 menu 10, AGC threshold 2–8 | Reported and set raw, not converted to dB |

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

`TUNE_DRIVE` is separate. The K4 keeps tune power in menu item 69, "TUNE LP" (1–50 W). QK4 reads it
from `MenuModel` and sets it with `ME0069.nnnn`; `TUNE_DRIVE` falls back to the `DRIVE` value only
until the radio has sent its menu definitions. See §12.6 for how this was found.

**Reported for receiver 0 only.** Drive belongs to the transmitter and the K4 has exactly one, so
`drive:1,…` would describe a second power control that does not exist. A drive query addressed to
receiver 1 is recognised and deliberately unanswered.

**Settable.** `drive:0,50;` sends `PC` with the current range letter (`CatFrames::setRfPower`).

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

Separately, on the CAT server (port 9299, not TCI): `catserver.cpp` used to answer `AG;` with a
hardcoded `AG000;` and `SQ;` the same way. Both are fixed (#134). `AG;` / `AG$;` are now asked of
the radio, and `SQ` / `SQ$` are answered from `RadioState` only once the radio has reported them.

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

**Chunking, padding and the `KYW` rule** (60-character chunks on word boundaries, no padding,
`KYW` only before a following `KS`) are described once, in
[tci-cw-for-client-authors.md](tci-cw-for-client-authors.md) §3–§4. The bench evidence is in §12.7
and [cw-by-cat-bench-log.md](cw-by-cat-bench-log.md).

**`CatFrames::keyerSpeed` clamps to 8..100**, the documented `KS` range. It was unreachable with
bad input before this, but `>` arithmetic has no upper bound of its own.

**An explicit CAT passthrough — not built, deferred to phase 2.** For K4 features with no TCI
equivalent at all (§10.2). Held back because it is the one change that hands an external program
unmediated control of the radio. The CAT server already forwards every SET it does not handle
(`catCommandReceived`); the TCI side has no equivalent, and an unrecognised TCI command is answered
with silence and a log line. When the passthrough is built it should be *deliberate*, not a
default-forward:

- **Do not** copy CatServer's fall-through. On a CAT server the client is *already* speaking K4 and
  a raw forward is honest. A TCI client is speaking TCI; forwarding an unrecognised TCI command
  name as a K4 string would forward garbage.
- **Do** expose it under a distinct command name, and require the client to spell a real K4 command.
- Gate it behind a setting, default **off**. It is a different trust level from the curated command
  set.

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

**This is the one place the `protocol:` rationale is recorded; other documents link here.**

QK4 sends `protocol:ExpertSDR3,1.5;` (`kProtocolIdentity`). WSJT-X string-matches this and **halves
transmit amplitude** when it does not match. It is a compatibility choice, not an oversight.
`protocol:` therefore cannot identify QK4; `device:` (`QK4 <version>`) does. A client that keys
2.0-only features off the protocol string will not offer them, although QK4 sends `vfo_lock` and
`rx_channel_sensors` anyway. Revisit only with a client that needs 2.0 and tolerates the string.

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
| `RX_SENSORS` | `RadioState::sMeterDbm` (SMH) | Deprecated in 2.0, still sent — older clients know only this |
| `RX_CHANNEL_SENSORS` | `sMeterDbm`, `sMeterDbmB` (SMH, SMH$) | Channel B only while the Sub RX is on |
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

**dBm source.** The level is the radio's own `SMH` / `SMH$` reading in dBm
(`docs/k4-protocol-quirks.md` §14), chosen by `SpectrumScale::rxLevelDbm()`. Until the first `SMH`
arrives it falls back to an estimate from the `SM` bars via `SpectrumScale::dbmForSMeterReading()`,
which read 3–5 dB low against `SMH` on a K4D. That fallback must handle RadioState's S-meter encoding
— 0–9 is S0–S9, and anything stronger is `9.0 + dBoverS9/10`, so S9+20 arrives as `11.0`, not `29`;
`sUnitDbm()` would clamp it to S9.

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
- ~~`agc_gain`, `rx_nb_param`, `vfo_lock`~~ — reported, broadcast and settable (§3.1). Not in the
  §12.6 round-trip run.

### Next

1. ~~**`CW_MACROS` + a `CatFrames::cwText`**~~ — done and verified on air (§5, §12.7).
2. **The SETs still missing** — `mute`, `sql_enable`/`sql_level`, `rx_anf_enable`,
   `rx_apf_enable`, `rx_nf_enable`, and plain `lock` (only `vfo_lock` sets; `CatFrames::setVfoLock`
   exists). Each needs a `CatFrames` builder in the K4 set form, which is where the §12.6 bugs
   lived; bench each with `tcitester.py`.
3. **Sub-receiver SETs** — the K4 forms are $-suffixed (RT$, GT$, NB$, NR$, BW$) and have no
   builders. Reporting for receiver 1 already works.
4. **`SQL_LEVEL`** — needs a decided K4-to-dBm scale before it can report anything true (§6.2).
5. **`tx_enable` on band change** — small and read-only (§11.6).

### Phase 2

6. **Explicit opt-in CAT passthrough** (§5.2), default off. Held to its own phase: it is
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

It is deliberately a **second, independent implementation** of RFC 6455. QK4's own WebSocket
transport is hand-rolled too (`src/network/websocketserver.h`, `websocketframe.h`); sharing one
implementation on both ends would agree with itself even when both are wrong. It also exercises `TciController`'s thread hop, which the unit tests cannot: they
drive `TciServer` directly on one thread.

### Measured result (2026-09-15, QK4 at `8942e1e`, radio disconnected)

Dated. The burst has grown since (76 commands on 2026-10-29), and `agc_mode` / `sql_level` were
fixed (§6.1, §6.2).

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
cannot tell the difference — neither can a client.** That is why §3.2 lists which values are
constants and why a count like this should never be read as a feature-coverage percentage. Most of
those values are now wired to `RadioState`; §12 records what a live radio confirmed.

---

## 10. The other direction: QK4 capabilities with no TCI representation

Sections 2–7 ask "what does the spec define that QK4 lacks". This section asks the inverse: **what
does QK4 already model that a TCI client cannot reach.** It matters for two reasons — it is where
a QK4-aware client could be given more than ExpertSDR3 offers, and it sets the boundary of what a
CAT passthrough (§5.2, now phase 2) would be *for*.

`RadioState` exposes roughly 180 getters. Almost all of them are invisible over TCI.

### 10.1 Modelled by QK4 and TCI has a command — status

| Capability | QK4 has | TCI command | Status |
|---|---|---|---|
| Noise blanker on/off + level/width | `noiseBlankerEnabled/Level/FilterWidth`, `CatFrames::setNoiseBlanker` | `RX_NB_ENABLE`, `RX_NB_PARAM` | Reported and set (receiver 0) |
| Noise reduction on/off | `noiseReductionEnabled/Level`, `CatFrames::noiseReduction` | `RX_NR_ENABLE` | Reported and set (receiver 0) |
| Auto-notch | `autoNotchEnabled` | `RX_ANF_ENABLE` | Reported only |
| Manual notch | `manualNotchEnabled` | `RX_NF_ENABLE` | Reported only |
| APF | `apfEnabled` | `RX_APF_ENABLE` | Reported only |
| Squelch level | `squelchLevel` | `SQL_LEVEL` | Not wired; needs a dBm mapping (§6.2) |
| Filter bandwidth | `filterBandwidth`, `CatFrames::setFilterBandwidth` | `RX_FILTER_BAND` | Reported and set (width only) |
| RF power / tune power | `rfPower`, `CatFrames::setRfPower`; menu 69 | `DRIVE`, `TUNE_DRIVE` | Reported and set |
| CW keyer speed | `keyerSpeed`, `CatFrames::keyerSpeed` | `CW_KEYER_SPEED`, `CW_MACROS_SPEED` | Reported and set |
| VFO lock | `lockA`, `lockB`, `CatFrames::setVfoLock` | `LOCK`, `VFO_LOCK` | Both reported; `VFO_LOCK` sets |
| S-meter (both receivers) | `sMeterDbm`, `sMeterDbmB` (SMH) | `RX_SENSORS`, `RX_CHANNEL_SENSORS` | Sent (§7.2) |
| SWR / forward power | `swrMeter`, `forwardPower` | `TX_SENSORS` | Sent; mic level and peak power cannot be measured (§7.2) |
| Mic gain | `micGain` | `MIC_LEVEL` | Reported only (undocumented extension — §6.6) |

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
   — the remaining gaps in §10.1 are wiring — but that entire categories above have no TCI
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
| `agc_gain` | ✅ | |
| `rx_nb_enable`, `rx_nb_param`, `rx_nr_enable`, `rx_anf_enable`, `rx_apf_enable`, `rx_nf_enable` | ✅ | |
| `lock`, `vfo_lock` | ✅ | |
| `rx_volume` | ✅ | QK4's mix, §4.5 |
| `sql_enable` | ✅ | Wired to broadcast, but the value never changes (§3.2) |
| `drive`, `tune_drive` | ✅ | **Fixed** — see §4.4 |
| `mic_level`, `cw_keyer_speed` + `cw_macros_speed` | ✅ | Global, no receiver index |
| `sql_level`, `mute`, `volume`, `tx_enable` | ✗ | Constants, so nothing changes. **When one is wired to `RadioState` it must gain a broadcast in the same commit** — a value that can change and is not broadcast is this same bug again. |

The broadcasts are in `TciServer::setSnapshot()`, which diffs the new snapshot against the last.

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
| `tx_footswitch` | **No** | The K4 reports that it is transmitting, not *what* keyed it. Cannot be sent honestly |
| `clicked_on_spot`, `rx_clicked_on_spot` | Partly | Needs panadapter/DX-cluster UI integration, not radio work |
| `app_focus` | Yes | About the server's own window; low value for a headless-ish control app |
| `cw_macros_empty`, `callsign_send` | No | CW, §5 |

With the sensors and `vfo_lock` done, the largest remaining omission is `tx_enable` on band change
— small and read-only.

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

Getting there took several rounds and found four bugs of one kind, none of which any unit test
could have caught, because all four were about the **bytes on the wire**:

| Command | Was sent | The K4 wants |
|---|---|---|
| `rx_nb_enable` | `NB1;` | `NBnnm` — nn level, m on/off |
| `rx_filter_band` | width in Hz | `BWnnnn` in **10-Hz units** |
| `drive` | `PCX045H;` | `PC045H;` — `PCX` is the extended *query* |
| `tune_drive` | `PC…` — the operating power | `ME0069.nnnn` — a menu item, not a command |

The first three came from `CatFrames` builders that had **never sent anything to a radio**. Their only
callers were `catserver.cpp` and `catpushbroadcaster.cpp`, both formatting *replies to a CAT
client*, and none of the three is in the K4's set form. The TCI SET path was the first outbound
user of any of them, which is why this surfaced now rather than years ago. QK4's own UI has always
sent these correctly by hand.

The fix added separate `setNoiseBlanker`, `setFilterBandwidth` and `setRfPower` builders rather
than changing the existing ones, so nothing a port-9299 client sees has changed.

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

- **The answered-only group** — `mute`, `sql_enable`, `sql_level`, `lock`, `rx_anf_enable`,
  `rx_apf_enable`, `rx_nf_enable`. They answer but cannot be set. §3.2, §8.
- **SETs not in the §12.6 run** — `agc_gain`, `rx_nb_param`, `vfo_lock`. This file holds no
  round-trip record for them. (`cw_macros_speed` was measured; see
  [tci-client-compatibility-notes.md](tci-client-compatibility-notes.md) §2.)
- **Per-receiver SETs addressed to the sub receiver** (`rit_*`, `xit_*`, NB, NR, AGC, filter,
  drive). Not acted on by design: the K4's sub forms are `$`-suffixed and have no set builders yet.
  `vfo`, `dds`, `modulation`, `rx_enable` and `vfo_lock` do reach the Sub.

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
