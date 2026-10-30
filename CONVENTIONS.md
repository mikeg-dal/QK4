# Conventions

The rules for QK4 code: naming, formatting, commits, comments, styling, and the numbered
architecture rules. This file owns the rules. How-to recipes live in `PATTERNS.md`; the controller
inventory lives in `docs/controllers.md`; the styling guide is `docs/styling.md`; what is in each
source directory is in that directory's `README.md`.

Rule numbers are cited from code comments (`Rule 2`, `Rule 11` …). Do not renumber; add new rules at
the end.

## General

- Preserve existing behaviour when adding features. Test against K4 hardware when the change touches
  the radio link, audio, CW keying or USB devices.
- Use `QByteArray` for binary protocol data.
- Radio state lives in `RadioState` and its subsystem structs. Widgets learn about it from
  `RadioState` signals (`PATTERNS.md` → Direct Observation, Controller Pattern).
- `Protocol` only frames packets. It emits the raw CAT text (`catResponseReceived(QString)`);
  `RadioState::parseCATCommand()` and the subsystem handlers parse it.
- Each controller wires its own slice. `MainWindow` constructs and coordinates controllers (Rule 12).
- **Parse order:** register specific prefixes before general ones (`RG$` before `RG`).
  `test_radiostate_registry` enforces this.
- **Sentinels:** initialise state to an invalid value (`-1`, `-999`) so the first radio update emits.
  Never a legal value, not even "off" or 0: the connection dump reports those, and a change guard
  would swallow them. On/off state is a `ReportedFlag` (`src/models/radiostate/reportedflag.h`).
  Every subsystem struct has a `reset()` that restores its sentinels; `RadioState::reset()` calls it
  on disconnect.

---

## Naming

| Element | Convention | Example |
|---------|------------|---------|
| Classes | PascalCase | `PanadapterRhiWidget`, `RadioState` |
| Methods | camelCase | `updateSpectrum()`, `setRefLevel()` |
| Member variables | `m_camelCase` | `m_frequencyALabel`, `m_ioThread` |
| Signals | camelCase, past tense for state changes | `frequencyChanged`, `clicked` |
| Slots | `on` + source + event | `onFrequencyChanged`, `onConnectClicked` |
| Namespaces | PascalCase | `K4Styles`, `K4Styles::Colors` |
| Design tokens (namespace scope) | PascalCase | `FontSizeBody`, `ShadowMargin`, `KnobButtonWidth` |
| Class- and header-scope constants | SCREAMING_SNAKE or `kCamelCase` — match the file | `BASE_TEXTURE_WIDTH`, `kPreferredWidth` |
| File-local constants in a `.cpp` | `kCamelCase` | `kOverlayMaxSegments` |

Constants in `.cpp` files are almost all `kCamelCase`. Header constants are mixed: the K4Styles and
panadapter tokens are PascalCase, protocol and audio limits are SCREAMING_SNAKE, and some classes use
`kCamelCase`. New code follows the file it lands in. Count the split with:

```bash
rg -o --no-filename "constexpr [\w:<>\s\*]+? (\w+)\s*(=|\{)" -r '$1' -g '*.h' src \
  | awk '/^[A-Z][A-Z0-9_]+$/{s++;next} /^k[A-Z]/{k++;next} /^[A-Z]/{p++} END{print "SCREAMING",s,"kCamel",k,"Pascal",p}'
```

Member pointer suffixes: `m_<purpose>Label`, `m_<name>Btn`, and an `A`/`B` suffix for dual-VFO pairs
(`m_vfoA`, `m_vfoB`).

---

## Formatting

`.clang-format`: LLVM base, 4-space indent, 120 columns, attached braces, right-aligned pointers
(`Type *name`), includes not sorted, `IndentCaseLabels: false`.

Always use the script. It reads the pinned version from the `pipx install clang-format==` line in
`.github/workflows/ci-checks.yml`, finds that exact clang-format, and refuses any other version.
clang-format changes its line breaking between patch releases, so a near-miss version can pass
locally and fail CI.

```bash
scripts/check-format.sh             # check src/ and tests/, the same set CI checks
scripts/check-format.sh --fix       # rewrite offending files
scripts/check-format.sh --staged    # check only staged content (the hook form)
scripts/check-format.sh --bootstrap # install the pinned version into .format-venv/
```

