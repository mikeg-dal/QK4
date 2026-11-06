# Summary

<!-- What does this PR do, and why? Link the issue it addresses. -->

Fixes #

## Test Plan

- [ ] `scripts/check-format.sh` passes (it uses the pinned clang-format; do not run plain `clang-format`)
- [ ] `cmake --build build` compiles cleanly
- [ ] `cmake --build build --target qk4_tests` builds every test
- [ ] `ctest --test-dir build --output-on-failure` passes
- [ ] Every new file is listed in its directory's `README.md`
- [ ] New/changed CAT command handlers have a test case in `tests/test_radiostate.cpp`
- [ ] Manually tested against K4 hardware (required for audio, CW keying, spectrum, PTT, or hardware-device changes — state what was tested)

## Notes

<!-- Commit messages follow conventional commits (feat/fix/refactor/perf/docs/chore/ci) —
     release notes are generated from them. Anything reviewers should know goes here. -->
