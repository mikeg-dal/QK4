# ui/pages/

Tab pages for `OptionsDialog`. Each is a `QWidget` that plugs into the dialog's page list. The About page is built with the dialog; the others are created the first time they are selected (`OptionsDialog::ensurePageCreated`).

## Files

- `aboutpage` — the connected radio: ID, model, installed options and software versions.
- `stationpage` — call sign, grid square, operator name, QTH, and the IARU region that drives the panadapter band-plan overlay (plus its show/hide toggle). Persists via settings.
- `displaypage` — interface scale: Auto (fit the screen: 50–100% in 5% steps, or 125%) or a fixed 75–150%, with a Restart Now button, since Qt applies a scale only at startup. Persists via settings; applied by `settings/interfacescale`.
- `rigcontrolpage` — CAT server: status, client count, enable and port.
- `tciserverpage` — TCI server enable/port, audio on/off, TCI transmit level, live status and connected-client list. Talks to `TciController`.
- `audioinputpage` — microphone device and Mic Gain.
- `audiooutputpage` — speaker device and RX Audio Boost.
- `cwkeyerpage` — the "HaliKey" page: HaliKey + iambic keyer config, and the K4's remote key-down delay (KZL, via `CwController::setKeyDownInitialDelay`). Port lists come from `HaliKeyPortScan` off the GUI thread; the list shows "Scanning…" until they arrive.
- `dxclusterpage` — per-cluster host/port + auto-connect list, shared callsign/spot settings, and the cluster console, which takes all the spare height and does not wrap.
- `diagnosticspage` — the log file switch, Open Log Folder (`QDesktopServices::openUrl` on the folder) and Copy Path, the level (Warnings only / Normal / Everything), a debug switch per subsystem, and the bench diagnostics, which are not saved. Edits `RadioSettings` only; `main.cpp` applies the result through `Logging::apply()` on `loggingChanged`, so every switch is live. A note appears when `QT_LOGGING_RULES`, `QT_LOGGING_CONF` or `--log-rules` overrides the page. See `src/logging/README.md`.
- `kpa1500page` — KPA1500: status, enable, host/port, poll rate, Connect, and an Amplifier Info section.
- `kpodpage` — the "K-Pod" page: status, enable, device summary, and the KPOD+ Encode Mode (Element KZ / ASCII KX). Keyer speed, pitch, iambic mode and paddle orientation follow the K4 and have no control here. KPOD button actions are macros, set in `MacroDialog`.
- `optionspagelayout` — the one layout standard every page is built with: page margins and spacing, title, sections, `QFormLayout` forms with every platform-dependent property set explicitly, wrapped help text, field width caps, device lists sized by character count, the scroll area each page sits in, and the page list's style and repaint behaviour.

## Pattern

1. Inherit `QWidget`, provide a constructor that accepts any injected controller/state pointers.
2. Build the page with `OptionsPageLayout`: `createPage()`, `addTitle()`, one `addForm()` per page with `addRow()` / `addSection(form, …)` / `addFieldHelp()`, `addHelp()` for page-wide notes, and `layout->addStretch()` last. Never a bare `QLabel` for prose — `helpText()` wraps; an unwrapped sentence is as wide as itself and once made the Audio Input page wider than the dialog. Device lists go through `fitDeviceCombo()`; growable fields through `capWidth()`. `test_optionspagelayout` checks pages fit the dialog's minimum width.
3. Use `K4Styles::Dialog::*` helpers for control styling — not inline `QString("color: ...")`.
4. Save each setting via `RadioSettings` when it changes (on edit, toggle or `editingFinished`); load on construction. No page saves on hide or close.
5. Never enumerate hardware on the GUI thread (see `hardware/halikeyportscan`).
6. Forward "apply now" actions (e.g., device re-selection) directly to owning controller via injected pointer.

## See also

- `ui/dialogs/optionsdialog.cpp` — tab wiring.
- `ui/styling/k4styles.h` → `Dialog::` namespace for helpers.
- `settings/radiosettings.h` — persistence surface.