Exit codes: 0 clean, 1 violations, 2 cannot check.

On an Apple Silicon Mac with Homebrew the pinned binary is
`/opt/homebrew/opt/llvm@18/bin/clang-format`. That path does not exist on Linux, Windows or an Intel
Mac. A major-version bump moves it; update this line with the bump.

### Pre-commit hook

```bash
printf '#!/bin/sh\nexec "$(git rev-parse --show-toplevel)/scripts/check-format.sh" --staged\n' \
    > .git/hooks/pre-commit
chmod +x .git/hooks/pre-commit
```

Do not symlink the script into `.git/hooks/`. A symlink cannot pass `--staged`, so every commit pays
for a whole-tree check.

---

## Before Every Commit

```bash
scripts/check-format.sh --fix
scripts/check-format.sh
cmake --build build
ctest --test-dir build --output-on-failure
```

All four must pass (Rule 10). Then review against this list:

- [ ] Follows the existing pattern for its kind of change (`PATTERNS.md`).
- [ ] Every new `connect()` has a context object, and cross-thread connections are queued (Rule 14).
- [ ] State initialised to sentinels; `reset()` covers new fields.
- [ ] Anything the change replaced is fully removed — code, tests, comments, docs (Rule 13).
- [ ] New files are listed in their directory's `README.md`.
- [ ] Another developer would understand it.

## Commit Messages

Release notes are generated from commits by `git-cliff` at tag time (`.github/workflows/release.yml`).
Use [conventional commits](https://www.conventionalcommits.org/):

```
type(scope): short summary

Optional body. Each line becomes a sub-bullet in the release notes.
```

| Type | Release notes section | Example |
|------|-----------------------|---------|
| `feat` | Added | `feat(audio): add jitter buffer for RX playback` |
| `fix` | Fixed | `fix(panadapter): correct CW pitch offset in passband` |
| `refactor` | Changed | `refactor(state): replace if-else chain with handler registry` |
| `perf` | Changed | `perf(dsp): reduce CPU usage with GPU spectrum rendering` |
| `docs` | skipped | `docs: update README build instructions` |
| `chore` | skipped | `chore: bump Qt to 6.8` |
| `ci` | skipped | `ci: add Raspberry Pi build job` |

Keep the summary under 72 characters. The scope is optional (`audio`, `panadapter`, `state`, `ui` …).
Do not add `Co-Authored-By` trailers.

---

## Comment Conventions

Default: **write no comments.** Names say what the code does. Comment only when the *why* is not
obvious: a hidden constraint, an invariant, a K4 protocol quirk, a threading or ordering requirement,
a workaround. If removing the comment would not confuse a later reader, do not write it.

Two prefixes are reserved so they can be surveyed with `rg`:

- **`// WHY: <rationale>`** — the reason for a non-obvious choice: protocol quirks (`RO`/`RO$`
  routing, SL not echoed), threading decisions (avoiding `BlockingQueuedConnection`, deferred setup to
  dodge a deadlock), load-bearing constants, and workarounds whose removal would silently break
  something. Survey: `rg "// WHY:"`.
- **`// TODO(gh#NNN): <summary>`** — tracked debt. File the issue first. Bare `TODO`, `FIXME`,
  `HACK` and `XXX` are not used. Survey: `rg "// TODO\(gh#"`.

Do not write comments that restate a name, refer to the current task or PR, describe deleted code, or
explain an obvious field.

---

## Styling

The rules below are the must-follow summary. `docs/styling.md` owns the detail and the reasons: Qt's
style-sheet cascade, inheritance and re-polish rules, the contrast policy, the type roles, side-panel
keys, the fixed 90×44 knob keys, and removed names. Values live only in
`src/ui/styling/k4constants.h`; helpers in `src/ui/styling/k4styles.h`.

- **Fonts:** `setPixelSize()` with a `K4Styles::Dimensions::FontSize*` role, never `setPointSize()`
  and never a literal. Custom-painted text uses `K4Styles::Fonts::paintFont()` / `dataFont()`.
  This deliberately departs from Qt's advice (*QFont::setPixelSize*: pixel sizes make a font "device
  dependent"); `docs/styling.md` → Type roles and pixel sizes gives the reason. The cost: QK4 ignores
  the OS text-size setting, and the interface scale (`src/settings/interfacescale.h`) is the only
  way to enlarge text.
- **Colours:** tokens only. Translucency is `withAlpha()` / `cssRgba()` with a `Colors::Alpha::*`.
  Never a hex string, `QColor(r, g, b)`, `Qt::white`/`Qt::black` or a style-sheet `rgb()`/`rgba()`
  literal. A new text/background pair goes into `test_tokenstandard`'s contrast list.
  `TokenStandardTests` enforces all of this over `src/` [CI].
- **Form controls:** never leave one to the OS theme. A **new** form-control type needs a rule in
  `K4Styles::applicationStyleSheet()` first; `FormControlThemeTests` fails until it has one [CI].
  Why: Windows 11 dark mode draws unstyled controls dark-on-dark.
- **A widget's own style sheet beats the app sheet** whatever the specificity (*The Style Sheet
  Syntax*, Cascading). The app sheet is only a fallback; to change a widget that has its own sheet,
  change that sheet.
