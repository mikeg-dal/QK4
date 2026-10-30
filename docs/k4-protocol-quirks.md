# K4 Protocol Quirks — Canonical Reference

This document catalogs non-obvious K4 CAT behaviors that shape the QK4 code base. Each quirk has a *symptom*, the *K4 behavior* that causes it, and *where QK4 encodes the workaround*. Code is cited by file and function or constant name, not by line number (see "How to extend this document").

Sources: every behaviour here was verified by driving a K4/0 server directly over a Python socket and reading what came back — QK4's own code is never the source of truth for this document. Raw session transcripts are kept in the maintainer's private notes; the findings, not the transcripts, are what this file records.

---

## 1. `$` suffix convention

**Symptom.** Same command, two variants: `MD` / `MD$`, `BW` / `BW$`, `RO` / `RO$`, `RT` / `RT$`, `#REF` / `#REF$`, etc.

**K4 behavior.** Commands with a `$` suffix act on the **sub receiver (VFO B)**; without the suffix, they act on the main receiver (VFO A). This applies to both queries and set commands.

**QK4 encoding.**
- `src/models/radiostate.cpp` — the handler registry. See preamble comment in `registerCommandHandlers()` (file top of that function). Registry sorts **longest prefix first** so dispatch correctly prefers `RO$` over `RO`. Each `$` variant typically updates a `*B` member and emits a `*BChanged` signal.
- `src/models/radiostate.h` — class-level Doxygen comment documents the convention.

**Rules for new parser work.** Register both variants and test both. The order you append them in does not matter: `registerCommandHandlers()` sorts the table longest prefix first after every entry is added. `tests/test_radiostate_registry.cpp` checks the sorted order.

---

## 2. `RO` vs `RO$` RIT/XIT offset routing

**Symptom.** Setting an XIT offset sometimes ends up in `RO`, sometimes in `RO$`, depending on split / BSET state. Raw RU/RD adjust a register whose identity depends on the current mode.

**K4 behavior (verified by direct socket session):**
- No split, RIT or XIT active → offset lives in `RO`  (VFO A).
- Split + XIT (TX on VFO B)  → offset lives in `RO$` (VFO B).
- BSET + RIT                 → offset lives in `RO$` (VFO B).

`RU;` / `RD;` (increment/decrement) adjust the active register and echo its name back (`RO` or `RO$`). One exception is encoded in QK4: with BSET on and only RIT active, QK4 sends `RU$;` / `RD$;`, because its code comment says plain `RU;` would adjust `RO` (VFO A). That exception has not been confirmed on a socket. `SW54;` (the BSET-aware RIT toggle) does not echo `RT$` or `RO$`, so QK4 queries both after sending it. `XT/;` (toggle) echoes `XT1;`/`XT0;`, but `XT1;` / `XT0;` (set) do *not* echo. `RC;` clears `RO`; to clear `RO$` send `RO$+0000;`.

**QK4 encoding.**
- `src/models/radiostate.cpp` — `registerCommandHandlers()` registers `RO$` and `RT$`, which forward to `FrequencyVfoHandlers::handleROSub` / `handleRTSub`. The WHY is in `handleROSub` (`src/models/radiostate/frequencyvfostate.cpp`). The base `RO` / `RT` go to `RadioState::handleRO` / `handleRT`.
- UI: `RitXitController` (`src/controllers/ritxitcontroller.cpp`) picks which register to display, sends `SW54;` or `RT/;` on a RIT click, and sends `RU$`/`RD$` from the wheel in the BSET + RIT case.
- KPOD: the rocker's RIT/XIT position in `HardwareController` (`src/controllers/hardwarecontroller.cpp`) applies the same `RU$`/`RD$` rule.

**Gotcha.** When testing via direct socket, make sure QK4 is not also connected to the same K4 — interleaved commands confuse the test.

---

## 3. `SL` (streaming latency) is not echoed

**Symptom.** You send `SL3;` to the K4; the K4 accepts it silently; the UI never updates from any response.

**K4 behavior.** The K4 applies the new SL tier but does not echo an `SL<n>;` response. There is no query form that returns the current tier either. The tier → packet-size map was verified by socket session: four distinct tiers, bundling 20 / 40 / 60 / 120 ms of audio per packet.

