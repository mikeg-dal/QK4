#!/bin/bash
set -euo pipefail

# =============================================================================
# Check that the packaged Raspberry Pi tarball resolves every shared library
# =============================================================================
# Runs in a clean debian:trixie arm64 container (_build-pi.yml), not in the build
# container: the build container has every -dev package installed, so a library
# the bundle forgot would still resolve there and the gap would only show on a Pi.
#
# The container gets only the runtime packages below, which stand in for what a
# Raspberry Pi OS (Trixie) desktop already has: X11/XCB, Wayland, Mesa GL/EGL, fontconfig,
# GLib, D-Bus, and libpulse0/libasound2 (whose own dependencies the bundled
# copies need). Everything else must come from the tarball's lib/ directory. ldd
# then runs on the binary, every bundled library and every bundled plugin, the way
# run.sh launches it (LD_LIBRARY_PATH=lib), and any "not found" fails the build.
#
# Usage: check-pi-deps.sh <tarball>
# =============================================================================

TARBALL="${1:?usage: $0 <tarball>}"

apt-get update
apt-get install -y --no-install-recommends \
  libc6 libstdc++6 libgcc-s1 zlib1g libzstd1 libbrotli1 libglib2.0-0t64 libdbus-1-3 libudev1 \
  libgl1 libegl1 libopengl0 libglx0 \
  libfontconfig1 libfreetype6 \
  libx11-6 libx11-xcb1 libsm6 libice6 libxkbcommon0 libxkbcommon-x11-0 \
  libxcb1 libxcb-cursor0 libxcb-glx0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0 \
  libxcb-render0 libxcb-render-util0 libxcb-shape0 libxcb-shm0 libxcb-sync1 libxcb-xfixes0 \
  libxcb-xkb1 libwayland-client0 libwayland-cursor0 libwayland-egl1 \
  libpulse0 libasound2t64

WORK=$(mktemp -d)
tar xzf "$TARBALL" -C "$WORK"
APP="$WORK/QK4"

FAIL=0
CHECKED=0
while IFS= read -r -d '' f; do
  # Only ELF files: the tarball also holds run.sh and the udev rule.
  head -c 4 "$f" | grep -q 'ELF' || continue
  CHECKED=$((CHECKED + 1))
  if ! out=$(LD_LIBRARY_PATH="$APP/lib" ldd "$f" 2>&1); then
    echo "ERROR: ldd failed on ${f#"$WORK"/}"
    echo "$out"
    FAIL=1
    continue
  fi
  if echo "$out" | grep -q "not found"; then
    echo "ERROR: unresolved libraries in ${f#"$WORK"/}:"
    echo "$out" | grep "not found"
    FAIL=1
  fi
done < <(find "$APP" -type f -print0)

echo "Checked $CHECKED ELF files."
[ "$CHECKED" -gt 0 ] || { echo "ERROR: no ELF files found in $TARBALL"; exit 1; }
if [ $FAIL -ne 0 ]; then
  echo "Dependency check FAILED: the tarball would not start on a stock Raspberry Pi OS."
  exit 1
fi
echo "Dependency check passed: every library resolves from the bundle or a stock Trixie desktop."
