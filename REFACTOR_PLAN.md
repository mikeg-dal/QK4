# QK4 Maintainability Refactoring Plan

## Executive Summary

**What this is:** A methodical, section-by-section refactoring of QK4 to improve maintainability, reduce class bloat, eliminate duplication, and make the codebase sustainable as features grow — without changing any user-visible behavior.

**What this is NOT:** No new features. No UI changes. No protocol changes. No threading model changes. Pure structural cleanup.

**Why now:** QK4 is in beta. The codebase is 39,000 lines across 156 classes. Two classes — `MainWindow` (6,063 lines, 90 members, 441 connects) and `RadioState` (4,164 lines, 158 members, 103 signals) — concentrate so much responsibility that every new feature risks merge conflicts, subtle regressions, and cognitive overload. The architecture underneath (threading, protocol, styling) is solid. The problem is organizational: too much logic in too few classes.

**Expected outcomes when complete:**
1. No single file exceeds ~800 lines (currently: 6,063)
2. Each class has one clear responsibility (currently: MainWindow has 15)
3. A/B VFO handler duplication is eliminated (~250 lines of copy-paste gone)
4. New features can be added by modifying 1-2 files instead of always touching MainWindow
5. CatServer dispatch matches RadioState's clean registry pattern
6. Every refactoring step compiles and runs identically to the previous commit

---

## Class Design Guidelines

These rules govern all decisions during and after this refactor. They apply to new feature work too — the whole point is that we never grow another MainWindow.

### When a New Class Is Required

| Signal | Action |
|--------|--------|
| 3+ related members (pointers, state vars) that are always used together | That group is a class waiting to be born |
| A method exceeds ~150 lines | Extract logical sub-steps into a helper class or at minimum private methods |
| A file exceeds ~600-800 lines | Split by responsibility — find the seam where concerns diverge |
| Adding a feature requires modifying a class that has nothing to do with that feature | Wrong class — create or find the right one |
| Two methods share 80%+ identical code differing only in parameters | Template/helper method, or parameterized class |
| A class has members from 3+ unrelated domains (e.g., audio + network + UI) | It's an orchestrator that should delegate to domain classes |
| Forward-declaring more than ~10 classes | Your class knows too many things |

### New Feature Decision Tree

```
Adding a new feature?
  │
  ├─ Does it fit cleanly into an existing controller/class?
  │   ├─ Yes, and that class stays under ~600 lines → Add it there
  │   └─ Yes, but the class is already ~600+ lines → Extract a sub-concern first, then add
  │
  ├─ Does it span multiple existing controllers?
  │   └─ Add the logic to the most relevant controller.
  │      Have it emit signals that other controllers consume.
  │      MainWindow only wires cross-controller signals.
  │
  └─ Does it represent a genuinely new domain?
      (new hardware device, new protocol, new UI panel category)
      └─ New class from day one. Don't "temporarily" put it in MainWindow.
```

### MainWindow's Role After Refactor

MainWindow's job is exactly three things:

1. **Own the top-level layout** (QMainWindow, menu bar, central widget)
2. **Create controllers and wire their cross-domain signals** (the glue)
3. **Handle window-level events** (resize, close, move)

If new code doesn't fit one of those three, it doesn't go in MainWindow. Period.

### Why These Thresholds

- **600-800 line limit:** Beyond this, a developer cannot hold the full class in their head. Scrolling replaces understanding. Merge conflicts multiply.
- **3+ related members:** If variables travel together, they represent a concept. Concepts deserve names (classes). Unnamed concepts become accidental coupling.
- **150 line methods:** Long methods hide control flow. A method should do one thing at one level of abstraction. If you need comments like `// --- Now handle the audio part ---`, that's a function boundary you skipped.
- **"Nothing to do with your feature":** This is the most important rule. The #1 way classes bloat is "I'll just add it here for now." There is no "for now" — there is only the next person who sees 90 members and gives up trying to understand the class.

---

## Commit Strategy

Every section produces **multiple small, atomic commits** following this discipline:

```
1. One commit per extractable unit (one class, one method group, one pattern)
2. Every commit compiles cleanly (cmake --build build)
3. Every commit passes clang-format
4. Conventional commit messages: refactor(scope): description
5. Never combine structural moves with logic changes in the same commit
6. Tag milestones: git tag refactor/section-N-complete
```

**Why this granularity matters:**
- `git bisect` can pinpoint exactly which extraction broke something
- `git revert <sha>` undoes one clean unit, not a tangled mess
- Code review is tractable (200-line diffs, not 2,000-line diffs)
- If we abandon mid-section, the completed commits still stand on their own

**Pre-flight before every commit:**
```bash
clang-format -i <changed files>
find src -name '*.cpp' -o -name '*.h' | xargs clang-format --dry-run --Werror
cmake --build build
```

---

## Section 1: RadioState A/B Handler Deduplication

### The Problem
RadioState has **22 handler pairs** where Main and Sub versions are near-identical. The only differences:
- String offset: `cmd.mid(2)` vs `cmd.mid(3)` (because Sub commands have `$` suffix)
- Member variable: `m_foo` vs `m_fooB`
- Signal: `fooChanged()` vs `fooBChanged()`

Example of the duplication (repeated 22 times):
```cpp
void RadioState::handleFP(const QString &cmd) {
    if (cmd.length() <= 2) return;
    bool ok;
    int fp = cmd.mid(2).toInt(&ok);
    if (ok && fp >= 1 && fp <= 3 && fp != m_filterPosition) {
        m_filterPosition = fp;
        emit filterPositionChanged(m_filterPosition);
    }
}
void RadioState::handleFPSub(const QString &cmd) {
    if (cmd.length() <= 3) return;       // <-- only this differs
    bool ok;
    int fp = cmd.mid(3).toInt(&ok);      // <-- and this
    if (ok && fp >= 1 && fp <= 3 && fp != m_filterPositionB) {
        m_filterPositionB = fp;                    // <-- and these
        emit filterPositionBChanged(m_filterPositionB);
    }
}
```

~250 lines of nearly identical code. Every bug fix must be applied twice. Every new A/B parameter requires writing two functions.

### The Fix
Introduce private helper templates/lambdas that capture the differences as parameters:

```cpp
// One helper replaces 22 pairs of simple int handlers
void RadioState::handleIntPair(const QString &cmd, int prefixLen,
                               int &member, int min, int max,
                               void (RadioState::*signal)(int)) {
    if (cmd.length() <= prefixLen) return;
    bool ok;
    int val = cmd.mid(prefixLen).toInt(&ok);
    if (ok && val >= min && val <= max && val != member) {
        member = val;
        emit (this->*signal)(val);
    }
}
```

Registration becomes:
```cpp
m_commandHandlers.append({"FP", [this](const QString &cmd) {
    handleIntPair(cmd, 2, m_filterPosition, 1, 3, &RadioState::filterPositionChanged);
}});
m_commandHandlers.append({"FP$", [this](const QString &cmd) {
    handleIntPair(cmd, 3, m_filterPositionB, 1, 3, &RadioState::filterPositionBChanged);
}});
```

### Scope
- **Files touched:** `radiostate.h`, `radiostate.cpp`
- **No other files change** — all signals remain identical, all external connections untouched
- **Zero behavioral change** — same parsing, same signals, same values

### Commit Sequence
1. `refactor(radiostate): add handleIntPair helper for A/B deduplication` — add the helper method, no callers yet
2. `refactor(radiostate): migrate simple int handler pairs to handleIntPair` — FP, RG, SQ, PA, RA, NB, NR, etc.
3. `refactor(radiostate): add handleBoolPair helper and migrate bool handlers` — NA, LK, etc.
4. `refactor(radiostate): add handleFreqPair helper and migrate FA/FB` — 64-bit frequency variant
5. `refactor(radiostate): migrate remaining mechanical pairs` — SM, AP, GT, etc. (handlers with enum mapping or extra logic stay as-is)
6. `refactor(radiostate): remove dead handler functions after migration` — delete the now-unused individual functions
7. Tag: `git tag refactor/section-1-complete`