**QK4 encoding.**
- `src/mainwindow.cpp::onRadioReady` — after the `RDY;` dump, QK4 sends its configured `SL` tier *and* calls `m_radioState->parseCATCommand("SL<n>;")` optimistically so RadioState (and everything downstream — AudioEngine frame sizing, the UI menu) matches what we just requested. Long comment block there explains why this post-RDY override is necessary.
- `TcpClient` also sends `SL<n>;` after `RDY;` in its fixed post-auth sequence, so `SL` goes out twice on connect. Both carry the same tier.
- Tier changes at runtime: a MainWindow lambda on `RadioManagerDialog::streamingLatencyChanged` sends `SL<n>;` and calls `parseCATCommand` with the same string. `RadioState` then emits `streamingLatencyChanged`, and `AudioController::onStreamingLatencyChanged` resizes the TX Opus frames.

---

## 4. `TX;` / `RX;` control

**Symptom.** WSJT-X and similar programs send `TX;` and `RX;` to QK4's CAT server. Forwarding them to the K4 would key it the wrong way.

**K4 behavior.** `TX;` keys the K4 to transmit from its own audio input. Audio packets from QK4 carry a tunnel header, and that header alone keys the K4 to transmit the streamed audio. So `TX;` and "stream audio" are two different ways to key the radio, not one. The rationale is recorded in `src/models/transmitowner.h`. This section has not been re-confirmed on a socket since it was written; the design rests on that header comment.

**QK4 encoding.**
- `CatServer` never forwards `TX;` or `RX;` to the K4. It records which client asked and emits `pttRequested(bool)` (the TX/RX branch in `catserver.cpp`). `TX/;` toggles from the radio's current TX state.
- MainWindow routes `pttRequested` to `TransmitController::engage(Owner::CatClient, Route::StreamedFromHere)` or `release(Owner::CatClient)`. `StreamedFromHere` opens the audio gate and sends no `TX;`.
- `TransmitOwner` (`src/models/transmitowner.h`) decides who holds the transmitter: PTT button, XMIT, CAT client, TCI client or the radio itself. It returns the effects of each decision. A release is the exact inverse of its engage, never a fixed `RX;`.
- `TransmitController::apply` (`src/controllers/transmitcontroller.cpp`) is the only place that puts `TX;` or `RX;` on the wire, and it orders them against the audio gate. The routes that send `TX;` are `RadioLocal` and `CatKeyedAndStreamed` (XMIT).
- The radio's own TX/RX is fed back through `RadioState::transmitStateChanged`. A disconnect calls `releaseAll()`. Esc calls `operatorStop()`, which always sends the CW abort (it carries its own `RX;`).
- A CAT client that disconnects while holding PTT is released (`CatServer::releasePttIfOwner`).
- Tests: `tests/test_transmitowner.cpp` covers the arbiter. `tests/test_catserver.cpp` covers `TX;`/`RX;` → `pttRequested` over a real TCP loopback socket.

---

## 5. Iambic latch semantics (internal keyer quirk)

**Symptom.** An iambic keyer that reads its paddles when its own timer fires decides on a state
the operator may have left milliseconds earlier: a tap is lost, or a released squeeze earns an
element nobody keyed.

**K4 behavior.** Not a K4 quirk — a keyer-state-machine quirk.

**QK4 encoding (since 2026-10-27).** `src/hardware/paddlekeyer.cpp`, the port of K4-Companion's
keyer, keeps every paddle edge in a time-stamped history. Each decision reads the paddles *as of
when it was due* (`paddleTimestamps(asOf)`), and the memories are measured from when the previous
element was *heard*. Iambic A keeps only the dit memory; Iambic B adds the dah memory, the
squeeze-release memories and the Dit Memory Delay. The rules are pinned by Dale Farnsworth's 554
recorded runs (`tests/test_paddlekeyersweeps.cpp`).

See section 8 below for the KZ keying protocol that carries these elements to the K4.

---

## 6. Packet framing: 4-byte marker + 4-byte length + 4-byte end

**Symptom.** TCP reads can land mid-packet; the parser has to reassemble across boundaries.

**K4 behavior (K4/0 server side).** All audio / spectrum / CAT data is wrapped in fixed frames:
`[START_MARKER (0xFE 0xFD 0xFC 0xFB)] [big-endian u32 payload length] [payload] [END_MARKER (0xFB 0xFC 0xFD 0xFE)]`.
Markers are mirror-imaged so corruption can't alias one for the other.

