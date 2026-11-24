# tests/

Unit tests. Qt Test framework, one standalone executable per suite, registered with `add_test()`
and run by `ctest`.

**This file does not list the suites.** For the current list, build and ask ctest — it is generated
from `CMakeLists.txt`, so it can never be stale:

```bash
ctest --test-dir build -N
```

A per-suite coverage table is maintained outside the repo, in the gitignored `CLAUDE.md`, so do not
add a second inventory here or in a header: it would drift against both. What follows is the files
in this directory that are *not* a suite, and the conventions that apply across all of them.

## Not a test suite

| Path | What it is |
|---|---|
| `CMakeLists.txt` | Every suite's target, its sources and its test properties. Also the four shared libraries below, and the `qk4_tests` aggregate target, which collects every `test_*` target in this directory from `BUILDSYSTEM_TARGETS` so no workflow has to keep a list |
| `paddlekeyersim.h` | Harness for `test_paddlekeyersweeps.cpp`: a port of Dale Farnsworth's `paddle_sweep.py`, driving the keyer against his K4-Companion recordings |
| `tcitestclient.h` | A TCI client over a real loopback socket, so the TCI suites exercise the real WebSocket framing. Its reads drive the shared event loop rather than `waitForReadyRead`, because the server under test lives in the same thread and would deadlock |
| `tsan.supp` | ThreadSanitizer suppressions for `scheduled-sanitizers.yml`. Dependency-internal findings only, each with its reasoning; nothing from `src/` belongs in it. See `.github/workflows/README.md` → ThreadSanitizer |
| `bench_radiostate.cpp` | RadioState dispatch microbenchmark. Not a test and not in `ctest`; built only with `-DQK4_BUILD_BENCHMARKS=ON` |
| `data/paddlekeyer/` | Dale's recorded sweeps, with their own `README.md` describing the fixture format and which verdicts it allows |
| `data/offscreen/` | A fixed screen for the offscreen platform (`screen-4k.json`, passed as `QT_QPA_PLATFORM=offscreen:configfile=…`), so placement tests assert against one 4K geometry at several `QT_SCALE_FACTOR`s instead of the runner's screen |
| `golden/` | **Gitignored.** The recorded K4 CAT trace behind `RadioStateGoldenTests`, which holds personal data. The suite `QSKIP`s without it, so it is a local gate only — never a CI one |

## Shared sources

An app source several suites need is compiled once into a static library instead of once per suite
(324 objects for the whole test build instead of 476). The four are defined near the top of
`CMakeLists.txt`:

| Library | Carries |
|---|---|
| `qk4_test_radiostate` | `radiostate.cpp` and all 13 subsystem handlers |
| `qk4_test_tci` | The TCI server, its grammar and audio frames, and the WebSocket transport under it |
| `qk4_test_common` | Core/Network sources used by three or more suites. No Gui, so non-UI suites stay Gui-free |
| `qk4_test_widgets` | Styling and the side-panel widgets, plus the two Gui-dependent utilities (QColor, QWheelEvent). Links `qk4_test_common` itself, so suites need only this one |

**Link the library; do not also list its sources.** Doing both gives duplicate symbols. A source a
third suite starts needing moves into the matching library rather than being listed a third time.

## Conventions

- Most suites link only `Qt6::Core` and `Qt6::Test`, plus `Qt6::Network` for socket tests. Widget
  suites add `Qt6::Gui`/`Qt6::Widgets` and run under `QT_QPA_PLATFORM=offscreen`, set per test with
  `set_tests_properties` in `CMakeLists.txt` — not globally, so a suite that must run natively can.
- Tests that touch settings redirect `QSettings` into a `QTemporaryDir` and then **check that the
  redirect took**, failing rather than running if the resolved path is not inside it, and asserting
  the operator's real settings file was neither created nor deleted. `QStandardPaths` test mode is
  not enough on its own: `QSettings(org, app)` ignores both it and `setDefaultFormat()`.
- Socket tests bind loopback on an OS-assigned port; nothing reaches a real radio.
- Naming follows what the test asserts, not the function it calls — `parseX()`/`rejectX()` in the
  DX cluster suites, not `testX()`.
- Any change to a `parseCATCommand()` handler needs a case in `test_radiostate.cpp`
  (CONVENTIONS Rule 6).

## Adding a suite

1. `tests/test_<name>.cpp` with `QTest` boilerplate.
2. In `CMakeLists.txt`: `add_executable`, `target_include_directories(... ${CMAKE_SOURCE_DIR}/src)`,
   `target_link_libraries` (a shared library above, **not** its sources again), `add_test`.
3. `cmake --build build --target test_<name>` and `ctest --test-dir build -R <Name>`.

No CI change is needed — `qk4_tests` picks the new target up on its own.