### Traceability
- Each pair being migrated is listed in the commit message body
- Handlers with **real logic differences** (handleMD emits qskDelayChanged; handleTD has different field offsets) are explicitly excluded and documented
- After section completes: grep for all remaining `handle.*Sub` to verify none were missed

### Expected Result
- ~250 lines removed from radiostate.cpp
- Adding a new A/B parameter pair goes from "write two functions" to "add two registration lines"
- Bug fixes to parsing logic apply once, not twice

### Pause Point
After Section 1 is complete, we stop and verify:
- [ ] Full compile passes
- [ ] clang-format passes
- [ ] Manual smoke test against K4 hardware (all radio state updates still work)

---

## Section 2: CatServer Command Registry

### The Problem
`catserver.cpp` (406 lines) dispatches commands via a 165-line `if (prefix == "XX")` chain (lines 178-343). RadioState already solved this problem with a handler registry. CatServer didn't follow the pattern.

The chain is:
```cpp
if (prefix == "FA") { return QString("FA%1;").arg(...); }
if (prefix == "FB") { return QString("FB%1;").arg(...); }
if (prefix == "MD") { return buildModeResponse(); }
// ... 30+ more
```

### Why It Matters
- Adding a new command means finding the right spot in a long chain and hoping order doesn't matter
- No consistency with RadioState's approach — two dispatch patterns to understand
- String comparison on every prefix for every command (minor perf, major readability)

### The Fix
Replace with a `QHash<QString, std::function<QString()>>` registry, initialized once:

```cpp
void CatServer::registerGetHandlers() {
    m_getHandlers["FA"] = [this]() { return buildFrequencyResponse("FA", m_radioState->frequency()); };
    m_getHandlers["FB"] = [this]() { return buildFrequencyResponse("FB", m_radioState->vfoB()); };
    m_getHandlers["MD"] = [this]() { return buildModeResponse(); };
    // ...
}
```

Dispatch becomes:
```cpp
auto it = m_getHandlers.find(prefix);
if (it != m_getHandlers.end()) return it.value()();
```

### Scope
- **Files touched:** `catserver.h`, `catserver.cpp`
- **No external interface changes** — same signals, same responses, same protocol
- Placeholder handlers (AG, SQ, TM returning hardcoded values) are preserved as-is with comments

### Commit Sequence
1. `refactor(catserver): add command registry and registerGetHandlers()` — add the map, populate it, no callers yet
2. `refactor(catserver): switch handleCommand() to registry dispatch` — replace if-chain with map lookup
3. `refactor(catserver): extract buildMeterResponse helper` — consolidate inline formatting
4. Tag: `git tag refactor/section-2-complete`

### Expected Result
- If-chain replaced with O(1) hash lookup
- Adding a new GET handler = one line in registerGetHandlers()
- Consistent pattern with RadioState

### Pause Point
- [ ] Full compile passes
- [ ] clang-format passes
- [ ] Test with WSJT-X or MacLoggerDX connecting via CAT server (same responses as before)

---

## Section 3: Extract MainWindow — Connection Controller

### The Problem
MainWindow manages TCP connection lifecycle, authentication, error handling, and connection state UI — roughly 200 lines of slots and helpers mixed in with 5,800 lines of unrelated code.

### Why Start Here
Connection management is the **most self-contained domain** in MainWindow:
- Clear entry/exit points (connect/disconnect buttons)
- Minimal coupling to other domains (just needs TcpClient + a few status labels)
- Small surface area — low risk, high learning value for the pattern we'll repeat

### What Gets Extracted
A new class `ConnectionController` that owns:
- `TcpClient` lifecycle (create, connect, disconnect, destroy)
- `NetworkMetrics` monitoring
- Connection state machine (idle → connecting → authenticating → connected → error)
- Authentication flow (onAuthenticated, onAuthenticationFailed)
- Error notification for connection issues
- Status label updates (connection status text, net health widget)

**Slots moving out of MainWindow:**
- `onConnectClicked()` / `onDisconnectClicked()`
- `onStateChanged()`
- `onError()`
- `onAuthenticated()` (the initialization sequence stays as a signal — ConnectionController emits `radioReady()`, MainWindow responds)
- `onAuthenticationFailed()`
- `updateConnectionState()`

