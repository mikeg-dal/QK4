# Workflows

Every branch carries the same set of files. A workflow does not belong to a branch; it declares
which events start it. The file name therefore says **when it runs**, and the run title says
**which branch or PR it ran on**.

## Naming

GitHub allows no subdirectories here, so the prefix is the grouping. The display `name:` repeats it,
because the Actions sidebar sorts by display name, not by file.

| Prefix | Runs when | Display name |
|---|---|---|
| `ci-` | every PR, and every push to `development` / `main` | `CI · …` |
| `build-` | on demand, and on push to `main` | `Build · …` |
| `release.yml` | a `v*` tag (dispatch is a rehearsal that publishes nothing) | `Release` |
| `scheduled-` | a cron schedule | `Scheduled · …` |
| `repo-` | issue and comment events; never builds code | `Repo · …` |
| `_build-` | only when another workflow calls it (`workflow_call`) | `Reusable · …` |

Workflows started by a PR or a push set `run-name` so the run list reads `PR #153 → development: …`
or `development: <commit subject>`.

## Inventory

| File | What it does |
|---|---|
| `ci-checks.yml` | clang-format gate, unit tests, ASAN + UBSAN |
| `ci-codeql.yml` | CodeQL static analysis → Security tab; also weekly |
| `build-signed.yml` | Signed, notarized artifacts for every platform, one run page. Dispatch it on `development` for a tester build. No Release, no gh-pages |
| `release.yml` | The same builders, then the GitHub Release and gh-pages |
| `scheduled-sanitizers.yml` | Weekly ThreadSanitizer; failures go to a sticky issue |
| `scheduled-qt611-canary.yml` | Monthly Qt 6.11 install attempt on Windows; delete once the bump lands |
| `repo-issue-triage.yml` | First-pass triage of new issues |
| `repo-comment-guard.yml` | Hides disguised-download spam from non-maintainers |
| `_build-macos.yml` `_build-windows.yml` `_build-pi.yml` `_build-flatpak.yml` | The one definition of each platform's build, packaging and packaged-artifact verification |

## Renaming a workflow

A rename starts a new entry in the Actions sidebar; the old file's runs stay under the old name.
Update the README badge, any `gh run list --workflow <file>` in docs, and any required status check
in branch protection. CodeQL's results survive a rename only because `ci-codeql.yml` pins its
`category`; keep that line.
