# hardware/

USB / serial / MIDI device wrappers. Owned by `controllers/hardwarecontroller.cpp`.

## Files

- `usbdevicelifecycle.h` — Header-only, Qt-free policy shared by the KPOD and KPOD+: given `{present, open, enabled}` and an event (detected / lost / opened / open-failed / enabled / disabled / shutting down), decides whether to open, close, report or notify. **One owner of "should this device be open"** — it used to be decided independently in the worker and in `HardwareController`, which is how a disabled KPOD+ got opened anyway (USB-002). Effects are derived from state EDGES, so the duplicate removals both devices emit per unplug report once. Covered by `test_usbdevicelifecycle`.
- `kpodhidworker.{cpp,h}` — `KpodHidWorker`, on `KpodDevice::m_hidThread`. Owns the hidapi handle and runs every `hid_*` call: detection, polling, and on Linux the udev hotplug thread. Public slots are dispatched from the façade by queued connection.
- `halikey_midi_ports.h` — header-only, no RtMidi dependency. Finds the HaliKey MIDI port by case-insensitive name containment, not equality: the WinMM backend appends the port index to the name, and Windows renumbers ports when any MIDI device is removed.
- `kpoddevice.{cpp,h}` — Thin Qt façade over `KpodHidWorker`; owns `m_hidThread` and re-emits the worker's signals. **No `hid_*` call lives here** — see the hidapi section below. Detection runs on the worker thread as soon as it starts, so the window appears immediately; consumers observe `deviceInfoReady()` before reading `isDetected()`.
- `kpodplusdevice.{cpp,h}` — Façade for the KPOD+ tuning knob + CW keyer. Owns **two** threads: `m_usbThread` (EP01 encoder/buttons/rocker polling and every config command) and `m_ep02Thread` (the keyer reader). Nothing here touches libusb. Configurable keyer parameters (speed, pitch, iambic mode, paddle orientation, encode mode, stuck timeout) are dispatched to the USB worker on change.
- `halikeydevice.{cpp,h}` — CW input façade. Delegates to one of three workers (selected by `deviceType`: 0 = HaliKey V1.4 serial, 1 = HaliKey MIDI, 2 = Vail MIDI); owns its own `m_workerThread`. Passes input straight through, with no debounce or queue of its own: `paddleChanged` from the V1.4 worker, and the MIDI sink set by `setMidiSink`.
- `vailmidiworker.{cpp,h}` — Vail input/output MIDI worker. Configures the adapter's onboard speed, pitch and Iambic A/B mode, captures incoming edge times before queuing, and pairs notes into timed KZD/KZU commands. A hold timeout and disconnect emit matching cleanup; generation-tagged routes reject stale key-downs after source or connection changes.
- `vail_midi_protocol.h` — Pure Vail MIDI port pairing, firmware configuration and timestamped keying state, covered by `test_vailmidiprotocol`.
- `vail_cat_routing.h` — I/O-thread ownership of forwarded Vail/software key-downs and their matching cleanup, covered by `test_vailcatrouting`.
- `vail_sidetone_routing.h` — Audio-thread eligibility and generation policy for Vail computer sidetone, covered by `test_vailsidetonerouting`.
- `cwmonitormute.h` — Shared I/O-thread CW monitor mute/restore policy for accepted HaliKey and Vail keying. Preserves ML0 across temporary zero echoes and restores after the queued KZL/keying tail. Covered by `test_cwmonitormute` and the real-link `test_cwmonitorrouting`.
- `halikeyworkerbase.{cpp,h}` — Abstract base for the workers. Declares `paddleChanged` (V1.4) and `HaliKeyMidiSink` (MIDI). `prepareShutdown()` runs on the main thread before the join; the MIDI worker closes its port there so RtMidi's callback thread stops.
- `kpodplususbworker.{cpp,h}` — owns the libusb context and handle for KPOD+, running all libusb I/O on its own QThread. Open/close and parameter setters are dispatched from the main thread as slots. Its pure command builders and decoders are covered by `test_kpodplususbworker`.
- `kpodudevworker.{cpp,h}` — Linux-only udev poll loop for KPOD hotplug detection, wired to `QThread::started` and run until `stop()`.
- `kpodudevmatch.h` — header-only, platform-neutral: whether a udev event is the KPOD, from the event's `PRODUCT` (fallback `ID_VENDOR_ID`/`ID_MODEL_ID`) properties. Sysattrs are gone by the time a `remove` arrives, which is why an unplug was never reported. Covered by `test_usbdevicelifecycle`.
- `halikeyv14worker.{cpp,h}` — V1.4 hardware-protocol worker (serial). Opens the port natively, powers the contacts with DTR/RTS, and polls the lines every 1 ms on every platform on a drift-free 1 ms grid (a periodic high-resolution waitable timer on Windows, absolute-deadline sleeps through `SteadyKeyerClock::sleepUntil` elsewhere) through `SerialLinePoller`. Dit = CTS, dah = DCD ∥ DSR. Emits `paddleChanged(dah, asserted, delta)`. Polling, not edge waits, on every OS: a blocking wait can't time a straight key's 20 ms debounce, and FTDI drivers on Windows miss isolated modem-line events.
- `halikeymidiworker.{cpp,h}` — MIDI-variant worker. Notes 20 = dit, 21 = dah, 31 = PTT (ignored). Every raw message also goes straight from RtMidi's thread to the `HaliKeyMidiSink` that `HalikeyDevice::setMidiSink` sets. CwController points it at `PaddleKeyer::midiMessage`, which decodes MoMIDI's own time stamps (note velocity = ms since the previous event, CC/poly aftertouch on channel 0 = 126 ms steps, velocity 0/127 = no time, use RtMidi's delta). Notes on any channel are normalised to channel 0; for a non-MoMIDI device a note-on with velocity 0 is passed as a note-off.
- `halikeyportscan.{cpp,h}` — Lists HaliKey serial or MIDI ports on a short-lived worker thread and delivers them queued to the caller's thread, for the Options page. The first CoreMIDI enumeration in a process took 0.8–1.9 s on macOS, and SetupAPI COM enumeration is slow on Windows with Bluetooth serial ports; both used to run on the GUI thread. One scan at a time: a repeat request is absorbed, a request for the other device type replaces the outdated scan. `chooseIndex()` is the saved-port / first-HaliKey-MIDI selection rule.
- `paddlekeyer.{cpp,h}` — Port of K4-Companion's keyer (Dale Farnsworth, GPL-3.0): Iambic A/B, Plain Iambic, Ultimatic and straight-key passthrough, sent as timed `KZD`/`KZU`. Paddle edges timestamped at capture (MoMIDI's own clock, the 1 ms serial poll); decisions read the paddles as of when they were due. Pure logic behind `keyertiming.h` seams; pinned by Dale's 564 recorded runs (`tests/test_paddlekeyersweeps.cpp`, data in `tests/data/paddlekeyer/`).
- `cwgate.h` — `CwGate::`, header-only: when the keyer is inhibited (radio not ready, not CW, KPOD+ owns CW) and when the sidetone holds its output stream open (not inhibited and a HaliKey open). CwController applies both. Covered by `test_cwgate`.
- `keyertiming.h` — The Clock / Link / OneShotTimer seams the keyer runs against, and `pyRound` (half-to-even, as Python's `round`).
- `keyerruntime.{cpp,h}` — Production seams: `SteadyKeyerClock` (steady_clock; sleeps to absolute deadlines: `mach_wait_until` on macOS, `clock_nanosleep(TIMER_ABSTIME)` with 1 ns timer slack on Linux, a high-resolution waitable timer on Windows), `ForwardingLink` (keyer → `TcpClient`'s tagged software `KzEgress` link, an atomic pointer set by `CwController`), `QtOneShotTimer` (the keyer-local monitor seam, disabled in production because the shared I/O policy owns restoration).
- `seriallinepoller.{cpp,h}` — Port of K4-Companion's serial line poll, run by the V1.4 worker: rest state at open is not an edge, paddles reported on the first poll that sees them, 20 ms straight-key debounce (dah line ignored), per-line deltas. A poll returns its changes in place and never allocates.

## Threading

- `PaddleKeyer::run()` on `HardwareController::m_keyerThread` (promoted to real-time by `RtScheduling::promoteCurrentThread` (see `src/utils/README.md`); a `QThread::create` with no event loop: the keyer blocks on its own event queue and sleeps to its deadlines).
- `SidetoneGenerator` on `HardwareController::m_sidetoneThread` (promoted to real-time by `RtScheduling::promoteCurrentThread` (see `src/utils/README.md`)); a 1 ms timer there pushes one 4 ms block at a time, keeping the sink's queue at the shortest level the device sustains (starts at 8 ms, rises a block per dry-out). Only while `CwGate::sidetoneStreamWanted`: with no keyable CW the sink is closed and nothing runs.
- MIDI paddle edges reach the keyer on RtMidi's own thread; V1.4 edges on the HaliKey worker thread.
- `HalikeyDevice` has its own `m_workerThread` ("HaliKey" in log lines) for the V1.4 and MIDI workers. The V1.4 worker promotes the thread to real-time once its port is open, before the 1 ms poll starts; the MIDI worker's thread (presence poll only) is left at normal priority. RtMidi's own callback thread is not ours and is not promoted.
- Each real-time thread promotes itself, because only the calling thread can be given a QoS class on macOS or join MMCSS on Windows; `QThread::HighPriority` is not used (a no-op on Linux, and on macOS its scheduling parameters opt the thread out of QoS). The outcome cannot be logged by the thread (CONVENTIONS Rule 16); `HardwareController::reportThreadScheduling()` logs it at info under `qk4.hardware` two seconds after startup and when the HaliKey opens, e.g. `Keyer thread: asked for real-time, got real-time - Mach time-constraint policy (1 ms period, 0.2 ms computation)`.
- `HaliKeyPortScan` runs each port scan on a short-lived `QThread::create` thread and delivers the list queued to the caller's thread.
- `KpodDevice` owns `m_hidThread`; all hidapi I/O is on it. On Linux `KpodHidWorker` starts a second thread for the udev hotplug poll.
- `KpodPlusDevice` owns `m_usbThread` (EP01 + config) and `m_ep02Thread` (keyer reader, real-time, promoted in `KpodPlusEp02Worker::run()`; with no KPOD+ open it waits on a condition variable that `setDeviceHandle()` and `requestStop()` signal, so it costs nothing on a machine without one).

**Neither device polls on the main thread.** Both used to, and the READMEs and header comments said so
long after they stopped (USB-011); if a comment here claims main-thread USB I/O, it is stale, not a
rule.

Threads are created two ways: `new QThread` (with `moveToThread`) and `QThread::create` (a function
run with no event loop). Count both with:

```bash
rg -c 'new QThread|QThread::create' src --glob '*.cpp'
```

In this directory: `kpoddevice`, `kpodhidworker`, `kpodplusdevice` ×2, `halikeydevice` and
`halikeyportscan`. In `hardwarecontroller.cpp`: the keyer (`QThread::create`) and the sidetone thread.
The rest are in `audiocontroller.cpp`, `connectioncontroller.cpp`, `dxclustercontroller.cpp` and
`tcicontroller.cpp`.

Live thread count is not the same as the site count: `kpodhidworker`'s udev hotplug thread is
`#ifdef Q_OS_LINUX`, `dxclustercontroller` creates one thread *per cluster instance*, and a port-scan
thread exists only while a scan runs.

## Who decides a device is open

`usbdevicelifecycle.h`, and nothing else. `{present, open, enabled}` plus an event in, effects out;
`HardwareController` holds one `State` per device and applies what comes back.

The rule the workers must keep: **a worker reports, it does not decide.** `KpodPlusUsbWorker`'s
presence timer used to call `openDevice()` itself, which is how a KPOD+ with "Enable K-Pod" unchecked
was opened and polled anyway (USB-002) — every enable check in the app lived in `HardwareController`,
and that one path went around all of them. Both workers now emit arrival/loss and wait.

Three things that look like one and are not:

| | |
|---|---|
| **detected** | enumerated on the bus |
| **enabled** | the operator's setting |
| **live** (`State::live()`) | we hold a handle and are polling it |

Only *live* means the device is doing anything. The CW gate follows it (USB-003), and so does the
Options page — both used to follow *detected*, which reported a KPOD+ as owning CW keying while it sat
closed and switched off.

## Keyer flow

- **V1.4:** paddle → V1.4 worker's 1 ms poll → `SerialLinePoller` → `HalikeyDevice::paddleChanged` → `CwController::onPaddle` (paddle reversal; DirectConnection) → `PaddleKeyer::paddleEvent`, stamped on arrival.
- **MIDI:** paddle → RtMidi callback → `HaliKeyMidiSink` → `PaddleKeyer::midiMessage`, stamped by MoMIDI's own clock.
- **Keyer thread:** `PaddleKeyer::run()` → timed `KZD####` / `KZU####` → `ForwardingLink` → `KzEgress` (a lock-free queue the I/O thread drains and writes; see `src/network/README.md`). Software key-down/up ordering stays on the keyer thread: `releasePaddles()`, the inhibit and `close()` from the main thread post requests to its queue, so a straight key's KZU always follows its KZD. The main thread takes no lock the keyer or a paddle thread waits on: configuration is atomic, requests go into rings only requesters lock, the keyer wakes on an `RtSemaphore`, and the keyer itself carries out a release (stamping the paddles up as of the moment it was asked for) at its next decision, including any wait to cancel a scheduled sidetone edge. Sidetone edges are scheduled on `SidetoneScheduler`, and `SidetoneGenerator`'s paced push places them on exact frames.

- **Vail:** onboard keyer/buzzer → RtMidi capture timestamp → Vail worker → generation-tagged I/O route → KZD/KZU. It bypasses `PaddleKeyer`. The optional computer tone follows the same captured edges through `LiveSidetone`.
- **Monitor:** accepted HaliKey and Vail commands share `CwMonitorMute::State` on I/O. It sends ML0000 before a key-down and restores the saved CW monitor level after replay has finished; a new down cancels the pending restoration. KPOD+ batches pass through unchanged.

The keyer keys nothing while CwController holds it inhibited: radio not ready, mode not CW or CW-R, or the KPOD+ owning CW. KPOD+ owns its whole chain: paddle → onboard keyer → its own sidetone → KZ strings forwarded to the K4 untouched (the EP02 reader posts each batch to the same `KzEgress`).

No per-edge, per-note or per-batch log line is written on the HaliKey worker, RtMidi, keyer or EP02
threads (CONVENTIONS Rule 16). The CAT traffic bench item logs every KZ frame from the I/O thread
as it is written, with how long it waited since the keying thread posted it.

## V1.4 serial latency (USB-serial bridge latency timer)

FTDI-class USB-serial bridges batch modem-status (CTS/DSR/DCD) updates on a driver-side
latency timer that defaults to **16 ms**. That sits *upstream* of QK4's sub-millisecond-to-1 ms
poll (`halikeyv14worker.cpp`), so paddle edges can reach the app up to 16 ms late regardless of
application code. Lowering it to 1 ms removes that delay:

- **Windows**: Device Manager → Ports (COM & LPT) → the HaliKey COM port → Properties →
  Port Settings → Advanced → Latency Timer (msec) = **1**. Confirmed effective by a user
  in the field.
- **Linux**: `echo 1 | sudo tee /sys/bus/usb-serial/devices/<dev>/latency_timer` — resets
  on re-plug; use a udev rule for persistence.
- **macOS**: not user-tunable with the stock driver.

The MIDI variant is unaffected (no serial bridge in the path).

## hidapi: Linux is not like macOS and Windows

**All hidapi calls live in `kpodhidworker.cpp` (`KpodHidWorker`), on its own thread.** Not in
`kpoddevice.cpp` — that is a thin signal-forwarding wrapper whose only mention of `hid_` is a
comment.

| Platform | Backend | `hid_open()` | `hid_init()` / `hid_exit()` |
|---|---|---|---|
| macOS | IOHIDManager | works | safe per-function |
| Windows | WinAPI | races on stale paths | safe per-function |
| Linux (Pi) | libusb | fails without root | **not safe** — see below |

The libusb backend keeps one global context. Calling `hid_exit()` while any handle is open
invalidates that handle, and the next poll aborts on a destroyed mutex
(`usbi_mutex_destroy: Assertion 'pthread_mutex_destroy(mutex) == 0' failed`). macOS and Windows have
no shared context, so per-function init/exit is harmless there.

Rules — violating these crashes the Pi build:

- **Never call `hid_exit()` on Linux while a handle is open.** Init once at construction, exit once
  at destruction, guard per-function calls with `#ifndef Q_OS_LINUX`.
- **Never use `hid_open(VID, PID)` on Linux** — it needs root on `/dev/hidraw*`. Use
  `hid_open_path()` with the path found during enumeration; it is more reliable everywhere.
- **Keep the Windows `hid_open()` VID/PID fallback** in the detect retry loop — it recovers stale
  device paths that `hid_open_path()` cannot.
- **Do not "unify" the macOS/Windows lifecycle with Linux.** Their backends are fine as-is.

## See also

- `docs/k4-protocol-quirks.md` → "KZ keying protocol" — the wire commands, including the 0x20
  letter-space byte that Elecraft's PDF renders misleadingly as an underscore.
- `docs/halikey-cw-trace.md` — end-to-end paddle-to-wire trace, and why V1.4 polls.
