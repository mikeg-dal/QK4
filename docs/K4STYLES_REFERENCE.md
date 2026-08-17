# K4Styles Reference

Complete reference for `K4Styles::Colors`, `K4Styles::Dimensions`, `K4Styles::Fonts`, and the helper functions declared under `src/ui/styling/`.

**Source of truth:**
- `src/ui/styling/k4constants.h` — `K4Styles::Colors::*` and `K4Styles::Dimensions::*` (lightweight, no Qt includes).
- `src/ui/styling/k4styles.h` — `K4Styles::Fonts::*`, free-function stylesheet helpers, `K4Styles::Dialog::*` cached helpers.

The split exists so files that touch only constants (panadapter renderers, hardware modules) don't pull in `<QPainter>` / `<QFont>` / `<QLinearGradient>`. `k4styles.h` includes `k4constants.h`, so `K4Styles::Colors::*` and `K4Styles::Dimensions::*` remain reachable from anywhere that includes either header.

Keep this doc in sync when you add a constant or helper. Quick cross-check:

```bash
rg 'constexpr const char \*' src/ui/styling/k4constants.h   # all color constants
rg '^constexpr int' src/ui/styling/k4constants.h            # all dimension constants
rg '^\s*QString\s+\w+\(' src/ui/styling/k4styles.h          # free-function stylesheet helpers
rg '^\s*const QString &\w+' src/ui/styling/k4styles.h       # Dialog:: cached-QString helpers
rg 'constexpr const char \*' src/ui/styling/k4styles.h      # font-family constants (Fonts::Primary, Fonts::Data)
```

---

## Colors (`K4Styles::Colors::*`)

### Backgrounds

| Constant | Value | Usage |
|----------|-------|-------|
| `Background` | `#1a1a1a` | Main app background |
| `DarkBackground` | `#0d0d0d` | Darker areas, panel backgrounds |
| `PopupBackground` | `#1e1e1e` | Popup menu fill |
| `DisabledBackground` | `#444444` | Disabled indicator backgrounds (SUB/DIV off state) |

### App Accent Color

| Constant | Value | Usage |
|----------|-------|-------|
| `AccentAmber` | `#FFB000` | TX indicators, labels, highlights |

### VFO Theme Colors

| Constant | Value | Usage |
|----------|-------|-------|
| `VfoACyan` | `#00BFFF` | VFO A: square, passband, markers, overlays |
| `VfoBGreen` | `#00FF00` | VFO B: square, passband, markers, overlays |

### Status Indicator Colors

| Constant | Value | Usage |
|----------|-------|-------|
| `TxRed` | `#FF0000` | TX indicator, transmit state |
| `StatusGreen` | `#00FF00` | Active/on/connected status indicators |

### Meter Gradient Colors (S/Po Meter)

| Constant | Value | Usage |
|----------|-------|-------|
| `MeterGreenDark` | `#00CC00` | Low signal (green start) |
| `MeterGreen` | `#00FF00` | Normal signal range |
| `MeterYellowGreen` | `#CCFF00` | Transition to yellow |
| `MeterYellow` | `#FFFF00` | Moderate signal |
| `MeterOrange` | `#FF6600` | High signal |
| `MeterOrangeRed` | `#FF3300` | Approaching max |
| `MeterRed` | `#FF0000` | Peak/overload signal |

### PA Drain Current Meter (Id Meter)

| Constant | Value | Usage |
|----------|-------|-------|
| `MeterIdDark` | `#2E7D32` | Muted forest green (Id meter low range) |
| `MeterIdLight` | `#66BB6A` | Lighter sage green (Id meter high range) |

### Text Colors

| Constant | Value | Usage |
|----------|-------|-------|
| `TextWhite` | `#FFFFFF` | Primary text, button labels |
| `TextDark` | `#333333` | Text on light/selected buttons |
| `TextGray` | `#999999` | Secondary text, labels |
| `TextFaded` | `#808080` | Faded text (e.g., auto mode values) |
| `InactiveGray` | `#666666` | Inactive/disabled elements |

### Overlay / Indicator Colors

| Constant | Value | Usage |
|----------|-------|-------|
| `OverlayBackground` | `#707070` | VFO indicator badges |

### Overlay Panel Colors (Menu, Macros, full-screen overlays)

