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

4. **Write descriptive commits** - GitHub Release notes are auto-generated from conventional commits by `git-cliff` at tag time (see `.github/workflows/release.yml`). Use commit body text for details that should appear in the release notes (see Commit Messages below).

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
| Constants (header / class) | SCREAMING_SNAKE | `WATERFALL_HISTORY`, `BASE_TEXTURE_WIDTH` |
| Constants (file-local in `.cpp`) | `kCamelCase` | `kArpMaxRetries`, `kOverlayMaxSegments` |
| Namespaces | PascalCase | `K4Styles`, `K4Styles::Colors` |

The two constant styles are split by **location**, not by taste: a constant visible through a header
is SCREAMING_SNAKE, one confined to a single `.cpp` is `kCamelCase`. The codebase follows this about
95% of the time; it was documented in 2026-08 after an audit found the rule in the code but not in
this file.

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

Commits drive the auto-generated GitHub Release notes (via `git-cliff --latest` in the release workflow). Use [conventional commit](https://www.conventionalcommits.org/) format:

```
type(scope): short summary

Optional body with detail — each line becomes a sub-bullet in the release notes.
```

| Type | Release Notes Section | Example |
|------|-----------------------|---------|
| `feat` | Added | `feat(audio): add jitter buffer for RX playback` |
| `fix` | Fixed | `fix(panadapter): correct CW pitch offset in passband` |
| `refactor` | Changed | `refactor(state): replace if-else chain with handler registry` |
| `perf` | Changed | `perf(dsp): reduce CPU usage with GPU spectrum rendering` |
| `docs` | *(skipped)* | `docs: update README build instructions` |
| `chore` | *(skipped)* | `chore: bump Qt to 6.8` |
| `ci` | *(skipped)* | `ci: add Raspberry Pi build job` |

**Tips:**
- The `(scope)` is optional but makes entries scannable (e.g., `audio`, `panadapter`, `state`, `ui`)
- Put detail in the commit body — it flows into the release notes as sub-bullets
- Keep the summary line under 72 characters

---

## Styling

**Source of truth:** `src/ui/k4styles.h`. Every color, font, dimension, and stylesheet helper is declared there.

Reference tables (colors, fonts, dimensions, helpers, `Dialog::` namespace): `docs/K4STYLES_REFERENCE.md`.

Rules for new widget code:
- Use `setPixelSize()` with `K4Styles::Dimensions::FontSize*` constants — never `setPointSize()` (macOS 72 PPI vs Windows 96 PPI divergence).
- Use `K4Styles::Colors::*` constants — never hardcoded hex strings.
- For custom-painted widgets, use `K4Styles::Fonts::paintFont()` / `dataFont()` instead of constructing `QFont` directly.
- For inline stylesheet construction, prefer `K4Styles::Dialog::labelText(color, size)` over `QString("color: %1; font-size: %2px;").arg(...)`.

**Adding a popup menu** follows a fixed recipe — see `PATTERNS.md` → "Adding a Popup Menu".

---

## Architecture Rules

These rules prevent the architectural issues identified in the 2026-03-30 audit. They are non-negotiable for all new code and refactoring work.

**How rules are enforced:** not all twelve rules have the same bite. The tag at the front of each rule tells a new contributor what happens when they're broken:

- **[CI]** — a test or lint check in `.github/workflows/ci.yml` fails. The rule *breaks the build*.
- **[sanitizer]** — ASAN or UBSAN in the sanitizer CI job catches the violation when the offending code path runs.
- **[review]** — no automation. Enforced by reviewer discipline. Violations ship if reviewers miss them.
- **[aspirational]** — target not fully met today; documented exemptions exist. Binds new code only.

The CI-enforced rules (4, 6, 10) are the load-bearing ones. Sanitizer rules (11) catch the nastiest violations at runtime. Review-only rules (2, 3, 5, 8, 9, 12) depend on human attention. Aspirational rules (1, 7) mark architectural intent without strict enforcement.

### 1. [aspirational] No Duplicated Static Functions

If a function is needed in more than one translation unit, it goes in `src/utils/` with a namespace (e.g., `RadioUtils::`). Copy-pasting a static function into another `.cpp` file is a defect. Fix it immediately.

No lint check enforces this; honored today by convention.

### 2. [review] Controllers Do Not Expose Owned Objects

Controllers expose **task-level APIs**, not internal workers. No `audioEngine()`, `kpodDevice()`, or `tcpClient()` accessors in the public interface. If external code needs to configure a device, add a method to the controller (e.g., `audioController->setInputDevice(...)` instead of `audioController->audioEngine()->setInputDevice(...)`).

**Exception:** `tcpClient()` is exposed for AudioController's performance-sensitive audio data path and CatServer's direct TCP forwarding. These are documented exceptions, not precedent.

### 3. [review] No Non-Const References to Shared State

Never return `Type&` from a getter when multiple callers may read or write. Use `const Type&` for read access and typed setters for mutations. This prevents data races and makes state changes auditable.

### 4. [CI] RadioState is Main-Thread Only

`parseCATCommand()` is enforced by `Q_ASSERT(QThread::currentThread() == thread())`. All callers must be on the main (GUI) thread. If cross-thread parsing is ever needed, use `QMetaObject::invokeMethod` with `Qt::QueuedConnection`.

Debug builds assert on violation; sanitizer CI catches any cross-thread access that slips past the assert.

### 5. [review] Network Buffers Have Explicit Size Limits

Any buffer that accumulates data from an external source must check against a maximum size and handle overflow (disconnect, discard, or reset). Use `K4Protocol::MAX_BUFFER_SIZE` (1MB) as the default limit.

### 6. [CI + review] Parser Changes Require Test Cases

Any modification to `RadioState::parseCATCommand()` or its handlers must include a corresponding test case in `tests/test_radiostate.cpp`. No merge without test coverage for the changed behavior.

Three CI-enforced invariants catch structural breaks:
- `test_radiostate_registry` — every registered prefix resolves; longest-first ordering preserved.
- `test_radiostate_golden` — byte-for-byte replay of captured K4 CAT session.
- `test_catserver` — `RadioState` public API pinned to `docs/radiostate-catserver-api-contract.md`.

Behavioral coverage of the specific change is still author discipline.

### 7. [aspirational] No File Over 800 Lines

If a `.cpp` or `.h` file grows past 800 lines, split it by responsibility before merging. Check with `wc -l` before committing.

**Status: aspirational with documented exemptions.** Binding for *new* code: PRs that introduce new files over 800 LOC or push a borderline file past the limit are blocked. PRs that leave an existing violator untouched are fine.

Current exempt files (do not green-light new additions that bloat these further):

| File | LOC | Notes |
|------|----:|-------|
| `src/dsp/panadapter_rhi.cpp` | 1884 | Naturally large — RHI pipeline + buffer management. Low priority. |
| `src/mainwindow.cpp` | 1364 | Down from 4967 pre-refactor. Remaining scope is genuine coordination (setupUi, event-filter dispatch, lifecycle). Further decomposition is judgment territory. |
| `src/models/radiostate.cpp` | 1152 | Down from 2893 pre-refactor. Handler registry + façade delegation. |
| `src/dsp/minipan_rhi.cpp` | 1090 | Mirrors panadapter structure. |
| `src/controllers/popupmanager.cpp` | 1063 | Coherent registry of 14 popup objects — size is justified, not a split candidate. |
| `src/controllers/spectrumcontroller.cpp` | 966 | Past 800-LOC threshold — split candidate (DX cluster spot overlay wiring is the natural extraction target). |
| `src/models/radiostate.h` | 896 | Down from 1156 pre-refactor; 11 subsystem struct references + public API. |
| `src/ui/popups/displaypopupwidget.cpp` | 865 | Close to threshold — hold the line. |

### 8. [review] Every Extraction is Traced First

Before moving code between classes: read every member variable, method, signal, and `connect()` call involved. Document what moves, what stays, and what the cross-domain dependencies are. Missing a dependency means a broken extraction.

### 9. [review] One Commit Per Logical Change

Never combine structural moves with logic changes in the same commit. If a refactor introduces a new class AND fixes a bug, those are two commits. This enables `git bisect` and clean `git revert`.

### 10. [CI] Build + Format + Tests Before Every Commit

```bash
clang-format -i <changed files>
find src tests -name '*.cpp' -o -name '*.h' | xargs clang-format --dry-run --Werror
cmake --build build
ctest --test-dir build --output-on-failure
```

All four must pass. No exceptions. `.github/workflows/ci.yml` mirrors the format gate and runs the test suites — skipping locally means the CI run will fail.

### 11. [sanitizer + review] Controlled Shutdown Order

Every controller and MainWindow calls `disconnect(this)` as the first statement in its destructor. This prevents queued signals from arriving during partial destruction. Thread shutdown follows the dependency chain: producers stop before consumers.

ASAN in the sanitizer CI job catches use-after-free on violation if the destruction path runs under test. Review catches the rest.

### 12. [review] New UI Concerns Belong in Controllers, Not MainWindow

After the 2026-04 MainWindow decomposition, `src/mainwindow.cpp`'s scope is **window chrome, top-level layout, and controller coordination — nothing else**. New features that need a widget, a signal wiring, a CAT dispatch helper, or mode-dependent UI behavior go in a controller under `src/controllers/` (see `PATTERNS.md` → Controller Pattern). If the widget only consumes a single RadioState property, use Direct Observation instead of adding a controller.

Regressing to "throw it on MainWindow" is the single biggest risk for re-drifting into a god object. The banned anti-patterns in `PATTERNS.md` are non-negotiable:

- No new widget member pointers on `MainWindow`.
- No slots on MainWindow that just forward a RadioState signal to a widget setter.
- No inline lambdas over ~5 lines or ~5 connect() calls clustered at one site — extract to a helper, or if it grows past ~30 lines, promote to a controller.
- No cross-controller reach-in (`controllerA->someGetter()->doThing()`) — communicate via signals.

No automated check; relies on PR review.

---

## Comment Conventions

Default: **write no comments.** Well-named identifiers already describe *what* the code does. Only add a comment when the *why* is non-obvious: a hidden constraint, a subtle invariant, a K4 protocol quirk, a threading/ordering requirement, a workaround for a specific transient. If removing the comment would not confuse a future reader, do not write it.

Two prefixes are reserved so grep-based surveys can find them:

- **`// WHY: <rationale>`** — design rationale for a non-obvious choice. Use this for protocol quirks (`RO`/`RO$` routing, SL no-echo), threading decisions (`BlockingQueuedConnection` avoidance, deferred-setup to dodge deadlocks), constants whose value is load-bearing (3-byte parse-tail, `PREBUFFER_PACKETS = 1`), and any workaround whose removal would silently break behavior. Prefer this over a bare paragraph so future audits can grep `rg "// WHY:"` to survey every non-obvious decision in the codebase.

- **`// TODO(gh#NNN): <summary>`** — tracked technical debt. The `gh#NNN` points at a GitHub issue. **A `// TODO` without an issue number is not allowed** — file the issue first, then reference it. This prevents TODOs from turning into ambient guilt. Bare `// FIXME`, `// HACK`, `// XXX` are not used; use `// TODO(gh#NNN)` with an issue that states the concern, or delete the comment.

Do NOT write comments that:
- Restate the function/type name in prose.
- Reference the current task, PR, or fix (that belongs in the commit message).
- Describe code that was deleted (git has it).
- Explain a field whose purpose is obvious from a read-through (e.g., `// the name`).
