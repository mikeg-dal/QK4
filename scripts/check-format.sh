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
# cost a red CI check on a PR and a red `development` before ci-checks.yml was pinned to an exact version
# in 32734a1. Pinning the major alone was not enough; neither is "some clang-format 18".
#
# The pinned version is READ FROM ci-checks.yml rather than written here, so this file cannot disagree
# with what CI installs. There is nothing to keep in sync.
#
# Usage:
#   scripts/check-format.sh              check every file CI checks
#   scripts/check-format.sh --fix        rewrite the offending files
#   scripts/check-format.sh --staged     check the STAGED content of staged files (pre-commit)
#   scripts/check-format.sh --bootstrap  install the pinned version into .format-venv/
#
# Exit codes, and nothing else: 0 clean, 1 formatting violations, 2 cannot check (no usable
# clang-format, bad usage, not in a repo, or the pin could not be read).
#
# As a pre-commit hook, write these two lines to .git/hooks/pre-commit and chmod +x it:
#   #!/bin/sh
#   exec "$(git rev-parse --show-toplevel)/scripts/check-format.sh" --staged
#
# Do NOT symlink this file to .git/hooks/pre-commit. A symlink runs, but it cannot pass --staged,
# so every commit pays for a whole-tree check. (It also used to fail outright: the repo root came
# from $0, which is not symlink-resolved, so the root resolved to <repo>/.git and every commit was
# blocked by a false failure. The root now comes from git, but the two-line hook is still the form
# to use.)
#
# Override with CLANG_FORMAT=/path/to/clang-format if you keep the pinned version somewhere
# unusual. An override that is missing or the wrong version is a hard error, not a hint.

set -euo pipefail

# Absolute, captured before any cd, so --help can read this header even when the script is reached
# through a symlink or from another directory.
SCRIPT_PATH="${BASH_SOURCE[0]}"
case "${SCRIPT_PATH}" in
    /*) ;;
    *) SCRIPT_PATH="$(pwd)/${SCRIPT_PATH}" ;;
esac

usage() {
    # Prints the header block by finding where it ends, not from a hardcoded line range: the old
    # `sed -n '2,27p'` was correct only until someone edited the comment above it.
    awk 'NR == 1 { next } /^#/ { sub(/^#[[:space:]]?/, ""); print; next } { exit }' "${SCRIPT_PATH}"
}

WANT_FIX=0
WANT_STAGED=0
WANT_BOOTSTRAP=0
for arg in "$@"; do
    case "${arg}" in
        --fix) WANT_FIX=1 ;;
        --staged) WANT_STAGED=1 ;;
        --bootstrap) WANT_BOOTSTRAP=1 ;;
        -h | --help)
            usage
            exit 0
            ;;
        *)
            echo "unknown option: ${arg}" >&2
            echo "Try: scripts/check-format.sh --help" >&2
            exit 2
            ;;
    esac
done

# --staged names a set of files; --fix rewrites files. Combining them reads like "fix what I
# staged", which is not what happened: the last flag won, so `--staged --fix` reformatted all 354
# files in the tree from a command a reasonable person would put in a pre-commit hook. Refuse
# rather than silently pick one.
if [ "${WANT_FIX}" -eq 1 ] && [ "${WANT_STAGED}" -eq 1 ]; then
    echo "--fix and --staged cannot be combined." >&2
    echo "  --staged checks the staged content of staged files, and changes nothing." >&2
    echo "  --fix rewrites files in the working tree." >&2
    echo "Run --staged to see what is wrong, then --fix, then re-stage." >&2
    exit 2
fi

# From git, not from ${BASH_SOURCE}: BASH_SOURCE is not symlink-resolved, so a hook symlinked into
# .git/hooks/ used to compute <repo>/.git as the repo root, find no src/ or tests/, and report a
# formatting failure on a clean tree.
if ! REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null)" || [ -z "${REPO_ROOT}" ]; then
    echo "Not inside a git repository, so the files to check cannot be located." >&2
    exit 2
fi
cd "${REPO_ROOT}"

VENV_DIR="${REPO_ROOT}/.format-venv"
CI_WORKFLOW=".github/workflows/ci-checks.yml"

# SINGLE SOURCE OF TRUTH. This used to be a literal here, which made it a fourth hand-maintained
# copy of the version (ci-checks.yml, CONVENTIONS.md twice, and this one) inside a script whose whole
# reason to exist is that copies of a pinned version drift apart. Reading it from the workflow
# means the guard below cannot disagree with what CI installs.
# Anchored to the `run:` line rather than matching anywhere, so a comment that happens to mention
# the package cannot be picked up as the pin.
REQUIRED_VERSION="$(sed -n 's/^[[:space:]]*run:[[:space:]]*pipx install clang-format==\([0-9][0-9.]*\).*/\1/p' "${CI_WORKFLOW}" 2>/dev/null | head -1 || true)"
if [ -z "${REQUIRED_VERSION}" ]; then
    echo "Could not read the pinned clang-format version from ${CI_WORKFLOW}." >&2
    echo "Expected a line matching: pipx install clang-format==<version>" >&2
    echo "If that step changed shape, this script needs updating to match it." >&2
    exit 2
