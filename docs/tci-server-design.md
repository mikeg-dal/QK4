# TCI Server for QK4

**Status: phases 0-5 BUILT, and a COMPLETE FT8 QSO has been worked through the path (2026-09-15).** Both audio directions and CAT control
work against a live K4 with WSJT-X: 25 FT8 decodes received through the server, and transmissions
sent through it are decoded by the spotting networks, and a full two-way contact has been
completed - receive, decode, transmit and be decoded holding together across successive 15-second
periods with CAT tracking the radio throughout. That is the operating case, not a bench test.
Phase 6 (the settings UI) and phase 7 (full TCI command coverage) remain.

## Context

QK4 owns the network link to the K4, including the audio path. A remote K4 is reachable only
through QK4, so any other program that wants the radio has to go *through* QK4.

Today that is `src/network/catserver.cpp` — a TCP server on port 9299 speaking native K4 CAT.
GETs are answered from the `RadioState` cache, SETs are forwarded to the radio over `TcpClient`,
and `pttRequested(bool)` gates TX audio. It works, and WSJT-X drives it using its built-in
Elecraft K4 support.

What it does **not** carry is audio. To run WSJT-X through QK4 today the operator must also wire a
loopback sound card: QK4 plays K4 receive audio to a virtual output device, WSJT-X listens to it,
WSJT-X transmits into a virtual input device, QK4 captures it. Two extra devices, per-machine
setup, and a per-platform support burden.

TCI (Transceiver Control Interface, Expert Electronics) carries **both** control and audio over one
WebSocket. WSJT-X speaks it natively. A TCI server in QK4 removes the loopback sound card entirely.

**Intended outcome:** WSJT-X connects to QK4 over TCI and gets CAT *and* audio in both directions,
with no virtual audio devices anywhere.

### Decisions taken (NY4I, 2026-09-14)

| | |
|---|---|
| **Coexistence** | New listener **alongside** `CatServer`. Nothing in `catserver.cpp` is deleted or changed. Retiring the 9299 path is a later, separate decision. |
| **Not a chain** | TCI **bypasses** `CatServer` — it does not connect to 9299 or call into it. It reuses the same underlying primitives (`sendCAT`, `parseCATCommand`, `setPttActive`) directly. |
| **Scope** | TCI carries **both CAT and audio**, and the two control paths must stay consistent. |
| **First cut** | Implement only the commands WSJT-X actually sends; full TCI command coverage is a later phase. |
| **Receiver mapping** | **Main VFO only.** `trx_count:1`; channel 0 = VFO A, channel 1 = VFO B. Sub receiver / "RX Two" is deferred. `trx:1,*` is refused. |
| **Threading** | The TCI server **does not run on the main thread**. |
| **Branch** | `development` on the `ny4i/QK4` fork. |

---

## Evidence base

Everything in the "Measured" sections below came from a live capture of WSJT-X talking to
AetherSDR on this machine (2026-09-14), plus direct experiments against WSJT-X. Where a fact was
verified in WSJT-X's own source, the file and line are cited from `~/projects/wsjtx`.

Two reference implementations were read:

- **AetherSDR** (`~/projects/AetherSDR/src/core/TciServer.cpp`, `TciProtocol.cpp`) — a production
  TCI **server** that WSJT-X, JTDX, SDC and RF2K-S all run against. The only source for the audio
  half.
- **TR4W-D12** (`~/projects/TR4W-D12/tr4w/src/uTCIServer.pas`, `docs/TCI_SERVER_DESIGN.md`) — a TCI
  server for a contest logger. **CAT only; audio/IQ explicitly out of scope.** Valuable for grammar,
  init-burst ordering, and its catalogue of client misbehaviour.

---

## Measured: the audio wire format

Binary frame = **64-byte header + samples**. Header is 8 × `quint32` followed by 8 reserved
`quint32`, little-endian:

| Field | Meaning |
|---|---|
| `receiver` | TRX index |
| `sampleRate` | Hz |
| `format` | 0 = int16, 1 = int24, 2 = int32, 3 = float32 |
| `codec` | 0 (uncompressed) |
| `crc` | 0 (unused) |
| `length` | number of floats in the valid region — **see the asymmetry below** |
| `type` | **0 = IQ, 1 = RX_AUDIO, 2 = TX_AUDIO, 3 = TX_CHRONO** |
| `channels` | nominally 1 or 2 — **untrustworthy inbound** |
| `reserved[8]` | zero-filled |

### The two directions use opposite payload rules

| | `length` | payload | interpretation |
|---|---|---|---|
| **RX_AUDIO** (QK4 → WSJT-X) | 2048 | 8192 B = 2048 floats | genuine interleaved stereo, 1024 frames. `floats/length = 1.0` |
| **TX_AUDIO** (WSJT-X → QK4) | 2048 | 16384 B = 4096 floats | **first 2048 floats only**, as 1024 duplicated pairs. `floats/length = 2.0` |

A single shared helper for both directions will be wrong in one of them.

**Measured on 400 captured TX frames:**

- Adjacent pairs with L == R: **409600 / 409600 = 100.00%**. Deduplicate by taking every other float.
- The payload region beyond `length` is **81.77% non-zero** — stale buffer, not padding. Sizing the
  read from the frame size instead of `hdr.length` injects garbage audio.
- Misreading it as true mono yields audio **2.0× too long with every tone an octave low**.
- `channels` held **eight distinct values** across 762 frames: `2`, `0`, `1818781545`, `1017483539`,
  `2959447138`, `3523932582`, `4098833031`, `2059641691`. AetherSDR's comment calls it
  *"garbage (FIFO reuse)"*. **Never read it.**