**QK4 encoding.**
- `src/network/protocol.h` — class/namespace header comment documents the frame format.
- `src/network/protocol.cpp::parse` — the **"keep last 3 bytes"** branch when no marker is found is load-bearing: `START_MARKER` is 4 bytes; if a TCP read delivered 1–3 of those bytes, dropping them would lose sync on the next read. Inline WHY comment.
- Bogus lengths: a header announcing more than `K4Protocol::MAX_PAYLOAD_SIZE` (256 KiB, `protocol.h`) is treated as a false start marker and skipped, so it cannot stall parsing. There is no cap on a single read: every complete packet in it is delivered, and the unconsumed tail is bounded by one maximum frame.

---

## 7. ARP cold-start retry (transport layer, not protocol — keep in mind)

Not a protocol quirk but a startup-reliability one worth mentioning alongside:

**Symptom.** First connect from a freshly-launched QK4 (after Finder launch / `open` command) fails synchronously with `EHOSTUNREACH` on macOS.

**Cause.** macOS `connect(2)` returns `EHOSTUNREACH` immediately when the ARP cache has no MAC for the destination IP. The ARP request *is* sent; the reply just lands too late for that first `connect()`.

**QK4 encoding.** `src/network/tcpclient.cpp`, in the socket-error handler — retry after 500 ms, up to 2 attempts, via the named constants `kArpRetryIntervalMs` and `kArpMaxRetries`. Not a workaround; correct handling of a real network transient.

---

## 8. KZ keying protocol

Sidebar — not directly a protocol *quirk*, but non-obvious enough to belong here.

| Command | Meaning |
|---|---|
| `KZ.;` | dit element (1 dit on) |
| `KZ-;` | dah element (3 dits on) |
| `KZ ;` | letter space |
| `KZP####;` | pause N ms before the next element (fist preservation) |
| `KZLnnnn;` | remote paddle **key-down initial delay**, 0–1000 ms, default 80. The K4 holds a remote key-down this long before transmitting, so the timed commands that follow arrive in time: a jitter buffer. **The operator's setting. QK4 reads it from the RDY dump and writes it only when the operator moves Key-Down Delay (Options → HaliKey → Remote Keying; `CwController::setKeyDownInitialDelay`).** It is *not* the element length |
| `KZD####;` / `KZU####;` | timed key-down / key-up: `KZD` = the gap in ms before this key-down, `KZU` = how long it was down. The K4 replays the intervals behind its `KZL` delay, so network jitter cannot change an element. **QK4's HaliKey keyer sends only these** (since 2026-10-27; K4/0 and K4-Companion do the same) |

**`KZL` was misread until 2026-10-27.** From `0bd443d` QK4 sent `KZL` = 1200 / WPM, two digits,
on every connect and every speed change, taking it for the element length. That set the K4's
remote-keying delay buffer to 30 ms at 40 WPM, shrinking it as the speed rose. The K4 stores the
value, so a radio QK4 has keyed still holds the last one written. The golden capture of
2026-04-20 shows `KZL60;` at `KS020`. Reset it once: Options → HaliKey → Remote Keying → Key-Down Delay, or the K4 menu.
Reference: Elecraft K4 CAT, "KZL (Remote Paddle / Key-Down Initial Delay)". K4-Companion reads
it the same way.

**The byte between `Z` and `;` in the letter-space form is 0x20, a literal SPACE.** Confirmed by
hexdump of EP02 traffic from a live KPOD+. Elecraft's `KPodKeyerInterface.pdf` v1.00 renders it as
`KZ_;`, which looks like an underscore and is a typographic artifact — sending a real underscore
(0x5F) does **not** work.

Undocumented contest behaviour (confirmed by N6TV): inside an element-form string such as
`KZ-.-. --.-  -.;`, one blank is a 3-dit letter space and **two blanks are a 6-dit word space** —
tighter than the ITU 7-dit default.

QK4's HaliKey keyer (`PaddleKeyer`) sends `KZD`/`KZU`. It runs on its own real-time thread (see
`src/utils/README.md` → `rtscheduling`). The element
forms `KZ.`/`KZ-`/`KZ `/`KZP` reach the K4 only from a KPOD+, whose EP02 strings are forwarded
untouched. See `src/hardware/README.md` for the keying chain.

---

## 9. PAN spectrum tiers — the radio picks the bandwidth, not the span

The K4 always sends **1024 bins**, whatever span you ask for. What changes is the bandwidth those
bins cover, chosen by the radio from five tiers. `sampleRate` in the PAN packet is in **kHz**, and
`tierSpanHz = sampleRate * 1000`.

