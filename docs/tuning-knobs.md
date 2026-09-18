# External Tuning Knobs (RC-28 & FlexControl)

QK4 can drive three USB tuning knobs: the Elecraft **K-POD** (the original,
first-class device) and two third-party knobs added later — the **Icom RC-28**
and the **FlexRadio FlexControl**. All three share the same job: turn the knob to
tune and use nearby buttons for radio controls or macros. This document describes
how the RC-28 and FlexControl behave in QK4 and, crucially, **how each differs from the K-POD**,
because those differences drive every design choice in the code.

Implementation lives in `src/hardware/` (`rc28device`, `rc28hidworker`,
`flexcontroldevice`, `flexcontrolserialworker`), wired together in
`src/controllers/hardwarecontroller.cpp`. Both clone the K-POD's façade +
worker-thread pattern; there is no shared device interface.

---

## The K-POD baseline

For reference, this is the behavior the two new devices are measured against.

| Aspect | K-POD |
|---|---|
| Transport | USB HID (hidapi), VID `0x04D8` / PID `0xF12D` |
| Comms model | **Host-polled**: QK4 writes a `'u'` request every 20 ms and reads an 8-byte response |
| Encoder | Signed 16-bit tick count per poll (hardware accumulates between polls) |
| Tuning target | **Physical rocker switch** — 3 positions select VFO A / VFO B / RIT-XIT |
| Buttons | **8 buttons**, each with a hardware **tap vs. hold** distinction (hold bit in the report) |
| Button actions | All 8 × {tap, hold} = 16 macro slots (`K-pod.1T` … `K-pod.8H`) |
| Feedback | Rocker position is visible on the physical switch; no LEDs driven by QK4 |
| Hotplug | Linux: udev netlink monitor; macOS/Windows: 2 s `hid_enumerate` presence poll |
| Enable | Single `kpodEnabled` setting (also gates the KPOD+) |

The rocker and the 8 tap/hold buttons are the two features neither new device
can reproduce in hardware. Everything below flows from that.

---

## Icom RC-28

The RC-28 is a Raw-HID tuning knob with a large encoder and **three buttons
(F1, F2, TX)**, each backed by an indicator LED.

### Transport & comms

