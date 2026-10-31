# CW keyer port: K4-Companion's keyer into QK4

> **Status (2026-10-29).** This began as the port plan (2026-10-27) and is now its design record.
> Phase 1 has landed. Phase 2 (sidetone in the RX output) has not been started. Phase 3 (the
> monitor-mute decision) is open. Sections written as plans say so; "What changes, file by file"
> and "Decisions" record what was planned, and the notes in them say where the code differs.
> Code comments in `cwcontroller.h`, `paddlekeyer.h`, `keyertiming.h`, `sidetonescheduler.h`,
> `sidetonegenerator.h` and `tests/CMakeLists.txt` point here.

**Phase 1 — landed.**
- `99e4d6e` (2026-10-27): keyer, sidetone scheduler and serial poller ported. Dale's 554 recorded
  runs replay byte for byte.
- `32c6891` (2026-10-27): `PaddleKeyer` replaces `IambicKeyer` on the HaliKey path. V1.4 lines and
  raw MIDI feed it; it sends `KZD`/`KZU`.
- `742a276` (2026-10-27): the V1.4 worker polls every 1 ms on every platform through
  `SerialLinePoller` (rest state at open, 20 ms straight-key debounce, per-line deltas). This
  replaced the per-platform loops, including Linux's `TIOCMIWAIT`. The Windows 1 ms
  high-resolution timer, the DTR/RTS power and the native port handling are unchanged. Why it
  polls: [halikey-cw-trace.md](halikey-cw-trace.md), "Why V1.4 polls instead of waiting for line
  events".
- `72025df` (2026-10-27): sidetone delay cut from ~100 ms to ~12 ms. See "Sidetone" below.
- `b54cf06` (2026-10-29): removed what the port left behind (`iambickeyer.*`, `halikey_edge.h`,
  their tests, `ConnectionController::m_kpodPlusKeyerActive`).
- `08abff6` (2026-10-29): CW docs brought up to date.

**Bench.** 2026-10-27, Mac mini speakers, HaliKey MIDI: sidetone delay "perfect". 2026-10-29: the
maintainer bench-verified HaliKey V1.4 (serial) and MIDI keying as working. On-air (RF) keying is
still to be confirmed by testers on the next release. The threaded test is clean under
ThreadSanitizer.

**Phase 2 — not started.** `SidetoneGenerator` is still the live sidetone output, on its own sink
and thread. Nothing in `AudioEngine` renders sidetone yet.

**Phase 3 — open.** Waits on bench question 2.

The CW Keyer options page has device selection, the HaliKey keyer mode, input type and dit
memory delay, plus shared monitor mute and Key-Down Delay. Vail adds a choice of its onboard
buzzer or computer sidetone.

**Source:** Dale Farnsworth's [K4-Companion](https://github.com/DaleFarnsworth/K4-Companion),
commit `040f711` (v2.2), GPL-3.0, the same licence as QK4. Ported files carry an SPDX header and
credit him.
**Scope:** HaliKey paddle and straight-key keying over serial and MIDI, and its sidetone.
**KPOD+ keying is out of scope and must not change.**

## Why his keying is more reliable

The middle column is QK4 **before the port** (pre-`99e4d6e`). Its file:line citations point at
code that has since been replaced.

| # | K4-Companion | QK4 before the port | Effect |
|---|---|---|---|
| 1 | `KZD####` / `KZU####`: the timing travels in the command (gap before, length of) | `KZ.` / `KZ-` sent at element start, untimed (`cwcontroller.cpp:139-170`) | The K4 replays exact intervals, so network jitter and thread stalls can't change what goes on air |
| 2 | Edges stamped at capture: MoMIDI's own clock, 1 ms serial poll. Decisions read the paddles **as of when they were due**, from a 64-entry history | MIDI time discarded (`halikeymidiworker.cpp:174`); decisions read the live lever when the timer fires | A 0.44 s host stall no longer turns S + T into V |
| 3 | Iambic memories measured from when the previous element was **heard** (and the last look), on an absolute grid with wake-overrun learning | Latches on lever edges, `QTimer` in whole ms | Bench-swept at 20–50 WPM: squeeze, tap, swap, dah-first, stall |
| 4 | An element is committed only once it is wanted. No queue-then-retract race | n/a | No phantom dits |
| 5 | Sidetone edges placed on exact frames against the device's play-out time; delay fixed at the first edge, never ratcheted by non-audio lateness, eases back 1 ms/s | One pre-built block per element pushed into a separate sink; partial writes ignored (`sidetonegenerator.cpp:201`) | Every element sounds its true length, with a constant delay |
| 6 | Optional `ML0000` for the burst so the K4's late monitor copy isn't heard on top | n/a | No echo (bench question; see below) |

