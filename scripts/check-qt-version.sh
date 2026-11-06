#!/usr/bin/env bash
#
# Check that every place QK4 names a Qt version agrees with .github/qt-version.
#
# .github/qt-version holds the one exact Qt version every runner builds and tests with, and the
# version a developer's machine should have. Workflows read it at run time; the places that cannot
# read a file are checked here instead:
#
#   - every jurplel/install-qt-action step takes `version: ${{ env.QT_VERSION }}`, and its workflow
#     reads .github/qt-version into that variable. A step that deliberately installs another version
#     (the Qt 6.11 canary) carries the marker `# qt-version: probe` on its version line;
#   - no workflow or .github script sets QT_VERSION to a literal, names a Qt install path or
#     repository folder with a literal version, or installs a distro Qt from apt;
#   - scripts/build-qtbase-tsan.sh reads the file and has a sha256 for this version;
#   - the Flatpak build image tag (kde-X.Y), the manifest's runtime-version and the README's
#     org.kde.Platform//X.Y and org.kde.Sdk//X.Y match the version's major.minor (a runtime branch
#     cannot pin the patch);
#   - every Qt/<version>/ path in README.md, and in CLAUDE.md where that file exists (it is
#     gitignored, so only a maintainer's checkout has it), is this version.
#
# Usage:
#   scripts/check-qt-version.sh           check the repository (what CI runs)
#   scripts/check-qt-version.sh --local   check this machine's Qt instead
#
# --local asks qtpaths for its version. It uses qtpaths (or qtpaths6) on PATH, else the one next to
# qt-cmake on PATH, else ~/Qt/<version>/{macos,gcc_64,gcc_arm64}/bin/qtpaths. When none of those
# exist but another ~/Qt/<x.y.z> install does, that install is reported as the mismatch.
#
# Exit codes, and nothing else: 0 everything agrees, 1 drift found, 2 cannot check (not in a repo,
# unreadable version file, bad usage, or no Qt found for --local).

set -euo pipefail

SCRIPT_PATH="${BASH_SOURCE[0]}"
case "${SCRIPT_PATH}" in
    /*) ;;
    *) SCRIPT_PATH="$(pwd)/${SCRIPT_PATH}" ;;
esac

usage() {
    awk 'NR == 1 { next } /^#/ { sub(/^#[[:space:]]?/, ""); print; next } { exit }' "${SCRIPT_PATH}"
}

MODE=repo
for arg in "$@"; do
    case "${arg}" in
        --local) MODE=local ;;
        -h | --help)
            usage
            exit 0
            ;;
        *)
            echo "unknown option: ${arg}" >&2
            echo "Try: scripts/check-qt-version.sh --help" >&2
            exit 2
            ;;
    esac
done

if ! REPO_ROOT="$(git -C "$(dirname "${SCRIPT_PATH}")" rev-parse --show-toplevel 2>/dev/null)" || [ -z "${REPO_ROOT}" ]; then
    echo "Not inside a git repository, so .github/qt-version cannot be located." >&2
    exit 2
fi
cd "${REPO_ROOT}"

VERSION_FILE=.github/qt-version
if [ ! -r "${VERSION_FILE}" ]; then
    echo "Cannot read ${VERSION_FILE}." >&2
    exit 2
fi
QT_VERSION="$(tr -d '[:space:]' <"${VERSION_FILE}")"
if ! printf '%s\n' "${QT_VERSION}" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+$'; then
    echo "${VERSION_FILE} must hold one exact version such as 6.10.3, not '${QT_VERSION}'." >&2
    exit 2
fi
QT_MINOR="${QT_VERSION%.*}"

# ---------------------------------------------------------------------------------------------
# --local: this machine
# ---------------------------------------------------------------------------------------------
if [ "${MODE}" = local ]; then
    qtpaths_candidates() {
        command -v qtpaths 2>/dev/null || true
        command -v qtpaths6 2>/dev/null || true
        local qtcmake
        qtcmake="$(command -v qt-cmake 2>/dev/null || true)"
        [ -n "${qtcmake}" ] && printf '%s\n' "$(dirname "${qtcmake}")/qtpaths"
        local kit
        for kit in macos gcc_64 gcc_arm64; do
            printf '%s\n' "${HOME}/Qt/${QT_VERSION}/${kit}/bin/qtpaths"
        done
    }

    QTPATHS=""
    while IFS= read -r candidate; do
        if [ -n "${candidate}" ] && [ -x "${candidate}" ]; then
            QTPATHS="${candidate}"
            break
        fi
    done < <(qtpaths_candidates)

    if [ -z "${QTPATHS}" ]; then
        # Nothing at the expected path. An older or newer install is the usual reason; name it.
        OTHER=""
        for dir in "${HOME}"/Qt/[0-9]*.[0-9]*.[0-9]*/; do
            [ -d "${dir}" ] || continue
            OTHER="${OTHER} $(basename "${dir}")"
        done
        if [ -n "${OTHER}" ]; then
            echo "Qt version drift: this machine has Qt${OTHER} in ~/Qt, but ${VERSION_FILE} says ${QT_VERSION}."
            echo "  Install Qt ${QT_VERSION} (MaintenanceTool: Multimedia, Shader Tools, Serial Port) and"
            echo "  reconfigure with -DCMAKE_PREFIX_PATH=\"\$HOME/Qt/${QT_VERSION}/macos\" (gcc_64 on Linux)."
            echo "  Then remove the old version so nothing on PATH picks it up."
            exit 1
        fi
        echo "No Qt found: no qtpaths or qt-cmake on PATH and nothing in ~/Qt/${QT_VERSION}." >&2
        exit 2
    fi

    if ! LOCAL_VERSION="$("${QTPATHS}" --qt-version 2>/dev/null)" || [ -z "${LOCAL_VERSION}" ]; then
        echo "${QTPATHS} did not report a Qt version." >&2
        exit 2
    fi
    if [ "${LOCAL_VERSION}" != "${QT_VERSION}" ]; then
        echo "Qt version drift: ${QTPATHS} is Qt ${LOCAL_VERSION}, but ${VERSION_FILE} says ${QT_VERSION}."
        exit 1
    fi
    echo "Local Qt ${LOCAL_VERSION} (${QTPATHS}) matches ${VERSION_FILE}."
    exit 0
