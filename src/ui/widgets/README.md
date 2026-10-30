# ui/widgets/

Top-level app widgets that aren't popups, pages, overlays, dialogs, or styling infrastructure. The "everything else" bucket.

## Files

### Main display
- `vfowidget`, `vforowwidget` — dual-VFO display (freq + mini-pan per VFO).
- `frequencydisplaywidget` — numeric frequency renderer (tabular figures, RIT/XIT aware).
- `filterindicatorwidget` — filter bandwidth indicator on VFO row.
- `txmeterwidget` — multifunction S / Po / ALC / COMP / SWR / Id meter. Scale marks carry the value they denote alongside their position on the bar (`ScaleMark`), so a mark cannot drift away from the value drawn there; `tests/test_meterscale.cpp` asserts the two agree.

### Menu bars + panels
- `bottommenubar` — MENU, Fn, DISPLAY, BAND, MAIN RX, SUB RX, TX, and PTT at the far right.
- `featuremenubar` — +/- control bar for ATTN / NB / NR / MANUAL NOTCH. A `K4PopupBase` (not a `Qt::Popup`), centred above the bottom menu bar.
- `sidecontrolpanel` — left panel: the six knob keys in three groups (WPM/PTCH or MIC/CMP by mode, PWR/DLY; BW/HI, SHFT/LO; M.RF/M.SQL, S.SQL/S.RF), the TX function keys (TUNE, XMIT, ATU TUNE, VOX, ANT, RX ANT), MON / NORM / BAL with their overlays, and the ? and globe (Server Manager) icons.
- `rightsidepanel` — right panel: the function key grids (PRE/ATTN … MODE/ALT, PF 1–4, FREQ ENT … SUB/DIVERSITY) and the KPA1500 mini-panel slot. The memory keys are not here; see `memorykeyrow`. As narrow as the left panel; wider, with the card's height reserved, only while the KPA1500 is enabled (`setKpa1500SlotReserved`).
- `kpa1500minipanel` — KPA1500 status + button slab for the right side panel.
- `sidepanelbutton` — the one builder for side-panel buttons (both panels, memory row, KPA1500 mini-panel): fixed size from the `K4Styles::Dimensions::Side*` tokens, light or dark sheet, measured orange sub-label, the standard two-column grid, `keyWidthFor()` (a key set's one width from its widest text plus `ButtonTextClearance`) and `addGap()` (a box-layout gap drawn at exactly its token, net of the layout's spacing).
- `memorykeyrow` — a namespace (`MemoryKeyRow::create`), not a widget class. MainWindow places the row; `MemoryButtonsController` wires the keys. The memory keys between the VFOs: M1–M4 over the MESSAGE bracket, REC / STORE / RCL with sub-labels. Two sets, each one width: REC/STORE/RCL from STORE's text, M1–M4 from what is left of the row.

### Small controls
- `dualcontrolbutton` — custom split-button for paired controls (the six knob keys): fixed `KnobButtonWidth`×`KnobButtonHeight`. The size is a fixed maintainer choice, not measured from the text; `measuredHeight` is the text's minimum, checked by a test.
- `controlgroupwidget` — labelled +/- control with an optional AUTO toggle.
- `togglegroupwidget` — pair toggle (e.g. APF on/off + bandwidth) with a third `bothClicked` region between the two buttons.
- `displaymenubutton` — dual-line nav button inside `DisplayPopupWidget`, one per DISPLAY choice.

### Status bar
- `nethealthwidget` — TCP health LED for the top status bar.
- `nethealthpopup` — `K4PopupBase` popup, opened and closed by a click on the LED, plotting the last 10 s of network metrics as three independently auto-scaled sparklines (RTT / JIT / BUF).
- `icontextlabel` — icon-plus-value composite used across the top status bar.
- `powerstatusbutton` — power icon that doubles as connection/power-state indicator and the click target for K4 remote power-off.

### Utility / misc
- `notificationwidget` — Toast notifications (K4 errors, etc.).

`WheelAccumulator` is **not** here — it lives in `src/utils/`, since it has no Qt UI dependency.

## Pattern

Most widgets here are either:
1. **Custom-painted** — override `paintEvent()`, use `QPainter` + `K4Styles::Fonts::paintFont()` + `K4Styles::buttonGradient()`.
2. **Composite Qt widgets** — `QHBoxLayout` / `QVBoxLayout` of child `QPushButton` / `QLabel` / `QSlider` with `K4Styles::*` stylesheets applied.

**Font sizing rule**: `setPixelSize()` with `K4Styles::Dimensions::FontSize*` — never `setPointSize()`. `docs/styling.md` explains why.

## See also

- `ui/styling/k4constants.h` — every visual constant. `ui/styling/k4styles.h` — the helpers.
- `docs/styling.md` — styling guide.