Items 1 and 2 fix what goes on air. Items 3 to 6 fix the feel and the monitoring.

## Decisions (2026-10-27)

- **Sidetone clock: Qt output with frame counting.** No PortAudio. Dale himself falls back to
  frame counting where PortAudio's DAC times stray by more than 1 ms, and on Linux and Pi they
  often do. The clock source sits behind `SidetoneScheduler::StreamTimeFn` and the block-timing
  call, so PortAudio could be added later without touching the keyer.
- **Sidetone mixed into the RX output** (one device, one clock). The tone crossfades over RX
  audio as Dale's does. This replaces `SidetoneGenerator`'s separate sink. *Decided, not built:
  phase 2.*
- **K4 monitor mute: built, off by default.** If the bench shows the K4 already suppresses its
  monitor for `KZD`/`KZU` (it does for `KZ.`/`KZ-`), it stays off and the K4 menu setting covers
  the rest.
  Production routing uses `CwMonitorMute::State` on the I/O thread for both HaliKey and Vail.
  HaliKey frames reach that policy through the lock-free `KzEgress`, carrying their source,
  generation and capture-time eligibility; stale key-downs are rejected before muting or writing.
  It saves the known `ML0` level, mutes before an accepted key-down, and restores after the
  queued keying tail, `KZL`, and a 500 ms margin. A new key-down cancels that restoration,
  including across device changes. Temporary zero-level feedback cannot replace the saved
  level. The keyer-local monitor timer stays disabled in production so it cannot
  compete with the shared policy. Normal disconnect restores before the socket closes;
  an unexpected connection loss cannot send a restoration, so check the K4's CW monitor
  level after reconnecting. Local computer sidetone and the Vail buzzer are unaffected.
