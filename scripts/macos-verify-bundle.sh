#!/usr/bin/env bash
#
# Release gate for a packaged QK4.app. Run it on the app INSIDE the mounted DMG, not on build/QK4.app:
# symlink flattening and missing libraries happen during packaging, so a check on the build tree
# passes while the shipped bundle is broken.
#
# Usage:
#   scripts/macos-verify-bundle.sh <QK4.app>
#
# Exit 0 when every check passes, 1 when any fails (all are run and reported), 2 on bad usage.

set -uo pipefail

if [ $# -ne 1 ] || [ ! -d "$1" ]; then
    sed -n '7,8p' "$0" >&2
    exit 2
fi

APP=$(cd "$1" && pwd -P)
FAIL=0
fail() { echo "ERROR: $*"; FAIL=1; }

LINKS=$(find "$APP" -type l | wc -l | tr -d ' ')
echo "framework symlinks: $LINKS"
[ "$LINKS" -ge 10 ] || fail "bundle symlinks flattened ($LINKS) - framework structure is broken"

codesign --verify --deep --strict "$APP" || fail "codesign strict verify failed"

test -f "$APP/Contents/PlugIns/tls/libqopensslbackend.dylib" \
    || fail "OpenSSL TLS backend plugin missing - TLS/PSK would fail"
for lib in libssl.3.dylib libcrypto.3.dylib; do
    test -f "$APP/Contents/Frameworks/$lib" || fail "$lib not bundled - TLS/PSK would fail"
done

# WHY: a library that names a path outside the bundle loads on the machine that built it, where
# Homebrew or Qt is installed at that path, and fails for everyone else. Only the OS's own
# libraries may be named absolutely, in load commands and in run-path entries alike.
echo "=== external library references ==="
EXTERNAL=0
while IFS= read -r -d '' file; do
    file -b "$file" | grep -q 'Mach-O' || continue
    REFS=$( { otool -L "$file" | awk '!/:$/ {print $1}'
              otool -l "$file" | awk '/cmd LC_RPATH/ {getline; getline; print $2}'; } \
            | grep '^/' | grep -Ev '^(/System/|/usr/lib/)' | sort -u)
    if [ -n "$REFS" ]; then
        echo "${file#"$APP"/}:"
        printf '  %s\n' $REFS
        EXTERNAL=1
    fi
done < <(find "$APP/Contents" -type f -print0)
[ "$EXTERNAL" -eq 0 ] && echo "none" || fail "bundle references libraries outside itself (listed above)"

# WHY run the app: the checks above prove the files are present, not that Qt's OpenSSL backend
# actually loads them. The self-check selects the backend exactly as the app does and fails unless
# OpenSSL is active with a TLS 1.2+ PSK cipher suite. The libssl path is checked as well, because
# on a build machine a missing bundled copy could otherwise be covered by an installed one.
echo "=== TLS self-check ==="
OUTPUT=$("$APP/Contents/MacOS/QK4" --tls-self-check 2>&1)
STATUS=$?
echo "$OUTPUT"
[ "$STATUS" -eq 0 ] || fail "TLS self-check failed (exit $STATUS)"
LIBSSL=$(echo "$OUTPUT" | sed -n 's/^libssl loaded from: //p')
case "$LIBSSL" in
"$APP"/Contents/Frameworks/*) ;;
*) fail "libssl was not loaded from the bundle: ${LIBSSL:-(nothing)}" ;;
esac

exit $FAIL
