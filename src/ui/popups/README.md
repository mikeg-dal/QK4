# ui/popups/

Popup widgets and their base class. Every popup here inherits from `K4PopupBase`, which lives in this directory. Two popups outside it inherit it too: `ui/widgets/featuremenubar` (ATTN / NB / NR / NOTCH) and `ui/widgets/nethealthpopup` (network metrics).

## Files

- `k4popupbase` — base class for every popup. Handles window flags, translucent background, placement (card centred directly above its anchor, clamped only at a screen edge), drop shadow and background painting, the `closed()` signal, and closing on click-away, Escape, its anchor going, the window moving or changing state and the app going inactive.

The popups, grouped by trigger. `rg -l ': public K4PopupBase' src/ui/popups` lists them.

- **Band / mode / display / Fn / button row** (5): `bandpopupwidget`, `modepopupwidget`, `displaypopupwidget`, `fnpopupwidget`, `buttonrowpopup` — main app popups. Most open from `BottomMenuBar`; the mode popup opens from the right panel's MODE key.
- **Audio effects** (1): `rxeqpopupwidget` — RX EQ and TX EQ (two instances of one class).
- **Antenna config** (1): `antennacfgpopup` — per-VFO antenna masking.
- **Audio I/O** (4): `lineinpopup`, `lineoutpopup`, `micinputpopup`, `micconfigpopup`.
- **Modulation** (2): `voxpopup`, `ssbbwpopup`.
- **Keying** (1): `keyingweightpopup`.
- **Confirmation / info** (2): `confirmpopup` — reusable yes/no confirm in the K4 popup language, first used by the remote power-off flow; `softwarelistpopup` — display-only view of the K4's parsed firmware versions.

## Pattern

See `PATTERNS.md` → "Adding a Popup Menu":

1. Inherit from `K4PopupBase` (`ui/popups/k4popupbase.h`), never from `QWidget` directly.
2. Implement the pure-virtual `contentSize()`.
3. Call `initPopup()` last in the constructor.
4. Use `contentMargins()` for layout (handles shadow + content padding).
5. Use `K4Styles::*` for all colors, gradients, and button styling.
6. Never implement shadow code — `K4PopupBase` handles this. For custom painting, override `paintContent()`, not `paintEvent()`.

## Ownership

- `controllers/popupmanager.cpp` — the primary family, plus `softwarelistpopup`.
- `controllers/modepopupcontroller.cpp` — `modepopupwidget`.
- `controllers/antennaconfigcontroller.cpp` — the three `antennacfgpopup` instances (Main RX, Sub RX, TX).
- `controllers/statusbarcontroller.cpp` — `confirmpopup`, for the remote power-off.

## See also

- `PATTERNS.md` → "Adding a Popup Menu".
- `ui/popups/k4popupbase.h` — base class reference.
