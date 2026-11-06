#!/bin/bash
set -ex

# =============================================================================
# QK4 Raspberry Pi Build Script
# =============================================================================
# Builds QK4 for Raspberry Pi (ARM64) natively, inside a debian:trixie container
# on GitHub's arm64 runner, and creates a portable tarball with every library
# it needs that Raspberry Pi OS does not provide.
#
# Target: Raspberry Pi 4/5 running Raspberry Pi OS / Debian Trixie (arm64)
# Qt: the official Qt linux_arm64 build named in .github/qt-version, installed
#     on the runner by install-qt-action and mounted read-only at $QT_ROOT.
#     Debian's own Qt is not used: Trixie ships 6.8, and every other runner
#     builds with the pinned version.
# System libraries (opus, hidapi, OpenSSL, PulseAudio, ALSA) come from Trixie,
# so the bundled copies are the ones Raspberry Pi OS itself ships.
#
# Usage (from _build-pi.yml): QT_ROOT=/opt/qt build-pi.sh <version>
# =============================================================================

: "${QT_ROOT:?QT_ROOT must point at the official Qt install (e.g. /opt/qt)}"

ARM_LIB=/usr/lib/aarch64-linux-gnu
QT_LIB=$QT_ROOT/lib
QT_PLUGINS=$QT_ROOT/plugins

apt-get update

# Build tools
apt-get install -y build-essential cmake pkg-config ccache file patchelf

# What the official Qt's CMake packages and shared libraries need to be found and linked:
# WrapOpenGL (GL/EGL headers) for Qt6Gui, and the libraries its .so files name in NEEDED.
apt-get install -y \
  libgl-dev libegl-dev libxkbcommon-dev \
  libfontconfig1 libfreetype6 libglib2.0-0t64 libdbus-1-3 libx11-6 libbrotli1 libzstd1

# QK4's own dependencies
apt-get install -y \
  libopus-dev libhidapi-dev libusb-1.0-0-dev libssl-dev \
  libasound2-dev libpulse-dev libudev-dev

# The Qt mounted here must be the one .github/qt-version names.
WANT_QT="$(tr -d '[:space:]' < .github/qt-version)"
HAVE_QT="$("$QT_ROOT/bin/qtpaths" --qt-version)"
if [ "$HAVE_QT" != "$WANT_QT" ]; then
  echo "ERROR: Qt at $QT_ROOT is $HAVE_QT, .github/qt-version says $WANT_QT"
  exit 1
fi

# Configure ccache (CCACHE_DIR may be mounted from host for persistence)
export CCACHE_DIR="${CCACHE_DIR:-/tmp/ccache}"
export CCACHE_MAXSIZE=500M
ccache --zero-stats

# Version passed as first argument (from release workflow tag)
VERSION_FLAG=""
if [ -n "$1" ]; then
  VERSION_FLAG="-DQK4_VERSION_FULL=$1"
  echo "Building version: $1"
fi

cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$QT_ROOT" \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  $VERSION_FLAG
cmake --build build -j"$(nproc)"

echo "=== ccache stats ==="
ccache --show-stats

# Verify output is actually ARM64
file build/QK4 | grep -q "aarch64" || { echo "ERROR: Binary is not ARM64!"; exit 1; }

# =============================================================================
# Create portable bundle
# =============================================================================
DIST=dist/QK4
mkdir -p $DIST/lib
mkdir -p $DIST/plugins

cp build/QK4 $DIST/
cp resources/99-kpod.rules $DIST/

# Helper: copy a library and fail the build if it's missing
require_lib() {
  local src="$1" dst="$2"
  cp -rL "$src" "$dst" || { echo "ERROR: Required library missing: $src"; exit 1; }
}

# Helper: copy a library/directory if it exists (truly optional)
optional_lib() {
  local src="$1" dst="$2"
  if ls $src 1>/dev/null 2>&1; then
    cp -rL $src "$dst"
  else
    echo "NOTE: Optional library not found: $src (skipping)"
  fi
}

