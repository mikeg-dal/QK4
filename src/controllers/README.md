# controllers/

Each controller owns a cohesive slice of the UI or domain: widgets + signal wiring + CAT dispatch +
slice-specific state.

**MainWindow coordinates controllers; it does not reach into them.**

## Find a controller

See `docs/controllers.md` — symptom → controller file map, and the **canonical inventory**. This
file describes the shape of the layer; that one lists every controller. Deliberately not duplicated
here: two hand-maintained lists drift apart, and this one already had.

## Groups

### Core I/O owners
`ConnectionController`, `AudioController`, `HardwareController`, `DxClusterController`, `TciController`
— each owns a device or network boundary and the threads behind it:

- `ConnectionController` — the I/O thread (TcpClient + Protocol).
- `AudioController` — the audio thread.
- `HardwareController` — the sidetone thread and the keyer thread. The USB and serial devices it owns
  start their own worker threads (see `hardware/README.md`).
- `DxClusterController` — one thread per cluster connection.
- `TciController` — the TCI server thread.

`rg -n 'new QThread|QThread::create' src/controllers` lists them.

### Main-thread coordinators
`SpectrumController` (panadapters, span, click/scroll tune), `CwController` (keying across the
HardwareController-owned devices) and `TransmitController` (the single owner of "are we
transmitting") own no thread.

### Popup family
`PopupManager`, `ModePopupController`, `FeatureMenuController`, `AntennaConfigController`,
`MenuController` — lifecycle + coordination for popup widgets.

### Signal dispatch
`ButtonRowDispatcher`, `MacroController`, `BandNavigationController`, `MemoryButtonsController` —
routing layer for click/macro/band/memory events.

### Display / indicator controllers
`StatusBarController`, `ModeLabelController`, `VfoFrequencyController`, `AntennaDisplayController`,
`ProcessingDisplayController`, `SideControlDisplayController`, `SideControlScrollController`,
`VfoRowIndicatorController`, `SubDivIndicatorController`, `TxStateController`, `RitXitController`,
`RightSideController`, `KPA1500UiController` — observe RadioState and render.

### Text decode
`TextDecodeController` — lifecycle of the windows that show the K4's own on-radio decoder (CW / AFSK / FSK / PSK / DATA / SSB).

## Adding a new controller

See `PATTERNS.md` → "Controller Pattern":

1. Inject `RadioState *` and `ConnectionController *` via constructor (never singletons).
2. Expose **task-level** APIs, not owned widget getters (Architecture Rule 2).
3. Read-access via `const Type &` only (Architecture Rule 3).
4. Destructor cuts the incoming connections that can still fire while it runs — `sender->disconnect(this)` or a stored `QMetaObject::Connection` — and nothing else; bare `disconnect(this)` only cuts self-connections (Architecture Rule 11).
5. Add to the symptom table in `docs/controllers.md`.

## Banned shapes

`PATTERNS.md` → "Anti-patterns (banned going forward)":

1. Adding new feature code to `MainWindow`.
2. `MainWindow` as signal middleman.
3. Inline lambdas > 5 lines or > 5 connect() calls clustered at one site.
4. Cross-controller reach-in (`controllerA->getter()->doThing()`).
5. Hidden state in globals or singletons.

## See also

- `docs/controllers.md` — symptom → file map.
- `PATTERNS.md` → Controller Pattern, Direct Observation.
- `CONVENTIONS.md` → Architecture Rules 2, 3, 11, 12.
