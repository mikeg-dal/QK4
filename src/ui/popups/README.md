# ui/popups/

Popup widgets. All inherit from `K4PopupBase` (in `ui/styling/`).

## Files

Grouped by trigger:

- **Band / mode / display / Fn / button row** (5): `bandpopupwidget`, `modepopupwidget`, `displaypopupwidget`, `fnpopupwidget`, `buttonrowpopup` — main app popups, triggered from `BottomMenuBar`.
- **Audio effects** (1): `rxeqpopupwidget` — RX EQ.
- **Antenna config** (1): `antennacfgpopup` — per-VFO antenna masking.
- **Audio I/O** (4): `lineinpopup`, `lineoutpopup`, `micinputpopup`, `micconfigpopup`.
- **Modulation** (2): `voxpopup`, `ssbbwpopup`.
- **Keying** (1): `keyingweightpopup`.
- **Confirmation / info** (2): `confirmpopup` — reusable yes/no confirm in the K4 popup language, first used by the remote power-off flow; `softwarelistpopup` — display-only view of the K4's parsed firmware versions.

## Pattern

See `PATTERNS.md` → "Adding a Popup Menu":

1. Inherit from `K4PopupBase` (in `ui/styling/k4popupbase.h`), never from `QWidget` directly.
2. Implement the pure-virtual `contentSize()`.
3. Call `initPopup()` last in the constructor.
4. Use `contentMargins()` for layout (handles shadow + content padding).
5. Use `K4Styles::*` for all colors, gradients, and button styling.
6. Never implement shadow code — `K4PopupBase` handles this.

## Ownership

Most popups are owned by `controllers/popupmanager.cpp` (primary family) or a specialized controller (`modepopupcontroller`, `featuremenucontroller`, `antennaconfigcontroller`).

## See also

- `PATTERNS.md` → "Adding a Popup Menu".
- `ui/styling/k4popupbase.h` — base class reference.