**Members moving out of MainWindow:**
- `m_tcpClient`, `m_networkMetrics`, `m_ioThread`
- `m_connectionStatusLabel`, `m_netHealthWidget`
- `m_currentRadio` (RadioEntry for current connection target)

### The Pattern (used for all subsequent extractions)
```
1. Create new class header + cpp
2. Move member variables from MainWindow to new class
3. Move slot implementations from MainWindow to new class
4. Move connect() calls from MainWindow to new class
5. MainWindow holds a pointer to the new class
6. New class exposes signals for cross-domain events (e.g., radioReady())
7. MainWindow connects to those signals for remaining orchestration
```

### Scope
- **New files:** `src/controllers/connectioncontroller.h`, `src/controllers/connectioncontroller.cpp`
- **Modified files:** `mainwindow.h`, `mainwindow.cpp`, `CMakeLists.txt`
- **No other files change**

### Commit Sequence
1. `refactor(build): add src/controllers/ directory to CMakeLists.txt`
2. `refactor(connection): create ConnectionController class with header` — empty class, compiles
3. `refactor(connection): move TcpClient ownership to ConnectionController` — member + creation + thread setup
4. `refactor(connection): move connection state slots to ConnectionController` — onStateChanged, onError, updateConnectionState
5. `refactor(connection): move authentication flow to ConnectionController` — onAuthenticated emits radioReady() signal
6. `refactor(connection): move connect/disconnect actions to ConnectionController` — button wiring
7. `refactor(connection): move NetworkMetrics and status widgets to ConnectionController` — net health display
8. `refactor(connection): wire MainWindow to ConnectionController signals` — radioReady, errorOccurred, etc.
9. `refactor(connection): remove dead connection code from MainWindow` — final cleanup
10. Tag: `git tag refactor/section-3-complete`

### Traceability
- Each commit message lists exactly which members/slots moved
- `onAuthenticated()` is the most complex — it sends ~50 initialization CAT commands. These stay as the body of a signal handler connected to `ConnectionController::radioReady()`. The controller doesn't need to know what initialization means; it just signals "connection is live."

### Expected Result
- ~200 lines out of MainWindow
- Connection logic is testable in isolation
- New connection features (reconnect logic, multi-radio) have a clear home
- Pattern established for all subsequent extractions

### Pause Point
- [ ] Full compile passes
- [ ] clang-format passes
- [ ] Connect/disconnect to K4 works identically
- [ ] Net health widget updates correctly

---

## Section 4: Extract MainWindow — Hardware Controller

### The Problem
MainWindow manages three hardware peripherals (KPOD, HaliKey, IambicKeyer) plus sidetone generation — ~150 lines of setup, connect() calls, and event handlers scattered through a 6,000-line file. None of this logic relates to UI layout or radio state.

### What Gets Extracted
A new class `HardwareController` that owns:
- `KpodDevice` — USB tuning knob (polling, encoder events, rocker events)
- `HalikeyDevice` — CW paddle serial device
- `IambicKeyer` — Iambic keyer state machine (runs on m_keyerThread)
- `SidetoneGenerator` — CW sidetone audio (runs on m_sidetoneThread)
- `m_keyerThread`, `m_sidetoneThread` — dedicated QThreads for these devices

**Slots moving out:**
- `onKpodEncoderRotated()`, `onKpodRockerChanged()`, `onKpodPollError()`, `onKpodEnabledChanged()`
- All HaliKey/Keyer/Sidetone wiring (11 connect() calls)

**Signals exposed by HardwareController:**
- `encoderTuned(int steps)` — for VFO frequency adjustment
- `rockerChanged(int direction)` — for KPOD rocker actions
- `keyerElement(const QString &catCmd)` — for CW element → TcpClient
- `keyerPaddleSettingsChanged(...)` — when RadioState updates keyer config

### Why This Order
Hardware is the second-most isolated domain after connection management. KPOD/HaliKey/Keyer talk to MainWindow through a narrow signal interface. Moving them out removes 4 member pointers, 2 threads, and ~15 connect() calls from MainWindow.

