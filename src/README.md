# src/

QK4 source tree. Qt 6 C++ desktop app for remote control of an Elecraft K4 ham radio — TCP/TLS transport, real-time Opus audio, GPU spectrum display, USB/serial/MIDI hardware.

## Layout

| Directory | Owns |
|---|---|
| `audio/` | RX/TX audio pipeline. AudioEngine (Qt Multimedia I/O), Opus encode/decode, mic resampling, RX output stage, CW sidetone. |
| `controllers/` | One controller per I/O owner or UI slice: signal wiring + CAT dispatch. `ls src/controllers/*.h \| wc -l` gives the count. See `docs/controllers.md` for the symptom → controller map. |
| `dsp/` | GPU-accelerated spectrum + waterfall (Qt RHI). Shader pipelines. |
| `hardware/` | USB / serial / MIDI device wrappers (KPOD, KPOD+, HaliKey variants, iambic keyer). |
| `logging/` | Logging: every category (`qk4.*`), the asynchronous log file, rules from the Diagnostics page, per-platform log folder. Qt Core only. |
| `models/` | `RadioState` façade + plain-struct subsystems (`ls src/models/radiostate/*.h`), `MenuModel`, `TransmitOwner`. |
| `network/` | TCP/TLS/PSK transport, K4 binary protocol, CAT server, TCI 2.0 server over WebSocket, DX cluster, KPA1500 client, K4 discovery by UDP broadcast. |
| `settings/` | `RadioSettings`: `QSettings`-backed persistence for radio profiles, DX clusters, audio, CAT/TCI, hardware and interface-scale preferences. |
| `ui/` | Widgets, popups, Options pages, overlays, dialogs and the styling tokens/helpers. |
| `utils/` | Shared helpers: frequency/band/span math, window fit, wheel accumulation, macro ids. Architecture Rule 1 lives here. |

Top-level:
- `main.cpp` — Qt app bootstrap: app identity (KF5O), High-DPI rounding policy and interface scale, OpenSSL TLS backend, the log handler and `--log-*` options (`logging/`), fonts.
- `mainwindow.cpp` / `mainwindow.h` — top-level window, layout construction, controller coordination.

## Architecture rules

Non-negotiable rules live in `CONVENTIONS.md` → Architecture Rules. The ones that matter when browsing `src/`:

- **Rule 4**: `RadioState` is main-thread only. A `Q_ASSERT` enforces it, so it fires only in Debug builds — in CI that is the sanitizer job, not the Release test job.
- **Rule 12**: new UI concerns land in a `controllers/` file, not `mainwindow.cpp`.
- **Rule 7**: 800-LOC file cap (aspirational; exempt files listed in `CONVENTIONS.md`). Files over the cap: `find src \( -name '*.cpp' -o -name '*.h' \) -exec wc -l {} + | awk '$1>800 && $2!="total"'`.

## Patterns

- Adding a feature: see `PATTERNS.md` → "Adding New Features".
- Adding a controller: see `PATTERNS.md` → "Controller Pattern".
- Adding a popup: see `PATTERNS.md` → "Adding a Popup Menu".
- Adding RadioState state: see `PATTERNS.md` → "Subsystem State".

## See also

- `docs/controllers.md` — symptom → file map (highest-leverage onboarding doc).
- `docs/radiostate-catserver-api-contract.md` — frozen public API for CatServer.
- `docs/k4-protocol-quirks.md` — K4 CAT oddities (`$` suffix, RO/RO$ routing, SL no-echo).
- `docs/tci-server-design.md` — TCI 2.0 server design; `docs/tci-command-coverage.md` for what is implemented.
