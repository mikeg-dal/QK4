# ui/popups/

Popup widgets and their base class. Every popup here inherits from `K4PopupBase`, which lives in this directory. Two popups outside it inherit it too: `ui/widgets/featuremenubar` (ATTN / NB / NR / NOTCH) and `ui/widgets/nethealthpopup` (network metrics).

## Files

- `k4popupbase` — base class for every popup. Handles window flags, translucent background, placement (card centred directly above its anchor, clamped only at a screen edge), drop shadow and background painting, the `closed()` signal, and closing on click-away, Escape, its anchor going, the window moving or changing state and the app going inactive. Also hands out `contentPanel()`, the opaque surface content belongs on — see **Why content goes on the panel** below.
- `popupdismissal` — `PopupDismissal`, the rule for when a popup closes by itself: an application-wide event filter, installed while shown, that closes it on a press outside it, Escape, its anchor or the anchor's window hiding, and that window moving, resizing or changing state; plus the short reopen guard behind `isVisibleOrJustHidden()`. Used by `K4PopupBase` and by the side overlays in `ui/overlays/sidecontroloverlay`.

The popups, grouped by trigger. `rg -l ': public K4PopupBase' src/ui/popups` lists them.

- **Band / mode / display / Fn / button row** (5): `bandpopupwidget`, `modepopupwidget`, `displaypopupwidget`, `fnpopupwidget`, `buttonrowpopup` — main app popups. Most open from `BottomMenuBar`; the mode popup opens from the right panel's MODE key and the VFO A/B mode labels.
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
4. Build on `contentPanel()` — parent every content widget to it, put the layout on it, and use `panelMargins()` for that layout. A popup that still builds on itself uses `contentMargins()` instead and is simply not protected from the seams below.
5. Use `K4Styles::*` for all colors, gradients, and button styling.
6. Never implement shadow code — `K4PopupBase` handles this. For custom painting, override `paintContent()`, not `paintEvent()`.

## Why content goes on the panel

A popup is a `Qt::Tool` window carrying `Qt::WA_TranslucentBackground`, which the Qt docs say
"causes `WA_NoSystemBackground` to be set"; `QWidget::autoFillBackground` adds that windows "are
always filled with `QPalette::Window`, unless the `WA_OpaquePaintEvent` or `WA_NoSystemBackground`
attributes are set". **Nothing therefore erases a popup's buffer between repaints.** Every repaint
composites onto the pixels already there, so a child that leaves any of its own rect unpainted
leaves a seam that builds up with each repaint.

Two shapes of child do that, and both shipped: a `QLabel` or `QPushButton` whose sheet sets only
`color` and `font-size` (`K4Styles::popupValueLabel()` is `background: transparent`), and a
`QSlider` whose sheet puts a `margin` on its groove, leaving the strips either side painted by
nothing. On the bench these read as faint rectangles around a value that had just changed, and a
vertical line where a slider handle had swept.

`contentPanel()` is the fix: an opaque ancestor. Qt repaints a non-opaque child by first repainting
the nearest opaque ancestor beneath it, and the panel declares `WA_OpaquePaintEvent` and fills its
whole rect, so whatever a child leaves unpainted is the panel's colour and never stale pixels. It
also runs `paintContent()`, so a popup's painted decorations sit on the same opaque surface.

`EqPopupTests` (`tests/test_eqpopup.cpp`) holds the invariant for the popups that are on it.

## Ownership

- `controllers/popupmanager.cpp` — the primary family, plus `softwarelistpopup`.
- `controllers/modepopupcontroller.cpp` — `modepopupwidget`.
- `controllers/antennaconfigcontroller.cpp` — the three `antennacfgpopup` instances (Main RX, Sub RX, TX).
- `controllers/statusbarcontroller.cpp` — `confirmpopup`, for the remote power-off.

## See also

- `PATTERNS.md` → "Adding a Popup Menu".
- `ui/popups/k4popupbase.h` — base class reference.
