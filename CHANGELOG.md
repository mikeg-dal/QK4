# Changelog

All notable changes to QK4 will be documented in this file.
This changelog is auto-generated from [conventional commits](https://www.conventionalcommits.org/) at release time.


## [0.5.0-beta.7] - 2026-03-20

### Added

- **net**: Add K4 UDP discovery with LAN scan button in Server Manager (PR #74, ny4i)
- **ui**: Add startup macro support — CAT commands sent pre-RDY on connect (PR #74, ny4i)

### Fixed

- **net**: Resolve mDNS `.local` hostnames before connecting — fixes TLS timeout (PR #76, ny4i)
- **net**: Handle ARP-cold connect failure on fresh app launch
- **ui**: Block ref level +/- and wheel when auto-ref is active
- **ui**: Prevent crash on app close when health bar popup is visible
- **cw,net**: Remove debug logging and dead code from CW/network paths

---

## [0.4.0-beta.3] - 2026-02-12

### Added

- **audio**: Add jitter buffer for RX audio playback
  - Decoded PCM was written directly to QAudioSink, so any network jitter
  - caused buffer underruns heard as crackle/pop artifacts. Add a small
  - packet queue with a 10ms feed timer and ~40ms prebuffer (2 Opus packets)
  - to absorb jitter before draining to the audio device.
  - Rename playAudio() to enqueueAudio() — packets queued, not written directly
  - feedAudioDevice() drains queue on 10ms timer, respecting sink bytesFree()
  - Prebuffering holds playback until 2 packets accumulate
  - Overflow cap at 50 packets (~1s) drops oldest to bound memory
  - stop() clears queue and resets prebuffer for clean reconnect
- **audio**: Add jitter buffer for RX audio playback
  - Decoded PCM was written directly to QAudioSink, so any network jitter
  - caused buffer underruns heard as crackle/pop artifacts. Add a small
  - packet queue with a 10ms feed timer and ~40ms prebuffer (2 Opus packets)
  - to absorb jitter before draining to the audio device.
  - Rename playAudio() to enqueueAudio() — packets queued, not written directly
  - feedAudioDevice() drains queue on 10ms timer, respecting sink bytesFree()
  - Prebuffering holds playback until 2 packets accumulate
  - Overflow cap at 50 packets (~1s) drops oldest to bound memory
  - stop() clears queue and resets prebuffer for clean reconnect

### Fixed

- **audio**: Reduce jitter prebuffer to 1 packet (~20ms)
  - 2-packet prebuffer introduced noticeable audio-visual desync — meters
  - and waterfall updated instantly while audio lagged ~40ms behind.
  - Reduce to 1 packet for tighter sync while still decoupling the write
  - path from the event loop.
- **state**: Use sentinel defaults for 17 RadioState fields to ensure initial signal emission
  - When connecting to a K4 in USB mode, the modeChanged signal was suppressed
  - because m_mode was already initialized to USB. The change guard (if old != new)
  - silently dropped the first update, leaving the side panel showing CW controls
  - instead of voice controls. Same pattern affected 16 other fields (tuning step,
  - filter bandwidth/position, antenna, ATU, message bank, audio mix, balance).
  - Changed all 17 fields to sentinel values (-1, Unknown, -99) that can never
  - match real K4 state, guaranteeing the first RDY dump value always triggers
  - the signal. Added Unknown case to modeToString() returning empty string.

---

## [0.4.0-beta.2] - 2026-02-12

### Added

- **audio**: Add stereo output, MX routing, SUB mute, and BL balance
  - Change audio engine from mono to stereo Float32 output (L=Main, R=Sub)
  - Parse MX command (RX Audio Mix) for all 10 routing modes that map
  - main/sub to L/R output channels when SUB RX is on (menu 105)
  - SUB RX off now produces true silence on sub channel — sub slider
  - has zero effect when sub receiver is disabled
  - BL balance applies as final L/R gain after MX routing, works
  - regardless of SUB RX state (per K4 manual)
  - Wire subRxEnabledChanged to mute/unmute sub audio channel
  - Wire audioMixChanged and balanceChanged to OpusDecoder
  - Fix BAL overlay: restore full-size button, move MAIN/SUB labels to
  - bottom, remove grey background artifacts on value rows

### Fixed

- **ui**: Wire REV button with press/release pattern (SW160/SW161)
  - REV is a momentary button — press sends SW160 to temporarily move
  - VFO B frequency to VFO A, release sends SW161 to restore.
  - Replaced clicked signal with pressed/released signal pair.
- **ui**: Popups follow app window to secondary monitor
  - Replace QApplication::primaryScreen() with referenceWidget->screen()
  - in all 8 popup positioning functions. Popups now clamp to whichever
  - screen the app window is on instead of always snapping to primary.

---

## [0.3.0-beta.5] - 2026-02-08

### Added

- **ui**: Add WheelAccumulator for trackpad scroll handling, fix PWR init bug
  - Replace per-widget angleDelta rounding with shared WheelAccumulator that
  - accumulates fractional trackpad deltas into discrete steps across all 16
  - wheel-enabled widgets. Enable trackpad momentum for panadapter frequency
  - tuning (flick-to-tune). Fix PWR display showing "--" on connect when
  - radio is at 50W QRO due to m_rfPower default matching radio state.

---

## [0.3.0-beta.3] - 2026-02-07

### Fixed

- **kpod**: Use hid_open_path() on all platforms and ship udev rule for Pi
  - hid_open() fails on Linux with "hid_error is not implemented yet" because
  - the hidraw backend requires permissions that non-root users don't have.
  - Switch to hid_open_path() on all platforms (was Windows-only) so the exact
  - device found during enumeration is opened. Add retry logic for all platforms.
  - Keep the Windows hid_open() VID/PID fallback for stale device path handling.
  - Ship a udev rule (99-kpod.rules) in the Pi bundle and auto-install it on
  - first launch via run.sh.
- **kpod**: Prevent libusb mutex crash on Linux by managing hidapi lifecycle
  - On Linux (libusb backend), calling hid_exit() in checkDevicePresence()
  - destroys the libusb context while a device handle is open from openDevice(),
  - causing a pthread_mutex_destroy assertion failure and crash.

---

## [0.3.0-beta.2] - 2026-02-07

### Fixed

- **ci**: Fix Pi cache save by fixing Docker root ownership
  - Docker writes apt and ccache files as root. The actions/cache step
  - runs as the runner user and fails to tar root-owned files. Add
  - sudo chown after docker run to transfer ownership before cache save.
- **ci**: Switch Windows to Ninja for sccache and warm caches on main
  - sccache was silently ignored on Windows because CMAKE_CXX_COMPILER_LAUNCHER
  - only works with single-config generators. Switch to Ninja (with MSVC dev
  - env setup) so sccache can intercept cl.exe invocations.
  - Also add push-to-main triggers on all three standalone build workflows so
  - dependency caches are saved on the main ref, allowing tag-scoped release
  - builds to restore them via restore-keys prefixes.

---

## [0.3.0-beta.1] - 2026-02-07

### Fixed

- **pi**: Clean up bundle with strict library checks and remove dead plugins
  - Replace silent || true on all library copies with require_lib/optional_lib
  - helpers that fail the build immediately when a required library is missing.
  - Remove multimedia plugins (GStreamer backend) that can't load without
  - unbundled dependencies — the audio engine uses QAudioSink/QAudioSource
  - directly through PulseAudio/ALSA. Set QT_QPA_PLATFORM=xcb in the launcher
  - since we only ship XCB plugins. Add a post-copy verification step that
  - validates all critical libraries landed in the bundle before packaging.
- **pi**: Pass release version into Pi build for consistent versioning
  - Accept version as first argument in build-pi.sh and pass it to CMake
  - as K4CONTROLLER_VERSION_FULL. Update the release workflow to extract
  - the tag version and forward it to the Pi build, matching how macOS
  - and Windows builds already receive the version string.
- **ci**: Add dependency caching across all platforms
- **pi**: Use cp -rL in bundle helpers to handle directories
  - The optional_lib and require_lib helpers used cp -L which fails on
  - directories like xcbglintegrations. Use cp -rL so both files and
  - directories are handled correctly.
- **pi**: Expand libpulsecommon glob before passing to cp
  - require_lib quotes its arguments which prevents wildcard expansion.
  - Resolve the libpulsecommon-*.so glob with ls first, then copy the
  - resolved path.

---

## [0.2.0-beta.24] - 2026-02-07

### Fixed

- **ci**: Speed up release builds with sccache upgrade and parallel compilation
  - Upgrade sccache-action v0.0.7 → v0.0.9 to fix 0% cache hit rate caused
  - by GitHub cache service v2 migration. Add sccache to macOS builds, enable
  - parallel compilation via CMAKE_BUILD_PARALLEL_LEVEL, and pin vcpkg to the
  - runner image version to prevent cache invalidation from git pull.

---

## [0.2.0-beta.23] - 2026-02-06

### Fixed

- **pi**: Bundle libpulsecommon to fix PulseAudio version mismatch
  - The Pi build bundles libpulse.so.0 from Debian Trixie (PulseAudio 17)
  - but was missing its private dependency libpulsecommon-17.0.so. On
  - Bookworm-based Pi OS, only libpulsecommon-16.x exists, causing a
  - runtime load failure.

---

## [0.2.0-beta.22] - 2026-02-06

### Fixed

- Remove KPA1500, Network, and DX Cluster tabs from Options dialog
  - These features aren't ready for public use. The underlying infrastructure
  - (KPA1500Client, KPA1500Window, RadioSettings keys) remains intact for
  - re-adding later.

---

## [0.2.0-beta.20] - 2026-02-06

### Fixed

- Make Qt6 GuiPrivate optional on Linux for cross-compilation
  - Debian cross-compilation may not ship Qt6GuiPrivateConfig.cmake.
  - Use the same graceful fallback as Windows — try to find it, fall
  - back to manual include paths if not available.
- **ci**: Link Homebrew packages after cache restore, GuiPrivate fallback
  - Add brew link --overwrite after install to fix cached-but-unlinked
  - packages causing cmake to fail finding Qt6
  - Make Qt6 GuiPrivate optional on Linux for cross-compilation
- **ci**: Remove Homebrew Cellar cache that breaks macOS builds
  - Caching /opt/homebrew/Cellar/* without the /opt/homebrew/opt/*
  - symlinks causes brew install to see packages as "already installed
  - but not linked", leaving Qt undiscoverable by cmake.

### Revert

- Remove broken Homebrew optimizations from macOS CI
  - Revert HOMEBREW_NO_AUTO_UPDATE, brew link, and ccache additions
  - that broke macOS builds. The Cellar cache + NO_AUTO_UPDATE caused
  - brew to skip relinking, leaving Qt undiscoverable by cmake.

---

## [0.2.0-beta.19] - 2026-02-06

### Fixed

- Update lint CI and remove dead CLAUDE.md references
  - Remove clang-format version pin in CI (use system default)
  - Add development branch to lint CI triggers
  - Remove all dead CLAUDE.md references from docs

---

## [0.2.0-beta.18] - 2026-02-06

### Added

- Honor K4 "Mouse L/R Button QSY" menu setting
  - Discover menu item ID dynamically via MEDF on connect
  - Track live changes via menuValueChanged signal
  - "Left Only" mode: disable right-click/drag on both panadapters
  - "L=A R=B" mode: left always tunes VFO A, right always tunes VFO B

### Fixed

- Restore correct PC (power) command parsing for L/H modes
  - The handlePC() function was incorrectly rewritten during the refactor in
  - 4b704c3. Original code correctly checked L/H suffix, but refactored version
  - assumed 00/01 format causing QRO power to be divided by 10 (50W → 5W).
  - L mode (QRP): nnn = watts × 10, divide by 10 to get watts
  - H mode (QRO): nnn = watts directly, use as-is
  - X mode (XVTR): skip for now
- **ui**: Use font metrics for cross-platform button text positioning
  - Switch all custom-painted widgets from setPointSize() to setPixelSize()
  - to fix cramped text on Windows (96 PPI vs macOS 72 PPI). Add
  - K4Styles::Fonts::paintFont() helper. Zero visual change on macOS.

---

## [0.2.0-beta.17] - 2026-01-31

### Changed

- Remove dead code across 16 files (-121 lines)
  - Remove unused members, empty functions, and legacy code:
  - sidetonegenerator: ToneIODevice class, legacy methods (playDit/Dah, etc.)
  - bandpopupwidget: m_row1Buttons, m_row2Buttons, focusOutEvent
  - displaypopupwidget: focusOutEvent, m_currentRefLevel
  - notificationwidget: dismissed signal, setShowErrorCode
  - radiostate: autoNotchFilter, legacy NT parser
  - vfowidget: m_inactiveColor
  - panadapter_rhi: m_peakTrailColor
  - minipan_rhi: m_smoothingAlpha
  - kpa1500client: atuPresentChanged signal
  - audioengine: unused QBuffer include
- **ui**: Replace hardcoded font sizes with K4Styles constants
  - menuoverlay.cpp: 8x font-size: 14px → FontSizePopup
  - mainwindow.cpp: 12px → FontSizeButton, 14px → FontSizePopup
  - kpa1500panel.cpp: setPointSize(10) → FontSizeMedium
  - kpa1500window.cpp: font-size: 14px → FontSizePopup
  - textdecodewindow.cpp: font-size: 14px → FontSizePopup
  - txmeterwidget.cpp: 2x setPointSize(10) → FontSizeMedium
  - minipan_rhi.cpp: font-size: 9px → FontSizeNormal
- **ui**: Consolidate stylesheet templates to K4Styles functions
  - Replace 10+ inline stylesheet templates (with 7-16 positional arguments)
  - across 6 files with 6 new centralized K4Styles functions:
  - compactButton(): Small controls (MON, NORM, BAL)
  - sidePanelButton(): Dark gradient panel buttons
  - sidePanelButtonLight(): Light gradient TX/PF buttons
  - panelButtonWithDisabled(): Panel buttons with disabled state
  - dialogButton(): Dialog action buttons
  - controlButton(bool): Decode window controls
  - Additional cleanup:
  - Rename AgcGreen → StatusGreen (semantic naming for general active state)
  - Remove unused RitCyan constant (dead code)
  - Fix vforowwidget.cpp: use VfoBGreen for VFO B square (was using AgcGreen)
  - Net change: -58 lines (329 added to K4Styles, 387 removed from 6 files)
- Comprehensive code quality improvements across 4 sections
- Standardize fonts and memory button styling
  - Consolidate to Inter font with tabular figures for all UI
  - Add K4Styles::Fonts namespace with dataFont() helper
  - Remove JetBrains Mono fonts (saves ~825KB)
  - Refactor memory buttons to use sidePanelButton styles
  - Add MemoryButtonWidth, FontSizeFrequency constants
  - Increase centerWidget width 310→330px for proper button spacing
  - Update LSMinimumSystemVersion to 15.0 for macOS Tahoe
  - Update documentation with font and button conventions
- **ui**: Replace tuning rate underline with digit color indicator
  - Change tuning rate visualization from an underline below the frequency
  - to coloring the affected digits in gray. Digits at or below the current
  - tuning rate position are grayed out, showing which digits change when
  - tuning. This eliminates alignment issues caused by font metric differences.
- Remove paddle test UI and clean up code formatting
  - Remove dit/dah paddle test indicators from CW Keyer options page
  - Remove unused IndicatorSize and IndicatorSpacing K4Styles constants
  - Clean up if/return formatting in radiostate.cpp command handlers
  - Remove paddle state change signal connections from options dialog

### Fixed

- Restore correct SIFP and TM command parsing
  - The refactoring in 4b704c3 broke radio meter parsing:
  - SIFP: Was using fixed-position integers, now correctly parses
  - "VS:" and "IS:" prefixed key-value pairs for voltage/current
  - TM: Was using 4-digit fields, now correctly uses 3-digit fields
  - (TMaaabbbcccddd format) with QRP mode power scaling
  - Fixes voltage, current, power, and SWR display in UI.
- **ui**: Use font metrics for cross-platform button text positioning
  - Replace hard-coded Y positions (18px, h-10) with QFontMetrics-based
  - calculations for DualControlButton text rendering. This fixes text
  - appearing cramped on Windows where font rendering produces taller
  - glyphs than macOS.
  - Calculate primary text baseline using font ascent + margin
  - Calculate alt text baseline using button height - descent - margin
  - Ensures consistent text positioning regardless of platform DPI or
  - font rendering differences

---

## [0.2.0-beta.16] - 2026-01-30

### Added

- **vfo**: Add VFO lock indicators with padlock arc visual
  - Add LK/LK$ command parsing in RadioState for lock state tracking
  - Create VfoSquareWidget with custom painting for lock arc
  - Add lockAChanged/lockBChanged signals and setLockA/setLockB methods
  - Wire LOCK A button (SW63) and LOCK B right-click (SW151)
  - Lock indicator shows semi-circular arc above VFO square (padlock effect)
- **panadapter**: Add right-click to tune opposite VFO
  - Right-click on waterfall A tunes VFO B, right-click on waterfall B tunes VFO A.
  - Click-and-drag supported for both left and right mouse buttons.
- **ui**: Add inline frequency editor and KPA1500 floating panel
  - Inline frequency editing (FrequencyDisplayWidget):
  - Click any digit to enter edit mode at that position
  - Digits change to VFO theme color (cyan for A, green for B)
  - Type digits to replace at cursor position with auto-advance
  - Arrow keys navigate between digits
  - Enter to send frequency, Escape or click-outside to cancel
  - Uses mouse grab to detect clicks outside widget
  - KPA1500 floating panel (KPA1500Panel, KPA1500Window):
  - Custom-painted amplifier control panel (320x250px)
  - Hero FWD power meter (0-1500W) with mini REF/SWR/TEMP meters
  - MODE/ATU/ANT/TUNE/RESET buttons with amber status LEDs
  - Floating window with draggable title bar
  - Position persistence via RadioSettings
  - Auto-connects when K4 connects, not on app start
- **panadapter**: Stabilize dB scale and add mouse wheel controls
  - Increase scale overlay fonts from 8pt to 9pt for better readability
  - Skip top/bottom dBm labels for visual breathing room
  - Fix dB range to use refLevel + scale formula (not hardcoded values)
  - Add Shift+Wheel for scale adjustment (±5 dB per step)
  - Add Ctrl+Wheel for reference level adjustment (±1 dB per step)
  - Add setScale() setter to RadioState for optimistic UI updates
  - Query #SCL on connect to sync initial scale from radio

### Changed

- **ui**: Remove unused FontSizeMicro constant
- **settings**: Rename rigctld to catServer with port 9299
  - Rename the CAT passthrough server settings from rigctld to catServer
  - to avoid confusion with the unrelated Hamlib rigctld utility.
- **ui**: Centralize hardcoded styles to K4Styles constants
  - Add IndicatorSpacing constant for dit/dah paddle indicators
  - Replace hardcoded setSpacing values with Dimensions constants
  - Replace hardcoded font-size values with FontSize constants
  - Clean up optionsdialog, macrodialog, displaypopup, antennacfgpopup

### Fixed

- **kpa1500**: Prevent crash on app shutdown and simplify status labels
  - Add MainWindow destructor to disconnect KPA1500 signals before child cleanup
  - Change KPA1500Client destructor to abort socket without emitting signals
  - Remove deprecated ^OS poll command and parser
  - Simplify status bar: "K4"/"KPA1500" with color indicating connection state
- **panadapter**: Switch to UP/DN commands when dragging past edges
  - When dragging past the visible spectrum edges, emit frequencyScrolled()
  - instead of frequencyDragged(). This delegates edge handling (page turns)
  - to the K4 radio, matching wheel scroll behavior.
  - Detect x < 0 or x > width during drag
  - Emit frequencyScrolled(-1/+1) at edges with 100ms rate limiting
  - Apply same logic to right-drag for opposite VFO tuning
  - Fixes hard stop at left edge and runaway scroll at right edge

### Security

- Remove proprietary PDFs and CLAUDE.md from tracking
  - Add *.pdf and **/CLAUDE.md to .gitignore
  - Remove all PDFs from resources/ (Elecraft internal docs)
  - Remove all CLAUDE.md files (AI-generated internal docs)
  - These files contain proprietary information and must never be
  - exposed publicly. History will be rewritten to purge all traces.

---

## [0.2.0-beta.15] - 2026-01-23

### Fixed

- **windows**: Add HID report ID byte for KPOD communication
  - Windows hidapi requires report ID (0x00) as the first byte for devices
  - without numbered reports. Without this, commands were being misinterpreted:
  - buf[0] was treated as report ID instead of command
  - Actual command data was shifted, causing zeros to be sent

---

## [0.2.0-beta.14] - 2026-01-23

### Added

- Add Display FPS and Streaming Latency settings
  - New features:
  - Display FPS menu item (12-30) in MENU overlay under APP SETTINGS
  - Streaming Latency setting (0-7) in Radio Manager dialog
  - Both settings are per-radio and persist
  - Technical changes:
  - Add synthetic menu item support (negative IDs for app-created items)
  - Query #FPS on connect, apply stored preference if different
  - Send SL command during connection initialization
  - Increase waterfall texture resolution (4096x1024) for HiDPI

### Changed

- **ui**: Replace hardcoded values with K4Styles constants
  - Replace hardcoded pixel values and colors with K4Styles::Dimensions
  - and K4Styles::Colors constants for better maintainability:
  - rxeqpopupwidget.cpp: ButtonHeightMini, PopupButtonSpacing, etc.
  - vforowwidget.cpp: FontSizeLarge for mode labels
  - mainwindow.cpp: DialogMargin for indicator height
  - textdecodewindow.cpp: ButtonHeightMedium, ButtonHeightMini, PaddingSmall
  - radiomanagerdialog.cpp: PopupContentMargin, DialogMargin, etc.
  - vfowidget.cpp: PopupButtonSpacing, BorderWidth, PaddingSmall
  - sidecontrolpanel.cpp: PaddingSmall, PopupButtonSpacing, ButtonHeightMini
  - Reduces K4Styles compliance violations from 184 to 163 (-11%).
  - Remaining violations are mostly stylesheet font-size values requiring
  - more extensive QString template refactoring.
- **dsp**: Replace Lanczos-3 with GPU bilinear interpolation
  - Simplify spectrum shader by removing custom 6-tap Lanczos kernel in favor
  - of hardware bilinear filtering. With 4096-wide textures, GPU interpolation
  - provides equivalent quality with less complexity.

### Fixed

- **ui**: Clean UI reset on disconnect, ALC meter scaling, SPAN default
  - Clear all UI state on disconnect: panadapters, minipans, VFOs, meters, menu
  - Fix waterfall artifact on disconnect by uploading zeroed buffer to GPU texture
  - Fix ALC meter scaling to use K4's 7-bar range instead of 10
  - Fix SPAN not updating from RDY by changing default from 10000 to 0
  - Fix duplicate frequency labels at narrow spans with adaptive decimal precision
- **windows**: Add retry logic for KPOD HID device open
  - Windows HID can have race conditions where the device path from
  - enumeration becomes stale before we can open it. This manifests as
  - "The system cannot find the file specified" (0x00000002) errors.

---

## [0.2.0-beta.13] - 2026-01-16

### Added

- Add application icon for macOS and Windows
  - Add K4Controller.ico for Windows (from Elecraft K4 Icon)
  - Generate K4Controller.icns for macOS from the .ico
  - Update CMakeLists.txt to include icon in macOS app bundle
  - Update Info.plist.in with CFBundleIconFile
  - Update K4Controller.rc.in with Windows icon resource

### Fixed

- **windows**: Use @ONLY syntax for icon path in RC file
  - CMake configure_file uses @VAR@ not ${VAR} with @ONLY option
- **ci**: Force link Homebrew packages after install
  - Cache may restore packages without symlinks, causing find_package to fail
- **ci**: Use qt instead of qt@6 (Homebrew formula renamed)
  - Homebrew renamed qt@6 to qt since Qt 6 is now the default version
  - Bust cache with v2 prefix to avoid stale cache issues
  - Remove unnecessary brew link commands (fresh cache will be clean)

---

## [0.2.0-beta.12] - 2026-01-16

### Debug

- **kpod**: Log all HID interfaces to find correct one on Windows
  - KPOD may expose multiple HID interfaces. Log usage_page and usage
  - for each to identify the correct vendor-defined interface.

---

## [0.2.0-beta.11] - 2026-01-16

### Fixed

- **kpod**: Use hid_open_path() on Windows instead of hid_open()
  - On Windows, hid_open(VID, PID) can fail even when hid_enumerate()
  - finds the device. Using hid_open_path() with the path from enumeration
  - is more reliable.

---

## [0.2.0-beta.10] - 2026-01-16

### Fixed

- **kpod**: Write debug log to TEMP directory on Windows
  - TEMP is guaranteed to exist and be writable, unlike AppDataLocation

---

## [0.2.0-beta.9] - 2026-01-16

### Fixed

- **release**: Fix hdiutil Resource busy error in DMG creation
  - Use ditto instead of cp -R for proper macOS bundle handling
  - Add sync and small delay to ensure filesystem is flushed
  - Add .metadata_never_index to prevent Spotlight interference
  - Add -nocrossdev flag to hdiutil

---

## [0.2.0-beta.8] - 2026-01-16

### Fixed

- **kpod**: Create log directory on Windows before writing

---

## [0.2.0-beta.7] - 2026-01-16

### Debug

- **kpod**: Add detailed Windows logging for HID troubleshooting
  - Write debug log to AppData/Local/K4Controller/kpod_debug.log on Windows
  - Log all hid_write/hid_read results and errors
  - Increase read timeout to 500ms on Windows
  - Helps diagnose KPOD detection issues on Windows 10/11

---

## [0.2.0-beta.6] - 2026-01-15

### Fixed

- **macos**: Switch release from ZIP to signed DMG, update bundle ID
  - Change bundle identifier from com.elecraft.k4controller to com.ai5qk.k4controller
  - Replace ZIP distribution with signed/notarized DMG
  - DMG includes Applications symlink for drag-to-install UX
  - Should resolve quarantine/TCC issues blocking network access on first launch

---

## [0.2.0-beta.5] - 2026-01-15

### Fixed

- **macos**: Add NSLocalNetworkUsageDescription for network permission prompt
  - macOS 14+ requires this Info.plist key to show the local network
  - permission dialog. Without it, connections to local network devices
  - (like the K4 radio) are silently blocked.

---

## [0.2.0-beta.4] - 2026-01-15

### Added

- **ui**: Meter improvements, DX Cluster tab, menu cleanup
  - Add 500ms peak hold time to TX meters before decay starts
  - Color S-meter +dB labels (20, 40, 60) red using TxRed
  - Add numeric labels (1, 3, 5, 7) to ALC meter scale
  - Make tuning rate indicator thicker (2px → 4px)
  - Remove redundant Connect > Radios menu (use globe button)
  - Add DX Cluster tab to Preferences dialog (placeholder)

### Fixed

- **audio**: Lower default mic gain from 50% to 25%
  - macOS mic input is typically hot, causing ALC overload at unity gain.
  - New default provides -6dB headroom out of the gate.

---

## [0.2.0-beta.3] - 2026-01-15

### Fixed

- **macos**: Add entitlements for microphone, network, USB, serial
  - Hardened runtime requires explicit entitlements for:
  - Network client access (K4 TCP connection)
  - Microphone input (TX audio)
  - Audio output (RX audio)
  - USB device access (KPOD)
  - Serial port access (HaliKey)
  - Without entitlements, macOS silently denies access with no permission prompt.

---

## [0.2.0-beta.2] - 2026-01-15

### Added

- **dsp**: Dual panadapter, Mini-Pan improvements, and passband cleanup
  - Fix dual panadapter frequency offset (spectrum was showing only partial range)
  - Fix Mini-Pan VFO B routing (discovered undocumented RX byte at position 4)
  - Add Mini-Pan color scheme: blue for A, green for B (matching main panadapter)
  - Add center frequency marker line to Mini-Pan
  - Remove passband edge lines for cleaner display (keep fill + center marker)
  - Add setPassbandColor() to MiniPanWidget for customizable passband colors
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **ui**: Display popup controls, ref/span A/B support, and right panel expansion
  - Display Popup Menu:
  - AVERAGE control group with +/- buttons (cycles 1, 5, 10, 15, 20)
  - DDC NB control group showing mode (OFF/ON/AUTO) and level (0-14)
  - Fixed Tune mode cycling through all 6 states (SLIDE1-TRACK)
  - VFO cursor mode toggling (A+/A-/A AUTO/A HIDE)
  - Waterfall color cycling (5 colors)
  - Peak mode and Freeze toggles via right-click
  - LCD/EXT target selection for display commands
  - All buttons now send CAT commands to K4
  - Ref Level & Span A/B Support:
  - A/B toggle selects which VFO's ref level/span value is displayed
  - +/- buttons target correct VFO based on A/B toggle selection
  - A+B mode targets both VFOs simultaneously for span changes
  - Ref level shows faded grey text when in auto mode
  - AUTO button toggles global auto-ref mode
  - Passband & Visual Improvements:
  - Passband indicator visibility synced with VFO cursor mode
  - Blue passband for VFO A, green for VFO B (matching K4 display)
  - Right Side Panel Expansion:
  - Added 8 new buttons in two 2x2 grids below existing 5x2
  - PF row: B SET/PF 1, CLR/PF 2, RIT/PF 3, XIT/PF 4
  - Bottom row: FREQ ENT/SCAN, RATE/KHZ, LOCK A/LOCK B, SUB/DIVERSITY
  - Visual spacing between button groups (16px/32px)
  - Bug Fixes:
  - AUTO button correctly highlights in auto-ref mode (fixed #AR parsing)
  - PAN button face updates when radio changes panadapter mode
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **dsp**: GPU-accelerated spectrum/waterfall via OpenGL
  - Migrate PanadapterWidget and MiniPanWidget from QPainter to QOpenGLWidget
  - for dramatically improved rendering performance.
  - Key changes:
  - GLSL 120 shaders (macOS OpenGL 2.1 compatibility)
  - GL_LUMINANCE texture format for waterfall circular buffer
  - Device pixel ratio support for HiDPI/Retina displays
  - Color LUT texture (256×1 RGBA) for waterfall coloring
  - Spectrum line strip rendering with peak hold
- **ui**: Wire left and right panel buttons to CAT commands
  - Left panel TX buttons (6 buttons, 12 actions):
  - TUNE/TUNE LP, XMIT/TEST, ATU/ATU TUNE, VOX/QSK, ANT/REM ANT, RX ANT/SUB ANT
  - Added TEST indicator (red, above TX), QSK indicator (next to VOX), ATU indicator (below RIT/XIT)
  - RadioState: Added TS parser, QSK flag from SD, atuModeChanged signal
  - Right panel buttons (14 buttons, 26 actions):
  - PRE/ATTN, NB/LEVEL, NR/ADJ, NTCH/MANUAL, FIL/APF, A/B/SPLIT
  - A→B/B→A, SPOT/AUTO, MODE/ALT
  - B SET/PF1, CLR/PF2, RIT/PF3, XIT/PF4
  - RadioState: Added FP$ parser, filterPositionChanged/filterPositionBChanged signals
  - Both panels use left-click for primary action, right-click for secondary.
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **ui**: Add filter position indicators (FIL1/FIL2/FIL3)
  - Add filter position indicators below A/B VFO squares, horizontally
  - aligned with RIT/XIT display. Color #FFD040 (lighter golden yellow),
  - 10px bold font. Updates from FP/FP$ CAT commands via RadioState signals.
- **ui**: Add feature menu bar for ATTN, NB LEVEL, NR ADJUST, MANUAL NOTCH
  - Phase 1 UI complete:
  - Custom paintEvent with gradient background and rounded border
  - Vertical delimiter lines between widget groups
  - Toggle behavior: right-click same button hides, different button switches
  - Centered over bottom menu bar (aligned with side panel)
  - NB LEVEL has extra "FILTER NONE" button
  - TBD Phase 2: Wire CAT commands for toggle/increment/decrement actions
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **ui**: Wire feature menu bar CAT commands (Phase 2)
- **ui**: Add B SET VFO targeting for feature menu bar (Phase 3)
  - When B SET is enabled, feature menu bar commands now target Sub RX:
  - Added TB command parsing to RadioState for B SET state tracking
  - Toggle/increment/decrement commands use $ suffix (RA$, NB$, NR$, NM$)
  - State display reads from Sub RX getters when B SET is enabled
  - Menu bar refreshes when B SET state changes
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **ui**: B SET indicator and bottom menu popups
  - B SET Indicator:
  - Green rounded rectangle with "B SET" text in center column
  - Appears when BS1 received, hides SPLIT label
  - Side panel BW/SHFT indicator changes from cyan to green
  - Filter values show VFO B bandwidth/shift when B SET active
  - Added BS command parsing (BS0/BS1) for B SET state
  - Added IS$ parsing for Sub RX IF shift
  - Bottom Menu Popups:
  - Added ButtonRowPopup widget for Fn, MAIN RX, SUB RX, TX buttons
  - Single-row popup with 7 buttons (same style as BAND popup)
  - Toggle behavior: second press closes, switching auto-closes
  - Bug Fixes:
  - Popup buttons now reset to normal color when popup closes
  - Added hideEvent() override to emit closed() signal consistently
  - Fixed BAND popup button also staying illuminated
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **ui**: Mode-dependent WPM/PTCH vs MIC/CMP display
  - Left side panel first button group now switches based on operating mode:
  - CW/CW-R: Shows WPM/PTCH (keyer speed / CW pitch)
  - Voice/Data: Shows MIC/CMP (mic gain 0-80 / compression 0-30)
- **dsp**: Metal/RHI panadapter overlay rendering
  - Major migration of panadapter overlays from OpenGL to Qt RHI Metal backend:
  - Add PanadapterRhiWidget and MiniPanRhiWidget with Metal/Vulkan/DirectX support
  - Add GLSL shaders for spectrum, waterfall, and overlay rendering
  - Fix overlay corruption by using separate GPU buffers for passband and marker
  - Fix passband/marker height to stay within spectrum area
  - Fix VFO B passband color alpha (translucent instead of opaque)
  - Add CW mode pitch offset to frequency marker positioning
  - Add missing VFO B connections for IF shift, CW pitch, notch filter
  - Known bug: Notch filter indicator renders incorrectly (tracked in CHANGELOG)
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **dsp**: Fragment shader spectrum rendering
  - Replace triangle strip spectrum rendering with fragment shader approach
  - for per-pixel control of spectrum fill. This eliminates the "flashing
  - columns" artifact when strong signals appeared.
  - Add spectrum_fill.vert/frag shaders for per-pixel spectrum sampling
  - Upload spectrum data as 1D R32F texture before render pass
  - Color based on absolute Y position (higher signals = brighter)
  - Smooth gradient from dark green at noise floor to bright lime at peaks
  - Use smoothstep for natural color transitions
- **audio**: Dual-channel mixing with independent volume controls
  - Add SUB volume slider (cyan) below MAIN slider for Sub RX audio
  - OpusDecoder extracts both stereo channels (L=Main, R=Sub) and mixes
  - Each channel has independent volume control (0-100%)
  - Volume settings persist via RadioSettings
  - Also wires SUB/DIVERSITY buttons:
  - SUB left-click sends SW83 (toggle Sub RX)
  - SUB right-click sends SW152 (toggle Diversity)
  - RadioState parses DV/SB commands with signals
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **ui**: VFO tuning rate indicator and span control improvements
  - Add visual underline beneath frequency digit showing current tuning rate
  - Parse VT/VT$ CAT commands for Main/Sub VFO tuning step tracking
  - Wire RATE button: left-click (SW73) cycles rates, right-click (SW150) for KHZ
  - Special case: VT5 (KHZ) displays at 100Hz position (digit 2)
  - Reverse span +/- button polarity to match intuitive behavior
  - Follow K4 span increment sequence: 1kHz steps 5-144kHz, 4kHz steps 144-368kHz
  - Fix mini-pan passband rendering for CW mode after QRhi/Metal migration
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- TLS/PSK encrypted connection and self-contained app bundle
  - TLS/PSK Support:
  - Add secure TLS v1.2 connection option on port 9204
  - PSK authentication with K4 radio ("ElecraftK4" identity)
  - "Use TLS (Encrypted)" checkbox in Server Manager dialog
  - Auto port switching (9204 for TLS, 9205 for unencrypted)
  - Supports ECDHE-PSK, DHE-PSK, RSA-PSK cipher suites
  - macOS App Bundling:
  - Self-contained .app bundle with all dependencies
  - Bundles OpenSSL, Opus, HIDAPI, Qt frameworks
  - cmake deploy target for distribution builds
  - Info.plist.in template for bundle metadata
  - OpenSSL discovery: checks bundled Frameworks first
  - CI/CD Release Pipeline:
  - Code signing with Developer ID certificate
  - Apple notarization via API key
  - Staples notarization ticket to app
  - Proper library path fixes with install_name_tool
- **dsp**: Honor K4 spectrum scale (#SCL) for display range
  - Parse and apply the K4's #SCL/#SCL$ commands (25-150 range) to
  - dynamically adjust the panadapter's dB display range. Higher scale
  - values compress the display (wider dB range, signals appear weaker),
  - while lower values expand it (narrower dB range, signals appear
  - stronger).
  - Add RadioState parsing for #SCL and #SCL$ CAT commands
  - Add setScale() method to PanadapterRhiWidget
  - Add updateDbRangeFromRefAndScale() helper for unified range calc
  - Connect scaleChanged signals in MainWindow for both VFOs
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **ui**: Add TX multifunction meter widget (IC-7760 style)
  - Add TxMeterWidget with 5 horizontal bar meters: Po, ALC, COMP, SWR, Id
  - Calculate PA drain current from forward power and supply voltage
  - Scale Id meter to 0-25A to accommodate 100W operation
  - Update volume slider colors: MAIN=cyan, SUB=green
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **ui**: Add S-meter peak indicator, mode popup improvements, and bug fixes
  - Add S-meter peak hold indicator with 500ms decay
  - Adjust S-meter gradient to transition colors earlier
  - Resize S-meter for 200px width constraint (compact labels)
  - Add mode popup widget with band-aware SSB defaults (LSB/USB)
  - Make VFO A/B squares clickable to open mode popup
  - Show full mode with data sub-modes (AFSK, FSK, PSK, DATA)
  - Change mode popup trigger from right-click to left-click
  - Fix AVERAGE popup sending command on open
  - Fix AVERAGE +/- to step by 1 instead of 5
  - Add optimistic updates for AVERAGE and FREEZE
  - Change FREEZE label to toggle between FREEZE/FROZEN
  - Remove circle and A label from MENU overlay
  - Remove waterfall color cycling (#WFC) functionality
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **network**: Optimize TLS to use ECDHE-PSK-CHACHA20-POLY1305
  - Use only ECDHE-PSK-CHACHA20-POLY1305 cipher for TLSv1.2 + forward secrecy
  - K4 server prefers weaker ciphers, so only offer the best one
  - Add SSL library version logging on TLS connection
  - Log negotiated cipher, protocol, key exchange, and encryption method
  - Also includes:
  - Add disconnect button to Radio Manager (globe) dialog
  - Parse and log SIRC client stats (RX/TX kB/s, ping, buffer)
  - Change PING interval from 3s to 1s to match SIRC updates
  - Remove redundant CAT queries on auth (RDY provides all state)
  - Add openssl to Windows CI build for consistency
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **ui**: TX meter decay/gradients, scale control, grid behind spectrum
  - TX Meter Widget:
  - 500ms decay animation for all meters
  - S-meter gradient colors for Po/ALC/COMP/SWR (green→red)
  - Id meter stays red
  - Peak hold indicators with 1s decay
  - SCALE Control:
  - Added scale +/- controls to DISPLAY popup (right-click REF LVL/SCALE)
  - Scale is GLOBAL (applies to both panadapters)
  - Removed incorrect #SCL$ parsing
  - Spectrum Grid:
  - Grid now drawn behind spectrum fill (standard design pattern)
  - Better signal visibility, cleaner appearance
  - Right Side Panel:
  - Rearranged button layout
  - BSET/CLR/RIT/XIT and FREQ/RATE/LOCK/SUB moved down above PTT
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **hardware**: Add KPOD hotplug detection and audio output device selection
  - KPOD Hotplug:
  - Periodic hid_enumerate() every 2s detects device plug/unplug
  - Auto-starts polling when device arrives (if enabled in settings)
  - Options dialog updates in real-time when device status changes
  - Avoids IOKit conflicts with hidapi's internal IOHIDManager
  - Audio Output:
  - Add speaker device selection in Options > Audio Output
  - AudioEngine: setOutputDevice(), outputDeviceId(), availableOutputDevices()
  - RadioSettings: speakerDevice persistence
  - Options Dialog:
  - KPOD page now shows real-time status updates
  - Connects to KpodDevice signals for live refresh
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **ui**: Combine S-meter and TX meters into multifunction S/Po widget
  - Consolidate SMeterWidget and TxMeterWidget into a single dual-mode meter:
  - First row now displays S-meter when RX, power when TX
  - S-meter scale: "1, 3, 5, 7, 9, +20, +40, +60"
  - K4 power scale: 0-110W (corrected from 100W)
  - Groundwork for KPA1500: 0-1900W scale (code commented for future use)
  - Additional UI improvements:
  - Add memory buttons row (M1-M4, REC, STORE, RCL)
  - Apply lighter grey styling to PF buttons (B SET, CLR, RIT, XIT)
  - Remove standalone SMeterWidget (now integrated)
- Add version info and About dialog for macOS and Windows
  - Add About K4Controller dialog accessible from:
  - macOS: Application menu (auto-moved by Qt)
  - Windows: Help > About K4Controller
  - Pass version from CMakeLists.txt to code via compile definition
  - Reorder menus: File, Connect, Tools, View, Help (Windows convention)
  - Add Windows version resource file (K4Controller.rc.in)
  - Use proper Qt menu roles for cross-platform behavior:
  - AboutRole for About dialog
  - PreferencesRole for Settings
  - QuitRole for Exit
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **ui**: Show PRE and ATT levels in VFO indicator labels
  - PRE now displays level: PRE-1, PRE-2, PRE-3 (or just PRE when off)
  - ATT now displays dB level: ATT-3, ATT-6, ATT-12, etc. (or just ATT when off)
  - Updated setPreamp() and setAtt() to accept level parameter
  - Both VFO A and B display their respective levels independently
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **network**: Add rigctld server for Hamlib integration
  - Implement TCP server on port 4532 supporting Hamlib rigctld protocol
  - for integration with external applications (loggers, WSJT-X, fldigi).
  - Core commands:
  - f/F: Get/set frequency (supports decimal for WSJT-X compatibility)
  - m/M: Get/set mode with Hamlib mode mapping (USB, LSB, USBD, LSBD, etc.)
  - t/T: Get/set PTT state
  - v/V: Get/set VFO (always VFOA)
  - s/S: Get/set split mode
  - i/I: Get/set split frequency (VFO B)
  - j/J: Get/set RIT offset
  - z/Z: Get/set XIT offset
  - y/Y: Get/set antenna
  - n: Get tuning step
  - l/L: Get/set levels (RFPOWER, KEYSPD, CWPITCH, MICGAIN, COMP, IF, NR, NB)
  - u/U: Get/set functions (VOX, NB, NR, TUNER)
  - Protocol features:
  - Extended response mode (+prefix) with labeled values and RPRT codes
  - dump_state for protocol v1 handshake with capabilities
  - get_vfo_info and get_rig_info for VFO status queries
  - Pending value cache to avoid set/get mismatch delays
  - UI integration:
  - Rig Control tab in Options dialog with enable/port settings
  - Real-time status display (listening state, connected clients)
  - Settings persistence via RadioSettings
- **network**: Add CatServer for external app integration
  - Replace RigctldServer with CatServer that speaks native K4 CAT protocol.
  - External apps (WSJT-X, MacLoggerDX, fldigi) connect to localhost:4532
  - and configure "Elecraft K4" as the radio type - no protocol translation.
  - Key features:
  - GET commands (FA;, MD;, TQ;, etc.) answered from RadioState cache
  - SET commands forwarded to real K4 via TcpClient
  - TX;/RX; commands control audio input gate for external app TX
  - AI command protection (returns AI4; for queries, ignores SET)
  - Multiple simultaneous client connections supported
  - CatServer integration:
  - MainWindow creates CatServer with RadioState
  - OptionsDialog "Rig Control" tab for enable/port/status
  - pttRequested signal enables mic and sets m_pttActive for TX flow
  - This enables WSJT-X digital mode operation through K4Controller with
  - audio routed via virtual audio device (e.g., BlackHole on macOS).
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **ui**: Add macro system with KPOD button support
  - Implement configurable macro system for programmable buttons:
  - Add MacroDialog for editing 29 macro slots (PF1-4, Fn.F1-F8, REM ANT, K-pod buttons)
  - Add FnPopupWidget with dual-action buttons for Fn.F1-F8 macros
  - Add RadioSettings macro storage with label and CAT command per slot
  - Wire KPOD buttonTapped/buttonHeld signals to macro execution
  - Fix KPOD button detection: always process HID response, not just when cmd='u'
  - Organize macro list with tap/hold pairs (K-pod.1T, K-pod.1H, K-pod.2T, etc.)
  - KPOD buttons now trigger macros immediately without requiring knob movement.
- **ui**: Add notification widget and improve popup styling
  - Add NotificationWidget for K4 error/status messages (ERxx: format)
  - Parse ER messages in RadioState, emit errorNotificationReceived signal
  - Display KPA1500 status changes (ER41/43/44) as center-screen notifications
  - Fix popup first-show positioning bug (show off-screen first to realize geometry)
  - Add translucent background and NoDropShadowWindowHint to all popups
  - Add rounded corners (8px radius) to Band, ButtonRow, Display popups
  - Update documentation (STATUS.md, CLAUDE.md)
- **ui**: Improve ModePopupWidget styling and arrow targeting
  - Add rounded corners (8px radius) matching other popups
  - Add translucent background and NoDropShadowWindowHint
  - Fix first-show positioning bug (show off-screen first)
  - Add arrowTarget parameter to showAboveWidget()
  - VFO A mode popup arrow now points to MAIN RX button
  - VFO B mode popup arrow now points to SUB RX button
  - MODE button respects B SET state for arrow targeting
  - Update documentation
- **network**: Simplify TLS-PSK config with identity field
  - Replace separate PSK field with identity field in connection dialog
  - Password field now serves as both password (unencrypted) and PSK (TLS)
  - Add optional TLS-PSK identity for future multi-user support
  - Enforce TLS 1.2+ with filtered PSK cipher list
  - K4 negotiates ECDHE-PSK-CHACHA20-POLY1305 (forward secrecy + AEAD)
  - UI changes:
  - Remove PSK field from Server Manager dialog
  - Add ID field (visible when TLS enabled, optional)
  - Password field used as PSK when TLS is checked
- **ui**: Add VfoRowWidget with SUB/DIV indicators
  - Create VfoRowWidget for absolute positioning of A-TX-B row
  - TX indicator perfectly centered regardless of asymmetric SUB/DIV
  - Add SUB/DIV LED-style indicators next to VFO B
  - DIV only lights when both SUB and DIV are enabled (DIV requires SUB)
  - Fix SB command parsing: SB3 (diversity mode) now recognized as SUB on
  - Update documentation for new widget and indicator logic
- **ui**: Add HD fonts for crisp rendering
  - Embed Inter font (Medium weight) as default UI font
  - Embed JetBrains Mono for frequency display and CAT commands
  - Enable HiDPI scaling with PassThrough policy
  - Configure font hinting and antialiasing for sharp edges
  - Update VFO frequency display to use JetBrains Mono
  - Update macro dialog to use JetBrains Mono for commands
  - Document font conventions in CONVENTIONS.md
  - Fonts loaded from Qt resources at startup for consistent
  - rendering across platforms.
- **ui**: Add HD visual polish with drop shadows and consistent styling
  - Add 8-layer soft drop shadows to all popup widgets (16px blur, 2,4 offset)
  - Change borders from 1px to 2px for visibility on Retina/4K displays
  - Standardize border radius to 6px for buttons, 8px for popup backgrounds
  - Update widget sizing and positioning to account for shadow margins
  - Document popup styling conventions in CLAUDE.md and CONVENTIONS.md
- **panadapter**: Show both VFO passbands in all display modes
  - Add secondary VFO passband rendering to PanadapterRhiWidget so both
  - VFO A and VFO B passbands are visible regardless of display mode.
  - VFO A passband: always cyan (#00BFFF)
  - VFO B passband: always green (#00FF00)
  - Secondary renders behind primary when overlapping
  - Uses dedicated GPU buffers for Metal compatibility
- **ui**: Add FilterIndicatorWidget for visual filter display
  - Add new 62×62px widget showing filter position (FIL1/2/3), bandwidth
  - shape, and IF shift position matching K4's visual filter indicator.
- **ui**: Implement MAIN RX/SUB RX popup controls
  - Add functional buttons for MAIN RX and SUB RX popups with dual-line
  - styling matching FnPopupWidget pattern:
  - RxMenuButton: Custom dual-line button with white primary text on top
  - and amber (or white) alternate text on bottom. setHasAlternateFunction
  - controls whether alternate text implies a right-click action.
  - AFX button: Cycles OFF → DELAY → PITCH modes (FX CAT command)
  - AGC button: Left-click toggles Fast/Slow, right-click toggles ON/OFF (GT)
  - APF button: Cycles bandwidth 30/50/150 Hz in CW mode (AP+)
  - VFO LINK: Right-click on LINE OUT toggles VFO linking (LN)
  - ANT CFG, RX EQ, TEXT DECODE: Dual-line white text (no alternate function)
  - RadioState additions:
  - afxMode, apfEnabled/apfBandwidth, vfoLink state with signals
  - Parses FX, AP$, LN CAT responses
- **ui**: Dim VFO B frequency/mode when SUB RX is off
  - Add frequencyLabel() accessor to VFOWidget for external styling
  - Extend subRxEnabledChanged handler to dim VFO B display (#666666)
  - when SUB RX is disabled, restore to white (#FFFFFF) when enabled
  - Always emit subRxEnabledChanged signal to ensure UI syncs on
  - initial connect (booleans can't use -1 initialization trick)
- **ui**: Add mouse wheel support to popup menu controls
  - Add wheelEvent() handlers to adjust values via scroll:
  - ControlGroupWidget: SPAN, REF, SCALE, AVG, NB, WF HEIGHT controls
  - FeatureMenuBar: ATTN, NB LEVEL, NR ADJ, NTCH/MANUAL popups
  - Scroll up = increment, scroll down = decrement.
- **ui**: Add APF indicator and separate Main/Sub RX APF state
  - Parse Main RX APF (AP) and Sub RX APF (AP$) separately in RadioState
  - Add APF indicator after NTCH in VFO widgets showing "APF-30/50/150"
  - MAIN RX popup APF: left-click toggles (AP/;), right-click cycles BW (AP+;)
  - SUB RX popup APF: left-click toggles (AP$/;), right-click cycles BW (AP$+;)
  - RightSidePanel APF: toggles Main or Sub RX based on B SET state
  - Fix MAIN RX/SUB RX popups to stay open on button click
  - Widen VFO widget to 230px to fit APF-xxx indicator
- **ui**: Add direct frequency entry for VFO A and B
  - Click on the frequency display to enter a new frequency directly.
  - Supports 1-11 digit input per K4 spec:
  - 1-2 digits: MHz (e.g., "7" = 7 MHz)
  - 3-5 digits: kHz (e.g., "14225" = 14.225 MHz)
  - 6+ digits: Hz (e.g., "7204000" = 7.204 MHz)
  - Press Enter to submit, Escape to cancel. Edit field styled with
  - VFO-colored border (amber for A, cyan for B).
- Add BSET-aware band change, mini pan SUB RX logic, and drag-to-tune
  - Band popup now targets VFO B when BSET enabled (BN$ commands)
  - Band stack (same band tap) uses BN$^ for VFO B
  - Added BN$ response parser and m_currentBandNumB tracking
  - Mini Pan B blocked when VFOs on different bands and SUB RX off
  - Auto-hides mini pan B when SUB RX turns off or bands diverge
  - Added getBandFromFrequency() and areVfosOnDifferentBands() helpers
  - Click and drag on panadapter changes frequency in real-time
  - Added m_isDragging state tracking with proper event handling
  - Updates RadioState directly during drag for immediate UI feedback
- **tools**: Add debug_shift tool and fix button shift bug
  - New debug_shift MCP tool analyzes widget layout context to identify
  - causes of unexpected visual shifts:
  - Stylesheet state count mismatches
  - Missing alignment specifications
  - Inconsistent size constraints among siblings
  - Tool diagnosed BottomMenuBar button shift issue: menuBarButtonActive()
  - had only 1 state while menuBarButton() has 3 states (normal/hover/pressed).
  - Qt defers size hint calculation until all states are needed, causing
  - layout shift on first style change.
- **ui**: Add clickable RIT/XIT, fix VFO alignment, improve TX meters
  - RIT/XIT Controls:
  - Make RIT/XIT labels clickable to toggle on/off (RT/; and XT/;)
  - Add mouse wheel support on RIT/XIT box for offset adjustment (RU;/RD;)
  - Fix RIT/XIT value color: grey when both off, white when either on
  - Fix RT/XT parsing to ignore RT$/XT$ variant commands
  - VFO Widget:
  - Fix VFO A/B frequency alignment (both left-aligned within containers)
  - Create mirrored layout: VFO A content left, VFO B content right
  - Match frequency container width (270px) with stacked widget for alignment
  - Expand indicator row width to prevent text clipping (ATT-6, NTCH-M, APF-50)
  - TX Meters:
  - Only update TX meters on active transmit VFO based on SPLIT state
  - SPLIT OFF: VFO A shows TX meters, VFO B stays in S-meter mode
  - SPLIT ON: VFO B shows TX meters, VFO A stays in S-meter mode
  - Other Fixes:
  - Fix popup positioning (call move() after show() for macOS)
  - Fix FeatureMenuBar filter button clipping (increase to ButtonHeightLarge)
  - Fix BottomMenuBar button shift (use setFixedSize with MenuBarButtonWidth)
  - Fix panadapter drag-to-tune edge behavior (clamp xToFreq to visible span)
- **ui**: Add S-unit display mode for spectrum amplitude scale
  - Respect K4's "Spectrum Amplitude Units" menu setting to toggle between
  - dBm and S-unit labels on the panadapter vertical scale. S-units follow
  - standard HF convention (S9 = -73 dBm, 6 dB per S-unit below S9).
  - Add getMenuItemByName() to MenuModel for name-based lookup
  - Add setUseSUnits() to DbmScaleOverlay with dBm-to-S-unit conversion
  - Add setAmplitudeUnits() to PanadapterRhiWidget
  - Wire menu value changes to update both panadapters automatically
- **audio**: Add local sidetone generator for CW keying
  - Add SidetoneGenerator class with push-mode audio playback
  - Generate dit/dah elements with proper timing based on keyer WPM
  - Include inter-element spacing and envelope shaping (3ms rise/fall)
  - Support repeat while paddle held with automatic KZ command sending
  - Add sidetone volume slider to Options → CW Keyer (independent of K4 MON)
  - Link sidetone frequency to K4's CW pitch (PT command)
  - Save sidetone volume in RadioSettings for persistence
- **audio**: Improve sidetone with push-mode audio and volume control
  - Switch sidetone to push-mode audio for reliable playback on macOS
  - Add individual dit/dah element playback with WPM-based timing
  - Add repeat functionality while paddle is held
  - Add sidetone volume slider in Options → CW Keyer (independent of MON)
  - Add HaliKey USB paddle device support
  - Add K4Styles::sliderHorizontal() for consistent slider styling
  - Add slider dimension constants to K4Styles::Dimensions
  - Add MON/BAL/SideControl overlay widgets
  - Update RadioState with balance and additional meter properties
  - Fix K4Styles compliance in slider code
- **dsp**: Add frame-rate-aware spectrum smoothing
  - Query K4's FPS setting (#FPS) on connection and dynamically calculate
  - spectrum smoothing alpha values so visual decay behavior remains
  - consistent regardless of frame rate (1-30 FPS).
- **audio**: Add encode mode selection in Server Manager
  - Add dropdown in RadioManagerDialog to select K4 audio encode mode:
  - EM3 - Opus Float (default)
  - EM2 - Opus Int
  - EM1 - RAW 16-bit PCM
  - EM0 - RAW 32-bit PCM
  - Updates OpusDecoder to handle all 4 encode modes for RX audio.
  - The selected mode is persisted per-server and sent during connection.
- **ui**: Add RX graphic equalizer with presets
  - Add RxEqPopupWidget with 8-band EQ sliders (-16 to +16 dB)
  - Add 4 user-customizable EQ presets stored in RadioSettings
  - Implement preset load/save/clear with right-click context menu
  - Add RadioState parsing for RE CAT command
  - Add 100ms debounce timer to prevent command flooding
  - Update documentation for settings and UI modules
- **ui**: Add antenna configuration popup with AR/AR$ display fix
  - Add AntennaCfgPopupWidget for configuring antenna cycling masks:
  - Three variants: Main RX (ACM), Sub RX (ACS), TX (ACT)
  - DISPLAY ALL / USE SUBSET mode toggle
  - Checkbox grid for enabling/disabling antennas in cycle
  - Custom antenna names from ACN commands
  - Fix AR/AR$ antenna display mappings to match K4 protocol:
  - 0=Disconnected, 1=EXT XVTR/RX2, 2=RX USES TX ANT (resolved)
  - 3=INT XVTR, 4=RX ANT IN1, 5-7=ATU ANT1-3
  - "RX USES TX ANT" now shows resolved value (e.g., "1:KPA1500")
  - Add K4Styles::checkboxButton() and radioButton() stylesheets.
- **ui**: Wire SUB RX EQ button to shared equalizer
  - SUB RX popup's RX EQ button now opens the same graphic equalizer
  - as MAIN RX. Both receivers share the same EQ settings on the K4.
- **ui**: Add LINE OUT popup for left/right level control
  - Add LineOutPopupWidget control bar accessible from MAIN RX and SUB RX
  - menus (button index 2). Features LEFT/RIGHT channel selection with
  - mouse wheel adjustment, and RIGHT=LEFT mode for synced levels.
  - LineOutPopupWidget with custom painting and wheel events
  - RadioState parsing for LO command (LOlllrrrm format)
  - Two-way sync with K4 via CAT commands
  - Level range 0-40 for each channel
- **ui**: Integrate TEXT DECODE controls, simplify spectrum, add B SET to RIT/XIT
  - TEXT DECODE: Move ON/OFF, WPM range, threshold controls into floating
  - window title bar; remove popup widget for single-click workflow
  - Spectrum: Remove STYLE button and alternate Blue style, keep LUT-based
  - BlueAmplitude as only renderer; delete unused shader
  - RIT/XIT: Add B SET awareness to wheel handler (RU$/RD$ for Sub RX)
  - Cleanup: Lint fixes and documentation updates
- **dsp**: Add GPU-based Lanczos interpolation for spectrum/waterfall
  - Implement Lanczos-3 kernel in waterfall.frag and spectrum_blue_amp.frag
  - Upload raw bins centered in texture, let GPU handle interpolation
  - Fix frequency alignment by correcting bin extraction centering formula
  - Update minipan to pass binCount/textureWidth for shared shader compatibility
  - Document GPU Lanczos architecture and bin centering gotcha in CLAUDE.md
  - Provides crisp, artifact-free display at narrow spans (5-10kHz) where
  - ~213 bins are upsampled to display resolution.
- **dsp**: Use nearest-neighbor waterfall, fix binOffset integer division
  - Waterfall: Switch from Lanczos to nearest-neighbor for crisp bin edges
  - Spectrum: Keep Lanczos-3 for smooth peaks
  - Fix binOffset mismatch between C++ integer division and shader float
  - division by adding floor() - fixes blur at 5/8/11kHz spans
  - Document K4 protocol findings: always 1024 bins, 368kHz max span,
  - tier system (24/48/96/192/384 kHz)
- **ui**: Add search functionality to menu overlay
  - Add magnifying glass button to menu nav panel that opens a search popup.
  - Users can type to filter menu items in real-time using case-insensitive
  - substring matching. Search popup closes on Escape or click outside.
  - Add filterByName() method to MenuModel
  - Add search button (🔍) to row 2 with NORM and BACK buttons
  - Create search popup with styled QLineEdit
  - Implement real-time filtering as user types
  - Clear filter when overlay closes
- **ui**: Add keyboard function key macros (F1-F12)
  - Add 12 new macro slots triggered by keyboard F1-F12 keys. When MainWindow
  - has focus, pressing F1-F12 executes the corresponding Keyboard-Fx macro.
  - Add KbdF1-KbdF12 macro IDs to MacroIds namespace
  - Add Keyboard-F1 through F12 entries to MacroDialog
  - Implement keyPressEvent in MainWindow for F-key handling
  - Update documentation (41 total macro slots)
- **ui**: Add LINE IN and TX EQ popups with audio enhancements
  - Add LINE IN popup (TX menu index 2) for sound card/line in jack source
  - selection and level control (0-250 range)
  - Add TX EQ popup (TX menu index 1) with 8-band graphic equalizer
  - Add RX/TX EQ preset persistence in RadioSettings
  - Add LI command parsing to RadioState for LINE IN state
  - Add TE command parsing to RadioState for TX EQ state
  - Add frequency labels to panadapter display
  - Refactor OptionsDialog and RadioManagerDialog for consistency
  - Update K4Styles with new color constants
  - Add smart_style_matcher.py audit tool for K4Styles compliance
- **ui**: Add TX popup controls and standardize popup fonts
  - TX Popup Buttons:
  - VOX GN / ANTIVOX popup (button 4) with VOX toggle and level control
  - SSB BW popup (button 5) with ESSB-aware title and bandwidth
  - ESSB toggle button (button 6) with mode indicator update
  - MIC INPUT popup with front/rear/line-in selection
  - MIC CONFIG popup with bias/preamp/buttons controls
  - Font Standardization:
  - Add centralized popup font constants (PopupTitleSize, PopupButtonSize,
  - PopupValueSize, PopupAltTextSize) to K4Styles::Dimensions
  - Update all horizontal bar popups to use consistent 12px/11px fonts
  - Change font-weight from bold to 600 (semibold) for less heavy appearance
  - PTT Button:
  - Add menuBarButtonPttPressed() style with TxRed background
  - PTT button turns red when pressed, returns to normal when released

### Changed

- **dsp**: Simplify spectrum styles to Blue and BlueAmplitude
  - Remove Classic (Lime), CyanGradient, Mapped, and Bars styles
  - Keep only Blue (Y-position gradient) and BlueAmplitude (LUT-based)
  - Set BlueAmplitude as default spectrum style
  - Add new GPU shaders: spectrum_blue.vert/frag, spectrum_blue_amp.frag
  - Remove unused spectrum_fill shaders
  - Style button now toggles between the two styles
  - Fix texture sizing for consistent waterfall performance
  - Revert decay alpha to 0.45 for crisp waterfall contrast
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **ui**: Add centralized K4Styles system for button/popup stylesheets
  - Create K4Styles namespace with shared stylesheet functions
  - popupButtonNormal(), popupButtonSelected() for popup buttons
  - menuBarButton(), menuBarButtonActive(), menuBarButtonSmall() for menus
  - Add K4Styles::Colors and K4Styles::Dimensions constants
  - Remove duplicate style functions from 6 widget files (~300 lines)
- **ui**: Consolidate colors and shadow drawing to K4Styles
  - Add K4Styles::Colors namespace with centralized color constants
  - Add K4Styles::Dimensions for shadow, button, and font constants
  - Add K4Styles::drawDropShadow() utility for consistent popup shadows
  - Refactor 6 popup widgets to use centralized shadow drawing:
  - bandpopupwidget, buttonrowpopup, displaypopupwidget,
  - featuremenubar, fnpopupwidget, modepopupwidget
  - Remove duplicate K4Colors namespaces from mainwindow, vfowidget,
  - vforowwidget, txmeterwidget, rightsidepanel, optionsdialog
  - Fix documentation: remove non-existent SMeterWidget, update
  - widget count, add CatServer to OptionsDialog signature
- **ui**: Add K4PopupBase and update documentation
  - Add K4PopupBase as base class for all triangle-pointer popups
  - Consolidate shadow/triangle/positioning code into base class
  - Add QPainter helpers to K4Styles (buttonGradient, selectedGradient, borderColor)
  - Migrate BandPopup, ModePopup, DisplayPopup, ButtonRowPopup, FnPopup to K4PopupBase
  - Fix DISPLAY button illumination bug (signal shadowing in DisplayPopupWidget)
  - Rewrite PATTERNS.md popup section for K4PopupBase pattern
  - Update CONVENTIONS.md with K4Styles::Colors documentation
  - Update CLAUDE.md files for AI-optimized consumption
  - Net reduction: ~1150 lines of duplicate code removed
- **ui**: Remove popup triangle pointer and indicator strip
  - Complete removal of triangle and strip from all K4PopupBase popups:
  - Remove drawing code from k4popupbase.cpp paintEvent()
  - Remove m_triangleXOffset member and arrowTarget parameter
  - Delete constants: PopupTriangleHeight, PopupBottomStripHeight, PopupTriangleWidth, IndicatorStrip
  - Update contentSize() in all 5 popup subclasses
  - Update contentMargins() and contentRect() calculations
  - Fix mainwindow.cpp showAboveWidget() calls (2 args → 1 arg)
  - Update all documentation files
  - Files modified:
  - k4popupbase.h/cpp
  - k4styles.h
  - bandpopupwidget.cpp, modepopupwidget.cpp, buttonrowpopup.cpp
  - fnpopupwidget.cpp, displaypopupwidget.cpp
  - mainwindow.cpp
  - CLAUDE.md, PATTERNS.md, CONVENTIONS.md
  - src/ui/CLAUDE.md, docs/UI_COMPONENTS.md
  - .claude/agents/UIGUARD-AGENT.md
- **ui**: Add style compliance checker and fix violations
  - Add check_style_compliance.py audit tool that scans C++ files for:
  - Hardcoded colors (QColor, hex, stylesheet)
  - Hardcoded dimensions (setFixed*, setMin/Max*, px values)
  - Hardcoded fonts (setPointSize, setFamily, font-size)
  - Add new K4Styles::Dimensions constants:
  - SeparatorHeight (1px) - separator lines
  - MenuItemHeight (40px) - menu overlay items
  - FormLabelWidth (80px) - dialog form labels
  - VfoSquareSize (45px) - VFO A/B indicators
  - NavButtonWidth (54px) - navigation buttons
  - Fix 64 style violations across 18 files:
  - Replace hardcoded colors with K4Styles::Colors constants
  - Replace hardcoded dimensions with K4Styles::Dimensions constants
  - Add k4styles.h includes where needed
  - Violations reduced: 644 → 580 (64 fixed)
- **ui**: Standardize font sizes on points with K4Styles constants
  - Add new font size constants:
  - FontSizeMicro = 6 (meter scale numbers)
  - FontSizeTitle = 16 (large control buttons)
  - Update existing constants documentation from "pixels" to "points"
  - to reflect proper usage with QFont::setPointSize().
  - Convert all setPixelSize() calls to setPointSize() for consistency:
  - buttonrowpopup.cpp, displaypopupwidget.cpp, fnpopupwidget.cpp
  - Fix all hardcoded setPointSize() values:
  - panadapter_rhi.cpp, txmeterwidget.cpp, dualcontrolbutton.cpp
  - filterindicatorwidget.cpp, notificationwidget.cpp
  - Violations fixed: 20 (580 → 560)
- **ui**: Simplify VFO color constants in K4Styles
  - Clean up confusing color constant naming:
  - AccentAmber (#FFB000): TX indicators, labels, highlights
  - VfoACyan (#00BFFF): VFO A theme (square, passband, markers)
  - VfoBGreen (#00FF00): VFO B theme (square, passband, markers)
  - Remove legacy aliases: VfoAText, VfoBText, VfoAAmber, VfoBCyan,
  - VfoAPassband, VfoBPassband
  - Update all code references and documentation.
- **ui**: Fix hardcoded color violations in mainwindow.cpp
  - Add DisabledBackground constant (#444444) to K4Styles
  - Replace hardcoded colors with K4Styles constants:
  - Status indicators (SUB/DIV): AgcGreen, DisabledBackground
  - Text colors: TextWhite, TextGray, InactiveGray
  - VFO passband colors: VfoACyan, VfoBGreen with alpha
  - Accent colors: AccentAmber for TX labels
  - Separators: BorderSelected
  - Reduces color violations from 196 to 161 (35 fixed).
- **ui**: Replace hardcoded colors with K4Styles constants
  - Convert hardcoded color strings to K4Styles::Colors constants in:
  - sidecontrolpanel.cpp: VFO theme colors for volume sliders
  - macrodialog.cpp: text colors and accent amber
  - menuoverlay.cpp: text and selection colors
  - radiomanagerdialog.cpp: background and label colors
  - Reduces color violations from 196 to 126 (70 fixed).
- **ui**: Replace hardcoded colors with K4Styles constants
  - Add overlay panel color constants to K4Styles for Menu and Macro dialogs.
  - Consolidate 78 color violations by replacing hex values and QColor() calls
  - with centralized K4Styles::Colors constants. Add meterGradient() helper
  - for consistent meter rendering across mic and TX meter widgets.
- **ui**: Replace hardcoded 13px font sizes with K4Styles constant
  - Update OptionsDialog and MacroDialog to use FontSizePopup (14px) instead
  - of hardcoded 13px values. This ensures consistent font sizing across all
  - dialogs and menus, and allows centralized control of dialog font sizes.
  - Also converted some hardcoded #00FF00 status colors to K4Styles::Colors::AgcGreen.
- **ui**: Use K4Styles dimension constants in LineOutPopup
  - Replace hardcoded button heights (36) with ButtonHeightMedium and
  - button widths (70) with PopupButtonWidth for consistency with
  - FeatureMenuBar template pattern.

### Fixed

- **release**: Use ditto to copy QtDBus.framework
  - Replace cp -RL with ditto (macOS-native framework copy)
  - Add readlink -f to resolve symlink chain
  - Add verification that copy succeeded
  - Add set -e to fail fast on errors
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **release**: Bundle missing transitive dependencies
  - macdeployqt misses transitive dependencies:
  - Add brotli libraries (libbrotlicommon, libbrotlidec, libbrotlienc)
  - Add libdbus-1.3.dylib (needed by QtDBus)
  - Fix permissions with chmod -R u+rw
  - Use install_name_tool to rewrite library paths to @rpath
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **ui**: DualControlButton click behavior and scroll signal wiring
  - First click on inactive button now only activates it (shows indicator)
  - Subsequent clicks swap primary/alternate labels
  - Added swapped() signal for tracking actual label swaps
  - Connected all 14 scroll signals to CAT commands in MainWindow:
  - WPM→KS, PTCH→CW, MIC→MG, CMP→CP, PWR→PC, DLY→SD,
  - BW→BW, HI→BW, SHFT→IS, LO→IS, M.RF→RG, M.SQL→SQ, S.RF→RG$, S.SQL→SQ$
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **ui**: Scroll values continue changing and button faces update
  - The K4 radio doesn't echo setting commands (KS, BW, PC, etc.), so we
  - now use optimistic updates: after sending CAT, immediately update
  - RadioState so the next scroll uses the correct value and button
  - faces update via signals.
- **ui**: RF Gain format and Power QRP/QRO transitions
  - RF Gain:
  - Fixed command format to RG-nn; and RG$-nn; (with minus sign)
  - Range corrected to 0-60 (was incorrectly 0-100)
  - Button faces display with minus sign (e.g., "-0", "-30")
- **build**: Remove test executables from CMakeLists.txt
  - Test files are local-only and not tracked in git.
  - This was causing GitHub Actions builds to fail.
- **ci**: Add qtshadertools module to Windows build
  - Required for qt6_add_shaders() used by panadapter shaders.
- **dsp**: Correct click-to-frequency coordinate mapping
  - Fixed xToFreq to use (w-1) divisor for accurate edge mapping
  - Fixed freqToNormalized usages to multiply by (w-1) for consistency
  - Click at x=0 now correctly maps to start frequency
  - Click at x=w-1 now correctly maps to end frequency
  - Passband, marker, and notch overlays now align with click positions
  - Also makes GuiPrivate optional on Windows for CI compatibility.
- **dsp**: Adjust coordinate mapping and fix Windows CI
  - Revert xToFreq to use w divisor (matches spectrum rendering coordinates)
  - Revert freqToNormalized multiplier to use w (consistent with spectrum)
  - Move Qt private headers inside #ifdef Q_OS_MACOS to fix Windows build
  - Private headers only needed for macOS Metal RHI capability check
- **protocol**: Correct PAN payload byte offsets per K4-Remote Protocol doc
  - The PAN payload structure was being parsed with incorrect byte offsets:
  - RX receiver: was byte 4, corrected to byte 1
  - Center Frequency: was bytes 11-18, corrected to bytes 8-15
  - Sample Rate: was bytes 19-22, corrected to bytes 16-19
  - Noise Floor: was bytes 23-26, corrected to bytes 20-23
  - PAN Data: was bytes 27+, corrected to bytes 24+
  - This fixes the ~470 Hz frequency alignment offset between the spectrum
  - display and VFO marker/click positions.
- **dsp**: Correct IF shift passband positioning
  - K4 reports IF shift in decahertz (10 Hz units), not as 0-99 index.
  - Changed calculation from (m_ifShift - 50) * 42 to m_ifShift * 10.
  - USB/DATA: passband center = dial + shiftOffsetHz
  - LSB: passband center = dial - shiftOffsetHz
  - CW/CW-R: passband centered at dial + pitch offset
  - AM/FM: passband centered at dial + shiftOffsetHz
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **ui**: NB/NR controls, manual notch A/B, and grid color
  - Fix NB/NR level controls skipping values (optimistic state updates)
  - Fix NB toggle button to toggle on/off instead of cycling filter
  - Separate NB filter cycling to extra button (NONE/NARROW/WIDE)
  - Add manual notch A/B separation (Main RX vs Sub RX)
  - Fix manual notch turning spectrum grid red (dedicated GPU buffers)
  - Add NM$ parsing for Sub RX notch commands
  - Clear known limitations in CLAUDE.md
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **ui**: VFO B auto notch (NTCH) indicator
  - Add NA$ (Auto Notch Sub RX) command parsing
  - Add autoNotchEnabledB() state to RadioState
  - Update VFO B NTCH indicator to reflect auto notch state
  - Fix code formatting (clang-format)
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **ci**: Add qtshadertools module to Windows release build
- **ci**: MacOS workflow app bundle path and lint violations
  - CI Fixes:
  - build-macos.yml: Check correct app bundle path (K4Controller.app)
  - src/main.cpp: Fix clang-format trailing comment alignment
  - Documentation Reorganization:
  - Condense root CLAUDE.md from 496 to 86 lines (index only)
  - Create focused reference docs: CONVENTIONS.md, PATTERNS.md, RULES.md, STATUS.md
  - Condense domain CLAUDE.md files to ~150 lines each
  - Add pre-commit lint checklist to RULES.md
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **ci**: Use Homebrew Qt in release workflow for GuiPrivate headers
  - The release workflow was using aqtinstall which doesn't include
  - Qt6GuiPrivate headers required by QRhiWidget. Switch to Homebrew Qt
  - which includes all private headers.
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **ci**: Remove invalid entitlements argument from codesign
  - The --entitlements /dev/null argument causes codesign to fail with
  - "cannot read entitlement data". Remove it since no special entitlements
  - are needed for this application.
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **ci**: Bundle QtDBus.framework for Homebrew Qt dependency
  - Homebrew's Qt has QtGui linked against QtDBus, but macdeployqt doesn't
  - detect this transitive dependency. Manually copy QtDBus.framework to
  - prevent "Library not loaded" crash at launch.
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **ci**: Fix QtDBus.framework install names and QtGui reference
  - Add verification that QtDBus.framework copy succeeds
  - Fix QtDBus binary install name to @executable_path/../Frameworks/
  - Update QtGui.framework's reference to use bundled QtDBus
  - Add error handling if QtDBus binary not found
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **ci**: Use qtbase prefix for QtDBus.framework location
  - Homebrew qt@6 is a meta-formula; actual Qt libraries are in qtbase.
  - QtDBus.framework is at $(brew --prefix qtbase)/lib/, not qt@6.
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **ci**: Bundle libdbus dependency for QtDBus.framework
  - QtDBus.framework depends on libdbus-1.3.dylib from Homebrew.
  - Add dbus to brew install
  - Copy libdbus-1.3.dylib to Frameworks
  - Fix libdbus install name
  - Update QtDBus's reference to bundled libdbus
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **ci**: Bundle brotli compression libraries for QtNetwork
  - QtNetwork depends on brotli for HTTP compression.
  - Add brotli to brew install
  - Copy libbrotlicommon, libbrotlidec, libbrotlienc
  - Fix install names and inter-library references
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **windows**: Hide console window and add OpenSSL for TLS/PSK
  - Add WIN32 flag to add_executable for GUI-only app (no console)
  - Install OpenSSL via vcpkg for PSK cipher support
  - Bundle libssl and libcrypto DLLs in Windows release
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **dsp**: Correct spectrum dBm calibration using per-packet noise floor
  - The spectrum display was showing signals ~19 dB lower than the K4's
  - actual display (e.g., -89 dBm instead of -70 dBm).
  - Root cause: decompressBins() used a hardcoded -160 offset, but the K4
  - sends the actual noise floor value in each PAN packet header. This
  - noise floor varies and should be used for correct dBm calibration.
- **dsp**: Calibrate spectrum dBm values to match K4 display
  - Adjust decompression offset from -160 to -146 (calibrated via
  - comparison with K4 spectrum peaks)
  - Simplify dB range calculation: minDb=refLevel, maxDb=refLevel+scale
  - Removes complex scaleFactor calculation in favor of direct mapping
  - The display range now correctly tracks K4's ref level and scale
  - settings, with signal levels matching the physical radio display.
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- Add missing files from previous commit
  - Include panadapter_rhi.cpp/.h changes (setWaterfallHeight)
  - Include featuremenubar.cpp/.h changes (showAboveWidget)
  - Include bandpopupwidget.cpp, buttonrowpopup.cpp updates
  - Fix clang-format issues in modepopupwidget.h
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **ci**: Correct macOS Qt6 cache paths
  - Cache /opt/homebrew/Cellar/qt@6 instead of qt
  - Add /opt/homebrew/opt/qt@6 symlink path
  - Update cache key to force fresh cache
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- Re-enable KPOD and KPA1500 disabled during QRhi debugging
  - Both components were temporarily disabled with #if 0 during QRhi Metal
  - backend debugging and never re-enabled:
  - KPOD device: VFO knob and rocker switch now functional
  - KPA1500 client: Amplifier connection restored
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **dsp**: Resolve Metal line flickering with filled rectangles
  - Convert dynamic overlay rendering from lines to 2px filled rectangles
  - to fix intermittent flickering on Metal backend:
  - Frequency marker (main pan): line → triangle rectangle
  - Center line (mini pan): line → triangle rectangle
  - Passband edges (mini pan): drawLines() → dedicated buffers + triangles
  - Notch marker (both): already fixed, cleaned up debug logging
  - Add dedicated passband edge buffers to mini pan to prevent buffer
  - conflicts with shared overlay buffers used by separator/border.
  - Update src/dsp/CLAUDE.md with Metal rendering notes and buffer patterns.
- **ui**: Swap ATU button behavior - left click now triggers ATU TUNE
  - Per tester feedback, the more common action (ATU TUNE) should be
  - on left click:
  - Left click: ATU TUNE (SW40) - start antenna tuning
  - Right click: ATU (SW158) - toggle ATU mode
  - Also updated button label to match: "ATU TUNE" / "ATU"
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **ui**: Stack ATU TUNE button text to fit button face
  - Display as:
  - ATU
  - TUNE
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **ui**: Move VFO antenna labels to left/right edges of row
  - VFO A antenna label now at leftmost position
  - VFO B antenna label now at rightmost position
  - TX antenna remains centered between them
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **ui**: Align VFO B AGC row with multifunction meter edge
  - Set fixed width (200px) on normal content to match meter
  - AGC/PRE/ATT/NB/NR/NTCH labels now align with meter edge on VFO B
  - Simplified meter layout since container width matches meter width
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **ui**: Properly right-align VFO B AGC row with meter edge
  - Wrap features row in a container widget with fixed 200px width
  - to ensure the stretch properly pushes labels to the right edge
  - on VFO B, matching the meter alignment above.
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **ui**: Simplify VFO B AGC row layout alignment
  - Both VFO A and B now use identical internal layout for the features row
  - (AGC, PRE, ATT, NB, NR, NTCH labels). The left/right positioning is
  - handled by the outer stackedRow layout:
  - VFO A: widget + stretch → left-aligned
  - VFO B: stretch + widget → right-aligned
  - Removes unnecessary branching and complexity from the features row setup.
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **ui**: Add optimistic updates for DATA submode selection
  - K4 echoes stale DT/DT$ values after SET commands, causing the UI to
  - revert to the old submode. Fix with optimistic updates:
  - Add setDataSubMode()/setDataSubModeB() optimistic setters to RadioState
  - Parse DT commands from modeSelected signal and update state immediately
  - Add 500ms cooldown to ignore stale K4 echoes after optimistic update
  - Works correctly for both Main RX and Sub RX (B SET mode)
  - Also adds debug output for DT parsing and mode popup state changes.
  - 🤖 Generated with [Claude Code](https://claude.com/claude-code)
- **hardware**: Correct KPOD HID protocol implementation
  - Check cmd byte (buffer[0]) before processing controls:
  - Only process when cmd='u' (0x75) indicates new event
  - cmd=0 means no event, controls byte is stale data
  - Fix button tap/hold detection:
  - KPOD sends hold=false initially, then hold=true after threshold
  - Detect hold transition while button is still pressed
  - Implicit release detection when cmd=0 with pending button
  - Fix rocker position tracking:
  - Rocker state only valid in cmd='u' events
  - RockerLeft(2)=VFO A, RockerCenter(0)=VFO B, RockerRight(1)=RIT/XIT
  - Update hardware/CLAUDE.md with complete protocol documentation
- **ui**: Remove off-screen popup positioning and KPA1500 meter scale
  - Replace move(-10000,-10000) with adjustSize() in popup widgets to
  - eliminate Qt warnings about windows outside screen bounds
  - Remove KPA1500 amplifier meter integration (setAmplifierEnabled,
  - m_kpa1500Enabled, 0-1900W scale) - will be replaced with dedicated
  - KPA1500 widget later
  - Multifunction meter now only displays K4 data (0-110W or 0-10W QRP)
- **panadapter**: Center AM/FM passband on carrier without IF shift
  - AM/FM modes use both sidebands centered symmetrically on the carrier
  - frequency. The passband should not be offset by IF shift like SSB modes.
- **dsp**: Correct notch filter positioning in panadapter and mini pan
  - For CW/CW-R modes, the notch offset is now calculated relative to the
  - passband center (notchPitch - cwPitch) instead of using the raw notch
  - pitch. This ensures the notch aligns correctly with the passband in
  - both displays regardless of tunedFreq/centerFreq mismatch.
- **ui**: Move TLS checkbox below ID field in Server Manager
  - Repositioned the "Use TLS" checkbox to appear after the ID text field
  - for better visual flow. Added spacing between checkbox and label text.
- **ui**: Implement REF LVL control with absolute value CAT commands
  - Add setRefLevel() and setRefLevelB() optimistic setters to RadioState
  - Remove invalid #REF+/#REF- increment commands from DisplayPopupWidget
  - Add proper handlers in MainWindow that send absolute values (#REFn;)
  - Respects A/B VFO selection for independent ref level control
  - Step size: 1 dB per click/scroll, range: -200 to 60 dB
- **audio**: Correct EM0 decode format from float to S32LE
  - K4 documentation describes EM0 as "RAW 32-bit float stereo" but packet
  - analysis revealed it actually sends 32-bit signed integers (S32LE).
- **ui**: Update Fixed Tune button cycle order and add optimistic update
  - Change cycle: STATIC → SLIDE2 → TRACK → FIXED1 → FIXED2 → SLIDE1
  - Use optimized CAT commands (#FXA3, #FXA4, #FXA0;#FXT0, #FXT1, #FXA1, #FXA2)
  - Add optimistic local update since K4 doesn't echo #FXT/#FXA commands
- **ci**: Add qtserialport to Windows Qt modules
  - HaliKey serial port support requires QtSerialPort module.

### Revert

- **protocol**: Restore working PAN payload byte offsets
  - The K4-Remote Protocol Rev. A1 document shows different byte offsets
  - than what the actual K4 firmware sends. Reverting to the original
  - working structure.
  - The frequency alignment issue needs to be investigated separately -
  - the actual payload structure differs from the documentation.

---

