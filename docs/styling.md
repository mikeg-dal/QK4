# Styling guide

How QK4's widgets get their colours, fonts and sizes, and the rules a new widget must follow.
This guide holds what the headers cannot. It carries no value tables: the values are in the headers.

## Where things live

| What | File |
|---|---|
| Colour, alpha and dimension tokens (`K4Styles::Colors::*`, `K4Styles::Dimensions::*`) | `src/ui/styling/k4constants.h` — the only value reference |
| Fonts, style-sheet helpers, QPainter helpers, `K4Styles::Dialog::*` | `src/ui/styling/k4styles.h` |
| Power, thermometer and lightning glyphs (`K4Glyphs::*`) | `src/ui/styling/k4glyphs.h` |
| Side-panel key builder (`SidePanelButton::*`) | `src/ui/widgets/sidepanelbutton.h` |
| Options page layout (`OptionsPageLayout::*`) | `src/ui/pages/optionspagelayout.h` |

`k4constants.h` has no Qt includes, so code that only needs a token (renderers, hardware) does not
pull in `<QPainter>` or `<QFont>`. `k4styles.h` includes it.

Each token in `k4constants.h` carries a comment saying what it is for, and contrast notes where they
matter. Read the header, not a copy of it.

## Rules for new code

- No literal colours or sizes. Use a token. `TokenStandardTests` scans `src/` and fails on a hex
  colour, `QColor(<digit>`, `QColor::fromRgb(<digit>`, `Qt::white` / `Qt::black`, a style-sheet
  `rgb()` / `rgba()` / `black` / `white`, `setPointSize`, or a literal size in `setPixelSize`,
  `paintFont`, `dataFont` or `font-size:`. The allow-list in the test is currently empty.
- Translucency is a named `Colors::Alpha::*` applied to a token: `K4Styles::withAlpha()` for
  QPainter, `K4Styles::cssRgba()` for style sheets.
- A new colour is written once. A second name for the same colour aliases the first token.
  `TokenStandardTests` fails on a repeated hex value.
- Custom-painted text uses `K4Styles::Fonts::paintFont()`, or `dataFont()` for numbers that change
  (tabular figures).
- Side-panel keys are built with `SidePanelButton`, never by hand.
- Options pages use the `OptionsPageLayout` helpers, not their own margins and spacing.

## Type roles and pixel sizes

There are six type roles: caption, body, readout, heading, indicator and display, above a floor
(`FontSizeFloor`). `static_assert`s in `k4constants.h` keep the roles at or above the floor and in
strictly increasing order. The comment on each `FontSize*` token says what it is for.

Text at the readout size or smaller on the dark background is Medium weight or bolder. The
application font is Inter Medium, set with `QApplication::setFont()` in `main.cpp`, so plain labels
get it. Custom-painted text passes `QFont::Medium` (or bolder) to `paintFont()` / `dataFont()`.

**Why pixel sizes.** Qt's QFont docs recommend `setPointSize()` for device independence and note
that `setPixelSize()` makes a font device dependent. QK4 deliberately does the opposite. Point sizes
follow the platform's logical DPI — 72 on macOS, 96 on Windows — so the same point size draws about
a third larger on Windows, and the panels would not fit the same way on both. Pixel sizes are
logical pixels, which Qt's High-DPI scaling still multiplies by the device pixel ratio, so text stays
sharp and the layout is the same on every platform. Always `setPixelSize()` with a `FontSize*` token.

`dataFont()` turns on tabular figures with `QFont::setFeature("tnum")`, which needs Qt 6.7. The call
is guarded, so a build on an older Qt keeps proportional digits. CI no longer builds on one: every
runner uses the Qt in `.github/qt-version`.

## Contrast policy

- Text meets WCAG 2.x **4.5:1** against every background it is drawn on. Translucent fills and
  scrims are judged as composited.
- Non-text graphics that carry meaning (frames, ticks, unlit bars) meet **3:1**.
- The only exemption is **disabled-control text**: WCAG 1.4.3 does not apply to inactive
  components. `TokenStandardTests` lists the exempt pairs and requires them to stay below 4.5:1, so a
  pair that starts passing must leave the list.
- `TxRed` is a fill and marker colour, never text. Red text uses `TextRed`; a red fill that carries
  text uses `TxActiveFill`. The test keeps `TxRed` below 4.5:1 and distinct from `TextRed`.
