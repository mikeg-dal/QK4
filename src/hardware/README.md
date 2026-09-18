# hardware/

USB / serial / MIDI device wrappers. Owned by `controllers/hardwarecontroller.cpp`.

## Files

- `usbdevicelifecycle.h` — Header-only, Qt-free policy shared by the KPOD and KPOD+: given `{present, open, enabled}` and an event (detected / lost / opened / open-failed / enabled / disabled / shutting down), decides whether to open, close, report or notify. **One owner of "should this device be open"** — it used to be decided independently in the worker and in `HardwareController`, which is how a disabled KPOD+ got opened anyway (USB-002). Effects are derived from state EDGES, so the duplicate removals both devices emit per unplug report once. Covered by `test_usbdevicelifecycle`.
- `kpoddevice.{cpp,h}` — Thin Qt façade over `KpodHidWorker`; owns `m_hidThread` and re-emits the worker's signals. **No `hid_*` call lives here** — see the hidapi section below. Detection runs on the worker thread as soon as it starts, so the window appears immediately; consumers observe `deviceInfoReady()` before reading `isDetected()`.
- `kpodplusdevice.{cpp,h}` — Façade for the KPOD+ tuning knob + CW keyer. Owns **two** threads: `m_usbThread` (EP01 encoder/buttons/rocker polling and every config command) and `m_ep02Thread` (the keyer reader). Nothing here touches libusb. Configurable keyer parameters (speed, pitch, iambic mode, paddle orientation, encode mode, stuck timeout) are dispatched to the USB worker on change.
- `halikeydevice.{cpp,h}` — HaliKey CW paddle. Delegates to one of 2 workers (selected by `deviceType`: 0 = V1.4 serial, 1 = MIDI); owns its own `m_workerThread`. Performs same-direction dedupe per line only — each worker is authoritative for its own debounce — then re-emits all three confirmed lines as one `lineStateChanged(dit, dah, ptt)` sample.
- `halikeyworkerbase.{cpp,h}` — Abstract base for the workers. Defines the single input signal, `lineStateChanged(dit, dah, ptt)`. `prepareShutdown()` is the escape hatch for the Linux variant's blocking ioctl.

  **The three lines travel together and must stay that way.** Iambic decisions are a function of the paddle *pair*, and `readPinState()` already samples every line in one call. When that sample was split into per-line signals, a released squeeze reached the keyer as two events — if the element timer fired between them one lever still read down, and the keyer appended an element the operator never sent. Do not reintroduce per-line paddle signals.
- `kpodplususbworker.{cpp,h}` — owns the libusb context and handle for KPOD+, running all libusb I/O on its own QThread. Open/close and parameter setters are dispatched from the main thread as slots. Its pure command builders and decoders are covered by `test_kpodplususbworker`.
- `kpodudevworker.{cpp,h}` — Linux-only udev poll loop for KPOD hotplug detection, wired to `QThread::started` and run until `stop()`.
- `halikeyv14worker.{cpp,h}` — V1.4 hardware-protocol worker (serial). One `monitorLoop()` with three platform branches: `TIOCMIWAIT` + confirming re-read on Linux, 1 ms high-resolution-timer poll of `GetCommModemStatus` on Windows, 500 µs `usleep` poll of `TIOCMGET` on macOS. `DEBOUNCE_COUNT=2` on all three.
- `halikeymidiworker.{cpp,h}` — MIDI-variant worker. Notes 20 = dit, 21 = dah, 31 = PTT. Implements the MoMIDI extended protocol (CC ch 0 = version detect; CC elsewhere = timing MSB; note velocity = timing LSB). **The MoMIDI timestamps are parsed but currently discarded** — `m_pendingTimeMsb` is never read, and the worker emits plain `bool` edges.
- `iambickeyer.{cpp,h}` — Iambic A/B CW keyer state machine. `HighPriority` thread, atomic paddle state.

## Threading

- `IambicKeyer` on `HardwareController::m_keyerThread` (HighPriority).
- `SidetoneGenerator` on `HardwareController::m_sidetoneThread`.
- `HalikeyDevice` has its own `m_workerThread` for platform-worker variants.
- `KpodDevice` owns `m_hidThread`; all hidapi I/O is on it. On Linux `KpodHidWorker` starts a second thread for the udev hotplug poll.
- `KpodPlusDevice` owns `m_usbThread` (EP01 + config) and `m_ep02Thread` (keyer reader, HighPriority).

**Neither device polls on the main thread.** Both used to, and the READMEs and header comments said so
long after they stopped (USB-011); if a comment here claims main-thread USB I/O, it is stale, not a
rule.

Eleven `new QThread` sites across the app; seven are in or adjacent to this directory — five in
`hardware/` (`kpoddevice`, `kpodhidworker`, `kpodplusdevice` ×2, `halikeydevice`) and two in
`hardwarecontroller.cpp` (keyer, sidetone). The remaining four are in `audiocontroller.cpp`,
`connectioncontroller.cpp`, `dxclustercontroller.cpp` and `tcicontroller.cpp`.

Count them with `rg -c 'new QThread' src --glob '*.cpp'` rather than trusting this line — it has been
wrong before.

Live thread count is not the same as the site count: `kpodhidworker`'s udev hotplug thread is
`#ifdef Q_OS_LINUX`, and `dxclustercontroller` creates one thread *per cluster instance*.

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

HaliKey paddle → platform worker (thread) → `HalikeyDevice::lineStateChanged` → CwController demux → `IambicKeyer::setPaddleState` (one packed atomic, DirectConnection) → IambicKeyer state machine (keyer thread) → KZ CAT commands out + SidetoneGenerator enqueue.

When KPOD+ is active, the HaliKey → IambicKeyer → KZ/Sidetone path is suppressed. KPOD+ owns the entire CW chain: paddle → onboard keyer → sidetone → KZ output forwarded directly to K4.

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
- `docs/halikey-cw-trace.md` — end-to-end paddle-to-wire trace.
- `docs/halikey-midi-windows-debounce-bug.md` — the MIDI stuck-paddle bug and its regression test.
