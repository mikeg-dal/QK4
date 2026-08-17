# ui/pages/

Tab pages for `OptionsDialog`. Each is a `QWidget` that plugs into the dialog's tab list.

## Files

- `aboutpage` — version, license, credits.
- `stationpage` — call sign, grid square, operator name, QTH, and the IARU region that drives the panadapter band-plan overlay (plus its show/hide toggle). Persists via settings.
- `rigcontrolpage` — CAT server config + radio timing.
- `audioinputpage` / `audiooutputpage` — device selection + test.
- `cwkeyerpage` — HaliKey + iambic keyer config.
- `dxclusterpage` — per-cluster host/port/callsign + auto-connect list.
- `kpa1500page` — KPA1500 host/port config.
- `kpodpage` — KPOD button bindings + enable.

## Pattern

1. Inherit `QWidget`, provide a constructor that accepts any injected controller/state pointers.
2. Use `K4Styles::Dialog::*` helpers for form/label/combo/line-edit styling — not inline `QString("color: ...")`.
3. Save state via `RadioSettings` at page dismissal; load on construction.
4. Forward "apply now" actions (e.g., device re-selection) directly to owning controller via injected pointer.

## See also

- `ui/dialogs/optionsdialog.cpp` — tab wiring.
- `ui/styling/k4styles.h` → `Dialog::` namespace for helpers.
- `settings/radiosettings.h` — persistence surface.
