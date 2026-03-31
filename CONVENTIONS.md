# Conventions

Code style, naming conventions, and development rules for QK4.

## Development Rules

- Preserve existing functionality when adding features
- Use Qt signal/slot for inter-component communication
- Use `QByteArray` for binary protocol data
- Test with actual K4 hardware when possible
- Keep custom widgets self-contained with clear signal interfaces

### Clean Coding Principles

1. **Do it right the first time** - A proper implementation now prevents technical debt later.

2. **Avoid patchwork** - If a feature requires changes to multiple components, make cohesive changes rather than scattered workarounds.

3. **Consistent patterns** - Follow established patterns:
   - RadioState for all radio state with signals for UI updates
   - Signal/slot connections in MainWindow for orchestration
   - Protocol for CAT parsing, emitting typed signals
   - Widgets self-contained with clear public interfaces

4. **Write descriptive commits** - `CHANGELOG.md` is auto-generated from conventional commits at release time. Use commit body text for details that should appear in the changelog (see Commit Messages below).

5. **Parse order matters** - Check specific patterns FIRST (e.g., `RG$` before `RG`)

6. **Initialize state to trigger signals** - Use invalid initial values (`-1`, `-999`) for state that needs to emit signals on first update

---

## Naming Conventions

| Element | Convention | Example |
|---------|------------|---------|
| Classes | PascalCase | `PanadapterRhiWidget`, `RadioState` |
| Methods | camelCase | `updateSpectrum()`, `setRefLevel()` |
| Member Variables | m_camelCase | `m_frequencyALabel`, `m_tcpClient` |
| Signals | camelCase | `frequencyChanged`, `clicked` |
| Slots | on + Source + Event | `onFrequencyChanged`, `onConnectClicked` |
| Constants | SCREAMING_SNAKE | `WATERFALL_HISTORY`, `TOTAL_BANDWIDTH_HZ` |
| Namespaces | PascalCase | `K4Styles`, `K4Styles::Colors` |

## Member Variable Prefixes

```cpp
// Pointers to UI elements
QLabel *m_frequencyALabel;      // Labels: m_<purpose>Label
QPushButton *m_spanUpBtn;       // Buttons: m_<name>Btn
SMeterWidget *m_sMeterA;        // Widgets: m_<name> or m_<name><VFO>
QStackedWidget *m_vfoAStackedWidget;

// A/B suffix for dual-VFO components
m_frequencyALabel, m_frequencyBLabel
m_sMeterA, m_sMeterB
m_panadapterA, m_panadapterB
```

## Qt Patterns

- **Signal/Slot**: Use `connect()` with lambda or member function pointers
- **Binary Data**: Use `QByteArray` for protocol data
- **Layouts**: Prefer `QVBoxLayout`/`QHBoxLayout`, use `QStackedWidget` for page switching
- **Styling**: Use `setStyleSheet()` with CSS-like syntax
- **Custom Painting**: Override `paintEvent()`, use `QPainter` with `QPainterPath`

---

## Code Formatting (clang-format)

| Aspect | Style |
|--------|-------|
| Indentation | 4 spaces |
| Line length | 120 characters max |
| Braces | Same line (Attach) |
| Pointers | Right-aligned (`Type *name`) |
| Includes | Preserve order (no auto-sort) |

```bash
# Check formatting
find src -name '*.cpp' -o -name '*.h' | xargs clang-format --dry-run --Werror

# Auto-format
find src -name '*.cpp' -o -name '*.h' | xargs clang-format -i
```

### Example

```cpp
void MainWindow::onFrequencyChanged(quint64 freq)
{
    if (freq > 0) {
        m_frequencyLabel->setText(QString::number(freq));
        m_radioState->setFrequency(freq);
    }
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_tcpClient(new TcpClient(this)),
      m_radioState(new RadioState(this))
{
    setupUi();
}
```

---

## Pre-Commit Checklist

**REQUIRED before every commit:**

