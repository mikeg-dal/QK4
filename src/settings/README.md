# settings/

`QSettings`-backed persistence surface.

## Files

- `radiosettings.{cpp,h}` — `RadioSettings` singleton (`RadioSettings::instance()`). Radio profiles, DX cluster list, KPA1500 config, CAT and TCI server config, audio device and gain preferences, hardware (KPOD, HaliKey, CW keyer) settings, macros, station data, the interface scale and the Diagnostics logging choices (`Logging/` keys; bench items are session-only). Includes the password obfuscation helpers.
- `interfacescale.{cpp,h}` — `InterfaceScale::` namespace. Applies the saved interface scale through `QT_SCALE_FACTOR` before `QApplication` exists (never overriding one the operator set), fits Auto to the screen (down in 5% steps on a small one, up to 125% only — never 105–120% — on a large one, never up where the platform reports no work area) with at most one silent relaunch per launch, and relaunches for Options → Display → Restart Now only after the main window has shut down, so the new instance never races the old one for the CAT/TCI ports or USB devices.

## Scope

Every persisted value goes through `RadioSettings`. Adding a setting:

1. Add a getter/setter (and a change signal if anything reacts to it) on `RadioSettings`.
2. Keys are managed inside `RadioSettings`. Callers do not use `QSettings` directly.
3. Settings are plain reads/writes; no async or atomic contract.

One place constructs a `QSettings` of its own: `RadioSettings::startupInterfaceScale()` reads the
scale before `QApplication` exists, so it cannot use the singleton. The store is named "QK4"/"QK4"
explicitly, so the application's organisation (KF5O) does not decide where settings live.

## Storage

The store is `QSettings(QSettings::defaultFormat(), QSettings::UserScope, "QK4", "QK4")`. The default format is `NativeFormat`:

- macOS: a CFPreferences plist under `~/Library/Preferences/`.
- Windows: the registry, under `HKEY_CURRENT_USER\Software\QK4\QK4`.
- Linux: an INI file, `~/.config/QK4/QK4.conf`. In the Flatpak, `~/.config` is the sandbox's own config directory under `~/.var/app/io.github.mikeg_dal.QK4/`.

WHY it names `defaultFormat()` instead of using the two-argument constructor: Qt's two-argument `QSettings(org, app)` ignores `QSettings::setDefaultFormat()`. Naming the format lets a test redirect the singleton to a sandbox file (see the comment in `radiosettings.cpp`).

## Threading

`QSettings` is reentrant, not thread-safe (Qt docs, QSettings). `RadioSettings` is used from the main thread only. Objects that later move to a worker thread (e.g. `AudioEngine`) are given their saved values before `moveToThread`.

## Security note

Passwords are obfuscated (not encrypted) via the `obfuscatePassword` / `deobfuscatePassword` helpers. Good enough to keep them out of clear-text scanning on disk; not a defense against a determined attacker. If that matters to you, use OS keychain APIs instead.
