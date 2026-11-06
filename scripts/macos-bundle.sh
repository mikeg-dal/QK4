#!/usr/bin/env bash
#
# Turn a freshly built QK4.app into a self-contained bundle: Qt frameworks and plugins, the
# third-party libraries QK4 links, and the OpenSSL that Qt's TLS plugin loads at run time.
#
# The one definition of macOS bundling. _build-macos.yml runs it, and so does `cmake --build build
# --target deploy`, so a bundle made on a desk is assembled exactly as CI assembles it.
#
# Usage:
#   scripts/macos-bundle.sh <QK4.app> <qt-prefix> [openssl-prefix]
#
#   qt-prefix       the Qt installation the app was built against (the directory holding bin/macdeployqt),
#                   e.g. ~/Qt/6.10.3/macos or $QT_ROOT_DIR in CI
#   openssl-prefix  defaults to `brew --prefix openssl@3`
#
# The bundle is left ad-hoc signed so it runs as it stands. A Developer ID signing step re-signs it.
# Check the result with scripts/macos-verify-bundle.sh.

set -euo pipefail

if [ $# -lt 2 ] || [ $# -gt 3 ]; then
    sed -n '9,15p' "$0" >&2
    exit 2
fi

APP=$1
QT_PREFIX=$2
OPENSSL_PREFIX=${3:-$(brew --prefix openssl@3)}
FRAMEWORKS="$APP/Contents/Frameworks"
MACDEPLOYQT="$QT_PREFIX/bin/macdeployqt"

[ -d "$APP" ] || { echo "error: no app bundle at $APP" >&2; exit 2; }
[ -x "$MACDEPLOYQT" ] || { echo "error: no macdeployqt at $MACDEPLOYQT" >&2; exit 2; }
for lib in libssl.3.dylib libcrypto.3.dylib; do
    [ -f "$OPENSSL_PREFIX/lib/$lib" ] || { echo "error: no $lib in $OPENSSL_PREFIX/lib" >&2; exit 2; }
done

echo "=== macdeployqt ($MACDEPLOYQT) ==="
"$MACDEPLOYQT" "$APP" -verbose=1

# WHY by hand: Qt's OpenSSL TLS plugin links no OpenSSL. It loads libssl/libcrypto with dlopen when
# the first TLS call is made, so macdeployqt cannot see the dependency and ships without it. Qt then
# looks in Contents/Frameworks (qsslsocket_openssl_symbols.cpp, libraryPathList()), and without a
# copy there it falls back to Secure Transport, which has no PSK cipher suites.
echo "=== OpenSSL from $OPENSSL_PREFIX ==="
mkdir -p "$FRAMEWORKS"
for lib in libssl.3.dylib libcrypto.3.dylib; do
    cp -f "$OPENSSL_PREFIX/lib/$lib" "$FRAMEWORKS/$lib"
    chmod u+w "$FRAMEWORKS/$lib"
    install_name_tool -id "@executable_path/../Frameworks/$lib" "$FRAMEWORKS/$lib"
done
CRYPTO_REF=$(otool -L "$FRAMEWORKS/libssl.3.dylib" | awk '/libcrypto\.3\.dylib/ {print $1}')
install_name_tool -change "$CRYPTO_REF" "@executable_path/../Frameworks/libcrypto.3.dylib" \
    "$FRAMEWORKS/libssl.3.dylib"

# WHY arm64 only: Qt's binary packages are universal, but QK4 itself and the Homebrew libraries it
# bundles are arm64, so the Intel half of every Qt framework and plugin could never run. Removing it
# roughly halves what is signed, notarized and downloaded.
echo "=== thinning to arm64 ==="
while IFS= read -r -d '' file; do
    if lipo -info "$file" 2>/dev/null | grep -q '^Architectures in the fat file'; then
        lipo "$file" -thin arm64 -output "$file.thin"
        chmod "$(stat -f '%Lp' "$file")" "$file.thin"
        mv -f "$file.thin" "$file"
    fi
done < <(find "$APP/Contents" -type f -print0)

# WHY: install_name_tool and lipo invalidate the signature the copied libraries carried, and an arm64 Mac
# refuses to load code whose signature does not match.
echo "=== ad-hoc signing ==="
codesign --force --deep --sign - "$APP"

echo "=== bundled OpenSSL ==="
otool -L "$FRAMEWORKS/libssl.3.dylib" "$FRAMEWORKS/libcrypto.3.dylib"
