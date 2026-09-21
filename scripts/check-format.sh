#!/usr/bin/env bash
#
# Run the exact formatting check CI runs, locally, before committing.
#
# WHY A SCRIPT AND NOT THE COMMANDS IN CONVENTIONS.md: those work, and on a Mac with Homebrew's
# llvm@18 they are correct. They hardcode `/opt/homebrew/opt/llvm@18/bin/clang-format`, which does
# not exist on Linux, on Windows, on an Intel Mac (`/usr/local/...`), or for anyone who installed
# LLVM another way. This finds the right binary wherever it is, and refuses to run if it is the
# wrong version rather than reporting a misleading pass.
#
# The version matters more than it looks. clang-format's line-breaking heuristics change between
# PATCH releases, so 18.1.3 and 18.1.8 disagree about real files in this repo - a difference that
# cost a red CI check on a PR and a red `development` before ci.yml was pinned to an exact version
# in 32734a1. Pinning the major alone was not enough; neither is "some clang-format 18".
#
# Usage:
#   scripts/check-format.sh              check every file CI checks
#   scripts/check-format.sh --fix        rewrite the offending files
#   scripts/check-format.sh --staged     check only what is staged (for a pre-commit hook)
#   scripts/check-format.sh --bootstrap  install the pinned version into .format-venv/
#
# As a pre-commit hook:
#   ln -s ../../scripts/check-format.sh .git/hooks/pre-commit   # then edit to pass --staged
# or in .git/hooks/pre-commit:
#   exec ./scripts/check-format.sh --staged
#
# Override with CLANG_FORMAT=/path/to/clang-format if you keep 18.1.8 somewhere unusual.

set -euo pipefail

# Must match `pipx install clang-format==X` in .github/workflows/ci.yml. Bumping it means a
# repo-wide reformat commit and the same bump in CONVENTIONS.md, in the same change.
REQUIRED_VERSION="18.1.8"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENV_DIR="${REPO_ROOT}/.format-venv"
VENV_BIN="${VENV_DIR}/bin/clang-format"

cd "${REPO_ROOT}"

bootstrap() {
    # The clang-format PyPI package ships the same upstream LLVM build CI installs, so this is the
    # identical binary rather than merely the same version number. In a venv inside the repo, not
    # on PATH, so it cannot shadow anyone's system toolchain.
    echo "Installing clang-format ${REQUIRED_VERSION} into ${VENV_DIR}"
    python3 -m venv "${VENV_DIR}" --clear
    "${VENV_DIR}/bin/pip" install --quiet "clang-format==${REQUIRED_VERSION}"
    echo "Done: $("${VENV_BIN}" --version)"
}

version_of() { "$1" --version 2>/dev/null | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1; }

# First match wins: an explicit override, the pinned venv, then the usual install locations. The
# Homebrew paths are the ones CONVENTIONS.md names, Apple Silicon and Intel; every candidate is
# version-checked below, so a wrong one here is caught rather than used.
find_formatter() {
    local candidate
    for candidate in "${CLANG_FORMAT:-}" "${VENV_BIN}" \
                     /opt/homebrew/opt/llvm@18/bin/clang-format \
                     /usr/local/opt/llvm@18/bin/clang-format \
                     "$(command -v clang-format-18 || true)" \
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
        -h|--help)   sed -n '2,27p' "$0" | sed 's|^# \{0,1\}||'; exit 0 ;;
        *)           echo "unknown option: ${arg}" >&2; exit 2 ;;
    esac
done

if ! FORMATTER="$(find_formatter)"; then
    echo "No clang-format found. Run: scripts/check-format.sh --bootstrap" >&2
    exit 2
fi

FOUND_VERSION="$(version_of "${FORMATTER}")"
if [ "${FOUND_VERSION}" != "${REQUIRED_VERSION}" ]; then
    # Loud, and fatal. A near-miss version is WORSE than no check: it green-lights code CI will
    # reject, which is the failure this exists to prevent.
    echo "clang-format version mismatch" >&2
    echo "  found:    ${FOUND_VERSION:-unknown}  (${FORMATTER})" >&2
    echo "  CI uses:  ${REQUIRED_VERSION}" >&2
    echo "" >&2
    echo "These versions disagree about real files in this repo, so this check would mislead." >&2
    echo "Run: scripts/check-format.sh --bootstrap" >&2
    exit 2
fi

# The same traversal CI uses, so the file set cannot drift from it.
#
# WHY `while read` AND NOT `mapfile`: mapfile and readarray arrived in bash 4.0. macOS ships
# 3.2.57 - the last GPLv2 release, frozen since 2007 - and on a stock Mac it is the only bash
# there is. The shebang is `env bash`, which is the portable choice and is also what hides this:
# anyone with Homebrew's bash 5 ahead of /bin/bash never sees it. A stock Mac gets
# "mapfile: command not found" and exit 127, which is not one of the exit codes above. This form
# behaves identically on 3.2 and 5.x.
#
# Newline-delimited, the same way CI enumerates these files. The `printf '%s\0' | xargs -0` below
# is what stops a path with a space being split; a path with an embedded newline would defeat
# both, and there are none in this repo.
FILES=()
if [ "${MODE}" = "staged" ]; then
    while IFS= read -r f; do FILES+=("$f"); done < <(git diff --cached --name-only --diff-filter=ACMR -- '*.cpp' '*.h' | grep -E '^(src|tests)/' || true)
    [ "${#FILES[@]}" -eq 0 ] && { echo "No staged C++ files."; exit 0; }
else
    while IFS= read -r f; do FILES+=("$f"); done < <(find src tests \( -name '*.cpp' -o -name '*.h' \) -print | sort)
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
