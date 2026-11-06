# ui/styling/

Shared styling infrastructure. Used by every other `ui/` subdirectory.

## Files

- `k4constants.h` — colour tokens (each hex written once), `Colors::Alpha::*`, dimensions and the six-role type scale (`FontSizeCaption` … `FontSizeDisplay`). No Qt includes. **Single source of truth for every visual constant.**
- `k4styles.{cpp,h}` — font helpers, `withAlpha()` / `cssRgba()`, stylesheet functions, `Dialog::` cached-QString helpers, `applicationStyleSheet()` (the app-wide form-control fallback).
- `k4glyphs.{cpp,h}` — procedurally drawn UI glyphs for the status bar and `PowerStatusButton`. Each factory returns a closure that renders the glyph in a supplied color, so glyphs follow the palette instead of shipping as fixed-color assets.

`K4PopupBase` is not here. It lives in `ui/popups/`.

## Rule

Never inline hex colors, `QColor(r, g, b)`, `Qt::white`/`Qt::black`, style-sheet `rgba()` or pixel font sizes in widget code. Always reference `K4Styles::Colors::*` and `K4Styles::Dimensions::*`. `tests/test_tokenstandard.cpp` fails the test run (ctest and CI) otherwise, and checks every text/background pair for WCAG 4.5:1.

## Reference

- `k4constants.h` — every value, with a comment per token. `rg 'constexpr const char \*' src/ui/styling/k4constants.h` lists the colour tokens.
- `docs/styling.md` — the styling guide: style-sheet cascade and precedence, the contrast policy, the type roles, and the tests that enforce them.