### Scope
- **New files:** `src/controllers/hardwarecontroller.h`, `src/controllers/hardwarecontroller.cpp`
- **Modified files:** `mainwindow.h`, `mainwindow.cpp`, `CMakeLists.txt`

### Commit Sequence
1. `refactor(hardware): create HardwareController class skeleton`
2. `refactor(hardware): move KpodDevice ownership and slots`
3. `refactor(hardware): move HalikeyDevice and IambicKeyer ownership`
4. `refactor(hardware): move SidetoneGenerator and thread lifecycle`
5. `refactor(hardware): move keyer wiring (RadioState → Keyer settings)`
6. `refactor(hardware): wire HardwareController signals to MainWindow`
7. `refactor(hardware): remove dead hardware code from MainWindow`
8. Tag: `git tag refactor/section-4-complete`

### Expected Result
- ~150 lines out of MainWindow
- 4 member pointers, 2 threads, 15 connects removed from MainWindow
- Hardware peripherals are self-contained — adding a new device doesn't touch MainWindow

### Pause Point
- [ ] Full compile passes
- [ ] KPOD encoder tunes VFO correctly
- [ ] HaliKey CW keying works
- [ ] Sidetone plays on key-down

---

## Section 5: Extract MainWindow — Audio Controller

### The Problem
MainWindow owns `AudioEngine`, `OpusDecoder`, `OpusEncoder`, the audio thread, PTT state, and microphone frame handling. ~100 lines of audio plumbing that has no business in a window class.

### What Gets Extracted
A new class `AudioController` that owns:
- `AudioEngine` (on `m_audioThread`)
- `OpusDecoder`, `OpusEncoder`
- `m_audioThread`
- PTT state machine (`m_pttActive`, `m_txSequence`)
- `onPttPressed()`, `onPttReleased()`, `onMicrophoneFrame()`

**Signals exposed:**
- `pttStateChanged(bool active)` — for UI updates (TX indicator)
- `audioFrame(QByteArray)` — encoded mic data for TcpClient

### Scope
- **New files:** `src/controllers/audiocontroller.h`, `src/controllers/audiocontroller.cpp`
- **Modified files:** `mainwindow.h`, `mainwindow.cpp`, `CMakeLists.txt`

### Commit Sequence
1. `refactor(audio): create AudioController class skeleton`
2. `refactor(audio): move AudioEngine, Opus codec ownership`
3. `refactor(audio): move PTT state machine and mic frame handler`
4. `refactor(audio): move audio thread lifecycle`
5. `refactor(audio): wire AudioController signals to MainWindow and TcpClient`
6. `refactor(audio): remove dead audio code from MainWindow`
7. Tag: `git tag refactor/section-5-complete`

### Expected Result
- ~100 lines out of MainWindow
- Audio pipeline is self-contained
- PTT logic can be understood without reading 6,000 lines of context

### Pause Point
- [ ] Full compile passes
- [ ] Audio plays from K4
- [ ] PTT transmits with mic audio
- [ ] TX indicator lights correctly

---

## Section 6: Extract MainWindow — Spectrum Controller

### The Problem
MainWindow manages panadapter display modes (Main/Dual/Sub), span control, spectrum data routing, passband visualization, and TX marker updates — ~200 lines of DSP display orchestration.

### What Gets Extracted
A new class `SpectrumController` that owns:
- `PanadapterRhiWidget` (A and B)
- `m_spectrumContainer`, `m_spectrumSeparator`
- Span up/down buttons and logic (including the adaptive step sizing)
- `m_panadapterMode` enum and `setPanadapterMode()`
- `onSpectrumData()`, `onMiniSpectrumData()`
- `updatePanadapterPassbands()`, `updateTxMarkers()`
- `m_vfoIndicatorA`, `m_vfoIndicatorB`
- `checkAndHideMiniPanB()`

**Signals exposed:**
- `clickTuned(quint64 frequency)` — when user clicks spectrum to QSY

### Scope
- **New files:** `src/controllers/spectrumcontroller.h`, `src/controllers/spectrumcontroller.cpp`
- **Modified files:** `mainwindow.h`, `mainwindow.cpp`, `CMakeLists.txt`

