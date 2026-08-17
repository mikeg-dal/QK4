# hardware/

USB / serial / MIDI device wrappers. Owned by `controllers/hardwarecontroller.cpp`.

## Files

- `kpoddevice.{cpp,h}` — KPOD tuning knob via hidapi. Main-thread (timing not critical). Device detection runs asynchronously at startup via `QTimer::singleShot(0, ...)` so the app window appears immediately; consumers observe `deviceInfoReady()` before reading `isDetected()`.
- `kpodplusdevice.{cpp,h}` — KPOD+ tuning knob + CW keyer via libusb. Encoder/buttons/rocker polling on the main thread; keyer output read on a dedicated worker thread. Configurable keyer parameters (speed, pitch, iambic mode, paddle orientation, encode mode, stuck timeout) sent to device on change.
- `halikeydevice.{cpp,h}` — HaliKey CW paddle. Delegates to one of 2 workers (selected by `deviceType`: 0 = V1.4 serial, 1 = MIDI); owns its own `m_workerThread`. Performs same-direction dedupe only — each worker is authoritative for its own debounce.
- `halikeyworkerbase.{cpp,h}` — Abstract base for the workers. `prepareShutdown()` is the escape hatch for the Linux variant's blocking ioctl.
- `kpodplususbworker.{cpp,h}` — owns the libusb context and handle for KPOD+, running all libusb I/O on its own QThread. Open/close and parameter setters are dispatched from the main thread as slots. Its pure command builders and decoders are covered by `test_kpodplususbworker`.
- `kpodudevworker.{cpp,h}` — Linux-only udev poll loop for KPOD hotplug detection, wired to `QThread::started` and run until `stop()`.
- `halikeyv14worker.{cpp,h}` — V1.4 hardware-protocol worker (serial). One `monitorLoop()` with three platform branches: `TIOCMIWAIT` + confirming re-read on Linux, 1 ms high-resolution-timer poll of `GetCommModemStatus` on Windows, 500 µs `usleep` poll of `TIOCMGET` on macOS. `DEBOUNCE_COUNT=2` on all three.
- `halikeymidiworker.{cpp,h}` — MIDI-variant worker. Notes 20 = dit, 21 = dah, 31 = PTT. Implements the MoMIDI extended protocol (CC ch 0 = version detect; CC elsewhere = timing MSB; note velocity = timing LSB). **The MoMIDI timestamps are parsed but currently discarded** — `m_pendingTimeMsb` is never read, and the worker emits plain `bool` edges.
- `iambickeyer.{cpp,h}` — Iambic A/B CW keyer state machine. `HighPriority` thread, atomic paddle state.

## Threading

- `IambicKeyer` on `HardwareController::m_keyerThread` (HighPriority).
- `SidetoneGenerator` on `HardwareController::m_sidetoneThread`.
- `HalikeyDevice` has its own `m_workerThread` for platform-worker variants.
- `KpodDevice` stays on the main thread.
- `KpodPlusDevice` polls on the main thread; keyer reader runs on `m_ep02Thread`.

Ten `new QThread` sites across the app; seven are in or adjacent to this directory — five in
`hardware/` (`kpoddevice`, `kpodhidworker`, `kpodplusdevice` ×2, `halikeydevice`) and two in
`hardwarecontroller.cpp` (keyer, sidetone). The remaining three are in `audiocontroller.cpp`,
`connectioncontroller.cpp`, and `dxclustercontroller.cpp`.

Live thread count is not the same as the site count: `kpodhidworker`'s udev hotplug thread is
`#ifdef Q_OS_LINUX`, and `dxclustercontroller` creates one thread *per cluster instance*.

## Keyer flow

HaliKey paddle → platform worker (thread) → IambicKeyer::setDitPaddle / setDahPaddle (atomics, DirectConnection) → IambicKeyer state machine (keyer thread) → KZ CAT commands out + SidetoneGenerator enqueue.

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