| Aspect | RC-28 | vs. K-POD |
|---|---|---|
| Transport | USB HID (hidapi), VID `0x0C26` / PID `0x001E`, 64-byte reports | Same library, different VID/PID and a much larger report |
| Comms model | **Interrupt-IN push**: the device streams reports; QK4 just `hid_read`s them (no request byte) | K-POD is host-*polled*; RC-28 is device-*pushed* |
| Handshake | On open, QK4 sends a `0x02` firmware-version request (mimics Icom's RS-BA1), which flips the device into its `0x01` report mode | K-POD needs no such handshake |
| Poll cadence | 10 ms read timer (draining all queued reports each tick) | K-POD polls at 20 ms |

### Report layout (reverse-engineered)

```
byte0  report type: 0x01 = encoder/button, 0x02 = firmware version
byte1  encoder acceleration / speed 1..4 (valid when byte5 == 0x07)
byte3  direction: 0x01 = CCW/down, 0x02 = CW/up, 0x00 = stopped
byte5  0x07 = encoder frame (no button); else a button code:
       0x7D = F1, 0x03 = F2, 0x06 = TX
```

Each movement report yields `±speed` ticks (direction × acceleration byte),
feeding the same frequency/RIT dispatch as the K-POD.

> **Caveat:** this layout comes from the community `gi1mic/rc28_emulator`
> project, **not** an official Icom specification. The encoder/button decode is
> plausible but the LED bitfield in particular is a guess. It needs validation
> against real hardware.

### Buttons: tap/hold is *synthesized*

The RC-28 report only says "button X is currently down" — there is **no hardware
tap-vs-hold bit** like the K-POD has. QK4 synthesizes the distinction in
`rc28hidworker.cpp` with a 500 ms timer: a release before 500 ms is a **tap**,
crossing 500 ms while held is a **hold**.

### Button assignments

Because there are only 3 buttons and no rocker, one button is spent on the
rocker substitute:

| Button | Action | Macro ID |
|---|---|---|
| **F1 tap** | Cycle tuning target (VFO A → VFO B → RIT/XIT) — *the rocker substitute* | *(reserved, not a macro)* |
| F1 hold | Macro | `RC-28.F1H` |
| F2 tap / hold | Macro | `RC-28.F2T` / `RC-28.F2H` |
| TX tap / hold | Macro | `RC-28.TXT` / `RC-28.TXH` |

That yields **5 macro slots** vs. the K-POD's 16.

### Tuning-target feedback

This is where the RC-28's LEDs earn their keep. After F1 cycles the target, QK4
lights the LED that names the current target:

- **F1 LED** on → knob tunes **VFO A**
- **F2 LED** on → knob tunes **VFO B**
- **TX LED** on → knob tunes **RIT/XIT**

QK4 also flashes a brief on-screen notice ("RC-28: VFO B"). (LED control is
best-effort given the reverse-engineered protocol; if it's wrong on real
hardware, tuning still works and the on-screen notice remains authoritative.)

### RC-28 vs. K-POD at a glance

- HID like the K-POD, but **pushed** rather than **polled**, and requires a
  version handshake to start streaming.
- **3 buttons vs. 8**; **tap/hold synthesized in software** vs. hardware-provided.
- **No rocker** → F1-tap cycles the target; **LEDs indicate** the target (a
  capability the K-POD path doesn't use).
- Fewer macro slots (5 vs. 16).

---

## FlexRadio FlexControl

The FlexControl is a CDC-ACM (virtual serial port) knob with **three AUX buttons
(AUX1, AUX2, AUX3)**, a separate switch in the center knob, and three AUX LEDs.
Every button distinguishes **short / double-click / long-hold** in firmware.

### Transport & comms

| Aspect | FlexControl | vs. K-POD |
|---|---|---|
| Transport | USB **CDC-ACM virtual serial** (`QSerialPort`), VID `0x2192` / PID `0x0010` | Not HID at all — a serial port |
| Comms model | **Push, ASCII, `;`-terminated tokens**; QK4 reads on `readyRead` and splits on `;` | K-POD is binary HID, host-polled |
| Line settings | 9600 8N1 (nominal — a CDC endpoint ignores baud) | K-POD has no line settings |
| Dependency | `Qt6::SerialPort` (already in the build for HaliKey) | K-POD uses hidapi |
| Hotplug | 2 s `QSerialPortInfo` presence poll (all platforms; no udev) | K-POD uses udev on Linux |

### Token protocol

```
U            one detent clockwise           → +1 tick
D            one detent counter-clockwise   → −1 tick
U02 .. U06   fast clockwise (accumulated)   → +N ticks
D02 .. D06   fast counter-clockwise         → −N ticks
X1S/X1C/X1L  AUX1: short / double / long
X2S/X2C/X2L  AUX2
X3S/X3C/X3L  AUX3
S/C/L         center knob: short / double / long
F0304        emitted on device reset        → ignored
```

The host controls the AUX LEDs with `Ixyz;`, where each digit is `1` or `0` for
AUX1, AUX2, and AUX3. For example, `I010;` lights only AUX2. Unlike the RC-28,
**tap classification is done by the device** — QK4 needs no timer.

The serial protocol was independently documented by AA6E from on-wire captures:
[FlexControl for Linux](https://blog.aa6e.net/2015/01/flexcontrol-for-linux-hamr.html).

### Button assignments

The AUX buttons provide direct, deterministic tuning-target selection plus
related radio actions:

| Button | Short | Double-click | Long |
|---|---|---|---|
| **AUX1** | Select VFO A | A/B swap (`SW41;`) | Toggle VFO A lock (`SW63;`) |
| **AUX2** | Select VFO B | Toggle Split (`SW145;`) | Toggle VFO B lock (`SW151;`) |
| **AUX3** | Select RIT/XIT | Clear offset (`SW64;`) | Cycle RIT → XIT → Off |
| **Center knob** | Cycle 1/10/100 Hz rate | Select 1 kHz rate | Macro (`FlexControl.KL`) |

The center-knob tuning-rate action applies to the most recently selected VFO,
including while AUX3 has RIT/XIT selected.

### Tuning-target feedback

QK4 lights exactly one AUX LED to show the active target: AUX1 for VFO A, AUX2
for VFO B, and AUX3 for RIT/XIT. It also shows a brief on-screen notice whenever
the selection changes.

### FlexControl vs. K-POD at a glance

- **Serial CDC**, not HID — an entirely different transport and library.
- **ASCII protocol** the device pushes, vs. binary polled reports.
- **3 AUX buttons plus a center switch**, each with **three press types**
  (short/double/long) classified by the device.
- **No rocker** → AUX1/2/3 directly select the target, with LEDs and an
  on-screen notice for feedback.
- One macro slot (center-knob long); the AUX gestures have built-in actions.

---

## Shared behavior (both new devices)

Everything the two new devices *do* share, and how it maps onto existing K-POD
machinery:

- **Same tuning dispatch.** Both feed `HardwareController::onKpodEncoderRotatedWithRocker()`
  — the exact function the K-POD uses. So frequency stepping (respecting the K4
  tuning step and VFO A/B locks) and RIT/XIT (`RU;`/`RD;`, `RU$;`/`RD$;` under
  BSET) behave identically to the K-POD.
- **Rocker substitute via a software tuning target.** Neither device has a
  rocker, so the target (VFO A = `2`, VFO B = `0`, RIT/XIT = `1`, matching the
  K-POD's rocker encoding) is held in `HardwareController`. RC-28 cycles it;
  FlexControl selects it directly with AUX1/2/3.
- **Macros** go through the same `MacroController` / Macros dialog as the K-POD.
  RC-28 exposes its assignable gestures there; FlexControl exposes center-knob
  long (`FlexControl.KL`).
- **Independent enable toggles.** `rc28Enabled` and `flexControlEnabled` are
  separate settings (unlike the single `kpodEnabled` that gates both K-POD
  variants). All three can be enabled and used simultaneously.
- **Options UI.** A single "Tuning Knobs" tab (`TuningKnobPage`) shows detection
  status, descriptor info, and the enable toggle for both devices.
- **Hotplug + auto-start** on the same signals as the K-POD (`deviceInfoReady` /
  `deviceConnected`), gated by the per-device enable setting.
- **udev rules** (`resources/99-kpod.rules`) grant non-root access on Linux:
  RC-28 (`0c26:001e`, hidraw) and FlexControl (`2192:0010`, tty).

## Feature comparison

| | K-POD | RC-28 | FlexControl |
|---|---|---|---|
| Transport | HID (polled) | HID (pushed) | Serial CDC (pushed) |
| Library | hidapi | hidapi | Qt SerialPort |
| Encoder | signed tick count | dir × accel (1–4) | ±1 or ±N tokens |
| Tuning target select | physical rocker | F1 tap | AUX1/2/3 direct |
| Target feedback | physical switch | 3 LEDs + notice | 3 LEDs + notice |
| Buttons | 8 | 3 | 3 AUX + center knob |
| Press types | tap / hold (hardware) | tap / hold (software timer) | short / double / long (device) |
| Macro slots | 16 | 5 | 1 |
| Enable setting | `kpodEnabled` | `rc28Enabled` | `flexControlEnabled` |
| Protocol source | vendor | reverse-engineered ⚠ | reverse-engineered ⚠ |

## Known limitations

- **RC-28 protocol is reverse-engineered.** Encoder/button decode is plausible;
  the LED bitfield and exact button codes need on-hardware verification. Tuning
  works regardless of LED correctness.
- **Fewer macro slots than the K-POD.** RC-28 leaves five gestures assignable;
  FlexControl reserves its AUX gestures for cohesive tuning controls and leaves
  center-knob long assignable.