- **Modes:** Iambic A, Iambic B (default), Plain Iambic, Ultimatic, plus straight key / external
  keyer passthrough (#130).
- **Dit memory delay: in, default 33 %** (Dale's app default). Iambic B only.
- **Auto character/word spacing: out.** It doesn't fix reliability. With it off, Dale's
  `auto_space_until()` returns None everywhere, so leaving it out changes nothing else.

## What changes, file by file

**New**
- `src/hardware/keyertiming.h`: the Clock / Link / OneShotTimer seams and `pyRound`.
- `src/hardware/paddlekeyer.{h,cpp}`: the port of `Keyer` (`k4companion:19313-21356`).
- `src/audio/sidetonescheduler.{h,cpp}`: the port of the sidetone half of `Audio`
  (`9122-9440`, `10473-10727`, `11001-11068`).
- `src/hardware/seriallinepoller.{h,cpp}`: the port of `SerialInput.signal_loop`
  (`18890-18970`).
- Production seams: steady_clock plus a precise sleep (Windows high-resolution waitable timer, as
  the V1.4 worker already uses); a `Link` onto `TcpClient::sendCAT`, which marshals the call to the
  I/O thread with a queued `invokeMethod`; a QTimer for the monitor restore.

**Replaced** (HaliKey only)
- `IambicKeyer` → `PaddleKeyer`, on the existing "Keyer" thread, which
  promotes itself to real-time (`RtScheduling`, since 2026-10-29; before that it was started with
  `QThread::HighPriority`, which Qt ignores on Linux). `run()` blocks on its event queue, so the
  thread has no Qt event loop.
- `SidetoneGenerator` → `SidetoneScheduler` plus a mixer stage in `AudioEngine`'s output. *Only
  the scheduler half is done. `SidetoneGenerator` stays as the output until phase 2.*
- `HalikeyDevice` / `halikey_edge.h`: edge dedupe moves into the keyer (MIDI up-without-down) and
  the poller (rest state, straight-key debounce).
- `halikeyv14worker`: the per-platform loops become one 1 ms poll through `SerialLinePoller`,
  keeping the native port open/close and the Windows timer.
- `halikeymidiworker`: hands raw `(status, note, velocity, deltaTime)` to
  `PaddleKeyer::midiMessage` directly, with no queued hop (Dale calls it on RtMidi's thread).
- `CwController`, HaliKey half: no more `KZ.`/`KZ-`/`KZ `/`KZP` wiring. Speed, weight and pitch
  from RadioState feed `PaddleKeyer::setSpeed` and the sidetone.

**Kept exactly as is (KPOD+)**
- `kpodplusdevice.*`, `kpodplususbworker.*`, the EP02 thread.
- The KPOD+ EP02 batches reaching the K4 untouched, one CAT packet per batch. (Since 2026-10-29
  the EP02 reader posts them to `TcpClient`'s `KzEgress` from its own thread instead of a queued
  `keyerDataReceived` → `sendCATBytes` connection; the bytes on the wire are the same.)
- The KPOD+ ownership gate → `CwController::setKpodPlusGate`, which raises the keyer's inhibit:
  `PaddleKeyer` refuses to key while it is set. (`ConnectionController::m_kpodPlusKeyerActive`,
  which the old HaliKey KZ lambdas read on the I/O thread, had no reader left after the port and
  was removed 2026-10-29. The KPOD+ path never read it.)
- The `keyerSpeedChanged` / `keyerPaddleChanged` / `cwPitchChanged` lambdas keep their
  `m_kpodPlus->set…` calls unchanged.
- `test_kpodplususbworker` must stay green and untouched.

**Retired** (in `b54cf06`)
- `iambickeyer.*`, `halikey_edge.h`
- `test_iambickeyer`, `test_halikeydevice`. Their cases are superseded by the sweeps; the
  Iambic A semantics change, see below.
- `sidetonegenerator.*` retires in phase 2, not before. It is the live sidetone output today.

**Docs:** `src/hardware/README.md`, `src/audio/README.md`, `docs/k4-protocol-quirks.md` §8,
`docs/halikey-cw-trace.md`, `docs/controllers.md`, and the CLAUDE.md test table.

## Phases

1. **Keyer on the air — landed.** `PaddleKeyer`, `SerialLinePoller` and the MIDI path; `KZD`/`KZU`
   out. `SidetoneScheduler` landed too, because the keyer's decisions depend on when a tone was
   heard. Until phase 2 it is fed by `SidetoneGenerator` (see "Sidetone"). Tests: all five suites
   below.
2. **Sidetone in the RX output — not started.** A mixer stage in `AudioEngine`'s output callback calls
   `noteBlockTiming` / `nextKeyEdge` per block, with DAC time = time of fill + frames still
   queued in the sink. Envelope and tone come from Dale (4 ms raised cosine, exact-period table,
   continuous phase). Pull mode is out: on macOS Qt pulls ~85 ms ahead (see Status). Phase 2 needs
   the same paced, adaptive push that `SidetoneGenerator::pump()` does now. Check that first: it's part of
   the audio path currently being reworked.
3. **Monitor mute decision — open.** From the bench, then remove or keep the setting.

## Sidetone

What runs now (phase 1). `SidetoneScheduler` decides when each tone edge plays.
`SidetoneGenerator` renders it on its own `QAudioSink`, on the "Sidetone" thread (which promotes itself to real-time with `RtScheduling`). Its `pump()` runs on a 1 ms
`Qt::PreciseTimer` and pushes 4 ms blocks into an adaptive queue. The queue starts at 8 ms and grows
one block each time the device runs dry. It reports play-out time back to the scheduler through
`streamTime()`. Volume comes from `RadioSettings::sidetoneVolume()`, not from the K4's `ML`.

How it got there (bench 2026-10-27, `72025df`):
- Pull mode held ~100 ms. Qt on macOS asked for up to ~85 ms per read even with a 20 ms buffer,
  and the scheduler rightly held the delay above that.
- Pushing with a fixed 8 ms queue ran dry every ~11 ms device period, which sounded awful.
- The adaptive queue settled at 12 ms on a Mac mini and stayed clean.

What phase 2 changes: the render moves into `AudioEngine`'s output, one device and one clock, as
planned under "Phases".

## Tests (registered, passing)

| Suite | File | What it pins |
|---|---|---|
| PaddleKeyerSweepsTests | `tests/test_paddlekeyersweeps.cpp` | **Golden replay of Dale's 554 recorded runs, byte for byte** (`tests/data/paddlekeyer/`); squeeze rule at 20–50 WPM; squeeze rule at Dit Mem 33 % and proof the setting bites; dah-first and stall verdicts at 20–50 WPM; KZU always nominal; mute off changes nothing but the ML |
| PaddleKeyerTests | `tests/test_paddlekeyer.cpp` | MoMIDI velocity/aftertouch decoding, velocity 0/127 → host delta, note-on vel 0 = down, unmatched up dropped; straight key: 5 s cap, minimum tone, single KZU, release closes an open KZD; close; every KZD closed in every run; release mid-squeeze; Ultimatic, Plain Iambic, Iambic A; weighting; monitor restore |
| SerialLinePollerTests | `tests/test_seriallinepoller.cpp` | Rest state not an edge; paddles on the same poll with per-line delta; dit first; 20 ms straight-key debounce and bounce restart; dah ignored when straight |
| PaddleKeyerThreadedTests | `tests/test_paddlekeyerthreaded.cpp` | The keyer on its own thread with a real steady clock; events and releases from other threads; no KZD left open; `close()` sends a held straight key's KZU before the join. Run under ThreadSanitizer too |
| SidetoneSchedulerTests | `tests/test_sidetonescheduler.cpp` | First edge fixes delay at lead + block; keyer-late and skipped-block downs dragged; other late downs raise the delay; late ups dragged; exact-frame placement; cancel before/after placement; ease in silence; edge deadline |
| CwMonitorMuteTests | `tests/test_cwmonitormute.cpp` | Shared HaliKey/Vail mute ownership, queued replay tails, saved monitor level and feedback, checkbox changes, forced releases, and connection isolation |
| CwMonitorRoutingTests | `tests/test_cwmonitorrouting.cpp` | Real TCP framing, mute-before-key-down ordering, restoration with a blocked GUI, source handoff, and restoration before graceful disconnect |

`tests/paddlekeyersim.h` is the C++ port of `paddle_sweep.py`'s `sim()`: virtual clock, 48 kHz,
384-frame blocks, 16 ms lead, 0.1 ms hop. The keyer's sleeps advance the clock and run the blocks
and deliveries inside them.

The golden files were recorded by Dale at `040f711`. His harness couldn't be re-run here (it
executes the downloaded script, which was not permitted), and its stub lacks two `Audio` fields the
current code reads. So the recordings may predate a late commit or two. A run that differs
because of that shows up as a single named failing run; check it against his code before
changing either.

## Porting rules

- Seconds are `double` and every expression keeps the Python order, so results are bit-identical.
- `round()` is `CwKeying::pyRound` (half to even). `math.ceil` / `math.floor` map to
  `std::ceil` / `std::floor`. `whole_ms(s) = ceil(s * 1000) / 1000`. `int(x)` truncates.
- MIDI: `0x90` is key-down whatever the velocity; only `0x80` releases. Every mapped or unmapped
  `0x90`/`0x80` advances the device clock. CC/aftertouch value × 126 ms. Velocity 0 or 127 →
  RtMidi's delta. Re-anchor when a delta or an offset jump reaches 1 s. Velocity is an `int`: the
  harness sends aftertouch counts above 127, as Dale's does.
- Serial: open the port with DTR and RTS asserted, as pyserial does by default, and treat the line
  state at open as rest. QK4 does not use `QSerialPort` for the V1.4: `HaliKeyV14Worker` opens a
  native handle and asserts both lines itself (`DTR_CONTROL_ENABLE`/`RTS_CONTROL_ENABLE` on
  Windows, `TIOCMBIS` on POSIX). `QSerialPortInfo` only lists the ports.
- Shutdown, as built in `HardwareController`'s teardown: close the HaliKey port first, so no new
  paddle events arrive → `PaddleKeyer::close()` (`releasePaddles(true)` refuses later events and
  queues a held straight key's release, then Close is posted behind it) → join the keyer thread
  (2 s) → clear the keyer's link. The link is cleared only after the join because the keyer thread
  may still send a held straight key's KZU while draining; `ConnectionController` is still alive at
  that point. Then the sidetone thread stops. Every KZ and ML frame is sent by the keyer thread.
- Shared state written by the audio callback and read by the keyer (`sidetone_block_timing`,
  `sidetone_down_placed`, the lag and edge shift, the asked/late/cancelled/dropped stamps, the
  key-event queue) sits behind one short `SidetoneScheduler` mutex. K4-Companion relies on the GIL
  for the same atomicity. The mutex is never held across a sleep, a send or a call out, so the
  callback waits at most a few hundred nanoseconds. If phase-2 profiling shows priority inversion
  on any platform, the fallback is per-field atomics with a seqlock for the tuples.
- Keyer-side state follows K4-Companion's locks: `first_element_lock` becomes `m_firstElementMutex`;
  paddle stamps and history get their own leaf mutex; straight-key on-air state and the monitor
  mute live on the keyer thread alone, so releases from other threads are queued to it and two
  releases can never send two KZUs or a KZU ahead of its KZD. No lock is held across
  `Link::send` or `Clock::sleep`, which the virtual-clock harness relies on.
- Deliberate deviation: after `close()`, paddle events are dropped at the door instead of queued
  for a thread that has gone.

## Behaviour changes testers will notice

- **Iambic A changes meaning.** QK4's current A reads live paddles only, after the CW-015/016 fix.
  Dale's A keeps the dit memory (a dit closed after a dah was heard is sent) but no dah memory and
  no squeeze-release memory. The port follows Dale; `iambicAKeepsDitMemoryOnly` pins it.
- **Serial pin map — stayed fixed.** The plan was a per-paddle setting. What landed is the
  bench-verified V1.4 wiring, hard-coded in `HaliKeyV14Worker::readPinState`: dit = CTS,
  dah = DCD ∥ DSR (the V1.4 asserts both together). `PaddleKeyer::setDitSignal` / `setDahSignal` /
  `serialSignalChanged` exist but only tests call them.
- Weighting follows the K4's `KP`, which QK4 already parses.

## Bench questions

1. ~~What does `KZL` mean?~~ **Answered 2026-10-27** from the K4 CAT reference: remote paddle
   key-down initial delay, 0–1000 ms, default 80. QK4 had been overwriting it with 1200 / WPM.
   Both writes are removed; RadioState now reads it from the RDY dump
   (`keyDownInitialDelayMs()`), and `PaddleKeyer::setKeyDownInitialDelayMs` is fed from there.
   See `k4-protocol-quirks.md` §8.
2. Does the K4 suppress its own monitor for `KZD`/`KZU`, as it does for `KZ.`/`KZ-`? This decides
   phase 3.
3. Is `ML0000;` echoed? Local sidetone volume is independent of `ML0`. Production monitor
   restoration ignores the temporary zero echo and remembers the original nonzero level; a
   nonzero operator change becomes the new restore value. `CwMonitorMuteTests` covers both
   echoed and unechoed responses. This still needs a live-radio check of the new shared route.
4. Does `KY \x04;RX;` (Esc) stop `KZD` playback already queued in the K4?
5. ~~Which modem line does the HaliKey V1.4 assert for dah?~~ **Answered:** DCD and DSR together
   (`HaliKeyV14Worker::readPinState`).