- WSJT-X transmits at **full scale: peak 0.9990, RMS 0.7056**.

### TX_CHRONO is a pull clock, and QK4 owns it

WSJT-X sends **no** TX audio unless asked. The server emits a header-only `type = 3` frame; each one
requests 2048 floats = 1024 stereo frames = 21.333 ms at 48 kHz. Measured over the capture:

```
TX_CHRONO frames : 762
TX_AUDIO  frames : 762
ratio            : 1.0000      <- strict one-block-per-request
```

Cadence during the FT8 transmission (637 frames over 13.56 s):

```
mean   21.328 ms   (target 21.333)
median 20.118 ms
stdev  14.082 ms   min 0.000   max 65.434   p95 47.067
frames sent back-to-back (<1 ms apart): 9.1%
implied sample rate: 48,087 Hz  ->  +0.18% error
```

**The requirement is correct long-run mean rate, not low jitter.** WSJT-X tolerated 65 ms
instantaneous gaps and 9% back-to-back frames without complaint. What it cannot tolerate is
systematic rate error — AetherSDR's own comment records that *a fixed 21 ms timer runs ~1.6% fast
and warps digital-mode tones*.

So: a `Qt::PreciseTimer` polling faster than the period, driving a **monotonic nanosecond
accumulator** with a `while (accum >= period) { send(); accum -= period; }` drain. Never a
fixed-interval one-shot.

WSJT-X's turnaround is median **0.702 ms**, p95 1.612 ms — it answers a chrono almost immediately.

### Sample rate is 48 kHz, and it is not negotiable

**WSJT-X ignores `audio_samplerate` entirely.** Confirmed four independent ways:

1. It never *sends* `audio_samplerate` — it only consumes what the server declares.
2. Declaring 12000 and streaming a 500 Hz tone rendered it at **2000 Hz** (4×).
3. WSJT-X's own saved WAV of that run measured **2000.0 Hz at 100.0% purity**, rms/peak 0.717.
4. In `~/projects/wsjtx/Transceiver/TCITransceiver.cpp`:
   - `audioSampleRate = 48000u;` at **line 216** is the only assignment in the file.
   - `Cmd_AudioSR` appears exactly **twice** — enum declaration and `mapCmd_` registration — with
     **no `case` in the dispatch switch**. Compare `Cmd_Device`, which has three (`case` at :824).
     The command is tokenised and silently dropped.
   - 48000 also appears as a **bare literal** at `:1624`, so patching the variable upstream would
     not even suffice.

`Cmd_TrxCount` and `Cmd_IqSR` are **also** parsed-and-ignored. WSJT-X therefore never adapts to a
declared `trx_count`; RX1 vs RX2 is chosen entirely in its own rig setting (`Rig: TCI Client RX1`).
QK4 must refuse `trx:1,*` explicitly rather than rely on `trx_count:1` steering the client.

**Consequence: QK4 must upsample its native 12 kHz K4 audio to 48 kHz for RX.** TX needs no
resampler — WSJT-X sends 48 kHz, which is exactly what `AudioEngine`'s TX path already wants.

---

## Measured: the CAT contract

### Init burst

**`ready;` last.** This is the burst AetherSDR sends, which WSJT-X accepts; a replay of it was
verified to work against a live WSJT-X.

**Framing: AetherSDR sends one command per WebSocket text frame** — 42 commands, 42 frames,
verified by decoding the capture without splitting payloads on `;` (`vfo_limits:1000,75000000;` is
a 25-byte frame on its own). TR4W's notes claim the opposite — *"AetherSDR sends its entire init
burst as one string of `;`-terminated commands"* — and that is not what this version does.

Either framing works on the sending side: the design-phase probe sent all 42 in a single frame and
WSJT-X accepted it, connected, and streamed audio. QK4 should send one command per frame to match
the proven reference. **The receive path must still tolerate several commands in one frame**, since
nothing stops a client from batching, and must buffer a trailing partial command for the next
frame.

```
vfo_limits:<lo>,<hi>;  if_limits:-48000,48000;  trx_count:1;  channels_count:2;
device:QK4;  receive_only:false;
modulations_list:usb,lsb,cw,cwr,am,sam,fm,nfm,digu,digl,rtty;
protocol:ExpertSDR3,1.5;
vfo:0,0,<hz>;  vfo:0,1,<hz>;  dds:0,<hz>;  modulation:0,<mode>;
rx_enable:0,true;  rx_filter_band:0,<lo>,<hi>;
rit_enable:0,<b>;  xit_enable:0,<b>;  rit_offset:0,<hz>;  xit_offset:0,<hz>;
split_enable:0,<b>;  lock:0,false;  sql_enable:0,false;  sql_level:0,<n>;
agc_mode:0,<mode>;  rx_nb_enable:0,false;  rx_nr_enable:0,false;
rx_anf_enable:0,false;  rx_apf_enable:0,false;  mute:0,false;
tx_enable:0,true;  drive:0,<pwr>;  tune_drive:0,<pwr>;
mic_level:<n>;  trx:0,false;  volume:0;
audio_samplerate:48000;  audio_stream_sample_type:float32;
audio_stream_channels:2;  audio_stream_samples:2048;
tx_stream_audio_buffering:50;  iq_samplerate:48000;
start;  ready;
```

`mic_level`, `volume` and `trx` are **global, single-argument** — no trx index.
`active_slice:0,A` is an AetherSDR extension; QK4 omits it.

The HTTP upgrade accepts **any path** (`GET /`) and negotiates **no subprotocol**.

### What WSJT-X actually sends

Its entire client→server vocabulary across a full receive + transmit session was **seven commands**:

