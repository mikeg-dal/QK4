#ifndef K4CONSTANTS_H
#define K4CONSTANTS_H

// Lightweight constants-only header — no Qt includes, no function declarations.
// Carries K4Styles::Colors and K4Styles::Dimensions for files that need only
// these compile-time values (panadapter renderers, network/hardware modules,
// status displays). The full k4styles.h header still includes this internally
// so existing K4Styles::Colors::* and K4Styles::Dimensions::* references are
// preserved everywhere.
//
// Split rationale: k4styles.h pulls in <QPainter>, <QFont>, <QLinearGradient>,
// <QString> for its function declarations. Files that touch only constants
// were paying that include cost for no reason. Touching this header rebuilds
// roughly 23 fewer translation units than touching the full k4styles.h.

namespace K4Styles {

namespace Colors {
// Every hex value appears once in this namespace; a second name for the same colour aliases the first.
// Text tokens carry their WCAG contrast on Background (#1a1a1a). TokenStandardTests checks every text and
// background pair in use, and that no hex is repeated.

// =============================================================================
// Base neutrals
// =============================================================================
constexpr const char *White = "#FFFFFF"; // Lit fills, overlay strokes over the spectrum (with an Alpha::)
constexpr const char *Black = "#000000"; // Halos, shadows, scrims (with an Alpha::), unrendered clear

// =============================================================================
// Surfaces
// =============================================================================
constexpr const char *DarkBackground = "#0d0d0d";     // Dialogs, inputs, mini-pan clear
constexpr const char *SpectrumBackground = "#141414"; // Panadapter GPU clear colour
constexpr const char *Background = "#1a1a1a";         // Main window
constexpr const char *PopupBackground = "#1e1e1e";    // Floating popups (raised one step)

// =============================================================================
// Text
// =============================================================================
constexpr const char *TextPrimary = White;               // 17.4:1
constexpr const char *TextSecondary = "#999999";         // 6.11:1 - labels, units, idle values, status text
constexpr const char *TextSecondaryOnRaised = "#AAAAAA"; // TextSecondary on buttons and nav keys (≥4.6:1 there)
constexpr const char *TextDisabled = "#666666";          // 3.03:1 - disabled controls only (WCAG-exempt)
constexpr const char *TextOnLight = "#333333";           // Text on selected/lit fills (6.4:1 on SelectedBottom)
constexpr const char *TextOnAccent = Black;              // Text on saturated fills (status green, VFO badges)
// WHY a separate red for text: TxRed (#FF0000) is 4.35:1 on Background, under the 4.5:1 that small
// text needs.
constexpr const char *TextRed = "#FF6666"; // 6.09:1 - over-S9 scale, errors, warnings

// =============================================================================
// Accent and status
// =============================================================================
constexpr const char *AccentAmber = "#FFB000"; // 9.5:1 - TX indicators, titles, highlights
constexpr const char *VfoACyan = "#00BFFF";    // VFO A theme
constexpr const char *StatusGreen = "#00FF00"; // Active/on/connected; VFO B theme
constexpr const char *VfoBGreen = StatusGreen;
constexpr const char *TxRed = "#FF0000"; // LEDs, meters, markers - never text (TextRed) or under text (TxActiveFill)
// WHY #E00000: white on TxRed is 4.0:1; #EE0000 is the lightest pure red to pass (4.53:1), #E00000 keeps a margin
// at 5.04:1 and still reads as transmitting.
constexpr const char *TxActiveFill = "#E00000"; // PTT key while transmitting (TextPrimary)

// Meter gradient (green -> yellow -> red). Data colours.
constexpr const char *MeterGreenDark = "#00CC00";
constexpr const char *MeterGreen = StatusGreen;
constexpr const char *MeterYellowGreen = "#CCFF00";
constexpr const char *MeterYellow = "#FFFF00";
constexpr const char *MeterOrange = "#FF6600";
constexpr const char *MeterOrangeRed = "#FF3300";
constexpr const char *MeterRed = TxRed;

// Id Meter Colors (PA drain current - subtle green theme)
constexpr const char *MeterIdDark = "#2E7D32";  // Muted forest green
constexpr const char *MeterIdLight = "#66BB6A"; // Lighter sage green

// =============================================================================
// Borders and non-text strokes
// =============================================================================
constexpr const char *DialogBorder = TextOnLight; // Dialog borders, separators, slider grooves
constexpr const char *PanelBorder = "#444444";    // Frame around panadapter/VFO panels
constexpr const char *BorderDisabled = PanelBorder;
constexpr const char *DisabledBackground = PanelBorder; // Disabled fills
constexpr const char *BorderNormal = "#606060";
// WHY #666 and not BorderNormal: these strokes sit straight on Background, where #606060 is 2.8:1, under
// the 3:1 that WCAG 1.4.11 asks of graphics. #666 is 3.03:1.
constexpr const char *MutedGraphic = TextDisabled; // Idle frames, ticks, unlit bars, unknown-state glyphs
constexpr const char *BorderHover = "#808080";
constexpr const char *BorderPressed = "#909090";
constexpr const char *BorderSelected = TextSecondaryOnRaised;

// =============================================================================
// Overlay panels (Menu, Macros)
// =============================================================================
// WHY bluish: these five near-blacks carry a slight blue cast that sets the full-screen overlays
// apart from the neutral main window. Not merged into the neutral surfaces; see docs/styling.md.
constexpr const char *OverlayContentBg = "#18181C";        // Main content area background
constexpr const char *OverlayHeaderBg = "#222228";         // Header bar + nav panel background
constexpr const char *OverlayColumnHeaderBg = "#1E1E24";   // Column header background
constexpr const char *OverlayItemBg = "#19191E";           // Unselected item row background
constexpr const char *OverlayNavButton = "#3A3A45";        // Nav button normal state
constexpr const char *OverlayNavButtonPressed = "#505060"; // Nav button pressed state
constexpr const char *OverlayDivider = "#28282D";          // Divider lines between items
constexpr const char *OverlayDividerLight = "#3C3C41";     // Lighter divider (demarcation)

// Selection Highlighting (K4-style dual panel for menu items)
constexpr const char *SelectionLight = "#DCDCDC"; // Light panel (selected zone)
constexpr const char *SelectionDark = "#505055";  // Dark panel (value zone)

// WHY #777: black text on the old #707070 was 4.24:1; #777777 is 4.69:1.
constexpr const char *OverlayBackground = "#777777"; // VFO A/B badges on the waterfall (TextOnAccent)

// =============================================================================
// Button Gradient Stops
// =============================================================================
constexpr const char *GradientTop = "#4a4a4a";
constexpr const char *GradientMid1 = "#3a3a3a";
constexpr const char *GradientMid2 = "#353535";
constexpr const char *GradientBottom = "#2a2a2a";

constexpr const char *HoverTop = "#5a5a5a";
constexpr const char *HoverMid1 = GradientTop;
constexpr const char *HoverMid2 = "#454545";
constexpr const char *HoverBottom = GradientMid1;

// Lighter button gradients (for TX function buttons, REC/STORE/RCL)
// WHY these two stops: white on the old #888888 top was 3.54:1 and on #777777 4.48:1. Two-line labels (ATU TUNE)
// reach almost to the top edge, so the top stop itself must pass: #747474 is 4.67:1, #6E6E6E 5.10:1.
constexpr const char *LightGradientTop = "#747474";
constexpr const char *LightGradientMid1 = "#6E6E6E";
constexpr const char *LightGradientMid2 = "#6a6a6a";
constexpr const char *LightGradientBottom = BorderNormal;

constexpr const char *SelectedTop = "#E0E0E0";
constexpr const char *SelectedMid1 = "#D0D0D0";
constexpr const char *SelectedMid2 = "#C8C8C8";
constexpr const char *SelectedBottom = "#B8B8B8";

constexpr const char *SelectedHoverTop = "#F0F0F0";
constexpr const char *SelectedHoverMid1 = SelectedTop;
constexpr const char *SelectedHoverMid2 = "#D8D8D8";
constexpr const char *SelectedHoverBottom = SelectedMid2;

constexpr const char *DisabledGradientTop = GradientMid1;
constexpr const char *DisabledGradientMid1 = GradientBottom;
constexpr const char *DisabledGradientMid2 = "#252525";
constexpr const char *DisabledGradientBottom = Background;

// =============================================================================
// Feature colours
// =============================================================================
constexpr const char *ErrorBgDark = "#331111"; // Muted dark-red background for error toasts (with TextRed)

// WHY GradientBottom and not DisabledBackground: TextSecondary on #444 was 3.42:1; on #2a2a2a it is 5.04:1, and the
// darker fill reads as more clearly off beside the green on state.
constexpr const char *BadgeOffBackground = GradientBottom; // SUB/DIV badge when off (TextSecondary)

// Filter-edge indicator gold. Distinct from AccentAmber (#FFB000): brighter/yellower so it reads
// clearly against the panadapter's amber passband fill. Used by FilterIndicatorWidget only.
constexpr const char *FilterIndicatorGold = "#FFD040";

// Network-health sparkline series colors (NetHealthPopup). RTT reuses VfoACyan, jitter reuses
// AccentAmber; buffer gets a distinct soft green so all three traces read apart on the dark card.
constexpr const char *ChartSeriesRtt = VfoACyan;        // RTT trace (cyan)
constexpr const char *ChartSeriesJitter = AccentAmber;  // jitter trace (amber)
constexpr const char *ChartSeriesBuffer = MeterIdLight; // buffer trace (soft sage green)

// Band-plan overlay colors. Mode-segment fills (rendered translucent over the spectrum), the
// out-of-band fill under its hatching, and the band-name tag.
constexpr const char *BandPlanCw = "#CB3B45";           // CW / Morse (crimson red)
constexpr const char *BandPlanData = "#E0852F";         // Digital / data (RTTY, PSK, FT8...) orange
constexpr const char *BandPlanPhone = "#6E83A6";        // SSB / phone (voice) slate blue
constexpr const char *BandPlanBeacon = "#AB47BC";       // Beacons (exclusive) purple
constexpr const char *BandPlanAll = "#90A4AE";          // All modes share (CW/data/phone) blue-grey
constexpr const char *BandPlanOutOfBand = GradientMid1; // Past the band edge (under the hatch)
constexpr const char *BandPlanTag = DarkBackground;     // Band-name tag ("20m")

// =============================================================================
// Alpha (0-255) for translucent uses of the colours above: K4Styles::withAlpha() for QPainter,
// K4Styles::cssRgba() for style sheets.
// =============================================================================
namespace Alpha {
// WHY 60% black, not opaque: a solid ring turns every glyph into a black blob at 10-12 px and hides the
// trace under the label; 60% keeps >= 4.5:1 for white text even over the brightest waterfall colour.
constexpr int Halo = 153;
constexpr int SpanButtonScrim = 153; // Span +/- buttons over the spectrum
constexpr int SpanButtonHover = 204;
// WHY 210: TextSecondary on the scrim must stay >= 4.5:1 over a white waterfall pixel; 160 gave 2.2:1.
constexpr int LabelScrim = 210;      // Mini-pan +/- kHz labels over the waterfall
constexpr int CloseButtonHover = 51; // Text-decode title-bar close button
constexpr int BandPlanSegment = 120; // Mode-segment fill, spectrum shows through
constexpr int BandPlanOutOfBand = 200;
constexpr int BandPlanHatch = 40;
constexpr int BandPlanDivider = 70;
constexpr int BandPlanMarker = 220;
constexpr int BandPlanCaret = 235;
constexpr int PassbandFill = 64; // Secondary VFO passband over the spectrum
constexpr int CardTint = 18;     // KPA1500 card: white tint over the panel background
constexpr int CardBorder = 35;   // KPA1500 card outline (white)
constexpr int LcdBorder = 20;    // KPA1500 status box outline (white)
constexpr int LcdShade = 40;     // KPA1500 status box fill (black)
} // namespace Alpha

} // namespace Colors

namespace Dimensions {
// =============================================================================
// Border & Radius
// =============================================================================
constexpr int BorderWidth = 2;
constexpr int BorderRadius = 6;
constexpr int BorderRadiusLarge = 8;

// =============================================================================
// Shadow (for popup widgets)
// =============================================================================
constexpr int ShadowRadius = 16;
constexpr int ShadowOffsetX = 2;
constexpr int ShadowOffsetY = 4;
constexpr int ShadowMargin = ShadowRadius + 4; // 20px
constexpr int ShadowLayers = 8;

// =============================================================================
// Button Heights
// =============================================================================
constexpr int ButtonHeightLarge = 44;  // Menu overlay nav buttons
constexpr int ButtonHeightMedium = 36; // Bottom menu bar, popup buttons
constexpr int ButtonHeightSmall = 28;  // Function buttons (side panels)
constexpr int ButtonHeightMini = 24;   // Compact toggle buttons (MON, NORM, BAL, etc.)

// =============================================================================
// Side-Panel Button Geometry — one standard for the left and right panels, the memory row and the
// KPA1500 mini-panel. Light and dark buttons share all of it and differ only in colour. Built by
// src/ui/widgets/sidepanelbutton.h; checked by SideControlPanelLayoutTests.
// =============================================================================
// WHY 28: the sheets carry no vertical padding, so 28 minus the two 2 px borders leaves the 24 px two
// lines of 10 px text need (ATU TUNE, FREQ ENT).
constexpr int SideButtonHeight = ButtonHeightSmall;
// WHY 57: the narrowest column every sub-label fits at the 10 px text floor, DIVERSITY the widest
// (measured, not estimated).
constexpr int SideButtonWidth = 57;
constexpr int SideButtonColumnGap = 4;   // Between buttons side by side, in every row and panel
constexpr int SideButtonSubLabelGap = 4; // Button bottom edge to its orange sub-label
constexpr int SideButtonRowGap = 8;      // Sub-label (or button, where none) to the next row's button
constexpr int SideButtonGridWidth = 2 * SideButtonWidth + SideButtonColumnGap;

// The gaps below are the space drawn between two items, edge to edge. SidePanelButton::addGap() puts them
// into a box layout net of its own spacing, which Qt still draws between items a spacer separates.
constexpr int SidePanelMarginH = 6;     // Panel edge to content, left and right
constexpr int SidePanelMarginV = 8;     // Panel edge to content, top and bottom
constexpr int SidePanelItemSpacing = 4; // Between consecutive items of one group
constexpr int SidePanelGroupGap = 12;   // Between groups (button grid, each knob pair, KPA1500 slot)
constexpr int SidePanelMinorGap = 8;    // Between the rows of one block (MON row, sliders, icon row)
// WHY double: the PF keys and the VFO keys under them are separate blocks on the K4's own panel.
constexpr int SidePanelSectionGap = 2 * SidePanelGroupGap;
static_assert(SidePanelMinorGap >= SidePanelItemSpacing && SidePanelGroupGap >= SidePanelItemSpacing,
              "a gap is drawn net of the layout spacing, so it cannot be smaller than that spacing");
constexpr int SidePanelInnerGap = 4; // Inside a painted card: label to bar, bar to value, text to box edge

constexpr int SideCompactButtonHeight = ButtonHeightMini; // MON / NORM / BAL
constexpr int SideCompactBorderWidth = 1;
constexpr int SideCompactBorderRadius = 4;

// Text on every key stands at least this far inside the key's border, left and right. Key widths are
// measured from their text plus this, so 5% wider text (Windows hinting, a fractional scale factor)
// still clears the border. Checked for every key by SideControlPanelLayoutTests.
constexpr int ButtonTextClearance = 4;

// The six knob controls (WPM/PWR, BW/SHFT, M.RF/S.SQL): a panel-wide key, two lines of text. Height is
// measured from the fonts (DualControlButton::measuredHeight), these two gaps and nothing else.
// WHY 90x44: the maintainer's chosen knob-key proportion (2026-10-29); do not stretch to the grid width.
constexpr int KnobButtonWidth = 90;
constexpr int KnobButtonHeight = ButtonHeightLarge; // 44
constexpr int KnobTextInset = 4;                    // Key edge to the top line, and to the bottom line
constexpr int KnobLineGap = 4;                      // Between the value line and the amber alternate line

// WHY 24: the smallest pointer target WCAG 2.2 (2.5.8) accepts. Small indicators keep their drawn size
// and take this much room around it.
constexpr int MinClickTarget = 24;

constexpr int SideIconSize = 24; // ? and globe glyphs
constexpr int SideIconInset = 2; // Clearance between the glyph and the button's border
constexpr int SideIconButtonSize = SideIconSize + 2 * (BorderWidth + SideIconInset);

// WHY: the right panel is wider than the left only so the KPA1500 meters get room, and only while the
// KPA1500 is enabled; its button grid is the same width and sits centred, this much in from the left
// panel's inset on each side.
constexpr int SidePanelMeterAllowance = 12;

// =============================================================================
// Popup Layout
// =============================================================================
constexpr int PopupButtonWidth = 70;
constexpr int PopupButtonHeight = 44;
constexpr int PopupButtonSpacing = 8;
constexpr int MenuBarButtonWidth = 90; // Bottom menu bar buttons (fits "MAIN RX")
constexpr int MenuBarMarginV = 6;      // Bottom menu bar edge to its buttons, top and bottom
constexpr int PopupContentMargin = 12;

// =============================================================================
// Common UI Heights
// =============================================================================
constexpr int SeparatorHeight = 1;                                     // Horizontal/vertical separator lines
constexpr int MenuItemHeight = 40;                                     // Menu overlay items, frequency labels
constexpr int MenuBarHeight = ButtonHeightMedium + 2 * MenuBarMarginV; // Bottom menu bar: its keys, no more

// =============================================================================
// Common UI Widths
// =============================================================================
constexpr int SmallIconSize = 20;        // Lock icons, health indicator, small controls
constexpr int NetHealthGlyphHeight = 16; // The four signal bars, drawn inside a MinClickTarget square
constexpr int CompactButtonSize = 32;    // Side panel icons, EQ +/- buttons
constexpr int FormLabelWidth = 80;       // Form field labels, numeric value displays
constexpr int VfoSquareSize = 45;        // VFO A/B indicator squares and mode labels
constexpr int NavButtonWidth = 54;       // Navigation buttons in overlays
constexpr int LeftSidePanelWidth = SideButtonGridWidth + 2 * SidePanelMarginH;           // 130
constexpr int RightSidePanelWidth = LeftSidePanelWidth;                                  // 130, KPA1500 off
constexpr int RightSidePanelKpaWidth = LeftSidePanelWidth + 2 * SidePanelMeterAllowance; // 154, KPA1500 on
constexpr int NavPanelWidth = 130; // Menu overlay navigation/search panel

// =============================================================================
// Type scale (pixels) - use with QFont::setPixelSize(), paintFont() or dataFont()
// =============================================================================
// WHY a floor of 10 px: 7-9 px text was unreadable on a 27" 1440p display (0.23 mm/px) and is smaller
// still once a small screen shrinks the interface. Nothing is drawn below it; text at or below
// FontSizeReadout on the dark background is Medium weight or bolder.
constexpr int FontSizeFloor = 10;
constexpr int FontSizeCaption = FontSizeFloor; // Sub-labels, scales, secondary and compact button text
constexpr int FontSizeBody = 11;               // Labels, popup selection buttons, help text
constexpr int FontSizeReadout = 12;            // Values, button text, popup titles, status bar
constexpr int FontSizeHeading = 14;            // Dialog forms, menu rows, notifications
constexpr int FontSizeIndicator = 18;          // TX/RX/SPLIT, VFO A/B badges, +/- and arrow keys, page titles
constexpr int FontSizeDisplay = 32;            // VFO frequency

// DX cluster spot labels - user-configurable in Options -> DX Cluster (FontSizeSpotMin..FontSizeSpotMax).
constexpr int FontSizeSpot = FontSizeBody;
constexpr int FontSizeSpotMin = FontSizeFloor;
constexpr int FontSizeSpotMax = 16;

// Text Decode window font, adjusted with its A-/A+ buttons and saved per receiver.
constexpr int FontSizeTextDecode = FontSizeBody;
constexpr int FontSizeTextDecodeMin = FontSizeFloor;
constexpr int FontSizeTextDecodeMax = 24;

static_assert(FontSizeCaption >= FontSizeFloor && FontSizeCaption < FontSizeBody && FontSizeBody < FontSizeReadout &&
                  FontSizeReadout < FontSizeHeading && FontSizeHeading < FontSizeIndicator &&
                  FontSizeIndicator < FontSizeDisplay,
              "the six type roles start at the floor and strictly increase");
static_assert(FontSizeSpotMin >= FontSizeFloor && FontSizeSpotMin <= FontSizeSpot && FontSizeSpot <= FontSizeSpotMax,
              "DX spot font range must start at the floor and contain the default");
static_assert(FontSizeTextDecodeMin >= FontSizeFloor && FontSizeTextDecodeMin <= FontSizeTextDecode &&
                  FontSizeTextDecode <= FontSizeTextDecodeMax,
              "text-decode font range must start at the floor and contain the default");

// =============================================================================
// Slider Dimensions
// =============================================================================
constexpr int SliderGrooveHeight = 6;     // Horizontal slider groove height
constexpr int SliderHandleWidth = 16;     // Slider handle width
constexpr int SliderHandleHeight = 16;    // Explicit handle height (robust on fractional DPI)
constexpr int SliderHandleMargin = -5;    // Vertical margin for handle positioning
constexpr int SliderBorderRadius = 3;     // Groove border radius
constexpr int SliderHandleRadius = 8;     // Handle border radius (half of width)
constexpr int SliderMinHeight = 20;       // Minimum widget height to prevent handle clipping
constexpr int SliderValueLabelWidth = 40; // Width for percentage value labels

// =============================================================================
// Network-Health Sparkline Popup (NetHealthPopup)
// =============================================================================
constexpr int ChartPopupContentWidth = 242; // Card content width (excl. shadow margin)
constexpr int ChartRowHeight = 46;          // Height of one metric sparkline row
constexpr int ChartValueColumnWidth = 100;  // Left column: legend label + current value + scale gutter
constexpr int ChartLineWidth = 2;           // Sparkline trace stroke width
constexpr int ChartPlotMargin = 4;          // Inset between a row's plot area and its edges
constexpr int ChartLegendDotSize = 6;       // Diameter of the series legend dot
constexpr int ChartScaleTickLen = 5;        // Length of the left-edge scale tick marks

// =============================================================================
// VFO Indicator Dimensions
// =============================================================================
constexpr int VfoIndicatorWidth = 34;  // VFO A/B label width on panadapter
constexpr int VfoIndicatorHeight = 30; // VFO A/B label height on panadapter
// VFO frequency/meter column to the edge of its widget on the centre-column side. The VFO row's own
// spacing adds to it, so the centre column stays clear of both meters.
constexpr int VfoInnerMargin = 4;

// Under each VFO's frequency: the multifunction meter OR the mini-pan, then the feature row (AGC, PRE,
// ATT, NB, NR, NTCH, APF), which stays visible either way. Panel + 2 px layout spacing + row = 150,
// the height the meter-or-mini-pan block had when the feature row was hidden by the mini-pan.
constexpr int VfoPanelHeight = 130;     // Meter (5 bars) or mini-pan; both pages of the VFO's stack
constexpr int VfoFeatureRowHeight = 18; // The feature indicator row below it

// =============================================================================
// Dialog Dimensions
// =============================================================================
constexpr int DialogMargin = 20;           // Dialog content margins
constexpr int TabListWidth = 150;          // Options dialog tab list width
constexpr int InputFieldWidthSmall = 100;  // Port number, small inputs
constexpr int InputFieldWidthMedium = 120; // Version labels, medium fields
constexpr int CheckboxSize = 18;           // Checkbox indicator dimensions
constexpr int PaddingSmall = 6;            // Small padding (inputs)
constexpr int PaddingMedium = 10;          // Medium padding (buttons)
constexpr int PaddingLarge = 15;           // Large padding (list items)

// =============================================================================
// Options dialog pages - geometry only, applied by ui/pages/optionspagelayout
// =============================================================================
constexpr int OptionsDialogMinWidth = 700;
constexpr int OptionsDialogMinHeight = 550;
constexpr int OptionsPageMargin = 16;                        // Page edge to content
constexpr int OptionsRowSpacing = 8;                         // Between rows of a form, and between items of one section
constexpr int OptionsSectionGap = 12;                        // Added above each section, on top of the row spacing
constexpr int OptionsLabelGap = 12;                          // Form label column to field column
constexpr int OptionsFieldMaxWidth = 360;                    // Growable fields (host, slider, device list) stop here
constexpr int OptionsShortFieldWidth = InputFieldWidthSmall; // Ports, call signs, small numbers
// WHY 24: a device list sized to its longest name made the Audio Input page wider than the dialog.
// The closed box shows this many characters and elides the rest; the open list shows every name.
constexpr int OptionsDeviceComboChars = 24;
constexpr int OptionsConsoleMinHeight = 120; // DX cluster console never shrinks below about 8 lines
} // namespace Dimensions

} // namespace K4Styles

#endif // K4CONSTANTS_H