fi

# ---------------------------------------------------------------------------------------------
# Repository mode
# ---------------------------------------------------------------------------------------------
FAIL=0
drift() {
    echo "DRIFT: $*"
    FAIL=1
}

WORKFLOWS=(.github/workflows/*.yml)

# 1. install-qt-action steps take the version from the file.
for wf in "${WORKFLOWS[@]}"; do
    # Print "<line>:<version value>" for the version: input of every install-qt-action step. A step
    # ends where the next list item at the same or a shallower indent starts.
    steps="$(awk '
        function indent(s) { match(s, /^ */); return RLENGTH }
        /uses:[[:space:]]*jurplel\/install-qt-action@/ {
            in_step = 1; step_indent = index($0, "uses:") - 1; found = 0; start = NR; next
        }
        in_step && /^[[:space:]]*-[[:space:]]/ && indent($0) < step_indent {
            if (!found) print start ":<none>"
            in_step = 0
        }
        in_step && /^[[:space:]]+version:/ {
            line = $0; sub(/^[[:space:]]+version:[[:space:]]*/, "", line)
            print NR ":" line; found = 1
        }
        END { if (in_step && !found) print start ":<none>" }
    ' "${wf}")"
    [ -n "${steps}" ] || continue

    pinned=0
    while IFS= read -r entry; do
        line="${entry%%:*}"
        value="${entry#*:}"
        case "${value}" in
            *'# qt-version: probe'*) continue ;;
        esac
        value="${value%%#*}"
        value="$(printf '%s' "${value}" | sed -e 's/[[:space:]]*$//')"
        # shellcheck disable=SC2016 # the literal workflow expression, not a shell expansion
        if [ "${value}" != '${{ env.QT_VERSION }}' ]; then
            drift "${wf}:${line}: install-qt-action version is '${value}', expected '\${{ env.QT_VERSION }}'"
        fi
        pinned=1
    done <<<"${steps}"

    if [ "${pinned}" -eq 1 ] && ! grep -q '\.github/qt-version' "${wf}"; then
        drift "${wf}: installs Qt but never reads ${VERSION_FILE} into QT_VERSION"
    fi
done