- **[aspirational] State styling uses selectors, not sheet swaps.** New code uses one sheet with a
  pseudo-state (`:checked` on a checkable button) or a property selector, and re-applies the sheet
  after a property change. Existing swaps (`popupButtonNormal()`/`popupButtonSelected()`,
  `menuBarButton()`/`menuBarButtonActive()`) stay until their area is reworked. Count:
  `rg -c "setStyleSheet\(" src | awk -F: '{s+=$2} END {print s}'`.
- **Image buffers carry a device pixel ratio.** Never draw into a `QPixmap` at 1× and let Qt stretch
  it. Recipe: `PATTERNS.md` → High-DPI drawing.

**Adding a popup** follows a fixed recipe: `PATTERNS.md` → Adding a Popup Menu.

---

## Architecture Rules

**Tags** say what happens when a rule is broken:

- **[CI]** — a named check in `.github/workflows/ci-checks.yml` fails.
- **[review]** — no automation. Reviewers catch it or it ships.
- **[aspirational]** — not met everywhere today. Binds new code; the exceptions are listed.

| Rule | Tag |
|---|---|
| 1 No duplicated static functions | review |
| 2 Controllers do not expose owned objects | aspirational |
| 3 No non-const references to shared state | review |
| 4 RadioState is main-thread only | review (+ Debug assert) |
| 5 Network buffers have size limits | review |
| 6 Parser changes need tests | CI + review |
| 7 No file over 800 lines | aspirational |
| 8 Every extraction is traced first | review |
| 9 One commit per logical change | review |
| 10 Format + build + tests before every commit | CI |
| 11 Controlled shutdown order | review |
| 12 New UI concerns go in controllers | review |
| 13 Replacing code removes everything only it used | review |
| 14 Threads and object affinity | review |
| 15 No new singletons | aspirational |
| 16 Nothing on a real-time thread formats, allocates, locks or does I/O | CI (RealtimeSanitizer, on `QK4_NONBLOCKING` functions a test reaches) + review |
| 17 Logging goes through `src/logging/` | CI (`LogCategoriesTests`, `LogRulesTests`) + review |

### 1. [review] No Duplicated Static Functions

A function needed in more than one translation unit goes in `src/utils/` under a namespace
(`RadioUtils::`). Copying a static function into another `.cpp` is a defect. No lint checks this; a
scan on 2026-10-29 found no duplicates.

### 2. [aspirational] Controllers Do Not Expose Owned Objects

Controllers expose task-level APIs, not their internal workers. To configure a device, add a method
on the controller (`audioController->setInputDevice(…)`), not a getter
(`audioController->audioEngine()->setInputDevice(…)`).

Existing exceptions. Each is a deliberate trade-off; none is precedent for a new one:

