# src/

QK4 source tree. Qt 6 C++ desktop app for remote control of an Elecraft K4 ham radio — TCP/TLS transport, real-time Opus audio, GPU spectrum display, USB/serial/MIDI hardware.

## Layout

| Directory | Owns |
|---|---|
| `audio/` | RX/TX audio pipeline. AudioEngine (QAudio I/O), Opus encode/decode, CW sidetone. |
| `controllers/` | 30 controllers. Each owns a cohesive UI slice + signal wiring + CAT dispatch. See `docs/controllers.md` for the symptom → controller map. |
| `dsp/` | GPU-accelerated spectrum + waterfall (Qt RHI). Shader pipelines. |
| `hardware/` | USB / serial / MIDI device wrappers (KPOD, KPOD+, HaliKey variants, iambic keyer). |
| `models/` | `RadioState` façade + 13 plain-struct subsystems. Single QObject in the model layer. |
| `network/` | TCP/TLS/PSK transport, K4 binary protocol, CAT server, TCI 2.0 server over WebSocket, DX cluster, KPA1500 client, K4 mDNS discovery. |
| `settings/` | `QSettings`-backed persistence for radio profiles, DX clusters, audio preferences. |
| `ui/` | 58 widgets / popups / pages / overlays / dialogs, styling infrastructure. |
| `utils/` | Shared helpers (frequency/band/span math). Architecture Rule 1 lives here. |

Top-level:
- `main.cpp` — Qt app bootstrap (fonts, HiDPI, OpenSSL backend, message filter).
- `mainwindow.cpp` / `mainwindow.h` — top-level window, layout construction, controller coordination.

## Architecture rules

Non-negotiable rules live in `CONVENTIONS.md` → Architecture Rules. The ones that matter when browsing `src/`:

- **Rule 4**: `RadioState` is main-thread only (CI-gated via `Q_ASSERT`).
- **Rule 12**: new UI concerns land in a `controllers/` file, not `mainwindow.cpp`.
- **Rule 7**: 800-LOC file cap (aspirational; 7 known exempt files).

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
