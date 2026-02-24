# QK4

A cross-platform desktop application for remote control of Elecraft K4 radios over TCP/IP with real-time audio streaming and spectrum display.

[![Release](https://img.shields.io/github/v/release/mikeg-dal/QK4?include_prereleases)](https://github.com/mikeg-dal/QK4/releases)
[![Build](https://github.com/mikeg-dal/QK4/actions/workflows/release.yml/badge.svg)](https://github.com/mikeg-dal/QK4/actions/workflows/release.yml)
[![Lint](https://github.com/mikeg-dal/QK4/actions/workflows/lint.yml/badge.svg)](https://github.com/mikeg-dal/QK4/actions/workflows/lint.yml)

## Supported Platforms

| Platform | Minimum Version | Architecture |
|----------|-----------------|--------------|
| macOS | 14 (Sonoma) | Apple Silicon (M1/M2/M3/M4) |
| Windows | 11 | x64 |
| Linux | Debian Trixie / Ubuntu 24.04+ | ARM64 (Raspberry Pi 4/5) |
| Android | 10+ | ARM64 (tablet/phone) |

## Features

- **TLS/PSK Encrypted Connection** — Secure connection via TLS v1.2 with Pre-Shared Key on port 9204
- **Dual VFO Display** — Frequency, mode, S-meter, and tuning rate indicator for VFO A and B
- **GPU-Accelerated Spectrum** — Real-time panadapter with waterfall via Qt RHI (Metal/DirectX/Vulkan)
- **Mini-Pan Widget** — Compact spectrum view in VFO area with mode-dependent bandwidth
- **Dual-Channel Audio** — Opus-encoded stereo with independent MAIN/SUB volume controls
- **Radio Controls** — Full control panel with mode-dependent controls, TX functions, and feature popups
- **Band Selection** — Quick band switching via popup menu
- **KPOD Support** — USB integration with Elecraft KPOD tuning knob
- **KPA1500 Support** — Optional integration with Elecraft KPA1500 amplifier
- **CAT Server** — Built-in CAT server (port 9299) for integration with third-party logging and contest software
- **Self-Contained Releases** — macOS DMG, Windows ZIP, and Raspberry Pi tarball include all dependencies

## Download

Pre-built releases are available on the [Releases](https://github.com/mikeg-dal/QK4/releases) page.

| Platform | Download | Notes |
|----------|----------|-------|
| macOS | [QK4-macos.dmg](https://github.com/mikeg-dal/QK4/releases/download/v0.4.0-beta.3/QK4-macos.dmg) | Signed and notarized — open the DMG and drag to Applications |
| Windows | [QK4-windows.zip](https://github.com/mikeg-dal/QK4/releases/download/v0.4.0-beta.3/QK4-windows.zip) | Extract and run `QK4.exe` |
| Raspberry Pi | [QK4-raspberry-pi-arm64.tar.gz](https://github.com/mikeg-dal/QK4/releases/download/v0.4.0-beta.3/QK4-raspberry-pi-arm64.tar.gz) | Extract and run `./QK4/run.sh` |
| Android | Build from source | No prebuilt APK published yet |

### Windows Prerequisite

- [Visual C++ Redistributable 2019+](https://aka.ms/vs/17/release/vc_redist.x64.exe) (if not already installed)

### Raspberry Pi Prerequisites

- Raspberry Pi 4 or 5 with a desktop environment (X11 or Wayland)
- Debian Trixie or Ubuntu 24.04+
- **First run requires `sudo`** — the launcher (`run.sh`) installs a udev rule to grant non-root access to the Elecraft KPOD USB device. Without this rule, the Linux kernel restricts access to `/dev/hidraw*` nodes and the KPOD cannot be opened. After the first run, `sudo` is no longer needed and the KPOD will work as a normal user. If you don't have a KPOD, `sudo` is not required.

## Building from Source

### Requirements

| Dependency | macOS (Homebrew) | Windows (vcpkg + Qt Installer) | Linux / Raspberry Pi (apt) |
|------------|------------------|-------------------------------|---------------------------|
| C++ compiler | Xcode Command Line Tools | Visual Studio 2019+ Build Tools | `apt install g++` |
| CMake | `brew install cmake` | Included with VS Build Tools | `apt install cmake` |
| Qt 6.7+ | `brew install qt` | [Qt Online Installer](https://www.qt.io/download-qt-installer) or [aqtinstall](https://github.com/miurahr/aqtinstall) | `apt install qt6-base-dev qt6-base-private-dev` |
| Qt modules | Included with Homebrew Qt | Multimedia, ShaderTools, SerialPort | `apt install qt6-multimedia-dev qt6-shadertools-dev qt6-serialport-dev` |
| libopus | `brew install opus` | `vcpkg install opus:x64-windows` | `apt install libopus-dev` |
| OpenSSL 3 | `brew install openssl@3` | `vcpkg install openssl:x64-windows` | `apt install libssl-dev` |
| HIDAPI | `brew install hidapi` | `vcpkg install hidapi:x64-windows` | `apt install libhidapi-dev` |
| Audio | Included with macOS | N/A | `apt install libasound2-dev libpulse-dev` |

### macOS

```bash
# Install dependencies
brew install qt opus openssl@3 hidapi cmake

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
vcpkg install opus:x64-windows hidapi:x64-windows openssl:x64-windows

# Install Qt 6.7+ via Qt Online Installer or aqtinstall
# Required modules: Multimedia, ShaderTools, SerialPort

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
sudo apt install cmake g++ file patchelf \
  qt6-base-dev qt6-base-private-dev \
  qt6-multimedia-dev qt6-shadertools-dev qt6-serialport-dev \
  libopus-dev libhidapi-dev libssl-dev \
  libasound2-dev libpulse-dev

# Clone and build
git clone https://github.com/mikeg-dal/QK4.git
cd QK4
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Run
./build/QK4
```

### Android (Real Tablet / Phone)

Android builds use Qt for Android and produce an APK.

Run all commands from a normal shell (`Terminal.app` or VS Code integrated terminal).
Android Studio is optional; it is only needed to install/manage SDK/NDK/JDK components.

#### Prerequisites

- Qt 6.7+ with Android kit installed (including `qt-cmake`)
- Android SDK + NDK + platform tools (`adb`)
- Java 17+
- Opus library for Android ABI (`arm64-v8a`) and matching headers
  - If auto-discovery fails, pass:
    - `-DQK4_OPUS_INCLUDE_DIR=/path/to/opus/include`
    - `-DQK4_OPUS_LIBRARY=/path/to/libopus.so` (or `.a`)

#### Build APK

```bash
# One-time shell setup (adjust if your SDK/Qt paths differ)
export ANDROID_SDK_ROOT="$HOME/Library/Android/sdk"
export ANDROID_NDK_ROOT="$(find "$ANDROID_SDK_ROOT/ndk" -mindepth 1 -maxdepth 1 -type d | sort -V | tail -1)"
export QT_HOST_PATH="$HOME/Qt/6.10.2/macos"
export PATH="$ANDROID_SDK_ROOT/platform-tools:$PATH"

# Java check (required for APK packaging)
java -version

# Configure (default is DEBUG deployment, signed for direct adb install)
"$HOME/Qt/6.10.2/android_arm64_v8a/bin/qt-cmake" -S . -B build-android-arm64 \
  -DQT_HOST_PATH="$QT_HOST_PATH" \
  -DANDROID_SDK_ROOT="$ANDROID_SDK_ROOT" \
  -DANDROID_NDK_ROOT="$ANDROID_NDK_ROOT" \
  -DQT_CHAINLOAD_TOOLCHAIN_FILE="$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake" \
  -DQK4_ANDROID_DEPLOYMENT_TYPE=DEBUG \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-26 \
  -DCMAKE_BUILD_TYPE=Release

"$HOME/Qt/6.10.2/android_arm64_v8a/bin/qt-cmake" \
  --build build-android-arm64 --target apk --parallel
```

#### Release Signing (Production)

For production, configure release signing credentials before running CMake:

```bash
export QT_ANDROID_KEYSTORE_PATH="/absolute/path/to/your-release.keystore"
export QT_ANDROID_KEYSTORE_ALIAS="your_alias"
export QT_ANDROID_KEYSTORE_STORE_PASS="your_keystore_password"
export QT_ANDROID_KEYSTORE_KEY_PASS="your_key_password"

"$HOME/Qt/6.10.2/android_arm64_v8a/bin/qt-cmake" -S . -B build-android-arm64-release \
  -DQT_HOST_PATH="$QT_HOST_PATH" \
  -DANDROID_SDK_ROOT="$ANDROID_SDK_ROOT" \
  -DANDROID_NDK_ROOT="$ANDROID_NDK_ROOT" \
  -DQT_CHAINLOAD_TOOLCHAIN_FILE="$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake" \
  -DQK4_ANDROID_DEPLOYMENT_TYPE=RELEASE \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-26 \
  -DCMAKE_BUILD_TYPE=Release

"$HOME/Qt/6.10.2/android_arm64_v8a/bin/qt-cmake" \
  --build build-android-arm64-release --target apk --parallel
```

#### Install on Device

```bash
# Verify device is connected (USB debugging enabled)
adb devices

# Locate APK (path may vary by Qt/CMake version)
find build-android-arm64 -name "*.apk" -print

# Install/upgrade
adb install -r <path-to-apk>
```

#### Android Notes

- On first launch, grant microphone permission for TX audio.
- K-Pod and HaliKey hardware integrations are currently unavailable on Android.
- Connect tablet and K4 to the same LAN; then use Radio Manager as on desktop.
- `INSTALL_PARSE_FAILED_NO_CERTIFICATES` means the APK is unsigned; use `-DQK4_ANDROID_DEPLOYMENT_TYPE=DEBUG` for local installs or configure release signing env vars.

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
└── hardware/             # KPOD USB device support
```

## License

This project is licensed under the [GNU General Public License v3.0](LICENSE).
