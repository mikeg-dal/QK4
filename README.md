# QK4

A cross-platform desktop application for remote control of Elecraft K4 radios over TCP/IP with real-time audio streaming and spectrum display.

[![Release](https://img.shields.io/github/v/release/mikeg-dal/QK4?include_prereleases)](https://github.com/mikeg-dal/QK4/releases)
[![Build](https://github.com/mikeg-dal/QK4/actions/workflows/release.yml/badge.svg)](https://github.com/mikeg-dal/QK4/actions/workflows/release.yml)
[![CI](https://github.com/mikeg-dal/QK4/actions/workflows/ci-checks.yml/badge.svg)](https://github.com/mikeg-dal/QK4/actions/workflows/ci-checks.yml)

## Supported Platforms

| Platform | Minimum Version | Architecture |
|----------|-----------------|--------------|
| macOS | 15 (Sequoia) — set by `LSMinimumSystemVersion` in `resources/Info.plist.in` | Apple Silicon |
| Windows | 11 | x64 |
| Linux | Raspberry Pi OS / Debian Trixie | ARM64 (Raspberry Pi 4/5) |
| Linux | Any distribution with Flatpak | x86_64 |

## Features

- **TLS/PSK encrypted connection** — TLS 1.2 or later with a pre-shared key on port 9204. PSK needs Qt's OpenSSL TLS backend; the packages ship it. Port 9205 is the unencrypted connection.
- **Dual VFO display** — frequency, mode, S-meter and tuning rate for VFO A and B
- **GPU spectrum** — panadapter and waterfall drawn with Qt RHI: Metal on macOS, Direct3D 11 on Windows, OpenGL on Linux
- **Mini-pan** — compact spectrum view in each VFO area
- **Band-plan strip** — IARU region 1/2/3 segments across the top of the panadapter
- **Dual-channel audio** — MAIN and SUB receiver audio (Opus or uncompressed, chosen per radio), with separate volume and balance
- **Radio controls** — side panels, feature popups, band and mode selection, K4 menu access
- **Text decode** — windows for the K4's own decoder output (CW, AFSK, FSK, PSK, DATA, SSB), one per receiver
- **Macros** — CAT command macros on the Fn and PF keys, KPOD buttons, keyboard F1–F12, and a startup macro run on connect
- **HaliKey CW keyer** — paddles or straight key through a HaliKey (serial V1.4 or MIDI), keyed by QK4's built-in iambic keyer with a local sidetone
- **KPOD / KPOD+** — Elecraft KPOD tuning knob and the KPOD+ CW keyer over USB
- **KPA1500** — control and status for the Elecraft KPA1500 amplifier
- **CAT server** — native K4 CAT on TCP port 9299 (default) for WSJT-X, loggers and contest software
- **TCI server** — TCI 2.0 over WebSocket on port 50001 (default), with RX/TX audio for digital-mode programs
- **DX cluster** — one or more telnet cluster connections, with spots shown on the panadapter
- **Self-contained releases** — macOS DMG, Windows ZIP, Raspberry Pi tarball and Linux Flatpak include all dependencies

## Download

Pre-built releases are available on the [Releases](https://github.com/mikeg-dal/QK4/releases) page.


### Windows Prerequisite

None — the Visual C++ runtime ships inside the package.

### Linux x86_64 (Flatpak)

One bundle runs on any distribution with Flatpak — no per-distro packages.

```bash
# Install Flatpak (if not already installed)
sudo apt install flatpak

# Add the Flathub repository
flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo

# Install QK4
flatpak install QK4-<version>-linux-x86_64.flatpak

# Install the udev rules — required for KPOD / KPOD+ USB access
sudo curl -o /etc/udev/rules.d/99-kpod.rules https://raw.githubusercontent.com/mikeg-dal/QK4/main/resources/99-kpod.rules
sudo chmod 644 /etc/udev/rules.d/99-kpod.rules
sudo udevadm control --reload-rules
sudo udevadm trigger

# Run QK4
flatpak run io.github.mikeg_dal.QK4
```

After installation you can also launch QK4 from your application menu.

The Flatpak packaging was contributed by [@CSVincentS](https://github.com/CSVincentS)
(see [#96](https://github.com/mikeg-dal/QK4/issues/96) and
[#127](https://github.com/mikeg-dal/QK4/pull/127)), with the original packaging approach
and PipeWire tuning from [@jmeloranta](https://github.com/jmeloranta)'s Arch AUR build.

### Raspberry Pi Prerequisites

- Raspberry Pi 4 or 5 with a desktop session. QK4 runs on the X11 (xcb) platform plugin; under Wayland it runs through XWayland.
- Raspberry Pi OS or Debian based on Trixie, 64-bit. The tarball is built in a Debian Trixie container with the official Qt for ARM64, which it carries along with its own copies of Qt's ICU, OpenSSL and the audio libraries; the rest (X11, Mesa, fontconfig, GLib) comes from the desktop install.
- **First run asks for `sudo`.** The launcher (`run.sh`) installs a udev rule that gives non-root access to the KPOD and KPOD+ USB devices. Without it, the kernel restricts `/dev/hidraw*` and the USB device nodes. If you decline, QK4 still starts, but a KPOD or KPOD+ will not work. Once the rule is installed, `run.sh` no longer asks.

## Building from Source

Build with the official Qt named in [`.github/qt-version`](.github/qt-version) (currently 6.10.3),
from the [Qt Online Installer](https://www.qt.io/download-qt-installer) or
[aqtinstall](https://github.com/miurahr/aqtinstall). Every CI runner builds and tests with exactly
that version: macOS, Windows, the Raspberry Pi, the unit and sanitizer tests, and CodeQL. The Flatpak
gets the same minor (6.10) from the KDE runtime, which decides the patch.

Older Qt is not tested. Qt 6.7 is the hard minimum (the panadapter uses `QRhiWidget`), and a distro
Qt such as Debian Trixie's 6.8 may still build, but CMake warns when the Qt it finds is not the
pinned version. `scripts/check-qt-version.sh --local` checks the Qt on your machine.

### Requirements

| Dependency | macOS | Windows (vcpkg + Qt Installer) | Linux / Raspberry Pi (apt + official Qt) |
|------------|-------|-------------------------------|-----------------------------------|
| C++ compiler | Xcode Command Line Tools | Visual Studio 2019+ Build Tools | `g++` |
| CMake | `brew install cmake` | Included with VS Build Tools | `cmake` |
| Qt (`.github/qt-version`) | [Qt Online Installer](https://www.qt.io/download-qt-installer) | [Qt Online Installer](https://www.qt.io/download-qt-installer) or [aqtinstall](https://github.com/miurahr/aqtinstall) | [Qt Online Installer](https://www.qt.io/download-qt-installer) or [aqtinstall](https://github.com/miurahr/aqtinstall) |
| Qt modules | Multimedia, Shader Tools, Serial Port, Svg (select in the installer) | Multimedia, Shader Tools, Serial Port, Svg | Multimedia, Shader Tools, Serial Port, Svg |
| GL / keyboard headers for Qt | — | — | `libgl-dev libegl-dev libxkbcommon-dev` |
| libopus | `brew install opus` | from `vcpkg.json` | `libopus-dev` |
| OpenSSL 3 | `brew install openssl@3` | from `vcpkg.json` | `libssl-dev` |
| HIDAPI | `brew install hidapi` | from `vcpkg.json` | `libhidapi-dev` |
| libusb 1.0 | `brew install libusb` | from `vcpkg.json` | `libusb-1.0-0-dev` |
| udev, pkg-config | — | — | `libudev-dev pkg-config` |
| Audio / MIDI | Included with macOS | Included with Windows | `libasound2-dev libpulse-dev` |

### macOS

Use Qt from the Qt Online Installer, not Homebrew's `qt`. A local build against Homebrew Qt cannot
load Qt's OpenSSL TLS backend. It falls back to Secure Transport, which has no PSK cipher suites, so
the encrypted connection on port 9204 fails. `CMakeLists.txt` looks for opus, hidapi and libusb
under `/opt/homebrew/opt/`, so the build expects Homebrew on Apple Silicon.

```bash
# Install the non-Qt dependencies
brew install cmake opus openssl@3 hidapi libusb

# Check which Qt version the installer put in ~/Qt
ls ~/Qt

# Clone and build (the Qt version CI uses is in .github/qt-version)
git clone https://github.com/mikeg-dal/QK4.git
cd QK4
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$HOME/Qt/6.10.3/macos"
cmake --build build

# Run
./build/QK4.app/Contents/MacOS/QK4

# Create a self-contained app bundle (optional)
cmake --build build --target deploy
```

### Windows

The vcpkg toolchain file runs vcpkg in manifest mode. It installs opus, hidapi, OpenSSL and
libusb from `vcpkg.json` during configure, so no separate `vcpkg install` is needed.
Run these commands from an "x64 Native Tools Command Prompt for VS", so Ninja finds the compiler.

```powershell
# Install the Qt in .github/qt-version via the Qt Online Installer or aqtinstall
# Required modules: Multimedia, Shader Tools, Serial Port, Svg

# Clone and build (add -DCMAKE_PREFIX_PATH=<Qt>\msvc2022_64 if CMake does not find Qt)
git clone https://github.com/mikeg-dal/QK4.git
cd QK4
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Run
.\build\QK4.exe

# Copy the Qt DLLs and plugins next to the executable (optional)
cmake --build build --target deploy
```

### Linux / Raspberry Pi

Use the official Qt, as CI does, not the distribution's. The Qt Online Installer puts it in
`~/Qt/<version>/gcc_64` on x86_64 and `~/Qt/<version>/gcc_arm64` on ARM64 (Raspberry Pi 4/5);
aqtinstall does the same without an account.

```bash
# Install the non-Qt dependencies (Debian/Ubuntu names)
sudo apt install cmake g++ pkg-config pipx \
  libgl-dev libegl-dev libxkbcommon-dev \
  libopus-dev libhidapi-dev libusb-1.0-0-dev libssl-dev libudev-dev \
  libasound2-dev libpulse-dev

# Install the pinned Qt with aqtinstall (or use the Qt Online Installer)
git clone https://github.com/mikeg-dal/QK4.git
cd QK4
QT_VERSION=$(cat .github/qt-version)
pipx install aqtinstall   # then open a new shell, or: export PATH="$HOME/.local/bin:$PATH"
aqt install-qt linux desktop "$QT_VERSION" linux_gcc_64 \
  -m qtmultimedia qtshadertools qtserialport -O "$HOME/Qt"
# On a Raspberry Pi: aqt install-qt linux_arm64 desktop "$QT_VERSION" linux_gcc_arm64 ...

# Build (gcc_arm64 instead of gcc_64 on a Raspberry Pi)
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$HOME/Qt/$QT_VERSION/gcc_64"
cmake --build build -j$(nproc)

# Run
./build/QK4
```

A distribution's own Qt 6.7 or later may also work (`qt6-base-dev qt6-base-private-dev
qt6-multimedia-dev qt6-shadertools-dev qt6-serialport-dev qt6-svg-dev` on Debian), but it is not
what CI tests, and CMake will warn about the version.

### Linux x86_64 (Flatpak)

Builds the same bundle CI produces, without needing the Qt dev packages above —
the KDE SDK supplies them.

```bash
# Install flatpak-builder and the KDE runtime
sudo apt install flatpak flatpak-builder
flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
flatpak install flathub org.kde.Platform//6.10 org.kde.Sdk//6.10

# Clone and build
git clone https://github.com/mikeg-dal/QK4.git
cd QK4
flatpak-builder --user --install build-dir flatpak/io.github.mikeg_dal.QK4.json

# Run
flatpak run io.github.mikeg_dal.QK4
```

A local build tracks the `main` branch and reports its version as `main`; CI
overrides both to pin the released commit and stamp the real version.

## Testing

The unit tests use the Qt Test framework. CI runs them on every push and pull request.

```bash
# Build every test target, then run them all
cmake --build build --target qk4_tests
ctest --test-dir build --output-on-failure

# Configure the tests alone. From Qt this needs only qtbase; the non-Qt libraries above are still needed
cmake -B build-tests -DQK4_TESTS_ONLY=ON
cmake --build build-tests --target qk4_tests
ctest --test-dir build-tests --output-on-failure
```

`ctest --test-dir build -N` lists the suites. `tests/CMakeLists.txt` defines them.

## Usage

1. Launch QK4.
2. Click the **globe icon** on the left side panel to open the **Server Manager**.
3. Click **Add**, then enter a name and your K4's host name or IP address. **Scan** finds K4s on the local network.
4. **Encrypted connection:** check **Use TLS (Encrypted)** and enter the radio's PSK as the password. The port changes to 9204.
5. **Unencrypted connection:** leave TLS unchecked. The port is 9205.
6. Click **Connect**. **Save** keeps the entry in the list for next time.

Application settings are under **Tools → Settings…** (on macOS, **QK4 → Settings…**), which opens the **Options** dialog.

Left-click **M1–M4** to play a stored message once, or right-click to repeat it, equivalent to holding
the corresponding button on the K4. The repeat delay uses the radio's **MENU → Message Repeat Interval**
setting. Click **REC** to cancel playback.

## Reporting a Bug: Log Files

QK4 can write a log file that shows what it was doing when something went wrong. It is off until you turn it on:

1. Open **Options → Diagnostics** and check **Write a log file**. It applies at once; no restart.
2. Reproduce the problem. If a developer asked for more detail, check the subsystem they named (for example **CW keying**), or set **Level** to **Everything**. **Bench diagnostics** (every CAT command, audio frames) are very verbose and switch themselves off when QK4 restarts.
3. Click **Open Log Folder** and attach `qk4.log` to the issue. If QK4 crashed or was restarted since, attach `qk4.1.log` as well: every launch starts a new `qk4.log` and moves the previous one to `qk4.1.log`. Five files of up to 10 MB each are kept.

| Platform | Log folder |
|---|---|
| macOS | `~/Library/Logs/QK4` (also listed in Console.app under Log Reports) |
| Windows | `%LOCALAPPDATA%\QK4\Logs` (paste it into the Explorer address bar) |
| Linux, Raspberry Pi | `~/.local/state/QK4/logs` (or `$XDG_STATE_HOME/QK4/logs`) |
| Linux Flatpak | `~/.var/app/io.github.mikeg_dal.QK4/.local/state/QK4/logs` |

A log contains connection attempts with your radio's address, audio device names and, with bench diagnostics on, the CAT commands sent to the radio. It never contains your radio password or PSK.

From the command line, `--log-file` writes a log for that launch only, `--log-rules "qk4.cw.*.debug=true"` adds Qt logging rules, and `--log-stderr` keeps printing to the terminal while a file is written. `QT_LOGGING_RULES` works as Qt documents it and overrides the Diagnostics page. Category names are listed in [`src/logging/README.md`](src/logging/README.md).

## Architecture

```
K4 (TCP 9204 TLS/PSK or 9205) → TcpClient + Protocol (I/O thread) → RadioState → controllers → UI widgets
UI widgets → controllers → ConnectionController → TcpClient → K4

RX audio: Protocol → OpusDecoder (I/O thread) → AudioEngine (audio thread) → speaker
TX audio: microphone → TxAudioPipeline (encode + packetize, AudioTX thread) → TcpClient → K4
Spectrum: Protocol → SpectrumController → PanadapterRhiWidget / MiniPanRhiWidget (GPU)
```

The thread map and the controller list are in `CLAUDE.md` and `docs/controllers.md`.

## Project Structure

```
src/
├── main.cpp              # Entry point: settings migration, TLS backend, fonts
├── mainwindow.cpp        # Main window and cross-controller wiring
├── controllers/          # One controller per I/O owner or UI slice
├── network/              # K4 link, CAT server, TCI server, DX cluster, KPA1500, discovery
├── audio/                # Audio engine, Opus codecs, resampling, sidetone
├── dsp/                  # GPU panadapter, waterfall and mini-pan
├── models/               # RadioState and the K4 menu model
├── settings/             # Persisted settings (RadioSettings)
├── utils/                # Shared helpers (RadioUtils, window fit, macro ids)
├── ui/                   # Widgets, popups, overlays, dialogs, Options pages, styling
└── hardware/             # KPOD, KPOD+, HaliKey and the CW keyer
```

Each directory has a `README.md` describing its files.

## License

QK4 — Remote control application for Elecraft K4 radios
Copyright (C) 2025-2026 Mike Garcia — KF5O

This program is free software: you can redistribute it and/or modify it under the terms of the
[GNU General Public License](LICENSE) as published by the Free Software Foundation, either version 3
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License along with this program. If not,
see <https://www.gnu.org/licenses/>.