```
split_enable:false;            <- ONE-ARGUMENT GLOBAL FORM
audio_start:0;
rx_sensors_enable:false,500;
tx_sensors_enable:false,500;
modulation:0,digu;
vfo:0,0,<hz>;
trx:0,true,tci;                <- third argument present on BOTH edges
trx:0,false,tci;
```

Eleven text frames across a full session with two transmit cycles. Note `trx:0,false,tci` — the
`tci` audio-source tag is sent on the **unkey** as well as the key, so a parser that only expects
it on `true` will mis-handle the unkey.

Everything else in the grammar is answered but never exercised by this client.

### Minimum viable command set — what ships first

Scope the first working server to exactly what WSJT-X exercises. Everything else in the TCI
grammar is deferred to the full-coverage phase.

**Inbound, must be handled:**

| Command | Action |
|---|---|
| `vfo:0,0,<hz>` | SET VFO A → `sendCAT("FA...")` + marshalled optimistic echo |
| `vfo:0,1,<hz>` | SET VFO B (TX VFO when split) |
| `modulation:0,<mode>` | SET mode → `sendCAT("MD...")` |
| `trx:0,<bool>[,tci]` | PTT → `AudioController::setPttActive()`, **no CAT command** |
| `split_enable[:0],<bool>` | split; accept the one-argument global form; act on transitions only |
| `audio_start:<n>` / `audio_stop:<n>` | begin/end RX_AUDIO; echo the command back |
| `rx_sensors_enable` / `tx_sensors_enable` | echo only, no effect |

**Outbound:** the init burst, broadcast-on-change for `vfo` / `modulation` / `trx` /
`split_enable`, `RX_AUDIO` frames, and `TX_CHRONO` frames.

**Everything else:** answered per the arity table with a GET reply where the snapshot has a value,
and otherwise met with silence — which is what the protocol specifies for an unknown or refused
command, with the one exception of rule 8 below.

Deferring the rest is safe because an unhandled command is *silence*, not an error, and WSJT-X
never sends them. It is not safe to defer any of the **behaviour rules** below: those apply to the
minimum set from the first commit.

### Hard-won: the TX_AUDIO payload has two candidate windows

This cost several on-air sessions and three fixes, so it is worth stating plainly.

WSJT-X allocates **twice** the length it is asked for (`TCITransceiver.cpp:871`) and cycles an
**8-entry ring of buffers** (`tx_fifo += 1; tx_fifo &= 7`). The live samples land in **either half**,
and which half is not under the server's control:

```
against AetherSDR : offset 0 in 689 frames, 2048 in 34, 2049 in 18
against QK4       : offset 2048 in all 581 frames carrying signal
```

So a decoder must **locate** the window, not assume it. Two traps on the way:

1. **"Non-zero" is not a signal test.** The unused window holds stale buffer in one capture (81.77%
   non-zero) and denormal dust around **7e-15** in a live session. Picking on non-zero chose garbage
   in the first case (peaks near 5e35) and inaudible dust in the second — the radio transmitted, the
   meters twitched, and nothing decoded. Require a real magnitude floor.
2. **Pick on structure, not just level.** Live audio is duplicated stereo pairs bounded by unity;
   stale memory is neither. Prefer the first window, which is what the reference server reads.

Diagnostic note: several intermediate measurements that *seemed* to show WSJT-X fragmenting its
audio into 64 ms bursts were artifacts of applying the same flawed "any non-zero" test during
analysis. With a proper floor the client delivers one contiguous transmission. Do not trust a
measurement that shares a bug with the code under test.

### TCI command → QK4 function map

**Rule: the TCI layer never spells a K4 command.** Every set goes through `CatFrames::` — documented
in `src/network/README.md` as existing to keep "command spelling in one place instead of scattered
string literals" — and then out via `ConnectionController::sendCAT`. A raw `"FA%1;"` anywhere in
`tciserver` or `tcicontroller` is a defect.

This also bounds the work: TCI defines 58 commands, QK4 has 24 frame builders, and the overlap is
what can be supported honestly. Anything with no builder is refused or ignored rather than faked.

| TCI command | QK4 function | Notes |
|---|---|---|
| `vfo:<t>,0,<hz>`, `dds` | `CatFrames::frequencyA` | `dds` is an alias for the receive VFO |
| `vfo:<t>,1,<hz>`, `tx_frequency` | `CatFrames::frequencyB` | the TX VFO when split |
| `modulation`, `mode` | `CatFrames::modeA` / `modeB` | needs a TCI-name ↔ `RadioState::Mode` map |
| **`trx:<t>,<b>`** | **`AudioController::setPttActive`** | **NOT `CatFrames::ptt`** — see below |
| `split_enable` | `CatFrames::split` | act on transitions only |
| `rit_enable` / `rit_offset` | `CatFrames::ritEnabled` / `ritOffset` | |
| `xit_enable` / `xit_offset` | `CatFrames::xitEnabled` / `ritOffset` | RIT and XIT share the `RO` register — see `docs/k4-protocol-quirks.md` |
| `drive`, `tune_drive` | `CatFrames::rfPower` / `rfPowerExtended` | always reply `<trx>,<power>` |
| `rx_filter_band` | `CatFrames::filterBandwidth` / `filterWidthExtended` | |
| `rx_nb_enable` | `CatFrames::noiseBlanker` | |
| `rx_nr_enable` | `CatFrames::noiseReduction` | |
| `agc_mode` | `CatFrames::agcSpeed` | |
| `cw_keyer_speed`, `cw_macros_speed` | `CatFrames::keyerSpeed` | |
| `sql_enable`, `sql_level` | `RadioState::setSquelchLevel` | |
| `volume`, `rx_volume`, `mute`, `rx_mute` | `AudioController` volume / mute | **local audio, never a CAT set** |
| `mic_level` | `AudioController::setMicGain` | local; also the TCI TX drive control |