# -----------------------------------------------------------------------------
# Qt Libraries (required)
# -----------------------------------------------------------------------------
for lib in \
  libQt6Core.so.6 \
  libQt6Gui.so.6 \
  libQt6Widgets.so.6 \
  libQt6Network.so.6 \
  libQt6Multimedia.so.6 \
  libQt6SerialPort.so.6 \
  libQt6ShaderTools.so.6 \
  libQt6OpenGL.so.6 \
  libQt6OpenGLWidgets.so.6 \
  libQt6DBus.so.6 \
  libQt6Svg.so.6 \
  libQt6XcbQpa.so.6; do
  require_lib "$QT_LIB/$lib" "$DIST/lib/"
done

# ICU. The official Qt is built against its own ICU (libQt6Core NEEDs libicuuc/libicui18n of
# that version), which Trixie does not have, so it ships with the bundle. Copied by soname.
ICU_COUNT=0
for path in "$QT_LIB"/libicu{data,i18n,uc}.so.*; do
  [[ "$(basename "$path")" =~ ^libicu(data|i18n|uc)\.so\.[0-9]+$ ]] || continue
  require_lib "$path" "$DIST/lib/"
  ICU_COUNT=$((ICU_COUNT + 1))
done
[ "$ICU_COUNT" -eq 3 ] || { echo "ERROR: expected libicudata, libicui18n and libicuuc in $QT_LIB, copied $ICU_COUNT"; exit 1; }

# -----------------------------------------------------------------------------
# Qt Plugins
# -----------------------------------------------------------------------------
# Platform plugins: XCB (the one run.sh selects; XWayland under Pi OS's Wayland session),
# Wayland, plus offscreen and minimal for diagnostics. The rest of Qt's platforms directory
# (eglfs, vnc, ...) needs Qt libraries this bundle does not carry.
mkdir -p $DIST/plugins/platforms
for plugin in libqxcb.so libqwayland.so libqoffscreen.so libqminimal.so; do
  require_lib "$QT_PLUGINS/platforms/$plugin" "$DIST/plugins/platforms/"
done
optional_lib "$QT_PLUGINS/xcbglintegrations" "$DIST/plugins/"

# The Wayland plugin with what it needs to open a window: the client library, the xdg-shell
# integration, EGL buffers and the fallback decoration. Unused while run.sh selects XCB, but
# complete, so `QT_QPA_PLATFORM=wayland ./QK4` works for a native-Wayland test.
require_lib "$QT_LIB/libQt6WaylandClient.so.6" "$DIST/lib/"
for plugin in \
  wayland-shell-integration/libxdg-shell.so \
  wayland-graphics-integration-client/libqt-plugin-wayland-egl.so \
  wayland-decoration-client/libbradient.so; do
  mkdir -p "$DIST/plugins/$(dirname "$plugin")"
  require_lib "$QT_PLUGINS/$plugin" "$DIST/plugins/$(dirname "$plugin")/"
done

# TLS/SSL plugins (the OpenSSL backend carries TLS-PSK for the K4 on port 9204)
cp -r $QT_PLUGINS/tls $DIST/plugins/

# Image format plugins
optional_lib "$QT_PLUGINS/imageformats" "$DIST/plugins/"

# Icon engine plugins
optional_lib "$QT_PLUGINS/iconengines" "$DIST/plugins/"

# NOTE: We intentionally do NOT bundle plugins/multimedia (nor the FFmpeg libraries it
# needs). The audio engine uses QAudioSink/QAudioSource, which libQt6Multimedia drives
# through PulseAudio directly; the FFmpeg/GStreamer plugins are for media playback.

# -----------------------------------------------------------------------------
# System Libraries (required)
# -----------------------------------------------------------------------------
for lib in \
  libopus.so.0 \
  libhidapi-hidraw.so.0 \
  libhidapi-libusb.so.0 \
  libusb-1.0.so.0 \
  libssl.so.3 \
  libcrypto.so.3; do
  require_lib "$ARM_LIB/$lib" "$DIST/lib/"
done

# PulseAudio/ALSA libraries (required for audio)
for lib in \
  libpulse.so.0 \
  libpulse-simple.so.0 \
  libasound.so.2; do
  require_lib "$ARM_LIB/$lib" "$DIST/lib/"