### Commit Sequence
1. `refactor(spectrum): create SpectrumController class skeleton`
2. `refactor(spectrum): move panadapter widgets and container layout`
3. `refactor(spectrum): move spectrum data handlers`
4. `refactor(spectrum): move span control logic and buttons`
5. `refactor(spectrum): move passband and TX marker updates`
6. `refactor(spectrum): move panadapter mode management`
7. `refactor(spectrum): wire SpectrumController signals to MainWindow`
8. `refactor(spectrum): remove dead spectrum code from MainWindow`
9. Tag: `git tag refactor/section-6-complete`

### Expected Result
- ~200 lines out of MainWindow
- Spectrum display is self-contained
- Span logic, click-tune, and display modes have a clear home

### Pause Point
- [ ] Full compile passes
- [ ] Spectrum/waterfall displays correctly
- [ ] Span up/down works
- [ ] Click-to-tune QSYs correctly
- [ ] Dual/Main/Sub display modes switch correctly

---

## Section 7: Extract MainWindow — Popup Manager

### The Problem
MainWindow holds **20+ popup widget pointers** and their toggle/close/show logic. Every popup follows the same pattern: create lazily, position relative to a trigger widget, close on outside click, close all others when one opens. This is mechanical orchestration, not application logic.

### What Gets Extracted
A new class `PopupManager` that owns:
- All popup widget pointers (BandPopup, DisplayPopup, FnPopup, RxEqPopup, TxEqPopup, LineOutPopup, LineInPopup, MicInputPopup, MicConfigPopup, VoxPopup, SsbBwPopup, KeyingWeightPopup, MainRxPopup, SubRxPopup, TxPopup, AntennaCfgPopups, ModePopup, MacroDialog)
- `toggleXxxPopup()` methods
- `closeAllPopups()`
- Lazy creation logic
- Debounce timers for EQ popups

### Scope
- **New files:** `src/controllers/popupmanager.h`, `src/controllers/popupmanager.cpp`
- **Modified files:** `mainwindow.h`, `mainwindow.cpp`, `CMakeLists.txt`

### Commit Sequence
1. `refactor(popups): create PopupManager class skeleton`
2. `refactor(popups): move popup member pointers to PopupManager`
3. `refactor(popups): move toggle methods (band, display, fn, mode)`
4. `refactor(popups): move audio popup toggles (eq, mic, line, vox)`
5. `refactor(popups): move button row popups (mainRx, subRx, tx)`
6. `refactor(popups): move closeAllPopups and mutual-exclusion logic`
7. `refactor(popups): move EQ debounce timers`
8. `refactor(popups): wire PopupManager signals to MainWindow`
9. `refactor(popups): remove dead popup code from MainWindow`
10. Tag: `git tag refactor/section-7-complete`

### Expected Result
- ~20 member pointers and ~300 lines out of MainWindow
- Adding a new popup means modifying PopupManager, not MainWindow
- Popup lifecycle (lazy create, position, close-others) is centralized

### Pause Point
- [ ] Full compile passes
- [ ] Every popup opens/closes correctly
- [ ] Popups close when another opens
- [ ] EQ sliders debounce correctly

---

## Section 8: Large Widget Decomposition

### The Problem
Three UI files exceed reasonable size:
- `OptionsDialog` (1,502 lines) — 7 tabs crammed into one class
- `DisplayPopupWidget` (1,281 lines) — multiple control pages in one widget
- `SideControlPanel` (704 lines) — multiple button groups + overlays

### The Fix

**OptionsDialog:** Extract each tab into its own widget class:
- `AudioInputPage`, `AudioOutputPage`, `RigControlPage`, `CwKeyerPage`, `KpodPage`, `Kpa1500Page`, `AboutPage`
- OptionsDialog becomes a thin tab container (~150 lines)

**DisplayPopupWidget:** Extract control groups into standalone widgets:
- Keep the stacked-page architecture but move each page's content into its own class
- Target: DisplayPopupWidget under 400 lines

**SideControlPanel:** Lower priority — 704 lines is borderline. Evaluate after other extractions.

