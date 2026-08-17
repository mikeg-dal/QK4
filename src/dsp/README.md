# dsp/

GPU-accelerated spectrum + waterfall rendering via Qt RHI (Metal / DX / Vulkan / GL).

## Files

- `panadapter_rhi.{cpp,h}` — Main panadapter (spectrum + waterfall). Click-tune, passband overlays, DX spot overlays, TX markers. ~1870 LOC (naturally large — RHI pipeline + buffer management).
- `minipan_rhi.{cpp,h}` — Per-VFO mini-pan widget. ~1065 LOC.
- `panadapter_constants.h` — Shared rendering parameters: RTTY shift, grid cell size, line widths, dash pattern. Texture and history dimensions are **not** here; they live in `panadapter_rhi.h` (`BASE_TEXTURE_WIDTH`, `MAX_WATERFALL_HISTORY`).
- `spectrumscale.{cpp,h}` — `SpectrumScale::` namespace. The amplitude axis: dBm → chart fraction, S-unit dBm values, and which values earn a label at a given size. Pure arithmetic, no widget dependency, covered by `test_spectrumscale`.
- `rhi_utils.h` — Shared RHI helpers (color LUT size, texture builders).
- `shaders/` — 4 shader pairs (vert/frag): spectrum, spectrum_fill, waterfall, overlay. Compiled at build time via `qt6_add_shaders()` in `CMakeLists.txt`.

## Amplitude axis

The radio owns it: `#REF` is the bottom of the window in dBm, `#SCL` its height in dB, so the
visible range is `[REF, REF + SCALE]` and a bin's height is `(dBm - REF) / SCALE`. Bin dBm is
`raw_byte - RhiUtils::K4_DBM_OFFSET`.

Nothing is subtracted from that. The renderer used to remove a smoothed per-frame minimum — an
automatic noise-floor tracker — which moved the trace by roughly 20% of the chart height depending
on band conditions, so signal height tracked the noise rather than the signal, and no dBm or S-unit
label could be correct. Auto-reference is the radio's job: in `AR=Auto` the K4 adjusts REF itself
and reports it, which arrives as `#REF` like any other change.

**Do not reintroduce a display-side baseline.** The trace, the waterfall and the scale labels all
route through `SpectrumScale::normalizedForDb` specifically so they cannot disagree about where a
given dBm belongs.

S-units follow the amateur convention — S9 = -73 dBm, 6 dB per unit, round decades above S9 — and
are drawn at their true dBm positions rather than at even divisions of the widget. Labels outside
the window are omitted, never clamped; clamping is what once printed S1 at two different heights.

With `qk4.pan.diag` enabled a `CALIB` line compares the radio's reported noise floor against our own
minimum bin. A persistent gap means `K4_DBM_OFFSET` is wrong and every value on the scale is off by
that amount.

## Sampling

Both the spectrum trace and the waterfall read the same raw bins through a `Linear` sampler, and
one line of shader math is the entire difference between them:

- `spectrum_fill.frag` → `texU = (binOffset + u * binCount + 0.5) / textureWidth`. Not floored, so
  samples land between texel centres and hardware bilinear blends adjacent bins — a smooth trace.
- `waterfall.frag` → `texU = (binOffset + floor(binIndex) + 0.5) / textureWidth`. The `floor()`
  snaps to an exact texel centre, where bilinear returns that texel unchanged, so the waterfall is
  effectively nearest-neighbour and keeps hard bin edges (`8ad70ea`). That `floor()` also fixes a
  C++ integer vs GLSL float division mismatch that blurred 5/8/11 kHz spans.

There is no Lanczos. A 6-tap Lanczos-3 kernel existed briefly (`050eb4f`) and was replaced by
hardware bilinear in `f274687` — equivalent quality on a wide texture, less shader complexity.

Neither axis stores anything downsampled: rows are one K4 frame each and hold all 1024 tier bins, so
resampling on resize cannot recover detail that is not there. The mini-pan is the opposite — it
averages 1024 bins into a 512-wide texture (`minipan_rhi.cpp`), and genuinely does discard data.

## Ownership

Owned by `SpectrumController` (`controllers/spectrumcontroller.cpp`).

## Data flow

K4 spectrum packets → `Protocol` → `ConnectionController` → `SpectrumController` → `PanadapterRhiWidget` / `MinipanRhiWidget`.

Spectrum data routing includes AR (auto-reference), #SCL (scale), #SPN (span), #REF (reference level), #WFC (waterfall color), #WFH (waterfall history) — all stored on `SpectrumDisplayState` in `models/radiostate/`.

## See also

- `docs/k4-protocol-quirks.md` → "PAN spectrum tiers" — tier boundaries, sample rates, bin counts,
  and why the waterfall is cleared when the radio changes tier.
- `docs/k4-protocol-quirks.md` → "Span dial skips 6 kHz going up".
- `controllers/spectrumcontroller.cpp` — all wiring + click-tune logic.
