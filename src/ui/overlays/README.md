# ui/overlays/

Widgets that paint on top of other widgets. Not popups — overlays are not self-dismissing; they're driven by app state.

## Files

- `menuoverlay` — Full-screen MEDF menu overlay. Driven by `MenuController`.
- `sidecontroloverlay` — Base class for the two mini overlays below. Child widgets over the side panel's knob keys, closed by their own key and by the popup rule (`ui/popups/popupdismissal`): a press outside, Escape, the panel hiding, the window moving or resizing. `showOverGroup()` refuses to reopen during the press that just closed it, so the panel's toggle on `isVisible()` cannot reopen it.
- `monoverlay` / `baloverlay` — MON / BAL side-control mini overlays.
- `mousevfoindicator` — small cursor-follow badge on the panadapter. Colours itself cyan (VFO A) or green (VFO B) while the pointer is over it. Purely visual: no CAT, no RadioState.
- `dxspotoverlay` — DX cluster spots painted onto the panadapter. Clicking a spot label emits the frequency for click-to-tune (left → VFO A, right → VFO B).

## Pattern

Overlays typically:
1. Take a pointer to their host widget as parent (for paint-coordinate mapping).
2. Get their data from their owner or from RadioState signals; re-render via `update()`.
3. Don't own CAT dispatch — pure presentation.

## Ownership

- `menuoverlay` → `MenuController`.
- `mon/bal` overlays → `SideControlPanel` (host widget).
- `mousevfoindicator` + `dxspotoverlay` → created and fed by `SpectrumController`, as children of each `PanadapterRhiWidget`.