### Commit Sequence
1. `refactor(options): extract AboutPage from OptionsDialog`
2. `refactor(options): extract AudioInputPage from OptionsDialog`
3. `refactor(options): extract AudioOutputPage from OptionsDialog`
4. `refactor(options): extract RigControlPage from OptionsDialog`
5. `refactor(options): extract CwKeyerPage from OptionsDialog`
6. `refactor(options): extract KpodPage from OptionsDialog`
7. `refactor(options): extract Kpa1500Page from OptionsDialog`
8. `refactor(display): extract control group widgets from DisplayPopupWidget`
9. Tag: `git tag refactor/section-8-complete`

### Expected Result
- OptionsDialog drops from 1,502 lines to ~150 lines
- Each settings tab is independently modifiable
- DisplayPopupWidget drops from 1,281 to ~400 lines

### Pause Point
- [ ] Full compile passes
- [ ] All Options tabs render and function correctly
- [ ] Display popup controls work identically

---

## Section 9: Named Constants Sweep

### The Problem
Hardware-specific magic numbers are scattered through meter and UI code:
- `25.0` (dB scale), `25.0` (amps), `10.0`/`110.0` (power), `15.0` (S-meter) in txmeterwidget.cpp
- Corner radii, button dimensions repeated across popup widgets
- Span constants in mainwindow.cpp

### The Fix
Move hardware constants to a `K4Constants` namespace (or extend `K4Styles::Dimensions`):
```cpp
namespace K4Constants {
    constexpr double MaxCurrentAmps = 25.0;
    constexpr double MaxPowerWattsQRP = 10.0;
    constexpr double MaxPowerWattsQRO = 110.0;
    constexpr int SMeterMaxDB = 25;
    // ...
}
```

UI layout constants that repeat across popups go into `K4Styles::Dimensions`.

### Commit Sequence
1. `refactor(constants): add K4Constants namespace for hardware limits`
2. `refactor(constants): replace magic numbers in txmeterwidget.cpp`
3. `refactor(constants): consolidate repeated UI dimensions into K4Styles`
4. Tag: `git tag refactor/section-9-complete`

### Expected Result
- Magic numbers have names and a single source of truth
- Hardware limits are documented by their constant names
- Changing a K4 hardware parameter means one edit, not a grep

### Pause Point
- [ ] Full compile passes
- [ ] Meter displays render identically

---

## Progress Tracking

| Section | Description | Est. Commits | Status |
|---------|-------------|-------------|--------|
| 1 | RadioState A/B dedup | 7 | Not started |
| 2 | CatServer registry | 4 | Not started |
| 3 | Extract ConnectionController | 10 | Not started |
| 4 | Extract HardwareController | 8 | Not started |
| 5 | Extract AudioController | 7 | Not started |
| 6 | Extract SpectrumController | 9 | Not started |
| 7 | Extract PopupManager | 10 | Not started |
| 8 | Large widget decomposition | 9 | Not started |
| 9 | Named constants sweep | 4 | Not started |
| **Total** | | **~68** | |

## Cumulative Impact

After all 9 sections:

| Metric | Before | After (est.) |
|--------|--------|-------------|
| MainWindow lines | 6,063 | ~800 |
| MainWindow members | 90 | ~15 |
| MainWindow connects | 441 | ~40 |
| RadioState handler duplication | 250 lines | 0 |
| CatServer dispatch style | if-chain | hash registry |
| OptionsDialog lines | 1,502 | ~150 |
| Largest single file | 6,063 | ~800 |
| Magic numbers in meter code | 12+ | 0 |
| Total commits | — | ~68 |

## Ground Rules

1. **We go section by section, always pausing when done.** No rushing ahead.
2. **Every commit compiles.** If it doesn't, we fix it before moving on.
3. **No behavior changes.** If something works differently after a commit, that's a bug in the refactor.
4. **No "while we're here" changes.** Refactoring only. Feature work is a separate branch.
5. **Explain every move.** Each commit message documents what moved, where, and why.
6. **Verify with hardware when possible.** The K4 is the ultimate test.
7. **Class Design Guidelines govern all decisions.** If a new class is needed per the thresholds above, we make one. If it isn't, we don't. No judgment calls — follow the rules.