```bash
# 1. Run lint check (MUST pass before commit)
find src -name '*.cpp' -o -name '*.h' | xargs clang-format --dry-run --Werror

# 2. Auto-fix if lint fails
find src -name '*.cpp' -o -name '*.h' | xargs clang-format -i
```

If lint fails in CI, it means this step was skipped locally.

## Code Review Checklist

Before considering a feature complete:
- [ ] Does it follow existing patterns?
- [ ] Are signals/slots properly connected?
- [ ] Is state initialized correctly?
- [ ] Lint check passes? (REQUIRED - run pre-commit checklist above)
- [ ] Would another developer understand this code?

## Commit Messages

Commits drive the auto-generated changelog. Use [conventional commit](https://www.conventionalcommits.org/) format:

```
type(scope): short summary

Optional body with detail — each line becomes a sub-bullet in the changelog.
```

| Type | Changelog Section | Example |
|------|-------------------|---------|
| `feat` | Added | `feat(audio): add jitter buffer for RX playback` |
| `fix` | Fixed | `fix(panadapter): correct CW pitch offset in passband` |
| `refactor` | Changed | `refactor(state): replace if-else chain with handler registry` |
| `perf` | Changed | `perf(dsp): reduce CPU usage with GPU spectrum rendering` |
| `docs` | *(skipped)* | `docs: update README build instructions` |
| `chore` | *(skipped)* | `chore: bump Qt to 6.8` |
| `ci` | *(skipped)* | `ci: add Raspberry Pi build job` |

**Tips:**
- The `(scope)` is optional but makes changelog entries scannable (e.g., `audio`, `panadapter`, `state`, `ui`)
- Put detail in the commit body — it flows into the changelog as sub-bullets
- Keep the summary line under 72 characters

---

## Color Palette (K4Styles::Colors)

**Source of truth:** `src/ui/k4styles.h` - All colors are `constexpr const char*` in `K4Styles::Colors::*`

For the complete reference of all color and dimension constants, see `docs/K4STYLES_REFERENCE.md`.

```cpp
// App Accent Color
K4Styles::Colors::AccentAmber    // "#FFB000" - TX indicators, labels, highlights

// VFO Theme Colors (square, passband, markers, overlays)
K4Styles::Colors::VfoACyan       // "#00BFFF" - VFO A: cyan theme
K4Styles::Colors::VfoBGreen      // "#00FF00" - VFO B: green theme

// Backgrounds
K4Styles::Colors::Background        // "#1a1a1a"
K4Styles::Colors::DarkBackground    // "#0d0d0d"
K4Styles::Colors::PopupBackground   // "#1e1e1e"

// Text
K4Styles::Colors::TextWhite      // "#FFFFFF"
K4Styles::Colors::TextDark       // "#333333"
K4Styles::Colors::TextGray       // "#999999"
K4Styles::Colors::TextFaded      // "#808080"
K4Styles::Colors::InactiveGray   // "#666666"

// Status
K4Styles::Colors::TxRed          // "#FF0000"
K4Styles::Colors::StatusGreen       // "#00FF00"
```

### Spectrum Colors

- **Main Panadapter**: QRhi gradient (green fill with lime line)
- **Mini-Pan Line**: VfoACyan `#00BFFF` for A, VfoBGreen `#00FF00` for B
- **Waterfall**: 8-stage LUT (Black → Blue → Cyan → Green → Yellow → Red)

## Fonts

Embedded HD fonts for crisp rendering on Retina/4K displays.

| Font | Type | Usage |
|------|------|-------|
| **Inter** | Sans-serif | All UI text, labels, and data displays |

Inter is used everywhere with tabular figures (`font-feature-settings: 'tnum'`) for numeric displays to ensure consistent digit widths.

### Font Constants (K4Styles::Fonts)

| Constant | Value | Usage |
|----------|-------|-------|
| `Fonts::Primary` | "Inter" | UI text, labels, buttons |
| `Fonts::Data` | "Inter" | Frequencies, numeric data (with tabular figures) |

### Font Size Constants (K4Styles::Dimensions)

| Constant | Size | Usage |
|----------|------|-------|
| `FontSizeFrequency` | 32px | VFO frequency display |
| `FontSizeTitle` | 16px | Large control buttons (+/-) |
| `FontSizePopup` | 14px | Notifications, popup titles |
| `FontSizeButton` | 12px | Button text, value displays |
| `FontSizeLarge` | 11px | Feature labels, primary labels |
| `FontSizeMedium` | 10px | Labels, descriptions |
| `FontSizeNormal` | 9px | Alt/secondary button text |
| `FontSizeSmall` | 8px | Scale fonts, secondary text |
| `FontSizeTiny` | 7px | Sub-labels (BANK, AF REC) |

### Font Usage

All font sizes are in **pixels** — use `setPixelSize()` (not `setPointSize()`) for cross-platform consistency.

```cpp
// Custom-painted widgets: use paintFont() helper
QFont labelFont = K4Styles::Fonts::paintFont(K4Styles::Dimensions::FontSizeLarge);

// Or set pixel size directly on an existing font
QFont font = font();
font.setPixelSize(K4Styles::Dimensions::FontSizeButton);

// Frequency/data display (use dataFont helper for tabular figures)
QFont dataFont = K4Styles::Fonts::dataFont(K4Styles::Dimensions::FontSizeFrequency);

// UI text via stylesheet (already uses px)
label->setStyleSheet("font-size: 12px; font-weight: bold;");

// In stylesheets, use font constants with tnum for numeric displays
QString style = QString("font-family: '%1'; font-feature-settings: 'tnum';")
    .arg(K4Styles::Fonts::Data);
```

## Popup & Button Styling (K4Styles)

**Source of truth:** `src/ui/k4styles.h` - Use K4Styles functions instead of inline CSS.

### Stylesheet Functions (for QPushButton)

| Function | Usage |
|----------|-------|
| `K4Styles::popupButtonNormal()` | Standard dark gradient popup buttons |
| `K4Styles::popupButtonSelected()` | Light/white selected state |
| `K4Styles::menuBarButton()` | Bottom menu bar buttons (with padding) |
| `K4Styles::menuBarButtonActive()` | Active/inverted menu button state |
| `K4Styles::menuBarButtonSmall()` | Compact +/- buttons |

### QPainter Helpers (for custom-painted widgets)

```cpp
// Fonts (pixel-based for cross-platform consistency)
QFont K4Styles::Fonts::paintFont(pixelSize, weight)  // General-purpose paint font
QFont K4Styles::Fonts::dataFont(pixelSize, weight)    // Tabular-figure font for numbers

// Gradients
QLinearGradient K4Styles::buttonGradient(top, bottom, hovered)
QLinearGradient K4Styles::selectedGradient(top, bottom)

// Colors
QColor K4Styles::borderColor()         // Normal border
QColor K4Styles::borderColorSelected() // Selected border

// Shadow
K4Styles::drawDropShadow(painter, contentRect, cornerRadius)
```

### Dimension Constants (K4Styles::Dimensions::*)

| Constant | Value | Purpose |
|----------|-------|---------|
| `ShadowMargin` | 20 | Space around popup for shadow |
| `PopupContentMargin` | 12 | Padding inside popup |
| `PopupButtonWidth` | 70 | Standard popup button |
| `PopupButtonHeight` | 44 | Standard popup button |
| `BorderWidth` | 2 | Button border width |
| `BorderRadius` | 6 | Standard corner radius |

### Side Panel & Memory Button Styles

**Side Panel Function Buttons** (PRE/ATTN, TUNE/XMIT, etc.):
- Height: `ButtonHeightSmall` (28px)
- Width: Fills container (in 2-column grid)
- Dark style: `sidePanelButton()` - standard dark gradient
- Light style: `sidePanelButtonLight()` - lighter gradient for PF/TX buttons
- Sub-label: `FontSizeSmall` (8px), `AccentAmber` color

**Memory Buttons** (M1-M4, REC, STORE, RCL):
- Width: `MemoryButtonWidth` (42px)
- Height: `ButtonHeightSmall` (28px)
- M1-M4, REC: `sidePanelButton()` (dark)
- STORE, RCL: `sidePanelButtonLight()` (light)
- Sub-labels (BANK, AF REC, AF PLAY, MESSAGE): `FontSizeSmall` (8px)
- Sub-label colors: `AccentAmber` (BANK, AF REC, AF PLAY), `BorderSelected` (MESSAGE)

**Compact Buttons** (MON, NORM, BAL):
- Height: `ButtonHeightMini` (24px)
- Style: `compactButton()`
- Used for small toggle buttons in horizontal rows

**Creating popups:** See `PATTERNS.md` → "Adding a Popup Menu" for current patterns.

---

## Architecture Rules

These rules prevent the architectural issues identified in the 2026-03-30 audit. They are non-negotiable for all new code and refactoring work.

### 1. No Duplicated Static Functions

If a function is needed in more than one translation unit, it goes in `src/utils/` with a namespace (e.g., `RadioUtils::`). Copy-pasting a static function into another `.cpp` file is a defect. Fix it immediately.

### 2. Controllers Do Not Expose Owned Objects

Controllers expose **task-level APIs**, not internal workers. No `audioEngine()`, `kpodDevice()`, or `tcpClient()` accessors in the public interface. If external code needs to configure a device, add a method to the controller (e.g., `audioController->setInputDevice(...)` instead of `audioController->audioEngine()->setInputDevice(...)`).

**Exception:** `tcpClient()` is exposed for AudioController's performance-sensitive audio data path and CatServer's direct TCP forwarding. These are documented exceptions, not precedent.

### 3. No Non-Const References to Shared State

Never return `Type&` from a getter when multiple callers may read or write. Use `const Type&` for read access and typed setters for mutations. This prevents data races and makes state changes auditable.

### 4. RadioState is Main-Thread Only

`parseCATCommand()` is enforced by `Q_ASSERT(QThread::currentThread() == thread())`. All callers must be on the main (GUI) thread. If cross-thread parsing is ever needed, use `QMetaObject::invokeMethod` with `Qt::QueuedConnection`.

### 5. Network Buffers Have Explicit Size Limits

Any buffer that accumulates data from an external source must check against a maximum size and handle overflow (disconnect, discard, or reset). Use `K4Protocol::MAX_BUFFER_SIZE` (1MB) as the default limit.

### 6. Parser Changes Require Test Cases

Any modification to `RadioState::parseCATCommand()` or its handlers must include a corresponding test case in `tests/test_radiostate.cpp`. No merge without test coverage for the changed behavior.

### 7. No File Over 800 Lines

If a `.cpp` or `.h` file grows past 800 lines, split it by responsibility before merging. Check with `wc -l` before committing.

### 8. Every Extraction is Traced First

Before moving code between classes: read every member variable, method, signal, and `connect()` call involved. Document what moves, what stays, and what the cross-domain dependencies are. Missing a dependency means a broken extraction.

### 9. One Commit Per Logical Change

Never combine structural moves with logic changes in the same commit. If a refactor introduces a new class AND fixes a bug, those are two commits. This enables `git bisect` and clean `git revert`.

### 10. Build + Format + Tests Before Every Commit

```bash
clang-format -i <changed files>
find src tests -name '*.cpp' -o -name '*.h' | xargs clang-format --dry-run --Werror
cmake --build build
ctest --test-dir build --output-on-failure
```

All four must pass. No exceptions.

### 11. Controlled Shutdown Order

Every controller and MainWindow calls `disconnect(this)` as the first statement in its destructor. This prevents queued signals from arriving during partial destruction. Thread shutdown follows the dependency chain: producers stop before consumers.