- A view whose selection fill is `AccentAmber` must also set `selection-color`. Left unset, the
  selected text takes the palette's highlighted-text colour (white), which fails on amber.

## Bluish overlay surfaces

The full-screen overlays (MenuOverlay, MacroDialog) use the `Overlay*` near-blacks in
`k4constants.h`. They carry a slight blue cast, unlike the neutral surfaces of the main window.

This is a deliberate maintainer decision: the cast sets the overlays apart from the main window.
Do not merge them into the nearest neutral token (`Background`, `PopupBackground`,
`GradientBottom`), even though the values are close. The `WHY bluish` comment in `k4constants.h`
points here.

## Side-panel keys

`SidePanelButton` (`src/ui/widgets/sidepanelbutton.h`) is the one builder for keys on the left and
right panels, the memory row and the KPA1500 mini-panel.

- `create()` makes a fixed-size key with its shade's sheet and `Qt::WA_LayoutUsesWidgetRect`.
  `Shade::Dark` and `Shade::Light` differ only in colour; geometry is shared.
- `createWithSubLabel()` adds the orange sub-label. An empty sub-label still reserves its line, so
  rows stay aligned.
- `keyWidthFor()` gives one width for a set of keys that must look alike: the widest text, measured
  in each key's own polished font, plus `ButtonTextClearance` inside the border. Measure after
  polish: on macOS a push button's font comes from the platform theme, not the application font.
- `addGap()` adds a gap that is drawn at exactly its token. `QBoxLayout` still draws its `spacing()`
  between two items a spacer separates, so `addSpacing()` alone draws too much.
- `createGrid()` makes the two-column key grid. Add it with `Qt::AlignHCenter`.

The side sheets (`sidePanelButton*()`, `compactButton()`) set colour, border, radius and font only.
Size is set on the widget: a style-sheet `height` has no effect on widgets, and `min-height` counts
from the content rectangle (Qt Style Sheets Reference). Spacing is explicit rather than the style's
`PM_Layout*Spacing`, which follows each platform's native guidelines and would make the panels
differ per platform.

**Knob keys are 90×44.** The six knob keys (WPM/PWR, BW/SHFT, M.RF/S.SQL) use `KnobButtonWidth`
and `KnobButtonHeight`. That size is the maintainer's choice. Do not stretch them to the key-grid
width or shrink them to their text. `SidePanelButtonStandardTests::knobKeysKeepTheirSize` holds the
size and checks the text still fits.

## Qt style-sheet rules

These come from Qt's style-sheet documentation (*The Style Sheet Syntax*, Qt 6.8). They decide
where a new rule has to go.

**A widget's own sheet always wins.** "The widget's own style sheet is always preferred to any
inherited style sheet, irrespective of the specificity of the conflicting rules." A parent's sheet
beats a grandparent's, and any widget or parent sheet beats the `QApplication` sheet. QK4 installs
`K4Styles::applicationStyleSheet()` on the application in `main.cpp`. It is only a fallback for
form controls that set no sheet of their own: a plain `"color: x"` on the widget overrides even a
more specific application rule. To change how a widget with its own sheet looks, change that sheet.

**Sheets do not pass font or colour to children.** By default a child does not inherit font or
colour from its parent's style sheet. `QWidget::setFont()` and `setPalette()` do propagate. This is
why the application font is set with `QApplication::setFont()`, not in a style sheet. Do not move it
into a sheet expecting it to cascade.

**Complex widgets are all or nothing.** "With complex widgets such as QComboBox and QScrollBar, if
one property or sub-control is customized, all the other properties or sub-controls must be
customized as well." `Dialog::comboBox()` and `Dialog::spinBox()` style every sub-control for this
reason. A new scroll-bar or progress-bar rule must do the same.

**Prefer selectors and pseudo-states for new code.** Much existing code swaps a whole sheet per
state, for example `popupButtonNormal()` → `popupButtonSelected()`. It works, but new code should
instead use one sheet with pseudo-states (`:checked` on a checkable `QPushButton`, `:hover`,
`:disabled`) or a property selector (`QPushButton[role="…"]`) driven by `setProperty()`.

