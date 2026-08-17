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
`ConnectionController`, `AudioController`, `HardwareController`, `SpectrumController`,
`DxClusterController` — each owns a dedicated thread and a device/network boundary.
`CwController` orchestrates keying across the HardwareController-owned devices.

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
`TextDecodeController` — FT8/PSK decoder window lifecycle.

## Adding a new controller

See `PATTERNS.md` → "Controller Pattern":

1. Inject `RadioState *` and `ConnectionController *` via constructor (never singletons).
2. Expose **task-level** APIs, not owned widget getters (Architecture Rule 2).
3. Read-access via `const Type &` only (Architecture Rule 3).
4. Destructor's first line: `disconnect(this);` (Architecture Rule 11).
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
