# QK4 - Intel Mac Fork

A fork of [mikeg-dal/QK4](https://github.com/mikeg-dal/QK4) modified to build and run on Intel Macs using MacPorts.

## Changes from Original

- Uses MacPorts (`/opt/local`) instead of Homebrew for all dependencies
- ICU libraries bundled explicitly (`macdeployqt` misses them on Intel)
- `librsvg` excluded from bundle (`macdeployqt` cannot rewrite its paths on Intel; not needed for Qt SVG rendering)

## Supported Platform

| Platform | Minimum Version | Architecture |
|----------|-----------------|--------------|
| macOS | 14 (Sonoma) | Intel x64 |

For Apple Silicon, Windows, and Linux builds see the [original QK4 repository](https://github.com/mikeg-dal/QK4).

## About QK4

QK4 is a desktop application for remote control of Elecraft K4 radios over TCP/IP with real-time audio streaming and spectrum display.

- **TLS/PSK Encrypted Connection** — Secure connection via TLS v1.2 with Pre-Shared Key on port 9204
- **Dual VFO Display** — Frequency, mode, S-meter, and tuning rate indicator for VFO A and B
- **GPU-Accelerated Spectrum** — Real-time panadapter with waterfall via Qt RHI (Metal)
- **Mini-Pan Widget** — Compact spectrum view in VFO area with mode-dependent bandwidth
- **Dual-Channel Audio** — Opus-encoded stereo with independent MAIN/SUB volume controls
- **Radio Controls** — Full control panel with mode-dependent controls, TX functions, and feature popups
- **Band Selection** — Quick band switching via popup menu
- **KPOD / KPOD+ Support** — USB integration with Elecraft KPOD tuning knob and KPOD+ CW keyer
- **KPA1500 Support** — Optional integration with Elecraft KPA1500 amplifier
- **CAT Server** — Built-in CAT server (port 9299) for integration with third-party logging and contest software

## Building from Source

Requires macOS 14+ on Intel x64 with [MacPorts](https://www.macports.org) installed.

### Install Dependencies

```bash
sudo port install qt6 libopus openssl hidapi libusb icu cmake
```

### Build

```bash
git clone https://github.com/krainika/QK4.git
cd QK4
cmake -B build -DCMAKE_PREFIX_PATH="/opt/local/libexec/qt6;/opt/local"
cmake --build build
./build/QK4.app/Contents/MacOS/QK4
```

### Create DMG (optional)

```bash
cmake --build build --target deploy
```

## Testing

```bash
ctest --test-dir build --output-on-failure
```

## Usage

For detailed usage instructions, please refer to the [original QK4 repository](https://github.com/mikeg-dal/QK4#usage).

## License

This project is licensed under the [GNU General Public License v3.0](LICENSE), same as the original QK4 project.
