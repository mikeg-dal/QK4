# utils/

Shared helper functions. Any helper needed in more than one translation unit goes here — Architecture Rule 1.

## Files

- `radioutils.{cpp,h}` — `RadioUtils::` namespace. Frequency → band (11 bands + gaps), band edges, tuning step tables, span-dial stepping with K4 quirks.
- `bandplan.{cpp,h}` — `BandPlan::` namespace. Segment lookup (CW / data / beacon / phone) per IARU region, driving the panadapter band-plan overlay. Region 4 is the US FCC layout with practical Extra-class mode edges. Covered by `test_bandplan`.
- `wheelaccumulator.{h}` — accumulates high-resolution trackpad / wheel deltas into discrete detents, so a fine-grained scroll device does not fire one tuning step per pixel.

## Rule 1 — No duplicated static functions

If you catch yourself copy-pasting a helper into a second `.cpp` file, promote it to `utils/` with a namespace. Copy-pasting is a defect; fix it immediately.

## Shape

Each utility is a free function or small namespace. Zero state, zero side effects, zero dependencies on Qt UI types.

## See also

- `CONVENTIONS.md` → Architecture Rule 1.
- `docs/k4-protocol-quirks.md` → "Span dial skips 6 kHz going up" — the K4 behaviour the span
  helpers model, including the one case they do not yet match.
