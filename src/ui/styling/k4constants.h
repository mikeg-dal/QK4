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
// =============================================================================
// App-Level Theme Colors (consolidated from K4Colors namespaces)
// =============================================================================

// Backgrounds
constexpr const char *Background = "#1a1a1a";
constexpr const char *DarkBackground = "#0d0d0d";
constexpr const char *PopupBackground = "#1e1e1e";
constexpr const char *SpectrumBackground = "#141414"; // Panadapter GPU clear color (spectrum bg)
constexpr const char *DisabledBackground = "#444444"; // Disabled indicator backgrounds

// App Accent Color
constexpr const char *AccentAmber = "#FFB000"; // TX indicators, labels, highlights

// VFO Theme Colors (square, passband, markers, overlays)
constexpr const char *VfoACyan = "#00BFFF";  // VFO A: cyan theme
constexpr const char *VfoBGreen = "#00FF00"; // VFO B: green theme

// Status Colors
constexpr const char *TxRed = "#FF0000";
constexpr const char *StatusGreen = "#00FF00"; // Active/on/connected status indicators
// Meter Gradient Colors (green → yellow → red progression)
constexpr const char *MeterGreenDark = "#00CC00";
constexpr const char *MeterGreen = "#00FF00";
constexpr const char *MeterYellowGreen = "#CCFF00";
constexpr const char *MeterYellow = "#FFFF00";
constexpr const char *MeterOrange = "#FF6600";
constexpr const char *MeterOrangeRed = "#FF3300";
constexpr const char *MeterRed = "#FF0000";

// Id Meter Colors (PA drain current - subtle green theme)
constexpr const char *MeterIdDark = "#2E7D32";  // Muted forest green
constexpr const char *MeterIdLight = "#66BB6A"; // Lighter sage green

// Text Colors
constexpr const char *TextWhite = "#FFFFFF";
constexpr const char *TextDark = "#333333";
constexpr const char *TextGray = "#999999";
constexpr const char *TextFaded = "#808080"; // Faded text (e.g., auto mode values)
constexpr const char *InactiveGray = "#666666";

// Overlay/Indicator Colors
constexpr const char *OverlayBackground = "#707070"; // VFO indicator badges

// Overlay Panel Colors (Menu, Macros, and similar full-screen overlays)
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

// =============================================================================
// Button Gradient Colors
// =============================================================================

// Normal state gradients
constexpr const char *GradientTop = "#4a4a4a";
constexpr const char *GradientMid1 = "#3a3a3a";
constexpr const char *GradientMid2 = "#353535";
constexpr const char *GradientBottom = "#2a2a2a";

// Hover state gradients
constexpr const char *HoverTop = "#5a5a5a";
constexpr const char *HoverMid1 = "#4a4a4a";
constexpr const char *HoverMid2 = "#454545";
constexpr const char *HoverBottom = "#3a3a3a";

// Lighter button gradients (for TX function buttons, REC/STORE/RCL)
constexpr const char *LightGradientTop = "#888888";
constexpr const char *LightGradientMid1 = "#777777";
constexpr const char *LightGradientMid2 = "#6a6a6a";
constexpr const char *LightGradientBottom = "#606060";

// Selected button gradients
constexpr const char *SelectedTop = "#E0E0E0";
constexpr const char *SelectedMid1 = "#D0D0D0";
constexpr const char *SelectedMid2 = "#C8C8C8";
constexpr const char *SelectedBottom = "#B8B8B8";

// Borders
constexpr const char *BorderNormal = "#606060";
constexpr const char *BorderHover = "#808080";
constexpr const char *BorderPressed = "#909090";
constexpr const char *BorderSelected = "#AAAAAA";