fi
REQUIRED_MAJOR="${REQUIRED_VERSION%%.*}"

# A binary that exists but cannot run - wrong architecture, broken install, missing library - used
# to kill the script with no message at all, because this was a bare pipeline under `set -e` and
# `pipefail` assigned straight to a variable. Now it fails as a value the callers can report on.
version_of() {
    local raw parsed
    raw="$("$1" --version 2>/dev/null)" || return 1
    parsed="$(printf '%s\n' "${raw}" | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1 || true)"
    [ -n "${parsed}" ] || return 1
    printf '%s\n' "${parsed}"
}

# Search order, most specific first. The llvm@N and clang-format-N paths follow the major read from
# ci-checks.yml, so bumping the pin moves the search with it.
formatter_candidates() {
    printf '%s\n' \
        "${VENV_DIR}/bin/clang-format" \
        "${VENV_DIR}/Scripts/clang-format.exe" \
        "/opt/homebrew/opt/llvm@${REQUIRED_MAJOR}/bin/clang-format" \
        "/usr/local/opt/llvm@${REQUIRED_MAJOR}/bin/clang-format" \
        "$(command -v "clang-format-${REQUIRED_MAJOR}" 2>/dev/null || true)" \
        "$(command -v clang-format 2>/dev/null || true)"
}

# Prints the path and returns 0; returns 1 if nothing on this machine is the pinned version;
# returns 2 if an explicit CLANG_FORMAT was given and is unusable (message already printed).
#
# WHY THE VERSION TEST IS INSIDE THE LOOP: it used to return the first candidate that merely
# EXISTED and version-check it afterwards, which is not what the comment claimed. On a machine with
# both an apt clang-format-18 (18.1.3) and a pipx 18.1.8, the apt one was picked and the run
# refused - telling the user to bootstrap a version that was already installed further down the
# very list being searched.
find_formatter() {
    local candidate ver
    if [ -n "${CLANG_FORMAT:-}" ]; then
        # An explicit override is an instruction, not a hint. Skipping a bad one and checking with
        # some other binary is the same class of quiet lie this script exists to prevent - and it
        # is what made the "18.1.3 via CLANG_FORMAT is refused" claim in #149 unreproducible: the
        # path did not exist, so the override was dropped and an unrelated 18.1.8 was used.
        if [ ! -x "${CLANG_FORMAT}" ]; then
            echo "CLANG_FORMAT is set to '${CLANG_FORMAT}', which is not an executable file." >&2
            return 2
        fi
        ver="$(version_of "${CLANG_FORMAT}" || true)"
        if [ "${ver}" != "${REQUIRED_VERSION}" ]; then
            echo "CLANG_FORMAT is set to '${CLANG_FORMAT}' (version ${ver:-unreadable})," >&2
            echo "but ${CI_WORKFLOW} pins ${REQUIRED_VERSION}." >&2
            return 2
        fi
        printf '%s\n' "${CLANG_FORMAT}"
        return 0
    fi
    while IFS= read -r candidate; do
        [ -n "${candidate}" ] || continue
        [ -x "${candidate}" ] || continue
        [ "$(version_of "${candidate}" || true)" = "${REQUIRED_VERSION}" ] || continue
        printf '%s\n' "${candidate}"
        return 0
    done < <(formatter_candidates)
    return 1
}

bootstrap() {
    # The clang-format PyPI package ships the same upstream LLVM build CI installs, so this is the
    # identical binary rather than merely the same version number. In a venv inside the repo, not
    # on PATH, so it cannot shadow anyone's system toolchain.
    echo "Installing clang-format ${REQUIRED_VERSION} into ${VENV_DIR}"
    python3 -m venv "${VENV_DIR}" --clear

    # POSIX venvs put these in bin/, Windows venvs in Scripts/. Checking both is why --bootstrap
    # has a chance on Windows at all; it is still untested there.
    local pip bin
    if [ -x "${VENV_DIR}/bin/pip" ]; then
        pip="${VENV_DIR}/bin/pip"
        bin="${VENV_DIR}/bin/clang-format"
    elif [ -x "${VENV_DIR}/Scripts/pip.exe" ]; then
        pip="${VENV_DIR}/Scripts/pip.exe"
        bin="${VENV_DIR}/Scripts/clang-format.exe"
    else
        echo "python3 -m venv produced no pip under ${VENV_DIR}/bin or ${VENV_DIR}/Scripts." >&2
        exit 2
    fi
    "${pip}" install --quiet "clang-format==${REQUIRED_VERSION}"
    echo "Done: $("${bin}" --version)"
}