| Constant | Value | Usage |
|----------|-------|-------|
| `OverlayContentBg` | `#18181C` | Main content area background |
| `OverlayHeaderBg` | `#222228` | Header bar + nav panel background |
| `OverlayColumnHeaderBg` | `#1E1E24` | Column header background |
| `OverlayItemBg` | `#19191E` | Unselected item row background |
| `OverlayNavButton` | `#3A3A45` | Nav button normal state |
| `OverlayNavButtonPressed` | `#505060` | Nav button pressed state |
| `OverlayDivider` | `#28282D` | Divider lines between items |
| `OverlayDividerLight` | `#3C3C41` | Lighter divider (demarcation) |

### Selection Highlighting (K4-style Dual Panel)

| Constant | Value | Usage |
|----------|-------|-------|
| `SelectionLight` | `#DCDCDC` | Selected item text/highlight (light) |
| `SelectionDark` | `#505055` | Selected item background (dark) |

### Button Gradient — Normal State

| Constant | Value | Position |
|----------|-------|----------|
| `GradientTop` | `#4a4a4a` | 0% (top) |
| `GradientMid1` | `#3a3a3a` | 33% |
| `GradientMid2` | `#353535` | 66% |
| `GradientBottom` | `#2a2a2a` | 100% (bottom) |

### Button Gradient — Hover State

| Constant | Value | Position |
|----------|-------|----------|
| `HoverTop` | `#5a5a5a` | 0% (top) |
| `HoverMid1` | `#4a4a4a` | 33% |
| `HoverMid2` | `#454545` | 66% |
| `HoverBottom` | `#3a3a3a` | 100% (bottom) |

### Button Gradient — Light (TX function buttons, REC/STORE/RCL)

| Constant | Value | Position |
|----------|-------|----------|
| `LightGradientTop` | `#888888` | 0% (top) |
| `LightGradientMid1` | `#777777` | 33% |
| `LightGradientMid2` | `#6a6a6a` | 66% |
| `LightGradientBottom` | `#606060` | 100% (bottom) |

### Button Gradient — Selected State

| Constant | Value | Position |
|----------|-------|----------|
| `SelectedTop` | `#E0E0E0` | 0% (top) |
| `SelectedMid1` | `#D0D0D0` | 33% |
| `SelectedMid2` | `#C8C8C8` | 66% |
| `SelectedBottom` | `#B8B8B8` | 100% (bottom) |

### Border Colors

| Constant | Value | Usage |
|----------|-------|-------|
| `BorderNormal` | `#606060` | Default button border |
| `BorderHover` | `#808080` | Button border on hover |
| `BorderPressed` | `#909090` | Button border when pressed |
| `BorderSelected` | `#AAAAAA` | Selected/active button border |

### Dialog & Panel Borders

| Constant | Value | Usage |
|----------|-------|-------|
| `DialogBorder` | `#333333` | Dialog borders and separators |
| `PanelBorder` | `#444444` | Subtle frame border for panadapter / VFO panels |
| `ErrorRed` | `#FF6666` | Error / not-connected status indicators |
| `ErrorBgDark` | `#331111` | Muted dark-red background for error toasts (pairs with `ErrorRed`) |

### Filter Indicator