| Accessor | Consumers | Why |
|---|---|---|
| `ConnectionController::tcpClient()` | AudioController (TX packets straight to `TcpClient::sendRaw`, RX decode via `tcpClient()->protocol()`), CwController (keyer-thread `KZD`/`KZU`, KPOD+ keyer bytes) | Audio and keying must not hop through the main thread |
| `ConnectionController::networkMetrics()` | AudioController (feeds samples), MainWindow (hands it to StatusBarController) | Shared metrics sink |
| `HardwareController::kpodDevice()`, `kpodPlusDevice()`, `halikeyDevice()` | OptionsDialog pages (KpodPage, CwKeyerPage), CwController | Pages configure and observe per-device signals; reason in `hardwarecontroller.h` |
| `HardwareController::paddleKeyer()`, `keyerLink()`, `sidetoneGenerator()` | CwController, wired once in MainWindow | HardwareController constructs the CW devices; CwController wires them |
| `KPA1500UiController::client()` | OptionsDialog KPA1500 page | The page sets host/port/enable directly |
| `SpectrumController::spectrumContainer()` | MainWindow layout, MenuController (menu overlay geometry) | Layout placement |
| `StatusBarController::widget()` | MainWindow layout | Layout placement |

Prefer a controller-level signal or setter when a consumer needs only one value, and remove that
consumer's use of the raw pointer.

### 3. [review] No Non-Const References to Shared State

Never return `Type&` from a getter that several callers use. Return `const Type&` for reads and use
typed setters for writes.

### 4. [review] RadioState Is Main-Thread Only

`parseCATCommand()` asserts `Q_ASSERT(QThread::currentThread() == thread())`. Every caller must be
on the main thread; parse requests from another thread arrive through a queued connection or
`QMetaObject::invokeMethod(…, Qt::QueuedConnection)`.

The assert fires only in a Debug build. CI's unit tests are Release, and no test calls the parser
off-thread, so CI cannot catch a violation. ASAN and UBSAN do not detect races; only the weekly
ThreadSanitizer job (`scheduled-sanitizers.yml`) could, and only on a path a test exercises.

### 5. [review] Network Buffers Have Size Limits

A buffer that accumulates external data checks a maximum size and handles overflow (disconnect,
discard or reset). Default limit: `K4Protocol::MAX_BUFFER_SIZE` (1 MB, `src/network/protocol.h`).

### 6. [CI + review] Parser Changes Require Tests

A change to `RadioState::parseCATCommand()` or a handler adds a case to `tests/test_radiostate.cpp`.
Covering the specific behaviour is the author's job [review].

CI catches structural breaks:

- `test_radiostate_registry` — every registered prefix resolves; longest prefix first.
- `test_catserver` — the `RadioState` API that CatServer uses, pinned to
  `docs/radiostate-catserver-api-contract.md`.

`test_radiostate_golden` replays a captured K4 session byte for byte, but its trace is not committed
(it holds personal data). It skips in CI and protects only machines that have the fixture.

### 7. [aspirational] No File Over 800 Lines

Split a `.cpp` or `.h` by responsibility before it passes 800 lines. Nothing enforces this, and files
have crossed the line since it was written. Do not grow a listed file further without a reason in the
PR.

`src/` files over 800 lines on 2026-10-29:

| File | Lines | Note |
|---|---:|---|
| `src/dsp/panadapter_rhi.cpp` | 1875 | RHI pipeline and buffer management |
| `src/mainwindow.cpp` | 1455 | Window chrome, layout, event-filter dispatch, lifecycle |
| `src/models/radiostate.cpp` | 1174 | Handler registry and façade delegation |
| `src/settings/radiosettings.cpp` | 1074 | Split candidate |
| `src/controllers/popupmanager.cpp` | 1026 | Registry of the shared popups |
| `src/controllers/tcicontroller.cpp` | 959 | Split candidate |
| `src/dsp/minipan_rhi.cpp` | 940 | Mirrors the panadapter |
| `src/models/radiostate.h` | 918 | Public API over the subsystems |
| `src/hardware/paddlekeyer.cpp` | 823 | Port of Dale's keyer; kept close to the original |
| `src/ui/styling/k4styles.cpp` | 809 | Style-sheet helpers |

Regenerate:

```bash
find src -name '*.cpp' -o -name '*.h' | xargs wc -l | awk '$1 > 800 && $2 != "total"' | sort -rn
```

Test files are not held to this limit.

### 8. [review] Every Extraction Is Traced First

Before moving code between classes, read every member, method, signal and `connect()` involved.
Write down what moves, what stays and what crosses the boundary. A missed dependency is a broken
extraction.

### 9. [review] One Commit per Logical Change