if [ "${WANT_BOOTSTRAP}" -eq 1 ]; then
    bootstrap
    exit 0
fi

if FORMATTER="$(find_formatter)"; then
    :
else
    find_status=$?
    # 2 means an explicit CLANG_FORMAT was rejected and has already said why.
    [ "${find_status}" -eq 2 ] && exit 2
    echo "No clang-format ${REQUIRED_VERSION} found - the version ${CI_WORKFLOW} pins." >&2
    echo "Looked at:" >&2
    while IFS= read -r candidate; do
        [ -n "${candidate}" ] || continue
        [ -x "${candidate}" ] || continue
        echo "  ${candidate}  ->  $(version_of "${candidate}" || echo 'will not run')" >&2
    done < <(formatter_candidates)
    echo "" >&2
    echo "Run: scripts/check-format.sh --bootstrap" >&2
    exit 2
fi

# By construction: find_formatter only ever returns an exact match, so there is no separate
# mismatch check to get out of step with the search.
FOUND_VERSION="${REQUIRED_VERSION}"

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
if [ "${WANT_STAGED}" -eq 1 ]; then
    while IFS= read -r f; do FILES+=("$f"); done < <(git diff --cached --name-only --diff-filter=ACMR -- '*.cpp' '*.h' | grep -E '^(src|tests)/' || true)
else
    while IFS= read -r f; do FILES+=("$f"); done < <(find src tests \( -name '*.cpp' -o -name '*.h' \) -print | sort)
fi

# WHY THIS GUARD AND NOT A BARE EXPANSION: "${FILES[@]}" on an empty array is a fatal `unbound
# variable` under `set -u` in bash 3.2 (and every bash up to 4.3). An empty list used to abort the
# script with a raw bash error and no explanation, which is what the broken symlink hook actually
# hit before printing "this is what CI will report" about a clean tree.
if [ "${#FILES[@]}" -eq 0 ]; then
    if [ "${WANT_STAGED}" -eq 1 ]; then
        echo "No staged C++ files."
        exit 0
    fi
    echo "Found no .cpp/.h files under src/ and tests/ in ${REPO_ROOT}." >&2
    echo "That is not a formatting result - the file list came back empty, so nothing was checked." >&2
    exit 2
fi

if [ "${WANT_FIX}" -eq 1 ]; then
    printf '%s\0' "${FILES[@]}" | xargs -0 "${FORMATTER}" -i
    echo "Reformatted ${#FILES[@]} file(s) with clang-format ${FOUND_VERSION}."
    git --no-pager diff --stat -- src tests
    exit 0
fi

if [ "${WANT_STAGED}" -eq 1 ]; then
    # WHY THE STAGED BLOB AND NOT THE FILE ON DISK: a hook has to judge what is about to be
    # committed. Reading the working tree gave a false PASS whenever a misformatted version was
    # staged and the worktree was then tidied - the commit went in misformatted and CI caught it -
    # and a false FAILURE when the worktree held unstaged work that was not being committed.
    #
    # `git show ":path"` is the staged blob. --assume-filename makes clang-format resolve
    # .clang-format from that path (verified: src/ gets the repo's 4-space style, not clang-format's
    # 2-space default), and the sed puts the real path back into diagnostics that would otherwise
    # all say "<stdin>".
    staged_rc=0
    for f in "${FILES[@]}"; do
        if ! git show ":${f}" | "${FORMATTER}" --assume-filename="${f}" --dry-run --Werror 2>&1 | sed "s|^<stdin>|${f}|" >&2; then
            staged_rc=1
        fi
    done
    if [ "${staged_rc}" -eq 0 ]; then
        echo "Formatting clean: ${#FILES[@]} staged file(s), clang-format ${FOUND_VERSION} (same as CI)."
        exit 0
    fi
    echo "" >&2
    echo "Staged content is misformatted - this is what CI will report." >&2
    echo "Fix with: scripts/check-format.sh --fix   then re-stage." >&2
    exit 1
fi

if printf '%s\0' "${FILES[@]}" | xargs -0 "${FORMATTER}" --dry-run --Werror; then
    echo "Formatting clean: ${#FILES[@]} file(s), clang-format ${FOUND_VERSION} (same as CI)."
else
    echo "" >&2
    echo "Formatting check FAILED - this is what CI will report." >&2
    echo "Fix with: scripts/check-format.sh --fix" >&2
    exit 1
fi
