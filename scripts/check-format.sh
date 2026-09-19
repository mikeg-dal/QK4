#!/usr/bin/env bash
#
# Run the EXACT formatting check CI runs, locally, before pushing.
#
# WHY THIS EXISTS, AND WHY IT PINS A PATCH VERSION:
#
# .github/workflows/ci.yml installs Ubuntu's `clang-format-18`, which on ubuntu-latest is
# 18.1.3. Homebrew's llvm@18 ships 18.1.8. Those two DISAGREE about real code: 18.1.8 accepts
# line wrapping that 18.1.3 rejects. So "clang-format --dry-run --Werror passed on my machine"
# means nothing unless it was the same patch version, and the first anyone hears about it is a
# red check on a pull request.
#
# Pinning the major alone is not enough for the same reason. If CI's version is ever bumped,
# change REQUIRED_VERSION here in the same commit as the workflow, and expect a repo-wide
# reformat.
#
# Usage:
#   scripts/check-format.sh              check every file, exactly as CI does
#   scripts/check-format.sh --fix        rewrite the offending files in place
#   scripts/check-format.sh --staged     check only what is staged (fast pre-commit)
#   scripts/check-format.sh --bootstrap  install the pinned formatter into .format-venv/
#
# Override the binary with CLANG_FORMAT=/path/to/clang-format if you have 18.1.3 elsewhere.

set -euo pipefail

REQUIRED_VERSION="18.1.3"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENV_DIR="${REPO_ROOT}/.format-venv"
VENV_BIN="${VENV_DIR}/bin/clang-format"

cd "${REPO_ROOT}"

bootstrap() {
    # The clang-format PyPI package ships a real prebuilt binary per version, which is the only
    # way to get an exact patch version on macOS without building LLVM. It goes in a venv inside
    # the repo (gitignored) rather than on PATH, so it cannot shadow anyone's system toolchain.
    echo "Installing clang-format ${REQUIRED_VERSION} into ${VENV_DIR}"
    python3 -m venv "${VENV_DIR}" --clear
    "${VENV_DIR}/bin/pip" install --quiet "clang-format==${REQUIRED_VERSION}"
    echo "Done: $("${VENV_BIN}" --version)"
}

version_of() { "$1" --version 2>/dev/null | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1; }

# First match wins: an explicit override, then the pinned venv, then whatever is on PATH. The
# PATH candidates are a convenience for people who already have the right version; they are
# version-checked below like everything else.
find_formatter() {
    local candidate
    for candidate in "${CLANG_FORMAT:-}" "${VENV_BIN}" "$(command -v clang-format-18 || true)" \
                     "$(command -v clang-format || true)"; do
        [ -n "${candidate}" ] && [ -x "${candidate}" ] && { echo "${candidate}"; return 0; }
    done
    return 1
}

MODE="check"
for arg in "$@"; do
    case "${arg}" in
        --fix)       MODE="fix" ;;
        --staged)    MODE="staged" ;;
        --bootstrap) bootstrap; exit 0 ;;
        -h|--help)   sed -n '2,26p' "$0" | sed 's|^# \{0,1\}||'; exit 0 ;;
        *)           echo "unknown option: ${arg}" >&2; exit 2 ;;
    esac
done

if ! FORMATTER="$(find_formatter)"; then
    echo "No clang-format found. Run: scripts/check-format.sh --bootstrap" >&2
    exit 2
fi

FOUND_VERSION="$(version_of "${FORMATTER}")"
if [ "${FOUND_VERSION}" != "${REQUIRED_VERSION}" ]; then
    # Loud, and fatal. A near-miss version is WORSE than no check: it reports success on code CI
    # will reject, which is the exact failure this script exists to prevent.
    echo "clang-format version mismatch" >&2
    echo "  found:    ${FOUND_VERSION:-unknown}  (${FORMATTER})" >&2
    echo "  CI uses:  ${REQUIRED_VERSION}" >&2
    echo "" >&2
    echo "These versions disagree about real code, so this check would be misleading." >&2
    echo "Run: scripts/check-format.sh --bootstrap" >&2
    exit 2
fi

# The same traversal CI uses, so the file set cannot drift apart from it.
# -print0/-0 rather than a bare pipe: CI gets away without it because no path here has a space,
# but a check that silently skips a file is the thing being guarded against.
if [ "${MODE}" = "staged" ]; then
    mapfile -t FILES < <(git diff --cached --name-only --diff-filter=ACMR -- '*.cpp' '*.h' | grep -E '^(src|tests)/' || true)
    [ "${#FILES[@]}" -eq 0 ] && { echo "No staged C++ files."; exit 0; }
else
    mapfile -t FILES < <(find src tests \( -name '*.cpp' -o -name '*.h' \) -print | sort)
fi

if [ "${MODE}" = "fix" ]; then
    printf '%s\0' "${FILES[@]}" | xargs -0 "${FORMATTER}" -i
    echo "Reformatted ${#FILES[@]} file(s) with clang-format ${FOUND_VERSION}."
    git --no-pager diff --stat -- src tests
    exit 0
fi

if printf '%s\0' "${FILES[@]}" | xargs -0 "${FORMATTER}" --dry-run --Werror; then
    echo "Formatting clean: ${#FILES[@]} file(s), clang-format ${FOUND_VERSION} (same as CI)."
else
    echo "" >&2
    echo "Formatting check FAILED - this is what CI will report." >&2
    echo "Fix with: scripts/check-format.sh --fix" >&2
    exit 1
fi
