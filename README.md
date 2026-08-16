# QK4 - Intel Mac Fork

A fork of [mikeg-dal/QK4](https://github.com/mikeg-dal/QK4) specifically modified to support Intel Mac compatibility.

This fork resolves Homebrew library paths dynamically to support both Intel Mac (/usr/local) and Apple Silicon (/opt/homebrew) architectures, enabling QK4 to build and run on Intel-based Macs.

## Changes from Original

- Modified CMakeLists.txt to dynamically resolve Homebrew prefixes for opus, hidapi, and libusb libraries
- Added support for Intel Mac Homebrew installation paths (/usr/local)
- Maintains full compatibility with Apple Silicon Macs

## Supported Platforms

| Platform | Minimum Version | Architecture |
|----------|-----------------|--------------|
| macOS | 14 (Sonoma) | **Intel x64** and Apple Silicon (M1/M2/M3/M4) |
| Windows | 11 | x64 |
| Linux | Debian Trixie / Ubuntu 24.04+ | ARM64 (Raspberry Pi 4/5) |
| Linux | Any distribution with Flatpak | x86_64 |

## About QK4

QK4 is a cross-platform desktop application for remote control of Elecraft K4 radios over TCP/IP with real-time audio streaming and spectrum display.

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

For complete feature details, see the [original QK4 repository](https://github.com/mikeg-dal/QK4).

## Building from Source on macOS

### Requirements

| Dependency | Installation |
|------------|--------------|
| Xcode Command Line Tools | `xcode-select --install` |
| Homebrew | Install from [brew.sh](https://brew.sh) |
| Qt 6.7+ | `brew install qt` |
| libopus | `brew install opus` |
| OpenSSL 3 | `brew install openssl@3` |
| HIDAPI | `brew install hidapi` |
| libusb 1.0 | `brew install libusb` |
| CMake | `brew install cmake` |

### Build Instructions

```bash
# Install dependencies
brew install qt opus openssl@3 hidapi libusb cmake

# Clone this fork
git clone https://github.com/krainika/QK4.git
cd QK4

# Configure build (works on both Intel and Apple Silicon Macs)
cmake -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"

# Build
cmake --build build

# Run
./build/QK4.app/Contents/MacOS/QK4

# Create distributable app bundle (optional)
cmake --build build --target deploy
```

**Note**: This fork automatically detects your Mac architecture (Intel or Apple Silicon) and configures the appropriate Homebrew library paths.

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

The Flatpak packaging was contributed by [@CSVincentS](https://github.com/CSVincentS)
(see [#96](https://github.com/mikeg-dal/QK4/issues/96) and
[#127](https://github.com/mikeg-dal/QK4/pull/127)), with the original packaging approach
and PipeWire tuning from [@jmeloranta](https://github.com/jmeloranta)'s Arch AUR build.

### Raspberry Pi Prerequisites

- Raspberry Pi 4 or 5 with a desktop environment (X11 or Wayland)
- Debian Trixie or Ubuntu 24.04+
- **First run requires `sudo`** — the launcher (`run.sh`) installs a udev rule to grant non-root access to the Elecraft KPOD and KPOD+ USB devices. Without this rule, the Linux kernel restricts access to `/dev/hidraw*` and USB device nodes. After the first run, `sudo` is no longer needed. If you don't have a KPOD or KPOD+, `sudo` is not required.

## Testing

```bash
# Run all tests
ctest --test-dir build --output-on-failure
```

## Usage

For detailed usage instructions, please refer to the [original QK4 repository](https://github.com/mikeg-dal/QK4#usage).

## Technical Details

The key modification in this fork is in `CMakeLists.txt` (lines 49-53), where Homebrew library paths are resolved dynamically:

```cmake
# macOS with Homebrew - resolve prefix dynamically to support both
# Intel (/usr/local) and Apple Silicon (/opt/homebrew)
execute_process(COMMAND brew --prefix opus OUTPUT_VARIABLE OPUS_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND brew --prefix hidapi OUTPUT_VARIABLE HIDAPI_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND brew --prefix libusb OUTPUT_VARIABLE LIBUSB_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE)
```

This change allows the build system to work correctly on both Intel Macs (where Homebrew installs to `/usr/local`) and Apple Silicon Macs (where Homebrew installs to `/opt/homebrew`).

## License

This project is licensed under the [GNU General Public License v3.0](LICENSE), same as the original QK4 project.
