#ifndef K4STYLES_H
#define K4STYLES_H

#include <QFont>
#include <QLinearGradient>
#include <QPainter>
#include <QSize>
#include <QString>

/**
 * @brief Centralized styling for QK4 UI components.
 *
 * This namespace provides consistent button and popup styles across all widgets,
 * eliminating duplicate CSS definitions and ensuring visual consistency.
 *
 * Style Categories:
 * - Popup buttons: Used in BandPopup, ModePopup, ButtonRowPopup, DisplayPopup
 * - Menu bar buttons: Used in BottomMenuBar, FeatureMenuBar
 * - Selected/Active states: Inverted colors for selected items
 */
namespace K4Styles {

// =============================================================================
// Popup Button Styles (BandPopup, ModePopup, ButtonRowPopup, DisplayPopup)
// =============================================================================

/**
 * @brief Standard dark gradient button for popup widgets.
 * Used for unselected band/mode buttons, popup action buttons.
 */
QString popupButtonNormal();

/**
 * @brief Light/white button for selected state in popups.
 * Used for currently selected band/mode.
 */
QString popupButtonSelected();

// =============================================================================
// Menu Bar Button Styles (BottomMenuBar, FeatureMenuBar)
// =============================================================================

/**
 * @brief Standard button for bottom menu bar (MENU, Fn, DISPLAY, BAND, etc.)
 * Includes padding suitable for text labels.
 */
QString menuBarButton();

/**
 * @brief Active/pressed state for menu bar buttons.
 * White background with dark text (inverted from normal).
 */
QString menuBarButtonActive();

/**
 * @brief Compact button for +/- controls in FeatureMenuBar.
 * Same gradient but no padding, larger font.
 */
QString menuBarButtonSmall();

/**
 * @brief PTT button pressed state.
 * Red background with white text for transmit indication.
 */
QString menuBarButtonPttPressed();

/**
 * @brief Standard horizontal slider stylesheet.
 * Amber handle on dark groove, uses Dimensions::Slider* constants.
 *
 * @param grooveColor Background color for the groove
 * @param handleColor Color for the handle and filled portion
 */
QString sliderHorizontal(const QString &grooveColor, const QString &handleColor);

/**
 * @brief Checkbox-style toggle button stylesheet.
 * Square button with gradient background, shows checkmark when checked.
 * Use with QPushButton::setCheckable(true).
 *
 * @param size Size of the checkbox (default 32x32)
 */
QString checkboxButton(int size = 32);

/**
 * @brief Radio button style for mode selection (DISPLAY ALL / USE SUBSET).
 * Circular indicator with gradient background.
 * Use with QPushButton::setCheckable(true).
 */
QString radioButton();

// =============================================================================
// Side Panel Button Styles
// =============================================================================

/**
 * @brief Compact button for small controls (MON, NORM, BAL).
 * Dark gradient with 1px border and 4px radius.
 */
QString compactButton();

/**
 * @brief Dark gradient button for side panel icons (?, globe).
 * Includes normal, hover, and pressed states.
 */
QString sidePanelButton();

/**
 * @brief Light gradient button for TX/PF function buttons.
 * Uses lighter grey gradient with hover border change only (no hover gradient).
 */
QString sidePanelButtonLight();

/**
 * @brief Panel button with disabled state support.
 * Used for KPA1500 panel buttons (STANDBY, ATU, ANT, TUNE).
 */
QString panelButtonWithDisabled();

// =============================================================================
// Dialog Button Styles
// =============================================================================

/**
 * @brief Standard dialog button style with all states.
 * Includes normal, hover, pressed, and disabled states.
 * Used for dialog action buttons (Connect, Save, Delete, etc.).
 */
QString dialogButton();

// =============================================================================
// Control Button Styles
// =============================================================================

/**
 * @brief Control button for decode windows.
 * @param selected If true, uses light/selected gradient. If false, uses dark gradient with disabled state.
 */
QString controlButton(bool selected = false);

// =============================================================================
// Common Style Constants
// =============================================================================

namespace Colors {
// =============================================================================
// App-Level Theme Colors (consolidated from K4Colors namespaces)
// =============================================================================

// Backgrounds
constexpr const char *Background = "#1a1a1a";
constexpr const char *DarkBackground = "#0d0d0d";
constexpr const char *PopupBackground = "#1e1e1e";
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

// Id Meter Colors (PA drain current - maroon theme)
constexpr const char *MeterIdDark = "#8B0000";  // Dark red/maroon
constexpr const char *MeterIdLight = "#CD5C5C"; // Lighter red highlight

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
constexpr const char *BorderLight = "#909090";

// Dialog-specific colors
constexpr const char *DialogBorder = "#333333"; // Dialog borders and separators
constexpr const char *ErrorRed = "#FF6666";     // Error/not connected status indicators
} // namespace Colors

namespace Dimensions {
// =============================================================================
// Border & Radius
// =============================================================================
inline int BorderWidth = 2;
inline int BorderRadius = 6;
inline int BorderRadiusLarge = 8;

// =============================================================================
// Shadow (for popup widgets)
// =============================================================================
inline int ShadowRadius = 16;
inline int ShadowOffsetX = 2;
inline int ShadowOffsetY = 4;
inline int ShadowMargin = ShadowRadius + 4; // 20px
inline int ShadowLayers = 8;

// =============================================================================
// Button Heights
// =============================================================================
inline int ButtonHeightLarge = 44;  // Menu overlay nav buttons
inline int ButtonHeightMedium = 36; // Bottom menu bar, popup buttons
inline int ButtonHeightSmall = 28;  // Function buttons (side panels)
inline int ButtonHeightMini = 24;   // Compact toggle buttons (MON, NORM, BAL, etc.)

// =============================================================================
// Popup Layout
// =============================================================================
inline int PopupButtonWidth = 70;
inline int PopupButtonHeight = 44;
inline int PopupButtonSpacing = 8;
inline int MenuBarButtonWidth = 90; // Bottom menu bar buttons (fits "MAIN RX")
inline int PopupContentMargin = 12;

// =============================================================================
// Common UI Heights
// =============================================================================
inline int SeparatorHeight = 1; // Horizontal/vertical separator lines
inline int MenuItemHeight = 40; // Menu overlay items, frequency labels
inline int MenuBarHeight = 52;  // Bottom menu bar container height

// =============================================================================
// Common UI Widths
// =============================================================================
inline int FormLabelWidth = 80;    // Form field labels in dialogs
inline int VfoSquareSize = 45;     // VFO A/B indicator squares and mode labels
inline int NavButtonWidth = 54;    // Navigation buttons in overlays
inline int SidePanelWidth = 105;   // Left and right side panels
inline int MemoryButtonWidth = 42; // M1-M4, REC, STORE, RCL buttons

// Main layout widths/heights
inline int CenterPanelWidth = 330;   // Center controls column between VFO A/B
inline int VfoColumnWidth = 270;     // VFO A/B column width
inline int VfoContentHeight = 150;   // VFO normal/mini-pan content height
inline int VfoMeterWidth = 260;      // TX meter width inside VFO column
inline int SpectrumMinHeight = 300;  // Minimum spectrum/waterfall section height
inline int VfoIndicatorBadgeWidth = 34;
inline int VfoIndicatorBadgeHeight = 30;

// VFO row internals
inline int VfoSquareWidgetSize = 30;
inline int VfoSquareWidgetTotalHeight = 40;
inline int VfoRowHeight = 65;
inline int VfoSubDivLabelWidth = 36;
inline int VfoSubDivLabelHeight = 14;

// =============================================================================
// Font Sizes (in pixels) - use with QFont::setPixelSize() or paintFont()
// =============================================================================
inline int FontSizeTiny = 7;       // Sub-labels (BANK, AF REC, MESSAGE)
inline int FontSizeSmall = 8;      // Scale fonts, secondary text
inline int FontSizeNormal = 9;     // Alt/secondary button text
inline int FontSizeMedium = 10;    // Labels, descriptions
inline int FontSizeLarge = 11;     // Feature labels, primary labels
inline int FontSizeButton = 12;    // Button text, value displays
inline int FontSizePopup = 14;     // Notifications, popup titles
inline int FontSizeTitle = 16;     // Large control buttons (+/-)
inline int FontSizeFrequency = 32; // VFO frequency displays (FrequencyDisplayWidget, VfoWidget)

// =============================================================================
// Popup Menu Font Sizes (standardized for horizontal control bar popups)
// =============================================================================
inline int PopupTitleSize = 12;   // Popup title labels (e.g., "MIC INPUT", "ATTENUATOR")
inline int PopupButtonSize = 11;  // Popup selection buttons (e.g., "FRONT", "REAR")
inline int PopupValueSize = 12;   // Value displays (e.g., "6 dB", "184 Hz")
inline int PopupAltTextSize = 10; // Alternate/secondary text on buttons

// =============================================================================
// Slider Dimensions
// =============================================================================
inline int SliderGrooveHeight = 6;     // Horizontal slider groove height
inline int SliderHandleWidth = 16;     // Slider handle width
inline int SliderHandleMargin = -5;    // Vertical margin for handle positioning
inline int SliderBorderRadius = 3;     // Groove border radius
inline int SliderHandleRadius = 8;     // Handle border radius (half of width)
inline int SliderValueLabelWidth = 40; // Width for percentage value labels

// =============================================================================
// Dialog Dimensions
// =============================================================================
inline int DialogMargin = 20;           // Dialog content margins
inline int TabListWidth = 150;          // Options dialog tab list width
inline int InputFieldWidthSmall = 100;  // Port number, small inputs
inline int InputFieldWidthMedium = 120; // Version labels, medium fields
inline int CheckboxSize = 18;           // Checkbox indicator dimensions
inline int PaddingSmall = 6;            // Small padding (inputs)
inline int PaddingMedium = 10;          // Medium padding (buttons)
inline int PaddingLarge = 15;           // Large padding (list items)
} // namespace Dimensions

/**
 * @brief Configure responsive dimensions for the current display.
 *
 * On phone-sized screens, this enables a compact profile with tighter spacing
 * and smaller fixed widths so the full UI remains usable.
 *
 * @param availableSize Available screen size in logical pixels
 * @param devicePixelRatio Screen DPR
 * @param physicalDiagonalInches Optional physical diagonal size in inches (0 if unknown)
 * @param forceCompact Force compact profile regardless of screen metrics
 */
void configureForScreen(const QSize &availableSize, qreal devicePixelRatio = 1.0,
                        qreal physicalDiagonalInches = 0.0, bool forceCompact = false);

/**
 * @brief True when compact phone layout profile is active.
 */
bool isCompactLayout();

namespace Fonts {
// =============================================================================
// Font Family Names - centralized for easy maintenance
// =============================================================================
constexpr const char *Primary = "Inter"; // UI text, labels, buttons, menus
constexpr const char *Data = "Inter";    // Frequencies, numeric data (uses tabular figures)

/**
 * @brief Create a font for custom-painted widgets with pixel-based sizing.
 *
 * Uses setPixelSize() instead of setPointSize() so that fonts render at
 * the same logical size on both macOS (72 PPI) and Windows (96 PPI).
 * On macOS where 1pt = 1px, this produces identical output to setPointSize().
 *
 * @param pixelSize Font size in pixels
 * @param weight Font weight (default: Bold)
 * @return QFont configured with pixel-based sizing
 */
QFont paintFont(int pixelSize, QFont::Weight weight = QFont::Bold);

/**
 * @brief Create a data display font with tabular figures enabled.
 *
 * Tabular figures ensure all digits have equal width, preventing visual
 * shifting when numeric values change (e.g., frequency displays).
 *
 * @param pixelSize Font size in pixels
 * @param weight Font weight (default: Bold)
 * @return QFont configured with tabular figures
 */
QFont dataFont(int pixelSize, QFont::Weight weight = QFont::Bold);

/**
 * @brief Get CSS font-family string for data displays in stylesheets.
 *
 * Returns a font-family declaration with tabular figures enabled.
 * Use in stylesheets for numeric/data displays.
 *
 * @return QString like "font-family: 'Inter'; font-feature-settings: 'tnum';"
 */
QString dataFontStylesheet();
} // namespace Fonts

// =============================================================================
// Utility Functions
// =============================================================================

/**
 * @brief Draw a soft drop shadow behind popup content.
 *
 * Uses 8-layer blur technique for smooth shadow appearance.
 * Call this in paintEvent() before drawing content.
 *
 * @param painter The painter to draw with (should have NoPen set)
 * @param contentRect The rectangle of the popup content (not including shadow margin)
 * @param cornerRadius Corner radius for the shadow rounded rect
 */
void drawDropShadow(QPainter &painter, const QRect &contentRect, int cornerRadius = 8);

/**
 * @brief Create a standard button gradient for custom-painted widgets.
 *
 * @param top Y coordinate of gradient start
 * @param bottom Y coordinate of gradient end
 * @param hovered Whether button is in hover state
 * @return QLinearGradient configured with proper color stops
 */
QLinearGradient buttonGradient(int top, int bottom, bool hovered = false);

/**
 * @brief Create a selected/active button gradient.
 *
 * @param top Y coordinate of gradient start
 * @param bottom Y coordinate of gradient end
 * @return QLinearGradient configured with light/selected colors
 */
QLinearGradient selectedGradient(int top, int bottom);

/**
 * @brief Get the standard border color for buttons.
 * @return QColor for button borders
 */
QColor borderColor();

/**
 * @brief Get the selected/active border color.
 * @return QColor for selected button borders
 */
QColor borderColorSelected();

/**
 * @brief Create a standard meter gradient (green → yellow → red).
 *
 * Used for S-meter, mic level, ALC, compression, and similar meters.
 * Provides consistent visual language across all level indicators.
 *
 * @param x1 X coordinate of gradient start
 * @param y1 Y coordinate of gradient start
 * @param x2 X coordinate of gradient end
 * @param y2 Y coordinate of gradient end
 * @return QLinearGradient with standard meter color stops
 */
QLinearGradient meterGradient(qreal x1, qreal y1, qreal x2, qreal y2);

} // namespace K4Styles

#endif // K4STYLES_H