Never mix a structural move with a logic change. A refactor that adds a class and fixes a bug is two
commits, so `git bisect` and `git revert` stay clean.

### 10. [CI] Format, Build and Tests Before Every Commit

Run the sequence in "Before Every Commit". CI repeats it: `ci-checks.yml` runs the format check, the
unit tests (Release) and the ASAN + UBSAN tests (Debug); `ci-packaging.yml` builds the app on all
four platforms for every PR. Skipping it locally means a red CI run.

### 11. [review] Controlled Shutdown Order

Threads stop in dependency order: producers before consumers, and before anything they read is
destroyed (`MainWindow::closeEvent`, CONC-001).

A destructor cuts every incoming connection that can still fire while its body runs, and nothing
else. `sender->disconnect(receiver)` removes that sender's signals into that receiver. Called bare
inside a member, `disconnect(this)` names `this` as the *sender*, so it removes only connections
from this object. No call cuts every incoming connection at once ("The sender may never be nullptr",
*QObject::disconnect*). Qt removes a connection by itself only when either end is destroyed — in
`~QObject`, after the derived destructor body, and for a `QWidget` after its children are gone.

In the destructor:

- **A sender that the body's own work makes emit** (a teardown call that emits back into this object
  synchronously): `sender->disconnect(this)` before that work. Guard with `QPointer` if the sender may
  already be gone.
- **A cross-thread `Qt::DirectConnection`, or a lambda whose context object is not `this`:** keep the
  `QMetaObject::Connection` and `QObject::disconnect(handle)` first. A handle is safe to disconnect
  after the sender is destroyed; a raw sender pointer is not. A disconnect stops new calls only. It
  does not wait for a call already running on another thread, so stopping that thread first is the
  real guarantee.
- **A `QWidget` that receives from its own descendants** (MainWindow): cut each descendant from it
  before the body ends.
- **Otherwise nothing.** Queued signals are not delivered mid-destructor unless the body runs an
  event loop, and `~QObject` removes events still posted to the object. Do not add a destructor only
  to disconnect.

**Outside a destructor, a disconnect does not cancel queued calls.** "If a queued connection is
disconnected, already scheduled events may still be delivered, causing the receiver to be called
after the connection is disconnected" (*QObject::disconnect*). A slot that can run after its
connection is cut must check its own state.

The ASAN job catches a use-after-free only on a path a test runs, and no test builds MainWindow or
runs the shutdown graph. Review is the real gate.

### 12. [review] New UI Concerns Belong in Controllers, Not MainWindow

`src/mainwindow.cpp` is window chrome, top-level layout and controller coordination. A new widget,
signal wiring, CAT helper or mode-dependent behaviour goes in a controller under `src/controllers/`.
A widget that consumes one `RadioState` property observes it directly. The banned shapes and their
reasons are in `PATTERNS.md` → Anti-patterns. In short: no new widget members or forwarding slots on
MainWindow, no long inline lambdas, no reaching through one controller into another.

### 13. [review] Replacing Code Removes Everything Only It Used

A change that replaces or removes a feature deletes, in the same change, everything that existed
only for it: signals nothing connects to and the code that emits them, members only that code wrote,
helpers only it called, the tests of those helpers, and every comment, README and doc line that names
any of it.

Before committing, `rg` each removed symbol and file name across `src/`, `tests/`, `docs/` and the
module READMEs. A hit is either a live user (so it was not dead) or leftover to remove. For a signal,
also check that something still `connect()`s to it; an emit with no receiver runs silently.

Why: the 2026-10 keyer port deleted `IambicKeyer` but left the HaliKey line-state signal, its dedupe
helper and test, and a dozen comments and docs. Everything compiled and passed.

### 14. [review] Threads and Object Affinity

A `QObject` lives in the thread that created it, or the one it was moved to. Its events, timers and
queued slots run there (*Threads and QObjects*). This rule is the contract every thread follows.
List the threads with `rg -n "new QThread|QThread::create|moveToThread" src`.