// Dialog-specific colors
constexpr const char *DialogBorder = "#333333"; // Dialog borders and separators
constexpr const char *PanelBorder = "#444444";  // Subtle frame border for panadapter/VFO panels
constexpr const char *ErrorRed = "#FF6666";     // Error/not connected status indicators
constexpr const char *ErrorBgDark = "#331111";  // Muted dark-red background for error toasts (pairs with ErrorRed)

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
constexpr const char *BandPlanCw = "#CB3B45";        // CW / Morse (crimson red)
constexpr const char *BandPlanData = "#E0852F";      // Digital / data (RTTY, PSK, FT8...) orange
constexpr const char *BandPlanPhone = "#6E83A6";     // SSB / phone (voice) slate blue
constexpr const char *BandPlanBeacon = "#AB47BC";    // Beacons (exclusive) purple
constexpr const char *BandPlanAll = "#90A4AE";       // All modes share (CW/data/phone) blue-grey
constexpr const char *BandPlanOutOfBand = "#3A3A3A"; // Past the band edge (under the hatch)
constexpr const char *BandPlanTag = "#0F0F0F";       // Band-name tag ("20m")
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
// Popup Layout
// =============================================================================
constexpr int PopupButtonWidth = 70;
constexpr int PopupButtonHeight = 44;
constexpr int PopupButtonSpacing = 8;
constexpr int MenuBarButtonWidth = 90; // Bottom menu bar buttons (fits "MAIN RX")
constexpr int PopupContentMargin = 12;

// =============================================================================
// Common UI Heights
// =============================================================================
constexpr int SeparatorHeight = 1; // Horizontal/vertical separator lines
constexpr int MenuItemHeight = 40; // Menu overlay items, frequency labels
constexpr int MenuBarHeight = 52;  // Bottom menu bar container height

// =============================================================================
// Common UI Widths
// =============================================================================
constexpr int SmallIconSize = 20;        // Lock icons, health indicator, small controls
constexpr int CompactButtonSize = 32;    // Side panel icons, EQ +/- buttons
constexpr int FormLabelWidth = 80;       // Form field labels, numeric value displays
constexpr int VfoSquareSize = 45;        // VFO A/B indicator squares and mode labels
constexpr int NavButtonWidth = 54;       // Navigation buttons in overlays
constexpr int LeftSidePanelWidth = 105;  // Left side panel (volume, controls)
constexpr int RightSidePanelWidth = 130; // Right side panel (function buttons, KPA1500)
constexpr int NavPanelWidth = 130;       // Menu overlay navigation/search panel
constexpr int MemoryButtonWidth = 42;    // M1-M4, REC, STORE, RCL buttons

// =============================================================================
// Font Sizes (in pixels) - use with QFont::setPixelSize() or paintFont()
// =============================================================================
constexpr int FontSizeTiny = 7;          // Sub-labels (BANK, AF REC, MESSAGE)
constexpr int FontSizeSmall = 8;         // Scale fonts, secondary text
constexpr int FontSizeNormal = 9;        // Alt/secondary button text
constexpr int FontSizeMedium = 10;       // Labels, descriptions
constexpr int FontSizeLarge = 11;        // Feature labels, primary labels
constexpr int FontSizeButton = 12;       // Button text, value displays
constexpr int FontSizeNotification = 13; // Toast / macro-dialog notifications (between Button and Popup)
constexpr int FontSizePopup = 14;        // Notifications, popup titles
constexpr int FontSizeTitle = 16;        // Large control buttons (+/-)
constexpr int FontSizeIndicator = 18;    // TX/RX/SPLIT indicator labels
constexpr int FontSizeFrequency = 32;    // VFO frequency displays (FrequencyDisplayWidget, VfoWidget)

// DX cluster spot labels — user-configurable in Options → DX Cluster (FontSizeSpotMin..FontSizeSpotMax).
constexpr int FontSizeSpot = 11;
constexpr int FontSizeSpotMin = 8;
constexpr int FontSizeSpotMax = 16;

// =============================================================================
// Popup Menu Font Sizes (standardized for horizontal control bar popups)
// =============================================================================
constexpr int PopupTitleSize = 12;  // Popup title labels (e.g., "MIC INPUT", "ATTENUATOR")
constexpr int PopupButtonSize = 11; // Popup selection buttons (e.g., "FRONT", "REAR")
constexpr int PopupValueSize = 12;  // Value displays (e.g., "6 dB", "184 Hz")

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
constexpr int ChartPopupGap = 4;            // Gap between the popup card and its anchor widget
constexpr int ChartScaleTickLen = 5;        // Length of the left-edge scale tick marks

// =============================================================================
// VFO Indicator Dimensions
// =============================================================================
constexpr int VfoIndicatorWidth = 34;  // VFO A/B label width on panadapter
constexpr int VfoIndicatorHeight = 30; // VFO A/B label height on panadapter

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
} // namespace Dimensions

} // namespace K4Styles

#endif // K4CONSTANTS_H