done

# PulseAudio private library (libpulse.so links to this at runtime;
# version must match the bundled libpulse, not the host OS)
PULSECOMMON=$(ls $ARM_LIB/pulseaudio/libpulsecommon-*.so 2>/dev/null) \
  || { echo "ERROR: libpulsecommon not found in $ARM_LIB/pulseaudio/"; exit 1; }
cp -L $PULSECOMMON $DIST/lib/

# -----------------------------------------------------------------------------
# Verify bundle completeness
# -----------------------------------------------------------------------------
echo "=== Verifying bundle ==="
MISSING=0
for check in \
  "$DIST/QK4" \
  "$DIST/lib/libQt6Core.so.6" \
  "$DIST/lib/libQt6Multimedia.so.6" \
  "$DIST/lib/libopus.so.0" \
  "$DIST/lib/libpulse.so.0" \
  "$DIST/lib/libasound.so.2" \
  "$DIST/lib/libssl.so.3" \
  "$DIST/lib/libcrypto.so.3" \
  "$DIST/plugins/platforms/libqxcb.so" \
  "$DIST/plugins/tls/libqopensslbackend.so"; do
  if [ ! -e "$check" ]; then
    echo "ERROR: Missing from bundle: $check"
    MISSING=1
  fi
done

# Verify libpulsecommon made it (glob name varies by version)
if ! ls $DIST/lib/libpulsecommon-*.so 1>/dev/null 2>&1; then
  echo "ERROR: Missing from bundle: libpulsecommon-*.so"
  MISSING=1
fi

if [ $MISSING -ne 0 ]; then
  echo "Bundle verification FAILED"
  exit 1
fi

echo "Bundle verification passed"
echo ""
echo "=== Bundle contents ==="
find $DIST -type f | sort
echo ""

# -----------------------------------------------------------------------------
# Set rpath so binary finds bundled libs
# -----------------------------------------------------------------------------
patchelf --set-rpath '$ORIGIN/lib' $DIST/QK4

# Also patch any bundled .so files that might need it
find $DIST/lib -name "*.so*" -type f -exec patchelf --set-rpath '$ORIGIN' {} \; 2>/dev/null || true
find $DIST/plugins -name "*.so*" -type f -exec patchelf --set-rpath '$ORIGIN/../../lib' {} \; 2>/dev/null || true

# -----------------------------------------------------------------------------
# Create launcher script
# -----------------------------------------------------------------------------
cat > $DIST/run.sh << 'LAUNCHER'
#!/bin/bash
# QK4 Launcher for Raspberry Pi
# This script sets up the environment to use bundled libraries

DIR="$(cd "$(dirname "$0")" && pwd)"

# Install KPOD udev rule if not already present (requires sudo, one-time setup)
UDEV_RULE="$DIR/99-kpod.rules"
UDEV_DEST="/etc/udev/rules.d/99-kpod.rules"
if [ -f "$UDEV_RULE" ] && [ ! -f "$UDEV_DEST" ]; then
    echo "KPOD udev rule not installed. Installing for USB HID access..."
    sudo cp "$UDEV_RULE" "$UDEV_DEST"
    sudo udevadm control --reload-rules
    sudo udevadm trigger
    echo "KPOD udev rule installed. You may need to re-plug the KPOD device."
fi

# Use bundled libraries
export LD_LIBRARY_PATH="$DIR/lib:$LD_LIBRARY_PATH"

# Use bundled Qt plugins
export QT_PLUGIN_PATH="$DIR/plugins"

# Use XCB (X11) platform — we ship XCB plugins, not Wayland
export QT_QPA_PLATFORM=xcb

# Launch the application
exec "$DIR/QK4" "$@"
LAUNCHER
chmod +x $DIST/run.sh

# -----------------------------------------------------------------------------
# Package
# -----------------------------------------------------------------------------
cd dist
tar czf "../QK4-${1}-raspberry-pi-arm64.tar.gz" QK4/

echo "============================================="
echo "Build complete!"
echo "Output: QK4-${1}-raspberry-pi-arm64.tar.gz"
echo "============================================="
