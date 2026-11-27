# src/logging/

QK4's diagnostic logging. Links against `Qt6::Core` only, so every piece is testable without a GUI.

## Files

- `logcategories.{h,cpp}` — the one declaration and definition of every logging category. Every
  category is defined at `QtInfoMsg`: info and warnings print, debug is off until a rule turns it
  on. `Logging::allCategories()` lists them for tests and the Options page.
- `logpaths.{h,cpp}` — where the log file goes, as a pure function of platform, environment and home
  (`LogPathsTests` covers every platform on every runner):
  macOS `~/Library/Logs/QK4`, Windows `%LOCALAPPDATA%\QK4\Logs`, Linux `$XDG_STATE_HOME/QK4/logs`
  (default `~/.local/state/QK4/logs`), Flatpak `~/.var/app/io.github.mikeg_dal.QK4/.local/state/QK4/logs`.
  Reads the environment itself because `QStandardPaths::StateLocation` needs Qt 6.7 and is
  `~/Library/Preferences` on macOS.
- `logqueue.h` — the bounded queue between the message handler and the writer: two rings allocated
  up front, a push is one slot assignment under a mutex, a full ring drops and counts.
- `logrules.{h,cpp}` — the Diagnostics switches as data (`groups()`, `benchItems()`) and the pure
  `buildRules()` that turns a level, groups, bench items and `--log-rules` into
  `QLoggingCategory::setFilterRules()` text.
- `logsession.{h,cpp}` — applies the operator's choices at runtime: rules (live, no restart), log
  file on or off, console copy. `main.cpp` calls `Logging::apply()` at startup and on
  `RadioSettings::loggingChanged`.
- `logsink.{h,cpp}` — the message handler and the writer thread. `Logging::install()` is the first
  line of `main()`; `Logging::shutdown()` writes the footer and restores Qt's handler.

## How a message travels

1. The handler (`LogSink::post`) runs on the thread that logged. It copies the message into the
   queue (a reference-count increment, no allocation) and returns. It never formats, never touches a
   file and never waits on the console.
2. Threads marked real-time (`Logging::markRealtimeOnStart`: Keyer, Sidetone, AudioEngine, HaliKey,
   KpodPlusEP02, I/O; the RtMidi callback marks itself) only `try_lock` the queue: under contention
   or when it is full the message is dropped and counted. They never wake the writer.
3. The writer thread wakes every 200 ms, or at once for a warning from an ordinary thread. It
   formats each line, writes the log file if one is open, and passes the message on to Qt's own
   handler (the console) when the console copy is on. The file gets
   `[logging] N messages dropped` whenever something was dropped.
4. A fatal message is written synchronously before Qt aborts.

What remains on the logging thread when a category is enabled is `QDebug`'s own formatting, which
happens before the handler is called. That is why per-element, per-frame and per-response lines
stay at debug in categories that are off by default.

## The log file

Off unless asked for (`--log-file`, or the Diagnostics page). Line format:

```
2026-10-29T14:03:12.345-05:00 W [Keyer] qk4.cw.keyer: message (paddlekeyer.cpp:412)
```

The source file and line are printed for warnings and worse (`QT_MESSAGELOGCONTEXT` is defined on
the app target, so release builds have them). Each launch rotates `qk4.log` to `qk4.1.log`, keeping
five files, and a file that passes 10 MB rotates the same way mid-session. So the current session
is always `qk4.log` and a session that crashed is `qk4.1.log`, whose missing footer the next header
reports. A second QK4 running at the same time writes `qk4-<pid>.log` instead.

The console copy stays on when there is no file (as before), and with a file only when stderr is a
terminal or `--log-stderr` is given.

## Category names

One scheme: `qk4.<area>[.<detail>]`, lower case, dots only. C++ symbols are `lc<Area><Detail>`.
Add a category here, never with a local `Q_LOGGING_CATEGORY` (`LogCategoriesTests` fails the build).
Every call names its category: `qCWarning(lcApp)`, never `qWarning()`, which logs to Qt's `default`
category that no rule or Options switch can address. The same test enforces it.