**No QK4 equivalent — refuse or ignore, never fake:**
`iq_start`/`iq_stop` and everything IQ (QK4 has no IQ stream), `rx_record`/`rx_play`,
`rx_channel_enable`, `rx_balance`, `rx_bin_enable`, `rx_dse_enable`, `rx_anc_enable`,
`rx_nf_enable`, `rx_nb_param`, `agc_gain`, `tx_gain`, `lock`/`vfo_lock`, `cw_terminal`,
`cw_macros_delay`, `tune`.

`spot`, `spot_delete` and `spot_clear` map onto QK4's DX-cluster overlay rather than the radio, and
`cw_msg`/`cw_macros` onto the keyer path in `HardwareController`. Both are real features, both are
later phases, and neither is a CAT set.

**The one deliberate exception is PTT.** `CatFrames::ptt` exists (`TQ1;`/`TQ0;`) and is *not* used
here: `catserver.cpp:317-330` establishes that the K4 keys when TX audio starts arriving — *"Don't
forward to K4 - the audio stream itself triggers K4 TX"* — so `trx` sets the audio gate. Sending a
PTT command as well would fight the mechanism that already works.

### Client-behaviour rules to build in from day one

Drawn from TR4W's catalogue and AetherSDR's bug history; the starred ones were confirmed on the
wire in our own capture.

1. **\* `split_enable:false` arrives with no trx index.** Expand it to `split_enable:0,false` at the
   parse boundary or it reads as a GET for receiver −1 and is answered with silence.
2. **A steady `false` is not an edge.** WSJT-X sends `split_enable:<n>,false` routinely *before*
   programming channel 1. Only a true→false transition may tear anything down.
3. **`drive` and `tune_drive` replies must always carry `<trx>,<power>`.** A bare `drive:0;` crashes
   ESDR3-mode WSJT-X and JTDX, which index `args[1]` unconditionally.
4. **Channel 1 reports VFO A's frequency when split is off** — never the 0 a blank VFO B holds,
   which a client will try to tune to.
5. **`ready;` is last.** Never emit `audio_start`/`iq_start` in the greeting; those are client-owned
   and a greeting-side primer wedges SDC.
6. **`channels_count` is plural.** The published PDF says `CHANNEL_COUNT`; the reference parser
   aborts the handshake on the singular form.
7. **Do not comma-scrub identity values.** `modulations_list` *is* comma-separated and
   `ExpertSDR3,1.5` is a two-field value. Scrubbing yields `protocol:expertsdr3_1.5;`, which WSJT-X
   fails to match — after which it halves transmit amplitude.
8. **A refused `trx:<n>,true` must answer `trx:<n>,false;`.** Silence surfaces in WSJT-X as
   "TCI failed to set ptt" with no cause.
9. **PTT never guesses a receiver.** `trx:1,*` is declined with `trx:1,false;`, never folded onto
   trx 0.
10. **Per-command GET/SET arity, never a global `argc >= 2` rule.** AetherSDR's global rule makes
    every legitimately single-argument SET unreachable (`cw_macros_speed:20` is answered as a GET).