**A property change needs the sheet re-applied.** Qt warns that if a property used in a selector
changes after the sheet is set, the sheet may need recomputing, and documents one remedy: unset the
sheet and set it again. `K4Styles::setSelected()` does exactly that for the `selected` property; use
it rather than calling `style()->unpolish()`/`polish()`, which Qt documents only for style changes.
Pseudo-states such as `:checked` and `:hover` need nothing.

**Selected looks:** `popupButton()` and `menuBarButtonSelectable()` are one sheet each, matching
`:checked` and `[selected="true"]`. Set the sheet once; a checkable button needs nothing more, any
other button calls `setSelected()`. Each is its normal sheet followed by the selected rules narrowed
to the selected state, so the selected look is exactly the standalone selected sheet's.

**`qproperty-` is read once**, when the widget is polished. It does not work in pseudo-states.

## Helpers worth knowing

`k4styles.h` is the full list, with a comment on each. The families:

- Popup, menu-bar, side-panel, compact, dialog and control buttons: `QString`-returning sheets.
- `K4Styles::Dialog::*`: cached `const QString &` sheets for Options pages and dialogs (labels,
  fields, combo box, line edit, spin box, check box, action buttons). `labelText()` and
  `labelTextBold()` build a fresh `QString` for a given colour and size.
- `Dialog::statusLabel(color)` returns `const QString &` to a buffer that the **next call
  overwrites**. Pass it straight to `setStyleSheet()`, which copies it; never keep the reference.
- QPainter helpers: `buttonGradient()`, `selectedGradient()`, `meterGradient()`, `borderColor*()`,
  `withAlpha()`, `drawDropShadow()`, `drawHaloText()` (text over the spectrum).
- `applicationStyleSheet()`: the application-wide form-control fallback.

Popups inherit `K4PopupBase` (`src/ui/popups/k4popupbase.h`). Its `paintEvent()` draws the shadow
and background, then calls the virtual `paintContent(QPainter &, const QRect &)`. Override
`paintContent()`, never `paintEvent()`, and never draw the shadow yourself.

## Tests that enforce the standard

| Test (ctest name) | File | What it holds |
|---|---|---|
| `TokenStandardTests` | `tests/test_tokenstandard.cpp` | Text/background pairs ≥ 4.5:1; the disabled-text exempt list stays below it; `TxRed` is not a text colour; no repeated hex in `k4constants.h`; the source scan for literal colours and sizes, plus a self-test that the scanner still finds them |
| `FormControlThemeTests` | `tests/test_formcontroltheme.cpp` | A bare unchecked checkbox under a dark Fusion palette has a visible outline (3:1); every form-control type created in `src/` has a fallback rule in `applicationStyleSheet()` |
| `SideControlPanelLayoutTests` (test classes `SideControlPanelLayoutTests`, `SidePanelButtonStandardTests`, `KeyTextClearanceTests`) | `tests/test_sidecontrolpanel.cpp` | Nothing overlaps at minimum size; one key size and one rhythm for all side-panel keys; knob keys keep their size; gaps are drawn at their tokens; every key clears its text |
| `OptionsPageLayoutTests` | `tests/test_optionspagelayout.cpp` | Every Options page fits the narrowest dialog; help text wraps; long device names do not widen a page |
| `MeterScaleTests` | `tests/test_meterscale.cpp` | Meter scale marks sit where their values fall |
| `ButtonSheetsTests` | `tests/test_buttonsheets.cpp` | The selectable popup and menu-bar sheets render the same pixels as the normal/selected sheets they replace, idle, hovered and pressed, whether selected by `:checked` or by `setSelected()` |

Run them with `ctest --test-dir build -R 'Token|FormControl|SideControl|OptionsPage|MeterScale|ButtonSheets'`.

## Removed names

For reading older code. Colours: `TextWhite` → `TextPrimary`; `TextGray` / `TextFaded` →
`TextSecondary`; `TextDark` → `TextOnLight`; `ErrorRed` → `TextRed`; `InactiveGray` →
`TextSecondary` (informative text), `MutedGraphic` (border or tick) or `TextDisabled` (disabled
control).

Font sizes: `FontSizeTiny` / `Small` / `Normal` / `Medium` → caption; `FontSizeLarge`,
`PopupButtonSize` → body; `FontSizeButton`, `PopupTitleSize`, `PopupValueSize` → readout;
`FontSizeNotification`, `FontSizePopup` → heading; `FontSizeTitle` → indicator;
`FontSizeFrequency` → display.