| Tier | `sampleRate` (kHz) | Tier span | Switches up at | Switches down at |
|---|---|---|---|---|
| 1 | 24 | 24 kHz | — | ≤ 19 kHz |
| 2 | 48 | 48 kHz | 20 kHz | ≤ 36 kHz |
| 3 | 96 | 96 kHz | 37 kHz | ≤ 82 kHz |
| 4 | 192 | 192 kHz | 83 kHz | ≤ 172 kHz |
| 5 | 384 | 384 kHz | 176 kHz | — |

The up and down thresholds differ deliberately — that hysteresis stops the tier oscillating when you
sit on a boundary. All 1024 bins are usable; there is no edge taper or filter rolloff to mask.

**Why it matters to QK4.** A stored waterfall row is only meaningful alongside the tier it was
captured at: the same 1024 bins mean 24 kHz of spectrum in tier 1 and 384 kHz in tier 5. The
renderer windows every row with one tier value, so `PanadapterRhiWidget` clears the waterfall when
`sampleRate` changes — otherwise a tier change would silently redraw all history at the wrong
frequency scale.

## 10. Span dial skips 6 kHz going up

Stepping **up** from 5 kHz the K4 jumps to 7 kHz. Stepping **down** it walks 7 → 6 → 5 in normal
1 kHz steps, so 6 kHz is reachable descending but skipped ascending. The K4 advertises 6 kHz as its
minimum span. Step size is 1 kHz from 5–144 kHz and 4 kHz from 144–368 kHz.

**QK4 does not currently replicate the skip** — `RadioUtils::getNextSpanUp` applies a flat 1 kHz
step below the threshold, so it lands on 6 kHz where the radio would not.

---

## 11. Audio encode modes `EM0`–`EM3` carry the same audio at different numeric scales

**Symptom.** The four encode modes sound like different volumes. Switching from EM3 to EM0 needs far
more mic gain to reach the same ALC. EM1 transmits audible noise while the operator is silent.

**K4 behavior.** Three facts, all measured 2026-09-19 against a stationary reference — dummy load,
**AGC OFF**, fixed AF gain, 20 s of receiver noise captured in each mode with
`tools/k4_audio_capture.py` (capture `bench-logs/audiocal-20260919-0741`, 240,000 samples/mode).
Both are local to the maintainer's machine: `tools/` and `bench-logs/` are gitignored.

1. **EM0 is 24-bit audio in a 32-bit container.** Its full scale is `2^23`, not the `2^17` QK4
   previously assumed. The K4's EM0 stream carries exactly **256× (2^8)** the values of its EM1
   stream for the same sound — a bit shift, which is why the ratio is exact rather than approximate.
2. **EM1, EM2 and EM3 are all shipped at an identical level** — 16.780 / 16.710 / 16.718 on a `2^15`
   scale for the same signal. EM2 and EM3 are the *same Opus bitstream*; they differ only in which
   decode call the client uses (`opus_decode` vs `opus_decode_float`), not on the wire.
3. **Every mode is shipped ~32× below its container's full scale.** That factor is a property of the
   radio, not of any codec, and it is independently corroborated: K4-Companion, an unrelated
   implementation, applies the same 32× to the same stream.

Elecraft's documentation calls EM0 "RAW 32-bit float". It is neither float nor scaled to 2^31 — it is
S32LE integers holding 24-bit values. Taking the documentation at its word is what produced AUD-003.

**QK4 encoding.**
- `src/audio/opusdecoder.h` / `.cpp` — one rule for all four modes:
  `out = native × K4_GAIN_BOOST / full_scale`, with full scale `2^23` (EM0), `2^15` (EM1/EM2) and
  `1.0` (EM3). This replaced four unrelated by-ear constants, two of which were wrong by 6 dB in
  opposite directions. Under the corrected rule the modes agree to **0.04 dB**; before, they spanned
  **12.03 dB**.
- `src/audio/rawaudioformat.h` — `EM0_FULL_SCALE` (`2^23`) is the single source of truth, shared by
  the TX side so the two directions cannot drift apart. `EM0_S16_TO_FULL_SCALE` (256) maps a captured
  S16 mic sample onto EM0's full scale; anything less caps TX below full modulation with no way to
  recover it, because `setMicGain` clamps at unity and can only attenuate.