| Category | Symbol | Covers | Before 2026-10-29 |
|---|---|---|---|
| `qk4.app` | `lcApp` | startup, fonts, TLS backend, command line, interface scale | uncategorised `qWarning`/`qInfo` |
| `qk4.ui` | `lcUi` | MainWindow | `qk4.main` |
| `qk4.ui.menu` | `lcUiMenu` | K4 menu overlay | `qk4.menu` |
| `qk4.ui.bandnav` | `lcUiBandNav` | band navigation | `qk4.bandnav` |
| `qk4.ui.modepopup` | `lcUiModePopup` | mode popup | `qk4.modepopup` |
| `qk4.ui.textdecode` | `lcUiTextDecode` | text decode window | `qk4.textdecode` |
| `qk4.ui.macro` | `lcUiMacro` | macros and the macro dialog | `qk4.macro`, `ui.macro` |
| `qk4.link` | `lcLink` | connection lifecycle (ConnectionController) | `qk4.connection` |
| `qk4.link.tcp` | `lcLinkTcp` | TCP, TLS-PSK, authentication, ping | `net.tcp` |
| `qk4.link.discovery` | `lcLinkDiscovery` | K4 discovery by UDP broadcast | `K4Discovery` |
| `qk4.tx.owner` | `lcTxOwner` | transmit arbiter | `tx.owner` |
| `qk4.cat.tx` | `lcCatTx` | every CAT command sent to the K4 | `CAT.TX` |
| `qk4.cat.rx` | `lcCatRx` | every CAT response from the K4 | `CAT.RX` |
| `qk4.audio` | `lcAudio` | audio devices, codecs, pipeline | `qk4.audio` |
| `qk4.audio.rx` | `lcAudioRx` | RX playback levels, once a second | `qk4.audio.rx` |
| `qk4.audio.tx` | `lcAudioTx` | what each TX frame put on the wire | `qk4.audio.tx` |
| `qk4.audio.sidetone` | `lcAudioSidetone` | sidetone output dry-outs and short writes (no per-tone line: real-time thread) | `qk4.audio.sidetone` |
| `qk4.cw.keyer` | `lcCwKeyer` | CW orchestration (CwController) | `cw.keyer` |
| `qk4.cw.halikey` | `lcCwHalikey` | HaliKey device and V1.4 serial worker | `hw.halikey` |
| `qk4.cw.midi` | `lcCwMidi` | HaliKey MIDI worker | `hw.midi` |
| `qk4.cw.vail` | `lcCwVail` | Vail adapter MIDI worker | `hw.vail` |
| `qk4.hardware` | `lcHardware` | HardwareController | `qk4.hardware` |
| `qk4.kpod` | `lcKpod` | KPOD (hidapi) | `hw.kpod` |
| `qk4.kpod.udev` | `lcKpodUdev` | KPOD hotplug on Linux | `hw.kpod.udev` |
| `qk4.kpod.plus` | `lcKpodPlus` | KPOD+ (libusb); its EP02 batches appear under `qk4.cat.tx` | `hw.kpodplus` |
| `qk4.catserver` | `lcCatServer` | CAT server for WSJT-X and friends | `net.cat` |
| `qk4.catserver.push` | `lcCatServerPush` | CAT server auto-information push | `net.catpush` |
| `qk4.tci` | `lcTci` | TCI server | `net.tci` |
| `qk4.tci.ws` | `lcTciWs` | TCI WebSocket transport | `net.ws` |
| `qk4.dxcluster` | `lcDxCluster` | DX cluster clients | `net.dxcluster` |
| `qk4.kpa1500` | `lcKpa1500` | KPA1500 client | `net.kpa` |
| `qk4.kpa1500.ui` | `lcKpa1500Ui` | KPA1500 UI controller | `qk4.kpa1500` |
| `qk4.spectrum` | `lcSpectrum` | SpectrumController | `qk4.spectrum` |
| `qk4.spectrum.render` | `lcSpectrumRender` | panadapter renderer | `qk4.dsp` |
| `qk4.spectrum.minipan` | `lcSpectrumMiniPan` | mini-pan renderer | `dsp.minipan` |
| `qk4.spectrum.diag` | `lcSpectrumDiag` | waterfall tier and geometry diagnostics | `qk4.pan.diag` |

## What a log contains, and what it never does

A log file holds what the app did: connection attempts with the K4's host and port, audio device
names, CAT commands when that bench item is on, the startup macro text and DX cluster callsigns. It
never holds a radio password, PSK or PSK identity:

- `RadioSettings` registers every saved password and PSK identity with `Logging::registerSecret()`
  when it loads or edits a radio, and the writer replaces any occurrence with `<redacted>` (strings
  shorter than 4 characters are not redacted).
- `LogCategoriesTests` fails any log statement that streams an identifier ending in `password`,
  `psk`, `secret`, `identity` or `presharedkey`. Log `.isEmpty()` or `.size()` of it instead.

## Turning debug on

Options → Diagnostics, or on the command line:

- `--log-file` — write the log file for this launch, whatever the page says.
- `--log-rules "<rules>"` — rules applied after the page's, separated by `;`.
- `--log-stderr` — keep the console copy while a file is written.

Settings live under `Logging/` in the QK4 settings store (`fileEnabled`, `level` =
`warnings|normal|everything`, `groups`). Bench items (CAT traffic, audio RX levels, audio TX frames,
spectrum geometry) are never saved, so they are off again at the next launch. `Everything` turns on
debug for every category except bench items.

Qt applies `QT_LOGGING_CONF` and `QT_LOGGING_RULES` after the app's rules, so they win over the page.

`QT_LOGGING_RULES` works as Qt documents it. Separate rules with `;`. A trailing `*` matches the
category's children but not the category itself, so name both:

```bash
QT_LOGGING_RULES="qk4.audio.debug=true;qk4.audio.*.debug=true" QK4
```

`QT_LOGGING_DEBUG=1` makes Qt report where it loaded its rules from.