- **Worker objects, not `QThread` subclasses with slots.** Create the worker without a parent,
  `moveToThread()` it, connect `&QThread::finished` to the worker's `deleteLater`, then `start()`.
  Stop it through `ThreadShutdown` (`src/utils/threadshutdown.h`), never a bare `quit()` +
  `wait()`: `callBlocking()` for the worker's last call (a `BlockingQueuedConnection` has no
  timeout), then `retire()`, or `join()` for a thread without an event loop. A thread that misses
  its deadline is disowned and reported, because deleting a running `QThread` crashes, and one made
  by `QThread::create()` instead blocks until it finishes (*~QThread*). Anything that thread's code
  uses that is not a `QObject` living on it is the owner's to leak too. A `QThread` object itself
  lives in the creating thread, so slots on a `QThread` subclass run there, not in the new thread.
- **A thread without an event loop gets no events.** `QThread::create(fn)` (the keyer, the HaliKey
  port scan) runs `fn` and exits; queued calls, timers and `deleteLater()` aimed at objects living
  there are never delivered (*Threads and QObjects*, Per-Thread Event Loop). Hand such a thread
  plain data or a thread-safe queue, not `QObject`s that expect events.
- **Touch an object only from its thread.** "QObject and all of its subclasses are not thread-safe"
  (*Threads and QObjects*). Call across threads with a queued connection or
  `QMetaObject::invokeMethod(obj, …, Qt::QueuedConnection)`. A direct call from another thread is
  allowed only where the class documents it as thread-safe and protects its data (for example a
  mutex- or atomic-guarded API); say so in a `// WHY:` comment at the call.
- **Timers and sockets belong to their thread.** "You cannot start a timer or connect a socket in a
  thread that is not the object's thread" (*Threads and QObjects*). Create them in the worker's
  thread (in a slot run after `started`), or as children of the worker before it moves.
- **Widgets are main-thread only.** `QWidget` and its subclasses are not reentrant.
- **Parent and child share a thread.** Never pass the `QThread` as parent of an object created in
  that thread.
- **Delete from the owning thread.** Never `delete` a `QObject` that lives in another thread; use
  `deleteLater()` (*~QObject*). Every object created in a thread is deleted before its `QThread`.
- **`BlockingQueuedConnection` only across threads.** Qt: "This connection must not be used if the
  receiver lives in the signalling thread, or else the application will deadlock" (*Qt namespace*,
  ConnectionType). It also deadlocks if the receiver's thread is itself blocked waiting on the
  caller. Before adding one, show the receiver's thread cannot be waiting on the caller, and record
  why in a `// WHY:` comment.
- **`Qt::DirectConnection` across threads** runs the slot in the emitter's thread. Use it only for a
  receiver documented as thread-safe, and keep the connection handle (Rule 11).
- **Every lambda connection has a context object.** The context-less overload "is error prone … the
  connection does not get automatically disconnected" and is always a direct connection
  (*QObject*, `QT_NO_CONTEXTLESS_CONNECT`). A scan on 2026-10-29 found none in `src/`.
- **An event filter lives in the same thread as the object it watches** (*Threads and QObjects*).
  `K4PopupBase` installs an app-wide filter on `qApp`, so popups are main-thread only.

The threading work in progress will extend this rule with the thread inventory and priorities; keep
this section to principles.

### 15. [aspirational] No New Singletons

Shared state reaches a controller through its constructor. Do not add a static service locator or
`instance()` accessor.

The one existing exception is `RadioSettings::instance()`, the QSettings-backed settings store, used
from many files (`rg -c "RadioSettings::instance\(\)" src`). It is a function-local static `QObject`,
so it is created on first use and destroyed after `main()` returns, after `QApplication`. Qt does not
support that: "creating QObjects before the QApplication is not supported … static instances of
QObject are also not supported" (*Threads and QObjects*). Treat it as tracked debt; do not copy the
shape.

### 16. [CI + review] Real-Time Threads Do Not Log, Allocate, Lock or Block

The keyer, the HaliKey serial poller, the RtMidi callback, the KPOD+ EP02 reader, the sidetone and
the audio thread run against deadlines. Code on them must never format a string, allocate, take a
lock that another thread can hold for long, or do I/O. That includes logging: no per-element,
per-edge, per-packet or per-frame `qCDebug`/`qCInfo` on these threads, even behind a category.

