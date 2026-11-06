# Tracing the HaliKey CW path

A recipe for investigating HaliKey (V1.4 serial or MIDI) keying reports: wrong elements, wrong
spacing, sidetone delay. It follows a paddle edge to the timed `KZD`/`KZU` commands that reach the
K4 and to the local sidetone. The keyer is `PaddleKeyer`, a port of K4-Companion's keyer; its design
is in `docs/cw-keyer-port-plan.md`.

## What gets logged

| Category | Source | Lines you'll see |
|---|---|---|
| `qk4.cat.tx` | `src/network/tcpclient.cpp` | `sent: "KZD0060;" ( 15 bytes, 42 us from the keying thread)` for every KZ and ML frame, written by the I/O thread as it hands the frame to the socket. The microseconds are how long the frame waited between the keyer (or KPOD+ EP02 reader) posting it and the write. Other commands log `sent: "…"` as before |
| `qk4.link.tcp` | `src/network/tcpclient.cpp` | at info, once a minute while keying and when the session ends: `KZ egress, keying thread to socket write, this session: N frames, median <= … us, 99% <= … us, max … us, dropped 0` |
| `qk4.cw.midi` | `src/hardware/halikeymidiworker.cpp` | `MoMIDI detected, version N` (from the presence poll), port open and presence-poll lines |
| `qk4.audio.sidetone` | `src/audio/sidetonegenerator.cpp` | output dry-outs and short writes |
| `qk4.cw.keyer` | `src/controllers/cwcontroller.cpp` | the KPOD+ keyer gate only |

`PaddleKeyer` itself logs nothing: what it decided is exactly what `qk4.cat.tx` shows it sent.

**No line is logged per paddle edge, MIDI note or tone onset.** Those threads (HaliKey worker,
RtMidi, keyer, sidetone, KPOD+ EP02) are real-time and must not format a string (CONVENTIONS Rule
16). The per-edge `qk4.cw.halikey` lines, the Windows `raw pins` lines, the per-note `qk4.cw.midi`
lines, the per-tone `tone on:` line and the KPOD+ `KZ@`/`TX@` pair were removed on 2026-10-29. A
lock-free trace buffer drained on the log writer thread would bring them back; until then, judge
paddle input by the `KZU` lengths and a recording of the paddle.

The KPOD+ path's EP02 batches appear as `sent:` lines too, so a KPOD+ session can still be A/B'd
against a HaliKey session.

## Enabling the trace

Easiest, on any platform: Options → Diagnostics → **Write a log file**, check **CW keying**, and under
Bench diagnostics **CAT traffic** (for the `sent:` lines). Key, then **Open Log Folder** and take
`qk4.log`. Every line carries a millisecond timestamp and the thread (`[Keyer]`, `[HaliKey]`,
`[Sidetone]`). The same from a terminal, with the rules below, is `--log-file --log-rules "<rules>"`.

Or with the environment, to the console. macOS / Linux:

```bash
QT_LOGGING_RULES="qk4.cw.midi.debug=true;qk4.cat.tx.debug=true;qk4.audio.sidetone.debug=true" \
  build/QK4.app/Contents/MacOS/QK4 2>&1 | tee /tmp/qk4-cw-trace.log
```

Windows (PowerShell):

```powershell
$env:QT_LOGGING_RULES = "qk4.cw.midi.debug=true;qk4.cat.tx.debug=true;qk4.audio.sidetone.debug=true"
$env:QT_FORCE_STDERR_LOGGING = "1"
$env:QT_MESSAGE_PATTERN = "%{time process} %{category}: %{message}"
.\build\Release\QK4.exe 2> qk4-cw-trace.log
```

Category strings are case-sensitive and all lower case (`qk4.cat.tx`). The names before 2026-10-29
(`CAT.TX`, `hw.halikey`, …) are listed in `src/logging/README.md`.

**Debug output is off by default.** Every QK4 category is declared with `QtInfoMsg` as its default
severity, so `qCDebug` lines are skipped, arguments and all, until a rule turns them on. That is
what the `QT_LOGGING_RULES` in the examples above do.

