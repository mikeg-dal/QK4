# ui/

UI widgets, popups, overlays, dialogs, and styling infrastructure. Files are grouped by role — the directory tells you what kind of thing a file is.

## Subdirectories

| Directory | Owns |
|---|---|
| `styling/` | Infrastructure used by every UI file: tokens in `k4constants.h` (colors, dimensions, type roles), `K4Styles` helpers (fonts, stylesheets) and `K4Glyphs` (drawn status-bar glyphs). |
| `dialogs/` | Modal-ish full windows: `OptionsDialog`, `RadioManagerDialog`, `MacroDialog`, `TextDecodeWindow`. |
| `pages/` | `OptionsDialog` tab pages: about, station, display, audio input/output, rig control, TCI server, HaliKey (CW keyer), K-Pod, KPA1500, DX cluster, diagnostics. Plus `OptionsPageLayout`, the layout standard every page uses. |
| `popups/` | `K4PopupBase` and the popups built on it — band, mode, Fn, display, button row, RX EQ, antenna config, line in/out, mic input/config, VOX, SSB BW, keying weight, confirm, software list. |
| `overlays/` | Overlays: menu (covers the spectrum area), mon/bal (side-control) and their base, mouse VFO indicator and DX spots (panadapter). |
| `widgets/` | The everything-else bucket — VFO widgets, meters, menu bars, side panels and their buttons, memory key row, KPA1500 mini-panel, status bar, notification. |

## Include convention

Explicit paths everywhere: `#include "ui/popups/bandpopupwidget.h"` — never `#include "bandpopupwidget.h"`. Matches how `models/`, `network/`, `controllers/` are included and keeps cross-subdirectory references visible in the include line.

## Source of truth for styling

Values live in `ui/styling/k4constants.h` (tokens). Helpers live in `ui/styling/k4styles.h`. `docs/styling.md` is the styling guide: style-sheet cascade and precedence, the contrast policy, the type roles. Never inline hex colors or pixel font sizes; always use `K4Styles::Colors::*` and `K4Styles::Dimensions::*`.

## See also

- `PATTERNS.md` → "Adding a Popup Menu" (K4PopupBase recipe).
- `docs/styling.md` — styling guide.
- `ui/styling/k4constants.h` — every token value.
- `CONVENTIONS.md` → "Styling" section.