# 2. No literal Qt versions or distro Qt anywhere in CI. Comment lines are skipped; the canary's
#    probe line is allowed by its marker.
LITERAL='QT_VERSION[[:space:]]*[:=][[:space:]]*["'"'"']?[0-9]|Qt/[0-9]+\.[0-9]+\.[0-9]+|qt6_[0-9]{3,}|qt6-base-dev|qt6-base-private-dev'
for file in "${WORKFLOWS[@]}" .github/scripts/*.sh; do
    [ -f "${file}" ] || continue
    while IFS= read -r hit; do
        [ -n "${hit}" ] || continue
        text="${hit#*:}"
        case "${text}" in
            *'# qt-version: probe'*) continue ;;
        esac
        if printf '%s\n' "${text}" | grep -qE '^[[:space:]]*#'; then
            continue
        fi
        # The Flatpak image is checked by its own rule below.
        if printf '%s\n' "${text}" | grep -qE 'flatpak-github-actions:kde-'; then
            continue
        fi
        drift "${file}:${hit%%:*}: hard-coded Qt version or distro Qt: $(printf '%s' "${text}" | sed -e 's/^[[:space:]]*//')"
    done < <(grep -nE "${LITERAL}" "${file}" || true)
done

# 3. The TSan qtbase build reads the file and has a checksum for this version.
TSAN=scripts/build-qtbase-tsan.sh
if ! grep -q '\.github/qt-version' "${TSAN}"; then
    drift "${TSAN}: does not read ${VERSION_FILE}"
fi
if ! grep -qE "^[[:space:]]*${QT_VERSION//./\\.}\)" "${TSAN}"; then
    drift "${TSAN}: no sha256 for qtbase ${QT_VERSION} in its checksum table"
fi

# 4. Flatpak: runtime branch and build image follow major.minor.
FLATPAK_WF=.github/workflows/_build-flatpak.yml
image_minor="$(sed -n 's/.*flatpak-github-actions:kde-\([0-9][0-9]*\.[0-9][0-9]*\).*/\1/p' "${FLATPAK_WF}" | head -1)"
if [ "${image_minor}" != "${QT_MINOR}" ]; then
    drift "${FLATPAK_WF}: build image is kde-${image_minor:-<missing>}, expected kde-${QT_MINOR}"
fi
MANIFEST=flatpak/io.github.mikeg_dal.QK4.json
runtime_minor="$(sed -n 's/.*"runtime-version":[[:space:]]*"\([^"]*\)".*/\1/p' "${MANIFEST}" | head -1)"
if [ "${runtime_minor}" != "${QT_MINOR}" ]; then
    drift "${MANIFEST}: runtime-version is '${runtime_minor:-<missing>}', expected '${QT_MINOR}'"
fi
while IFS= read -r hit; do
    [ -n "${hit}" ] || continue
    ref="$(printf '%s\n' "${hit}" | grep -oE 'org\.kde\.(Platform|Sdk)//[0-9.]+' | head -1)"
    if [ "${ref##*//}" != "${QT_MINOR}" ]; then
        drift "README.md:${hit%%:*}: ${ref}, expected //${QT_MINOR}"
    fi
done < <(grep -nE 'org\.kde\.(Platform|Sdk)//[0-9]' README.md || true)

# 5. Documented install paths.
for doc in README.md CLAUDE.md; do
    [ -f "${doc}" ] || continue
    while IFS= read -r hit; do
        [ -n "${hit}" ] || continue
        found="$(printf '%s\n' "${hit#*:}" | grep -oE 'Qt/[0-9]+\.[0-9]+\.[0-9]+/' | sort -u)"
        while IFS= read -r path; do
            if [ "${path}" != "Qt/${QT_VERSION}/" ]; then
                drift "${doc}:${hit%%:*}: documents ${path}, expected Qt/${QT_VERSION}/"
            fi
        done <<<"${found}"
    done < <(grep -nE 'Qt/[0-9]+\.[0-9]+\.[0-9]+/' "${doc}" || true)
done

if [ "${FAIL}" -ne 0 ]; then
    echo
    echo "The Qt version is ${QT_VERSION} (${VERSION_FILE}). Change it there and make the places above follow."
    exit 1
fi
echo "Qt ${QT_VERSION}: every workflow, script and documented path agrees with ${VERSION_FILE}."