| Constant | Value | Usage |
|----------|-------|-------|
| `FilterIndicatorGold` | `#FFD040` | Filter-edge indicator gold (brighter/yellower than `AccentAmber`; reads clearly against the panadapter's amber passband fill). Used by `FilterIndicatorWidget` only. |

---

## Dimensions (`K4Styles::Dimensions::*`)

### Border & Radius

| Constant | Value | Usage |
|----------|-------|-------|
| `BorderWidth` | `2` | Standard button border width (px) |
| `BorderRadius` | `6` | Standard button corner radius (px) |
| `BorderRadiusLarge` | `8` | Large button/popup corner radius (px) |

### Shadow (Popup Widgets)

| Constant | Value | Usage |
|----------|-------|-------|
| `ShadowRadius` | `16` | Shadow blur radius (px) |
| `ShadowOffsetX` | `2` | Shadow horizontal offset (px) |
| `ShadowOffsetY` | `4` | Shadow vertical offset (px) |
| `ShadowMargin` | `20` | Space around popup for shadow (`ShadowRadius + 4`) |
| `ShadowLayers` | `8` | Number of blur layers for soft shadow |

### Button Heights

| Constant | Value | Usage |
|----------|-------|-------|
| `ButtonHeightLarge` | `44` | Menu overlay nav buttons (px) |
| `ButtonHeightMedium` | `36` | Bottom menu bar, popup buttons (px) |
| `ButtonHeightSmall` | `28` | Function buttons in side panels (px) |
| `ButtonHeightMini` | `24` | Compact toggle buttons (MON, NORM, BAL) (px) |

### Popup Layout

| Constant | Value | Usage |
|----------|-------|-------|
| `PopupButtonWidth` | `70` | Standard popup button width (px) |
| `PopupButtonHeight` | `44` | Standard popup button height (px) |
| `PopupButtonSpacing` | `8` | Spacing between popup buttons (px) |
| `MenuBarButtonWidth` | `90` | Bottom menu bar buttons (fits "MAIN RX") (px) |
| `PopupContentMargin` | `12` | Padding inside popup content area (px) |

### Common UI Heights

| Constant | Value | Usage |
|----------|-------|-------|
| `SeparatorHeight` | `1` | Horizontal/vertical separator lines (px) |
| `MenuItemHeight` | `40` | Menu overlay items, frequency labels (px) |
| `MenuBarHeight` | `52` | Bottom menu bar container height (px) |

### Common UI Widths

| Constant | Value | Usage |
|----------|-------|-------|
| `SmallIconSize` | `20` | Lock icons, health indicator, small controls (px) |
| `CompactButtonSize` | `32` | Side panel icons, EQ +/- buttons (px) |
| `FormLabelWidth` | `80` | Form field labels, numeric value displays (px) |
| `VfoSquareSize` | `45` | VFO A/B indicator squares and mode labels (px) |
| `NavButtonWidth` | `54` | Navigation buttons in overlays (px) |
| `LeftSidePanelWidth` | `105` | Left side panel (volume, controls) (px) |
| `RightSidePanelWidth` | `130` | Right side panel (function buttons, KPA1500) (px) |
| `NavPanelWidth` | `130` | Menu overlay navigation/search panel (px) |
| `MemoryButtonWidth` | `42` | M1–M4, REC, STORE, RCL buttons (px) |

### Font Sizes

All font sizes are in **pixels** — use with `QFont::setPixelSize()` or `K4Styles::Fonts::paintFont()`. This ensures consistent rendering across macOS (72 PPI) and Windows (96 PPI).

| Constant | Value | Usage |
|----------|-------|-------|
| `FontSizeTiny` | `7` | Sub-labels (BANK, AF REC, MESSAGE) (px) |
| `FontSizeSmall` | `8` | Scale fonts, secondary text (px) |
| `FontSizeNormal` | `9` | Alt/secondary button text (px) |
| `FontSizeMedium` | `10` | Labels, descriptions (px) |
| `FontSizeLarge` | `11` | Feature labels, primary labels (px) |
| `FontSizeButton` | `12` | Button text, value displays (px) |
| `FontSizeNotification` | `13` | Toast / macro-dialog notifications (between Button and Popup) (px) |
| `FontSizePopup` | `14` | Notifications, popup titles (px) |
| `FontSizeTitle` | `16` | Large control buttons (+/-) (px) |
| `FontSizeIndicator` | `18` | TX/RX/SPLIT indicator labels (px) |
| `FontSizeFrequency` | `32` | VFO frequency displays (px) |

### DX Cluster Spot Label Font Sizes

User-configurable in Options → DX Cluster (the slider runs `FontSizeSpotMin..FontSizeSpotMax`).

| Constant | Value | Usage |
|----------|-------|-------|
| `FontSizeSpot` | `11` | Default DX cluster spot-label size (px) |
| `FontSizeSpotMin` | `8` | Slider minimum for spot label size (px) |
| `FontSizeSpotMax` | `16` | Slider maximum for spot label size (px) |

### Popup Menu Font Sizes (horizontal control-bar popups)

| Constant | Value | Usage |
|----------|-------|-------|
| `PopupTitleSize` | `12` | Popup title labels (e.g., "MIC INPUT", "ATTENUATOR") (px) |
| `PopupButtonSize` | `11` | Popup selection buttons (e.g., "FRONT", "REAR") (px) |
| `PopupValueSize` | `12` | Value displays (e.g., "6 dB", "184 Hz") (px) |

### Slider Dimensions

| Constant | Value | Usage |
|----------|-------|-------|
| `SliderGrooveHeight` | `6` | Horizontal slider groove height (px) |
| `SliderHandleWidth` | `16` | Slider handle width (px) |
| `SliderHandleHeight` | `16` | Explicit handle height (robust on fractional DPI) (px) |
| `SliderHandleMargin` | `-5` | Vertical margin for handle positioning (px) |
| `SliderBorderRadius` | `3` | Groove border radius (px) |
| `SliderHandleRadius` | `8` | Handle border radius (half of width) (px) |
| `SliderMinHeight` | `20` | Minimum widget height to prevent handle clipping (px) |
| `SliderValueLabelWidth` | `40` | Width for percentage value labels (px) |

### VFO Indicator Dimensions

| Constant | Value | Usage |
|----------|-------|-------|
| `VfoIndicatorWidth` | `34` | VFO A/B label width on panadapter (px) |
| `VfoIndicatorHeight` | `30` | VFO A/B label height on panadapter (px) |

### Network Metrics Chart (`NetHealthPopup` sparklines)

| Constant | Value | Usage |
|----------|-------|-------|
| `ChartPopupContentWidth` | `242` | Card content width, excluding shadow margin (px) |
| `ChartRowHeight` | `46` | Height of one metric sparkline row (px) |
| `ChartValueColumnWidth` | `100` | Left column: legend label + current value + scale gutter (px) |
| `ChartLineWidth` | `2` | Sparkline trace stroke width (px) |
| `ChartPlotMargin` | `4` | Inset between a row's plot area and its edges (px) |
| `ChartLegendDotSize` | `6` | Diameter of the series legend dot (px) |
| `ChartPopupGap` | `4` | Gap between the popup card and its anchor widget (px) |

Series colors are aliases, not new values: `ChartSeriesRtt` = `VfoACyan`,
`ChartSeriesJitter` = `AccentAmber`, `ChartSeriesBuffer` = `MeterIdLight`.

### Dialog Dimensions

| Constant | Value | Usage |
|----------|-------|-------|
| `DialogMargin` | `20` | Dialog content margins (px) |
| `TabListWidth` | `150` | Options dialog tab list width (px) |
| `InputFieldWidthSmall` | `100` | Port number, small inputs (px) |
| `InputFieldWidthMedium` | `120` | Version labels, medium fields (px) |
| `CheckboxSize` | `18` | Checkbox indicator dimensions (px) |
| `PaddingSmall` | `6` | Small padding (inputs) (px) |
| `PaddingMedium` | `10` | Medium padding (buttons) (px) |
| `PaddingLarge` | `15` | Large padding (list items) (px) |

---

## Fonts (`K4Styles::Fonts::*`)

### Font Family Constants

| Constant | Value | Usage |
|----------|-------|-------|
| `Primary` | `"Inter"` | UI text, labels, buttons, menus |
| `Data` | `"Inter"` | Frequencies, numeric data (uses tabular figures via `font-feature-settings: 'tnum'`) |

### Font Helpers

```cpp
// Build a font for custom-painted widgets with pixel-based sizing.
// setPixelSize() is used (not setPointSize()) so fonts render at the same
// logical size on macOS (72 PPI) and Windows (96 PPI).
QFont K4Styles::Fonts::paintFont(int pixelSize, QFont::Weight weight = QFont::Bold);

// Like paintFont() but enables tabular figures so digit widths stay equal —
// prevents visual shifting when numeric values change (e.g., frequency displays).
QFont K4Styles::Fonts::dataFont(int pixelSize, QFont::Weight weight = QFont::Bold);
```

---

## Helper Functions

### Stylesheet Helpers (`QString`-returning)

```cpp
// Popup buttons
QString K4Styles::popupButtonNormal();    // Dark gradient (unselected band/mode buttons)
QString K4Styles::popupButtonSelected();  // Light/white selected state

// Menu bar buttons (BottomMenuBar, FeatureMenuBar)
QString K4Styles::menuBarButton();          // Standard (MENU, Fn, DISPLAY, BAND)
QString K4Styles::menuBarButtonActive();    // Active/inverted (white bg, dark text)
QString K4Styles::menuBarButtonSmall();     // Compact +/- controls
QString K4Styles::menuBarButtonPttPressed();// PTT pressed state (red / white text)

// Sliders + form controls
QString K4Styles::sliderHorizontal(const QString &grooveColor, const QString &handleColor);
QString K4Styles::checkboxButton(int size = 32);  // Checkable toggle button
QString K4Styles::radioButton();                  // Mode selection (DISPLAY ALL / USE SUBSET)

// Side panel buttons
QString K4Styles::compactButton();              // MON, NORM, BAL (dark gradient, 1px border)
QString K4Styles::sidePanelButton();            // Dark gradient side-panel icons (?, globe)
QString K4Styles::sidePanelButtonLight();       // Light gradient for TX/PF function buttons
QString K4Styles::panelButtonWithDisabled();    // KPA1500 panel buttons (STANDBY, ATU, ANT, TUNE)

// Dialog + control buttons
QString K4Styles::dialogButton();               // Dialog action buttons (Connect, Save, Delete)
QString K4Styles::controlButton(bool selected = false);  // Decode window controls
```

### QPainter Helpers

```cpp
// Gradients
QLinearGradient K4Styles::buttonGradient(int top, int bottom, bool hovered = false);
QLinearGradient K4Styles::selectedGradient(int top, int bottom);
QLinearGradient K4Styles::meterGradient(qreal x1, qreal y1, qreal x2, qreal y2);

// Colors
QColor K4Styles::borderColor();          // BorderNormal (#606060)
QColor K4Styles::borderColorSelected();  // BorderSelected (#AAAAAA)

// Shadow
void K4Styles::drawDropShadow(QPainter &painter, const QRect &contentRect, int cornerRadius = 8);
```

### Dialog::* Cached Stylesheet Helpers

Under `K4Styles::Dialog::`. Each returns a `const QString &` to a static cached string (zero per-call allocation) unless explicitly dynamic. Use these in `OptionsDialog` and its tab pages — eliminates the 26+ inline `QString("color: %1; font-size: %2px;").arg(...)` patterns that used to be scattered across `src/ui/`.

```cpp
// Static/cached — `const QString &`
const QString &K4Styles::Dialog::pageBackground();   // Page background-color
const QString &K4Styles::Dialog::separator();        // Horizontal/vertical separator line
const QString &K4Styles::Dialog::titleLabel();       // Amber bold title (AccentAmber, FontSizeTitle)
const QString &K4Styles::Dialog::formLabel();        // Gray form label (TextGray, FontSizePopup)
const QString &K4Styles::Dialog::formValue();        // White bold value (TextWhite, FontSizePopup)
const QString &K4Styles::Dialog::helpText();         // Gray italic help (TextGray, FontSizeLarge)
const QString &K4Styles::Dialog::sectionHeader();    // White bold header (TextWhite, FontSizePopup)
const QString &K4Styles::Dialog::comboBox();         // Full combo box styling (dark bg, arrow, item view)
const QString &K4Styles::Dialog::lineEdit();         // Full line edit (dark bg, focus border)
const QString &K4Styles::Dialog::checkBox();         // Checkbox + indicator sizing (enabled)
const QString &K4Styles::Dialog::checkBoxDisabled(); // Checkbox (disabled / gray text)
const QString &K4Styles::Dialog::actionButton();     // Dialog action button (dark bg, padding 10px 20px)
const QString &K4Styles::Dialog::actionButtonSmall();// Small action button (padding 6px 12px)

// Dynamic — `QString` (fresh each call; color/size vary)
const QString &K4Styles::Dialog::statusLabel(const QString &color);  // Bold status label, dynamic color
QString K4Styles::Dialog::labelText(const QString &color, int sizePx);
QString K4Styles::Dialog::labelTextBold(const QString &color, int sizePx);
```

**Rule:** if you catch yourself writing `QString("color: %1; font-size: %2px;").arg(K4Styles::Colors::X).arg(K4Styles::Dimensions::Y)`, use `K4Styles::Dialog::labelText(K4Styles::Colors::X, K4Styles::Dimensions::Y)` instead. Single source of truth keeps styling consistent and makes global tweaks one-line.

---

## Usage Examples

### Stylesheet-based Button

```cpp
auto *btn = new QPushButton("BAND", this);
btn->setStyleSheet(K4Styles::popupButtonNormal());

// On selection:
btn->setStyleSheet(K4Styles::popupButtonSelected());
```

### Custom-painted Widget

```cpp
void MyWidget::paintEvent(QPaintEvent *) {
    QPainter painter(this);

    // Draw gradient background
    QLinearGradient grad = K4Styles::buttonGradient(0, height(), m_hovered);
    painter.fillRect(rect(), grad);

    // Draw border
    painter.setPen(QPen(K4Styles::borderColor(), K4Styles::Dimensions::BorderWidth));
    painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1),
                           K4Styles::Dimensions::BorderRadius,
                           K4Styles::Dimensions::BorderRadius);

    // Draw text
    painter.setPen(QColor(K4Styles::Colors::TextWhite));
    painter.drawText(rect(), Qt::AlignCenter, "Label");
}
```

### Popup with Shadow

```cpp
void MyPopup::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QRect content = contentRect();  // From K4PopupBase

    // Draw shadow (K4PopupBase does this automatically)
    K4Styles::drawDropShadow(painter, content, K4Styles::Dimensions::BorderRadiusLarge);

    // Draw popup background
    painter.setBrush(QColor(K4Styles::Colors::PopupBackground));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(content,
                           K4Styles::Dimensions::BorderRadiusLarge,
                           K4Styles::Dimensions::BorderRadiusLarge);
}
```

### Dialog Field Pair

```cpp
auto *label = new QLabel("Callsign:", this);
label->setStyleSheet(K4Styles::Dialog::formLabel());

auto *edit = new QLineEdit(this);
edit->setStyleSheet(K4Styles::Dialog::lineEdit());
```

---

## Color Palette Visual Reference

```
Backgrounds:
  #1a1a1a ████ Background
  #0d0d0d ████ DarkBackground
  #1e1e1e ████ PopupBackground
  #444444 ████ DisabledBackground

Accent & VFO Colors:
  #FFB000 ████ AccentAmber (TX indicators, highlights)
  #00BFFF ████ VfoACyan (VFO A theme)
  #00FF00 ████ VfoBGreen (VFO B theme)

Status:
  #FF0000 ████ TxRed
  #00FF00 ████ StatusGreen

Text:
  #FFFFFF ████ TextWhite
  #333333 ████ TextDark
  #999999 ████ TextGray
  #808080 ████ TextFaded
  #666666 ████ InactiveGray

Normal Gradient (top→bottom):
  #4a4a4a ████ GradientTop
  #3a3a3a ████ GradientMid1
  #353535 ████ GradientMid2
  #2a2a2a ████ GradientBottom

Selected Gradient (top→bottom):
  #E0E0E0 ████ SelectedTop
  #D0D0D0 ████ SelectedMid1
  #C8C8C8 ████ SelectedMid2
  #B8B8B8 ████ SelectedBottom

Borders:
  #606060 ████ BorderNormal
  #808080 ████ BorderHover
  #909090 ████ BorderPressed
  #AAAAAA ████ BorderSelected

Dialog / Panel Borders:
  #333333 ████ DialogBorder
  #444444 ████ PanelBorder
  #FF6666 ████ ErrorRed
  #331111 ████ ErrorBgDark (muted red error-toast background)

Filter Indicator:
  #FFD040 ████ FilterIndicatorGold

Meter Gradient (green→yellow→red):
  #00CC00 ████ MeterGreenDark
  #00FF00 ████ MeterGreen
  #CCFF00 ████ MeterYellowGreen
  #FFFF00 ████ MeterYellow
  #FF6600 ████ MeterOrange
  #FF3300 ████ MeterOrangeRed
  #FF0000 ████ MeterRed

PA Drain Meter (Id):
  #2E7D32 ████ MeterIdDark (muted forest green)
  #66BB6A ████ MeterIdLight (lighter sage green)

VFO Indicator Badges:
  #707070 ████ OverlayBackground

Overlay Panels (menu/macro full-screen overlays):
  #18181C ████ OverlayContentBg
  #222228 ████ OverlayHeaderBg
  #1E1E24 ████ OverlayColumnHeaderBg
  #19191E ████ OverlayItemBg
  #3A3A45 ████ OverlayNavButton
  #505060 ████ OverlayNavButtonPressed
  #28282D ████ OverlayDivider
  #3C3C41 ████ OverlayDividerLight

Selection:
  #DCDCDC ████ SelectionLight
  #505055 ████ SelectionDark
```

---

## Keeping This Doc Current

This file is hand-maintained against `src/ui/styling/k4constants.h` (constants) and `src/ui/styling/k4styles.h` (helpers). It has previously drifted significantly. When you add or change a constant / helper:

1. Update the appropriate header first (the source of truth):
   - Colors / dimensions → `src/ui/styling/k4constants.h`
   - Fonts / stylesheet helpers / `Dialog::` helpers → `src/ui/styling/k4styles.h`
2. Add or update the corresponding row in this doc.
3. Spot-check with the greps at the top of this file:

```bash
rg 'constexpr const char \*' src/ui/styling/k4constants.h | wc -l   # color count sanity check
rg '^constexpr int' src/ui/styling/k4constants.h | wc -l            # dimension count sanity check
```

If the counts produced by those greps don't match what you see tabled above, this doc is stale again. Fix the tables; don't ship with mismatch.
