# QK4

A cross-platform desktop application for remote control of Elecraft K4 radios over TCP/IP with real-time audio streaming and spectrum display.

[![Release](https://img.shields.io/github/v/release/mikeg-dal/QK4?include_prereleases)](https://github.com/mikeg-dal/QK4/releases)
[![Build](https://github.com/mikeg-dal/QK4/actions/workflows/release.yml/badge.svg)](https://github.com/mikeg-dal/QK4/actions/workflows/release.yml)
[![CI](https://github.com/mikeg-dal/QK4/actions/workflows/ci.yml/badge.svg)](https://github.com/mikeg-dal/QK4/actions/workflows/ci.yml)

## Supported Platforms

| Platform | Minimum Version | Architecture |
|----------|-----------------|--------------|
| macOS | 14 (Sonoma) | Apple Silicon (M1/M2/M3/M4) |
| Windows | 11 | x64 |
| Linux | Debian Trixie / Ubuntu 24.04+ | ARM64 (Raspberry Pi 4/5) |
| Linux | Any distribution with Flatpak | x86_64 |

## Features

- **TLS/PSK Encrypted Connection** — Secure connection via TLS v1.2 with Pre-Shared Key on port 9204
- **Dual VFO Display** — Frequency, mode, S-meter, and tuning rate indicator for VFO A and B
- **GPU-Accelerated Spectrum** — Real-time panadapter with waterfall via Qt RHI (Metal/DirectX/Vulkan)
- **Mini-Pan Widget** — Compact spectrum view in VFO area with mode-dependent bandwidth
- **Dual-Channel Audio** — Opus-encoded stereo with independent MAIN/SUB volume controls
- **Radio Controls** — Full control panel with mode-dependent controls, TX functions, and feature popups
- **Band Selection** — Quick band switching via popup menu
- **KPOD / KPOD+ Support** — USB integration with Elecraft KPOD tuning knob and KPOD+ CW keyer
- **KPA1500 Support** — Optional integration with Elecraft KPA1500 amplifier
- **CAT Server** — Built-in CAT server (port 9299) for integration with third-party logging and contest software
- **Self-Contained Releases** — macOS DMG, Windows ZIP, Raspberry Pi tarball, and Linux Flatpak include all dependencies

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

- Raspberry Pi 4 or 5 with a desktop environment (X11 or Wayland)
- Debian Trixie or Ubuntu 24.04+
- **First run requires `sudo`** — the launcher (`run.sh`) installs a udev rule to grant non-root access to the Elecraft KPOD and KPOD+ USB devices. Without this rule, the Linux kernel restricts access to `/dev/hidraw*` and USB device nodes. After the first run, `sudo` is no longer needed. If you don't have a KPOD or KPOD+, `sudo` is not required.

## Building from Source

### Requirements

| Dependency | macOS (Homebrew) | Windows (vcpkg + Qt Installer) | Linux / Raspberry Pi (apt) |
|------------|------------------|-------------------------------|---------------------------|
| C++ compiler | Xcode Command Line Tools | Visual Studio 2019+ Build Tools | `apt install g++` |
| CMake | `brew install cmake` | Included with VS Build Tools | `apt install cmake` |
| Qt 6.7+ | `brew install qt` | [Qt Online Installer](https://www.qt.io/download-qt-installer) or [aqtinstall](https://github.com/miurahr/aqtinstall) | `apt install qt6-base-dev qt6-base-private-dev` |
| Qt modules | Included with Homebrew Qt | Multimedia, ShaderTools, SerialPort, Svg | `apt install qt6-multimedia-dev qt6-shadertools-dev qt6-serialport-dev qt6-svg-dev` |
| libopus | `brew install opus` | `vcpkg install opus:x64-windows` | `apt install libopus-dev` |
| OpenSSL 3 | `brew install openssl@3` | `vcpkg install openssl:x64-windows` | `apt install libssl-dev` |
| HIDAPI | `brew install hidapi` | `vcpkg install hidapi:x64-windows` | `apt install libhidapi-dev` |
| libusb 1.0 | `brew install libusb` | `vcpkg install libusb:x64-windows` | `apt install libusb-1.0-0-dev` |
| Audio | Included with macOS | N/A | `apt install libasound2-dev libpulse-dev` |

### macOS

```bash
# Install dependencies
brew install qt opus openssl@3 hidapi libusb cmake

# Clone and build
git clone https://github.com/mikeg-dal/QK4.git
cd QK4
cmake -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build

# Run
./build/QK4.app/Contents/MacOS/QK4

# Create distributable app bundle (optional)
cmake --build build --target deploy
```

### Windows

```powershell
# Install vcpkg dependencies
vcpkg install opus:x64-windows hidapi:x64-windows openssl:x64-windows libusb:x64-windows

# Install Qt 6.7+ via Qt Online Installer or aqtinstall
# Required modules: Multimedia, ShaderTools, SerialPort, Svg

# Clone and build
git clone https://github.com/mikeg-dal/QK4.git
cd QK4
cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release

# Run
.\build\Release\QK4.exe
```

### Linux / Raspberry Pi

```bash
# Install dependencies (Debian Trixie / Ubuntu 24.04+)
sudo apt install cmake g++ pkg-config file patchelf \
  qt6-base-dev qt6-base-private-dev \
  qt6-multimedia-dev qt6-shadertools-dev qt6-serialport-dev qt6-svg-dev \
  libopus-dev libhidapi-dev libusb-1.0-0-dev libssl-dev libudev-dev \
  libasound2-dev libpulse-dev

# Clone and build
git clone https://github.com/mikeg-dal/QK4.git
cd QK4
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Run
./build/QK4
```

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

QK4 includes a unit test suite built with the Qt Test framework. Tests run automatically in CI on every push and PR.

```bash
# Run all tests
ctest --test-dir build --output-on-failure

# Build and run only tests (no GUI dependencies needed)
cmake --build build --target test_radiostate test_radioutils test_protocol test_catserver
ctest --test-dir build --output-on-failure
```

| Suite | Tests | Coverage |
|-------|-------|----------|
| **RadioState** | 34 | CAT command parsing: frequency, mode, power, filters, notch, lock, split, edge cases |
| **RadioUtils** | 28 | Shared utilities: tuning steps, band detection, span stepping |
| **Protocol** | 14 | K4 binary packet framing, routing, roundtrip, overflow recovery |
| **CatServer** | 26 | TCP CAT server: GET responses, SET forwarding, PTT, multi-command |

## Usage

1. Launch QK4
2. Click the **globe icon** on the left side panel to open the Radio Manager
3. Enter your K4's IP address
4. **For encrypted connection**: Check "Use TLS", enter your PSK, port auto-sets to 9204
5. **For unencrypted connection**: Leave TLS unchecked, port defaults to 9205
6. Click **Connect**

Once connected, the application displays real-time spectrum, audio, and radio state from your K4.

## Architecture

```
Radio (TCP:9204 TLS / 9205 unencrypted) → TcpClient → Protocol → RadioState / DSP Widgets
                                                               ↓
                                                        OpusDecoder → AudioEngine → Speaker
Microphone → AudioEngine → OpusEncoder → Protocol → TcpClient → Radio
```

## Project Structure

```
src/
├── main.cpp              # Application entry point
├── mainwindow.cpp        # Main window and UI orchestration
├── network/              # TCP client and K4 protocol handling
├── audio/                # Opus codec and Qt audio engine
├── dsp/                  # Panadapter and spectrum widgets
├── models/               # Radio state model
├── settings/             # QSettings persistence
├── ui/                   # UI components (VFO, S-meter, controls)
└── hardware/             # KPOD, KPOD+, HaliKey USB device support
```

## License

QK4 — Remote control application for Elecraft K4 radios
Copyright (C) 2025-2026 Mike Garcia

This program is free software: you can redistribute it and/or modify it under the terms of the
[GNU General Public License](LICENSE) as published by the Free Software Foundation, either version 3
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License along with this program. If not,
see <https://www.gnu.org/licenses/>.