11. **`vfo:` confirmation echoes the frequency actually reached**, not the requested one. A stale
    echo caused WSJT-X to transmit out of band (AetherSDR #4500/#4493). A refused or no-op tune must
    still be confirmed with what the model holds — never met with silence.
12. **Inbound `tx_enable` is notification-only** — mutate nothing, reply nothing.
13. **Sanitise at the wire boundary.** Any value containing `;` or `,` corrupts framing for every
    client on the socket.

---

## Architecture

### Unit layout

Rule 7 (no file over 800 lines) is **binding for new code**, so this is four units, not one.

```
src/network/websocketserver.{h,cpp}   NEW  RFC 6455 server: handshake, framing, masking,
                                           ping/pong/close, size limits. No TCI knowledge.
src/network/tciprotocol.{h,cpp}       NEW  Grammar only: tokenise "name:a,b;", per-command
                                           arity table, reply formatting. Pure, no sockets.
src/network/tciserver.{h,cpp}         NEW  Session state, init burst, GET/SET dispatch,
                                           broadcast-on-diff, TX_CHRONO, audio framing.
src/controllers/tcicontroller.{h,cpp} NEW  Owns the TCI thread and the server object;
                                           task-level API; bridges to Audio/Connection.
```

The seam to preserve: **transport moves opaque bytes, grammar lives above it.** Do not let TCI
vocabulary leak into `websocketserver`, and do not let sockets leak into `tciprotocol`.

`Qt6::WebSockets` is **not** currently a dependency — `CMakeLists.txt:19` and the link list at
`:410-416` have no entry. It ships with Homebrew Qt, but adding it touches the macOS, Windows
(vcpkg) and Linux (apt) workflow files, which are upstream-owned. **Alternative under consideration:**
implement the RFC 6455 subset directly over `QTcpServer`, which is what TR4W chose
(*"~300 lines in one dependency-free unit"*) and which avoids a three-platform dependency change for
one feature. Decide before phase 1; the rest of the design is unaffected either way.

### Threading

QK4 has ten `new QThread` sites and one uniform convention: `new QThread(this)` +
`setObjectName(...)` + `moveToThread()` at construction, owned by a controller. Teardown is a
blocking-invoked stop, then `quit()` + `wait(2000)`, then delete
(`connectioncontroller.cpp:42-50`, `audiocontroller.cpp:88-99`).

`TciController` follows it exactly:

```cpp
m_tciThread = new QThread(this);
m_tciThread->setObjectName("TCI");
m_tciServer->moveToThread(m_tciThread);
m_tciThread->start();
```

Destructor, per Rule 11: `disconnect(this)` first, then blocking-invoke `stop()` (which closes
sessions and unkeys any owned PTT), then `quit()` + `wait(2000)`. Producers stop before consumers —
the TCI server stops before `AudioController` and `ConnectionController`.

### The four cross-thread edges

**1. CAT sets — TCI thread → radio. Mechanism already exists.**
`TcpClient::sendCAT` / `sendRaw` / `sendCATBytes` are `Q_INVOKABLE` and auto-marshal to the I/O
thread. `tcpclient.h:50-55` documents the precedent: the KPOD+ EP02 reader, a HighPriority worker
thread, delivers straight to the I/O thread bypassing main. TCI takes the identical path.

**2. CAT state reads — radio → TCI thread. The trap.**

Rule 4 is CI-enforced: `RadioState::parseCATCommand()` asserts main-thread affinity, and the getters
carry **no locking**. Reading `vfoA()` from the TCI thread is a data race that will mostly work and
occasionally publish a torn frequency.

Instead: connect `RadioState`'s `*Changed` signals to `TciServer` slots with
`Qt::QueuedConnection`, and keep a **snapshot owned solely by the TCI thread**. Qt's event queue
does the serialising — no lock, no retry loop. This is the idiomatic equivalent of the seqlock TR4W
had to add to `logradio.pas`.

**Known limitation to design around:** `RadioState` emits fine-grained per-field signals with **no
batch boundary**. TR4W deliberately bracketed its whole poll update (*"THE BATCH BOUNDARY"*) so
observers saw coherent multi-field state; QK4 has nothing equivalent. One CAT burst changing
frequency *and* mode arrives as two queued events.

- Tolerable for broadcasts — TCI is a change-notification protocol.
- **Not** tolerable for the init burst: build it from **one pass over the snapshot**, never from
  live reads, or a client can be seeded with a frequency and mode that never coexisted.
- Diff before broadcasting, so a message means something actually moved.

**3. TX audio — TCI thread → audio thread.**
Add `Q_INVOKABLE void feedTciTxAudio(const QByteArray &f32Mono48k)` to `AudioEngine`, invoked
queued. It then runs the existing `resample48kTo12k` → S16 → SL-tier frame → encode path.

**Never touch `m_micBuffer` / `m_micReadOffset` from the TCI thread** — `audioengine.h:191-194`
declares them audio-thread-only, deliberately, so a busy GUI cannot stall voice TX.

**4. RX audio — I/O thread → TCI thread.**
Fan out at `audiocontroller.cpp:45-50`, the lambda on `Protocol::audioDataReady`, which holds
decoded 12 kHz stereo Float32 (L = Main, R = Sub) **before** the jitter buffer and before
mix/volume/balance. Emit a signal there; connect queued to `TciServer`.

Do **not** tap downstream of `enqueueAudio` — it deliberately drops the oldest audio to recover
speaker latency (`audioengine.cpp:230-240`), and WSJT-X must not inherit the operator's speaker
buffer policy. Do **not** frame RFC 6455 on the I/O thread; it also carries the K4 control stream.

**TX_CHRONO placement:** on the TCI thread. Measured client tolerance (65 ms instantaneous jitter,
9% back-to-back) means the accumulator absorbs ordinary scheduling delay, so a dedicated thread is
not justified. Instrument the cadence (mean/stdev) and revisit only if measurement says so.

---

## RX audio path

```
K4 -> Protocol -> OpusDecoder -> [FAN-OUT at audiocontroller.cpp:45-50]
                                     |                    |
                          AudioEngine::enqueueAudio   TciController
                          (speakers, unchanged)           |
                                                   upsample 12k -> 48k
                                                   interleave L/R
                                                   RX_AUDIO frames -> client
```

### The upsampler

**Designed, measured, and proven against the real decoder.** 4× zero-stuff followed by a 255-tap
Blackman-windowed sinc, cutoff 5000 Hz, normalised to unity passband gain.

Prototype measurements — **use these as the unit-test acceptance criteria**:

```
passband ripple (<= 2800 Hz)   +0.00 dB
image rejection (>= 11 kHz)    -124.8 dB
worst image above 6.5 kHz      -123.7 dBc
round-trip correlation          1.000000
round-trip SNR                  72.0 dB
```

**Decoder proof:** `~/projects/wsjtx/samples/FT8/210703_133430.wav` (12 kHz, mono, 15.000 s)
upsampled to 48 kHz, decimated back, and decoded with WSJT-X's own `jt9`:
**14 / 14 messages recovered, with identical SNR, DT and audio frequency to the original.** The
upsampler costs nothing in sensitivity, timing or frequency accuracy.

A naive design is not acceptable here. The existing `resample48kTo12k` is a 4-tap boxcar whose
stopband is roughly −10 dB; mirroring that approach upward would fold images at 12/24/36 kHz
straight back into the passband.

### Channel mapping

The K4 packet is already L = Main, R = Sub, which is the exact shape a stereo TCI `RX_AUDIO` frame
wants. With `trx_count:1` the client only addresses receiver 0, so **send Main in both channels**
for now and keep the Sub routing decision with the deferred RX Two work.

---

## TX audio path

```
client -> TX_AUDIO frame
       -> take FIRST hdr.length floats (ignore the oversized tail)
       -> deduplicate stereo pairs -> 1024 mono samples @ 48 kHz
       -> [queued] AudioEngine::feedTciTxAudio
       -> resample48kTo12k -> S16 -> SL-tier frame -> encode -> txPacketReady -> K4
```

The seam is `audioengine.cpp:476`, `m_audioSourceDevice->readAll()` — the only source-specific line.
Everything below it is source-agnostic, and QK4's TX path natively wants 48 kHz mono Float32, which
is exactly what WSJT-X sends.

Three things this path must do that the microphone path does not:

1. **Source selection, not merging.** Mic and TCI must not both feed the encode pipeline. An
   explicit selector; the existing `m_pttActive` gate and `flushMicBuffer()` still apply.
2. **Bypass `m_micGain`.** It is applied unconditionally at `audioengine.cpp:489-495`, and WSJT-X
   transmits at full scale (peak 0.9990). Any slider position above unity **clips** against the
   `qBound(-1.0f, ..., 1.0f)`; below unity it silently attenuates digital drive. TCI needs its own
   calibrated level.
3. **Flow control is the chrono clock.** `m_micBuffer` has no backpressure and no drain policy
   because a capture device produces at exactly real time. A network source does not. Pacing
   TX_CHRONO correctly *is* the flow control; get it wrong and latency grows monotonically across a
   13-second FT8 transmission.

Quality of the existing decimator on this path was measured and is **adequate**: against a synthetic
FT8 GFSK burst it gave SINAD 85.0 dB, worst spur −75.0 dBc, 0.0125 dB gain spread across the eight
FT8 tones, and linear phase. It is adequate *because WSJT-X sends a clean band-limited waveform* —
its alias rejection is poor (−9.95 dB at 9 kHz), which remains a latent weakness of the **microphone**
path and is out of scope here.

---

## Relationship to CatServer: reuse the primitives, bypass the server

**`TciServer` does not call `CatServer` and does not connect to port 9299.** TCI is a peer of the
CAT server, not a client of it. Chaining them would put a K4-CAT text encode/decode round trip in
the middle of a TCI request for no benefit, and would couple two protocols that have no reason to
know about each other.

What TCI reuses is the **primitives `CatServer`'s wiring already calls** — the same three
functions, reached directly.

Tracing the existing path (`mainwindow.cpp:352-375`):

| Need | Primitive | Notes |
|---|---|---|
| Send a SET to the K4 | `ConnectionController::sendCAT(QString)` | marshals to the I/O thread itself |
| Optimistic local echo | `RadioState::parseCATCommand(QString)` | **main-thread only, Rule 4** |
| PTT | `AudioController::setPttActive(bool)` | **not** a K4 CAT command — see below |
| Answer a GET | `RadioState` getters | TCI reads its own snapshot instead |

### PTT is an audio gate, not a CAT command

`catserver.cpp:317-330` is explicit, and it is the single most important thing to copy:

> `TX`/`RX` commands - control audio input gate for external app transmit.
> Don't forward to K4 - the audio stream itself triggers K4 TX.

So `trx:0,true` from a TCI client maps to `AudioController::setPttActive(true)` and **must not**
send a PTT command to the radio. The K4 keys because TX audio starts arriving. This is the proven
path for FT8 through QK4 today, and the comment flags it as timing-critical.

### The optimistic echo must be marshalled

`CatServer`'s SET path does two things, not one:

```cpp
m_connectionController->sendCAT(command);
m_radioState->parseCATCommand(command);   // optimistic, so the passband tracks immediately
```

The second exists because K4 spectrum packets arrive *before* the CAT echo, so without it the
panadapter passband goes off-screen until the echo lands. TCI wants the same behaviour — but
`parseCATCommand` is main-thread-only and CI-enforced. From the TCI thread it must go through
`QMetaObject::invokeMethod(..., Qt::QueuedConnection)`.

Per Rule 12 this wiring belongs in `TciController`, not as another lambda on `MainWindow`.

### Two servers, one radio

Both listeners can be active at once, and two clients can then fight over frequency and PTT. TR4W
hit this and explicitly declined to arbitrate: *"Out of scope to arbitrate, but worth a warning in
the log when both are on."* QK4 adopts the same position — log a warning when both are listening,
do not arbitrate. Consistency comes from a single source of truth upstream (`RadioState` and the
K4 itself), not from inter-server coordination.

PTT ownership is per-session and **fails closed**: losing the client that owns a TCI PTT unkeys.
An *unowned* `trx:<n>,false` reports actual state and must never unkey the operator or another
client.

---

## Configuration and UI

Per Rule 12, nothing goes on `MainWindow`. A new **TCI** page under `src/ui/pages/`, following
`audioinputpage.cpp` exactly — build widgets, read/write `RadioSettings`, delegate to the
controller, no logic:

- Enable / disable the TCI server (runtime-toggleable, not construction-time)
- Port (default **50001**, the TCI convention)
- Bind loopback-only by default, with an explicit opt-in to all interfaces
- Read-only status: listening state, connected client count
- TCI transmit drive level (separate from mic gain — see TX path above)

Register it in `optionsdialog.cpp` alongside the existing pages.

Avoid the trap TR4W documented in its own predecessor: create the server object unconditionally at
startup and let `start()`/`stop()` be genuinely runtime-toggleable, rather than reading the enable
flag only in the constructor and requiring a restart.

---

## Hardening

Per Rule 5, every buffer fed from an external source needs an explicit limit. Mirroring AetherSDR's
(`TciServer.cpp:41-42, 685-699`):

- **Bind 127.0.0.1 by default.** `CatServer` binds all interfaces; do not repeat that here without
  an explicit opt-in.
- Maximum 8 concurrent clients.
- 64 KiB cap on both message and frame; `K4Protocol::MAX_BUFFER_SIZE` (1 MB) is the fallback ceiling.
- No path check and no subprotocol negotiation — clients rely on both being permissive.
- Reject a masked-violation or oversize frame by closing that session only, never the listener.

---

## Files

**New:**
`src/network/websocketserver.{h,cpp}`, `src/network/tciprotocol.{h,cpp}`,
`src/network/tciserver.{h,cpp}`, `src/controllers/tcicontroller.{h,cpp}`,
`src/ui/pages/tcipage.{h,cpp}`, `tests/test_tciprotocol.cpp`, `tests/test_tciserver.cpp`,
`tests/test_upsampler.cpp`, `docs/tci-server-design.md`.

**Modified:**
`src/audio/audioengine.{h,cpp}` (TCI TX source + `feedTciTxAudio` + gain bypass + upsampler hook),
`src/controllers/audiocontroller.{h,cpp}` (RX fan-out signal),
`src/ui/dialogs/optionsdialog.{h,cpp}` (register the page),
`src/settings/radiosettings.{h,cpp}` (enable, port, bind-all, TCI drive),
`CMakeLists.txt` (new sources; possibly `Qt6::WebSockets`),
`src/mainwindow.cpp` (construct `TciController` only — no widgets, no slots).

**Untouched:** `src/network/catserver.cpp`, `src/models/radiostate.{h,cpp}`.

---

## Phases

Each phase builds green and is committable on its own (Rule 9: one commit per logical change).

**Ordering rationale (NY4I): audio first.** CAT control already works over `CatServer` on 9299, so
a TCI server that only does CAT is a lateral move — *"if the audio does not work, the TCI CAT
control is less useful but still potentially useful."* Audio is the whole reason this feature
exists: it is what removes the loopback sound card. It is also the highest-risk part, and the one
open question in this document is an audio question. So audio is front-loaded, and CAT control —
the part we already know how to do, via primitives that are already proven in `CatServer` — comes
after.

**0 — Upsampler.** `RadioUtils` or a dedicated unit, plus `test_upsampler.cpp` asserting the
measured acceptance criteria above. No sockets, no threads. Independently verifiable, and already
prototyped and proven against `jt9`.

**1 — WebSocket transport.** `websocketserver.{h,cpp}`: handshake, frame encode/decode, server-side
unmasking, ping/pong/close, the limits above. Test by round-tripping text and binary over loopback.

**2 — Just enough protocol to reach audio.** `tciprotocol.{h,cpp}` tokeniser and formatters, plus
the init burst built from one snapshot pass, `ready;` last, and handling for `audio_start` /
`audio_stop` (echo), `rx_sensors_enable` / `tx_sensors_enable` (echo), and `split_enable` accepted
as a no-op. **No CAT SETs yet.** The goal is narrow: WSJT-X connects, completes the handshake, and
asks for audio.

**3 — RX audio. First real milestone.** Fan-out at `audiocontroller.cpp:45-50`, upsample 12k → 48k,
frame and send `RX_AUDIO`. **Gate: WSJT-X decodes FT8 from the K4 over TCI.** This is the single
result that determines whether the feature is worth building, and it is reachable without one line
of CAT SET handling.

**MET, off the air, 2026-09-15.** QK4 itself served TCI on 50001 with a live K4 on 40m at
7.074 MHz, and `jt9` decoded **25 FT8 messages** from a single 15-second window pulled through the
server — EA8BS, 9Y4DG, LU5BBV, HC2GRC, PD5FL, DL6TK and others.

Two figures make this stronger than the recorded-sample run:

- **DT clustered at 0.1-0.2 s.** Timing survives the whole path with no accumulated latency or drift.
- **A −25 dB decode**, against an FT8 floor near −24. Distortion, aliasing or dropouts cost the
  marginal signals first; none were lost.

The path exercised: K4 -> `Protocol::audioDataReady` -> `OpusDecoder` -> the fan-out at
`audiocontroller.cpp:45-50` -> queued to the TCI thread -> `TciAudioBridge` 12k->48k ->
`TciAudioFrame` -> `WebSocketServer` -> client. No loopback sound card.

Note that this phase alone may already be independently useful: TCI carrying audio while
`CatServer` on 9299 carries CAT. **Unverified** — WSJT-X's "Use TCI Audio" checkbox may require the
TCI rig backend to be selected. Worth testing once phase 3 works, because it would let the loopback
sound card die before any TCI CAT exists.

**4 — TX audio. BUILT 2026-09-15; bench gate still owed.** `trx:0,<bool>` →
`AudioController::setPttActive()`, the TX_CHRONO accumulator, `TX_AUDIO` ingest (first
`hdr.length` floats, deduplicate pairs), `feedTciTxAudio`, source selector and gain bypass in
`AudioEngine`. PTT ownership enforced: one owner at a time, an unowned unkey only reports, and
losing the client or stopping the server unkeys.

**Gate fully met 2026-09-15: WSJT-X transmits through this path and is DECODED by the spotting
networks.** Sustained peak 0.738846 across 1900+ blocks, constant as FT8 requires, with 2010 chrono
requests against 2011 blocks received - 0.05%% apart.

Getting here took three fixes after the first transmission; see "the TX_AUDIO payload has two
candidate windows" above for what went wrong and why a capture alone did not reveal it.

Earlier partial result, kept for the numbers: measured
across two transmissions:

```
TX_CHRONO stopped after 5093 requests, 5101 blocks received   <- 0.16% apart
TX audio: peak 0.223812 steady, final block peak 0 (wind-down)
PTT ON / PTT OFF clean on both edges, chrono started and stopped in step
RX audio continued throughout at peak ~0.36
```

The request/response tracking is the number that matters: a starving client shows blocks lagging
requests, a free-running one shows them diverging. Neither happened, so the chrono accumulator is
pacing correctly and is doing its job as flow control.

Note the TX peak of 0.22 rather than the 0.9990 measured in the reference capture — that is WSJT-X's
own power slider, not the path. It is also why the `m_micGain` bypass matters: at full slider that
gain would clip.

**5 — CAT control.** The rest of the minimum viable command set: `vfo`, `modulation`,
`split_enable` transitions. Snapshot from queued `RadioState` signals, broadcast-on-diff, the
marshalled optimistic `parseCATCommand` echo, per-session PTT ownership. Table-driven tests for
every numbered client-behaviour rule — those are not deferrable even though most *commands* are.

**6 — UI.** A **TCI Server** page mirroring the existing **CAT Server** panel on the Rig Control
page, which is the established pattern for exactly this and should be copied rather than
reinvented:

| Element | Behaviour |
|---|---|
| Status | `Not running` / `Listening on <port>`, coloured as the CAT panel does |
| Clients | `N connected`, live — driven by `TciController::clientCountChanged` |
| Port | line edit, `(default: 50001)` beside it |
| Enable TCI server | toggle, **effective immediately and persisted so it comes up enabled at startup** |
| Enable TCI audio | toggle — CAT-only is a legitimate configuration, and audio is the expensive half |
| Help text | which host and port to configure in WSJT-X, and that no loopback sound card is needed |

`TciController` and the `RadioSettings` keys (`tciServer/enabled`, `tciServer/port`) already exist
and are wired; this phase is the page itself plus registering it in `optionsdialog.cpp`.

**7 — Bench.** Full end-to-end verification; see below. **This is the gate for declaring the
feature working**, and it comes before any grammar expansion.

**8 — Full TCI coverage.** Flesh out every call a TCI client can make, beyond WSJT-X's seven:
`tune`, `drive`/`tune_drive` SET, `rit_offset`/`xit_offset`, `rx_filter_band`, `cw_macros*`,
`spot`/`spot_delete`/`spot_clear`, `iq_start`/`iq_stop`, `volume`, `mute`, `agc_mode`, `sql_*`,
the `rx_*_enable` DSP flags, and `dds`. Each needs an arity-table entry, a snapshot field, and a
test. Driven by whichever clients get attached next (SDC, JTDX, RF2K-S, Stream Deck), since the
client population is the real spec.

---

## Verification

**Unit** — `ctest --test-dir build` (currently 16 tests, all passing). New suites add to it.
Per Rule 6, any `RadioState` parser change needs a matching case in `tests/test_radiostate.cpp`;
this design does not modify the parser.

**Decoder-in-the-loop, no radio required.** WSJT-X's decoder runs standalone:

```
/Applications/wsjtx.app/Contents/MacOS/jt9 -8 -a <dir> -t <dir> file.wav
```

It takes a 12 kHz mono WAV and prints decodes. `samples/FT8/210703_133430.wav` yields 14. Push a
sample through the upsampler, back down, and assert all 14 still decode with unchanged SNR/DT/freq.
This belongs in CI if the sample can be vendored; otherwise it is a documented manual gate.

**Protocol replay, no radio required.** The capture tooling built during design
(`tci_tap.py`, `tci_decode.py`, `rate_probe.py`) records a real client session and replays a
known-good init burst. A replayed burst was verified to be accepted by a live WSJT-X.

**Bench (the real gate)** — none of this is provable by review:

1. WSJT-X connects to QK4, reads frequency and mode, sets frequency, keys and unkeys.
2. K4 receive audio reaches WSJT-X over TCI and **decodes FT8**.
3. WSJT-X FT8 audio reaches the K4 over TCI and is **decoded by a third party**.
4. PTT round-trip latency inside the client's timeout.
5. Kill a client mid-transmission; verify it unkeys and the session is released.
6. `CatServer` on 9299 and the TCI server both active; confirm neither corrupts the other.

---

## Risks

- **Highest: the client population is the spec.** Every numbered rule above exists because a
  technically-correct server broke a real client. Phase 6 is the gate, not phase 5.
- **New Qt module on three platforms** if `Qt6::WebSockets` is chosen over a hand-rolled RFC 6455
  subset. The workflow files are upstream-owned.
- **No batch boundary in `RadioState`.** The init burst must be built from one snapshot pass.
  A future `RadioState` change that alters signal granularity can silently change broadcast volume.
- **Two masters, one radio.** Logged, not arbitrated. Revisit if it bites in practice.
- **TX flow control.** The chrono accumulator is the only thing preventing unbounded latency growth
  during a 13-second transmission. It needs instrumentation, not just correctness at review time.

---

## Open questions

- ~~FT8 has not yet been decoded over a live TCI stream.~~ **CLOSED.** A standalone binary running
  the real `TciServer`, `WebSocketServer`, `AudioUpsampler` and `TciAudioFrame` streamed the stock
  FT8 sample over a real socket to an independent client, and `jt9` decoded **11 of the 14** messages
  the file yields directly, with SNRs within 1 dB. The three that dropped were marginal signals
  (−3, −6, −7 dB, two of them adjacent at 466/472 Hz) lost to about 0.1 s of timing offset in the
  test client, which aligns by discarding audio until the UTC boundary and so lands up to one
  21.3 ms block plus socket latency late — every decoded DT shifted 0.3 → 0.2 consistently. A real
  client controls its own period timing and does not align that way. **The receive path is proven
  end to end; the remaining gap is the harness, not the server.**
- **`Qt6::WebSockets` versus a hand-rolled RFC 6455 subset.** Decide before phase 1.
- **Sub receiver / RX Two.** Deferred. When picked up, note that `trx_count` is ignored by WSJT-X,
  so the client-side rig selection (`TCI Client RX2`) is the only lever — QK4 can only accept or
  refuse it.