- `tests/test_opusdecoder.cpp` — pins the rule. Five of its eight cases fail on the pre-calibration
  constants, including the 12.03 dB EM0-vs-EM1 spread.
- `tests/test_rawaudioformat.cpp` — pins the TX wire format and scale.

**Reproducing it.** `tools/k4_audio_capture.py capture ...` then `analyze` (local-only script, not in git). AGC **must** be off: AGC
normalises level and erases the quantity being measured. The modes cannot be compared simultaneously
because `EM` is only applied at connect (`TcpClient` sends it after `RDY;` in its fixed post-auth
sequence; the user's startup macro goes out before `RDY;`), so the method relies on a
stationary reference instead — captures minutes apart are comparable when the signal is receiver
noise into a dummy load.

**A second, independent defect on the same path.** Mic gain was applied and the signal quantised to
S16 *before* the wire format was chosen, so every mode inherited a 16-bit bottleneck and lost bits in
proportion to the attenuation — at a 30% mic slider only ~10.8 bits survived, and EM0 carried 16 bits
shifted into its 24-bit container. Undithered quantisation error at that depth correlates with the
signal, so it is harmonic distortion rather than noise. On the bench this was a tonal buzz on dead
air at low mic gain that vanished completely at 50% and above — a level-dependent *character* change,
which is what distinguishes it from any acoustic or preamp noise. The path now stays Float32 and
quantises once, at the wire format.

**Still unmeasured.** TX levels for EM2/EM3; whether the K4 requires *stereo* Opus on TX (QK4 sends
mono; K4-Companion duplicates to stereo and its author notes the K4 requires it).

---

## 12. Mini-pan state `#MP` / `#MP$` is pushed by the radio

The mini-pan can be toggled on the radio itself, so QK4 cannot treat its own clicks as the only
source of truth. A capture of the front-panel buttons (2026-09-27, Main then Sub) shows the radio
sends the toggle, then re-sends `SB` and the `#MP` / `#MP$` pair:

```
#MP1;                           Main on
#MP0;SB1;#MP0;#MP$-1;           Main off, then the SB + pair dump
#MP$1;SB3;#MP0;#MP$1;           Sub on
#MP$0;SB1;#MP0;#MP$0;           Sub off
SB0;#MP0;#MP$-1;                Sub RX off
```

The same pair also arrives in the `RDY;` dump and after `#DPM` changes.

**`SB3` is the Sub mini-pan, not diversity.** The K4 needs `SB3` for the Sub mini-pan to run and
switches `SB1` ↔ `SB3` itself when `#MP$` toggles. QK4 reads any non-zero `SB` as Sub RX on, and the
DIV badge follows `DV` only, so neither misreads it.

**Main and Sub mini-pans are independent.** Both can run at once (`#MP1;#MP$1;SB3;`). Turning the Sub
mini-pan on with Sub RX off is refused with `ER105:Operation requires SUB RX to be ON.`

**`-1` is not "can never be turned on".** The radio reported `#MP$-1` just after `SB1`, then
accepted `#MP$1` moments later. QK4 therefore treats `-1` as *off* (closing an open mini-pan) and
does not use it to refuse the operator's click.

**Where QK4 relies on it:** `SpectrumController::setupSpectrumUI` connects `miniPanAEnabledChanged`
/ `miniPanBEnabledChanged` to the VFO widgets' `showMiniPan()` / `showNormal()`; the click handlers in
`MainWindow` only update `RadioState` and send the CAT. `SpectrumController::clearDisplays` closes
both on disconnect, because `SpectrumDisplayState::reset()` clears the flags without emitting.

---

## 13. AutoRef: the radio tracks REF itself, and a client `#REF` freezes it

**Symptom.** The Display popup's AUTO button showed manual after every radio update, and Ctrl+Wheel
could change REF while AutoRef was on. QK4 had parsed `#AR` against an invented format.

**K4 behavior.** Measured on a K4D on 2026-10-19, once with `RDY;` alone and once with QK4's own
`RDY;K41;ER1;` start-up. Both gave the same results.

- **Format.** `#ARaadd±oom`: averaging, debounce, signed offset in dB, then `1` (auto) or `0`
  (manual). Example: `#AR1201+041`. It is not `A`/`M`.
- **Offset storage.** The AutoRef Offset menu is `ME0050`, stored 0–16 with 8 meaning 0 dB. The
  front panel shows −8…+8. Each step sends `ME0050.00NN;` and a fresh `#AR`.
- **`#AR` is pushed.** The radio sends it unprompted on every change, whether from the front panel
  or from a client's `#AR/;` toggle.
- **REF is pushed in AutoRef, with the offset already applied.** The radio sends `#REF`, `#HREF` and
  their `$` forms unprompted as it tracks, in 1 dB steps.

  ```
  offset +4                #AR1201+041   REF -131/-132
  offset +4 -> +8          #AR1201+081   REF -140 ... -136   (settles ~4 dB lower)
  offset +8 -> +4          #AR1201+041   REF -137 ... -132
  AutoRef off              #AR1201+040   REF updates stop
  AutoRef on               #AR1201+041   REF -135 ... -132 within ~2 s
  ```

- **A client `#REF` during AutoRef is silently accepted and freezes tracking.** `#REF-110;` got no
  reply. `#AR` still read `1`, but REF stayed at −110 against a −132 floor for over 20 s, until
  AutoRef was toggled off and on.

**QK4 encoding.**
- `SpectrumDisplayHandlers::handleAR` (`src/models/radiostate/spectrumdisplaystate.cpp`) keeps only
  the mode. QK4 follows the offset by applying the published `#REF`, so it never reads the offset.
- `PopupManager`'s REF ± lambda and the two Ctrl+Wheel `refLevelScrolled` handlers in
  `SpectrumController` both step REF through `RadioState::stepRefLevel()`, which refuses while
  AutoRef is on (and before the radio has reported a REF).
- The AUTO button in `DisplayPopupWidget` sends `#AR/;` and waits for the radio's `#AR` to set its
  state.
- Tests: the `testAutoRef*` cases in `tests/test_radiostate.cpp` use the radio's own strings.

**Do not compute REF locally from the PAN noise floor.** The radio already publishes its tracked REF.
A second tracker in QK4 would move the scale independently of the radio's own screen.

---

## 14. S-meter: `SM` streams bars, the dBm reading `SMH` must be asked for

**Symptom.** TCI clients were sent a receive level ~3–5 dB below the radio's own: QK4 converted the
`SM` bar count to dBm (`SM04` → −115 dBm) while the radio's `SMH` read −110…−112 dBm.

**K4 behavior.** Captured on a K4D on 2026-10-30 with QK4's start-up sequence:

- `SM` / `SM$` (bars, 0–42, matching the K4's bar graph) arrive unprompted whenever the meter
  changes — about five a second on a quiet band.
- `SMH` / `SMH$` (signal level in dBm, `SMH-110`, `SMH$-122`) arrived only in reply to `SMH;` /
  `SMH$;`.
- Programmer's Reference D5: "SMH$ (High-Resolution S-meter; GET only) RESP format: SMH$+nnn; where
  +nnn is the signal level in dBm. SET format: SMHn; where n is 1 (Automatically delivered when
  s-meter value changes.) … NOTE: SET format is not meta-mode dependent. Requires AI4 for
  delivery." No way to turn delivery off again is documented.

**QK4 encoding.**
- `MainWindow::onRadioReady` sends `SMH1;` (delivery on change, at the rate the `SM` stream already
  runs — no polling timer), then `SMH;` and `SMH$;` once so the first value does not wait for a
  change.
- `RxTxMeterHandlers::handleSMH` / `handleSMHSub` require the sign, so an echoed `SMH1` cannot be read
  as +1 dBm. `RadioState::sMeterDbm()` / `sMeterDbmB()` are empty until the radio reports.
- `SM` still drives the bar graphs. Anything that reports a level in dBm (TCI sensors, the CAT
  server's `SMH` answer) uses `SMH`; `SpectrumScale::rxLevelDbm` falls back to the bar estimate only
  before the first `SMH` arrives.
- **Not yet bench-verified:** that `SMH1;` makes the radio push `SMH` and `SMH$` unprompted. If it
  does not, TCI shows the one value read at connect; the fix is then to poll, not to go back to bars.

---

## How to extend this document

Add a new section only when a behavior satisfies **all** of:
1. A direct socket-session test confirms the behavior (QK4 alone cannot be the source of truth).
2. QK4 encodes a workaround or decision based on it (cite the file and function or constant).
3. The behavior is non-obvious from reading the code alone.

Each section must cite the file where the workaround lives. Prefer naming the function or constant
over a line number — line numbers rot silently, and a stale one sends the reader to unrelated code.

Do **not** add sections for generic CAT commands whose behavior is documented in Elecraft's official programmer's reference.