> **Windows logging gotcha.** Qt's default handler writes to stderr only if it thinks a console is
> attached. Redirected to a file, `GetConsoleMode` fails and Qt routes everything to
> `OutputDebugString` — the log stays empty. `QT_FORCE_STDERR_LOGGING=1` fixes that, or use the log
> file above, which does not depend on a console.

## Reading the wire

Each element is one pair: `KZDnnnn;` (the gap before key-down, in ms) then `KZUnnnn;` (how long it
was down). `docs/k4-protocol-quirks.md` §8 is the reference for these forms. The K4 replays those intervals behind its KZL key-down delay, so the `sent:` times can
jitter without changing anything on the air — read the numbers, not the timestamps.

A clean `C` (dah dit dah dit) at 20 WPM, weight 1.00, sent from idle:

```
sent: "KZD0000;"   ← first element of the first burst since start: gap 0 (see below)
sent: "KZU0180;"   ← dah
sent: "KZD0060;"   ← inter-element gap
sent: "KZU0060;"   ← dit
sent: "KZD0060;"
sent: "KZU0180;"
sent: "KZD0060;"
sent: "KZU0060;"
```

The first `KZD` of a burst carries the measured gap since the previous burst's last element,
which is how the operator's character and word spacing reaches the air. It is 0 only for the first
burst since start, or when the gap is negative or over 5 s (`PaddleKeyer`, the first-element branch).
Every later `KZD` in a burst is the nominal gap. Element and gap lengths follow the K4's KS speed and
KP weight.

## Diagnosing

- **Wrong element on the wire** (an extra dit, a missing dah): the `KZU` lengths tell you which
  elements were sent. Line them up against what the paddle did (a recording of it, or a known
test pattern). The keyer's
  decision rules — and the recorded runs that pin them — are in `tests/test_paddlekeyersweeps.cpp`;
  reproduce the edge sequence there before changing the keyer.
- **Right on the wire, wrong on the air:** the K4's reproduction, not QK4. Check KZL is the value
  you set in Options → HaliKey → Remote Keying → Key-Down Delay. What KZL means:
  `docs/k4-protocol-quirks.md` §8, the canonical description of the KZ wire forms.
- **Sidetone delay or wobble:** the per-tone timing line is gone (real-time thread). An audio loopback
  of the sidetone against the paddle is the measurement; `qk4.audio.sidetone` still reports the
  output running dry, which raises the delay one block at a time.
- **MIDI on Windows:** WinMM delivers MIDI input in bursts — a press and release 60 ms apart can
  reach RtMidi about 1 ms apart. Arrival time is therefore a poor timestamp there, which is why the
  keyer uses MoMIDI's own timing when the HaliKey sends it. Never debounce MIDI on processing time:
  an earlier 3 ms gate did exactly that and dropped real releases, latching both paddles on. There
  is no electrical bounce in a MIDI message stream to protect against.
- **V1.4 edges arrive late:** FTDI-class bridges batch modem-status updates on a 16 ms latency
  timer by default — see `src/hardware/README.md` → "V1.4 serial latency".

## Why V1.4 polls instead of waiting for line events

`HaliKeyV14Worker::monitorLoop()` polls the modem lines every 1 ms on every OS (a high-resolution
waitable timer on Windows). **Do not go back to `WaitCommEvent`.** FTDI VCP drivers do not reliably
deliver `EV_CTS`/`EV_DSR`/`EV_RLSD` for an isolated transition: a release with no other line activity
often never woke the wait, so the key stayed down until an unrelated line change. Captured
signature: a dah held 2559 ms that "released" only when the next CTS press arrived, with no
modem-line change logged in between (the `raw pins` trace of the time). A blocking wait also cannot time the 20 ms quiet a straight key's
debounce needs.

The V1.4 pin map: `CTS` = dit lever (and a foot pedal, indistinguishable — which is why footswitch
PTT is withdrawn), `DCD ∥ DSR` = dah lever.
