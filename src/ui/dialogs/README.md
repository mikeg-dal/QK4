# ui/dialogs/

Top-level auxiliary windows. Most are non-blocking, but they are full windows or full-area overlays rather than inline widgets.

## Files

- `optionsdialog` — the **Options** window (opened from Tools → Settings…). Hosts the `ui/pages/` tab pages; `optionsdialog.cpp` adds them in order.
- `radiomanagerdialog` — the **Server Manager**: saved K4 connections, host/port/password, "Use TLS (Encrypted)", encode mode and streaming latency. Modified forms prompt with Save / Discard / Cancel before Add, selection changes, connecting or closing (including Escape and the globe toggle). The form snapshot includes every editable field; loading a saved or discovered radio resets it. Auto-discovery uses `K4Discovery` (`network/`), a UDP broadcast probe on the local network. It is not mDNS.
- `macrodialog` — the macro editor (Startup, PF1–4, Fn.F1–F8, REM ANT, KPOD tap/hold, keyboard F1–F12). A `QWidget` overlay that `PopupManager` sizes to cover the spectrum area.
- `textdecodewindow` — floating window showing the K4's own on-radio decoder output (CW / AFSK / FSK / PSK / DATA / SSB). One per receiver (Main / Sub). Not a `K4PopupBase` subclass; the header says why.

## Pattern

1. `OptionsDialog` is created on first open and kept for reuse: `m_optionsDialog` on MainWindow is `nullptr` until the Settings action's lambda creates it. The Server Manager is created by `MainWindow::showRadioManager()` on each open and deleted on close. `MacroDialog` and the two `TextDecodeWindow`s are built up front by `PopupManager` and `TextDecodeController`.
2. New styling uses the `K4Styles::Dialog::*` helpers; the older inline styles in `macrodialog`, `radiomanagerdialog` and `textdecodewindow` predate them.
3. Persist any user-edited state via `RadioSettings` (`settings/`).

## See also

- `ui/pages/README.md` — tab pages hosted by OptionsDialog.
- `settings/radiosettings.h` — persistence.