- Every category is declared with a level, normally `Q_LOGGING_CATEGORY(name, "…", QtInfoMsg)`
  (`QtWarningMsg` for diagnostic-only categories such as `qk4.pan.diag`), so debug is off by default. With no level argument, "all message types are
  enabled" (*QLoggingCategory*, Q_LOGGING_CATEGORY), including debug, in Release builds too:
  "Logging categories are not affected by your C++ build configuration".
- Turn debug on per category at run time with `QT_LOGGING_RULES`, e.g.
  `QT_LOGGING_RULES="qk4.cw.keyer.debug=true"`, or tick the subsystem in Options → Diagnostics.
- A disabled `qCDebug` skips evaluating its arguments, so it is cheap on ordinary threads. On a
  real-time thread it still is not free when someone enables it, which is why the rule bans it there
  rather than relying on the default.
- To observe a real-time thread, record into a preallocated buffer or lock-free queue and format it
  on another thread.

- A promoted thread gets its class from its entry in `RtScheduling::Threads`
  (`src/utils/rtscheduling.h`), the one table of who is promoted: the CW threads (keyer, sidetone,
  HaliKey poller, KPOD+ EP02) are `RealTime`; the I/O thread and the RX and TX audio threads are
  `High`, never real-time. It promotes itself first thing, on itself:
  `RtScheduling::promoteCurrentThread(RtScheduling::Threads::kName)` at the top of its run function,
  or `RtScheduling::promoteOnStart(thread, RtScheduling::Threads::kName)` before `start()` for an
  event-loop thread. Never `QThread::HighPriority` or any `QThread::Priority`. A real-time thread
  must block between wake-ups, never spin: under `SCHED_FIFO` or the Mach time-constraint policy a
  spinning thread starves its core. `test_rtscheduling` checks the table and where each entry is
  applied [CI].
- A thread that must never block on logging is marked with `Logging::markRealtimeOnStart(thread)`
  (connected before any `started()` connection that enters a loop); the handler then only
  try-locks its queue and drops and counts instead of waiting.

**Enforcement.** Mark the work a real-time thread does between wake-ups `noexcept QK4_NONBLOCKING`
(`src/utils/rtsan.h`): per element, edge, packet or block, not the loop that waits, not
configuration. The RealtimeSanitizer job in `ci-checks.yml` then fails any test in which such a
function, or anything it calls, allocates, takes a lock, sleeps or does I/O. It sees only what a
test reaches; the rest is review.

- The short locks this rule allows are `RtSan::BoundedMutex`, not `std::mutex`. Its contract is the
  rule's: no holder a real-time thread can wait on holds it across a sleep, a wait, a send or an
  allocation, or calls out into code that does. RTSan cannot check that; review does.
- `RtSan::Allow` exempts one statement that is bounded by construction (a non-blocking wake-up
  write) or is the thread's own wait between wake-ups (the keyer's deadline sleep). Every use has a
  `// WHY allowed:`. Test doubles that record what they are sent use it too; the real
  implementation they stand in for is checked on its own.
- No `std::deque` or `std::function` in a marked path: both can allocate in steady use. Use
  `RingDeque` and `FunctionRef` (`src/utils/`), or a preallocated ring.

### 17. [CI] Logging Goes Through `src/logging/`

- Every category is declared in `src/logging/logcategories.{h,cpp}` at `QtInfoMsg`, named
  `qk4.<area>[.<detail>]` with an `lc<Area><Detail>` symbol, and belongs to a group in
  `logrules.cpp`. Every log call names a category (`qCWarning(lcX)`), never bare `qWarning()`.
  Enforced by `LogCategoriesTests` and `LogRulesTests`.
- No info-or-above line on a per-sample, per-frame, per-element or per-CAT-response path; those
  stay `qCDebug` in a category that is off by default, or become a bench item. [review]
- Never log a password, PSK or PSK identity; log `.isEmpty()` or `.size()`. A new credential field
  is passed to `Logging::registerSecret()` when loaded. Enforced by the secret scan in
  `LogCategoriesTests`; the writer also redacts registered secrets.
- Only `main()` installs a message handler, through `Logging::install()`. Production code never
  calls `QLoggingCategory::installFilter()`, which would make Qt ignore every rule; rules change
  only through `Logging::apply()`. [review]

Where logs go and how testers turn them on: `README.md` → "Reporting a Bug: Log Files", and `src/logging/README.md`.
