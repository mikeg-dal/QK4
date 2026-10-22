# dsp/

GPU-accelerated spectrum + waterfall rendering via Qt RHI (Metal / DX / Vulkan / GL).

## Files

- `panadapter_rhi.{cpp,h}` — Main panadapter (spectrum + waterfall). Click-tune, passband overlays, DX spot overlays, TX markers. ~1870 LOC (naturally large — RHI pipeline + buffer management).
- `minipan_rhi.{cpp,h}` — Per-VFO mini-pan widget. ~1065 LOC.
- `panadapter_constants.h` — Shared rendering parameters: RTTY shift, grid cell size, line widths, dash pattern. Texture and history dimensions are **not** here; they live in `panadapter_rhi.h` (`BASE_TEXTURE_WIDTH`, `MAX_WATERFALL_HISTORY`).
- `waterfallgeometry.{cpp,h}` — `WaterfallGeometry::` namespace. Which stored waterfall row each screen pixel shows: visible-row count, ring offsets, and the uniforms `waterfall.vert` consumes. Mirrors the shaders' V math so the mapping can be tested without a GPU; covered by `test_waterfallgeometry`.
- `spectrumscale.{cpp,h}` — `SpectrumScale::` namespace. The amplitude axis: dBm → chart fraction, S-unit dBm values, and which values earn a label at a given size. Pure arithmetic, no widget dependency, covered by `test_spectrumscale`.
- `bandplanstrip.{cpp,h}` — `BandPlanStrip::` namespace. Layout of the band-plan strip at the top of the panadapter: mode-segment blocks, out-of-band blocks past the band edge (so the strip always spans the full width), the pinned band tag, sticky mode labels and marker labels that never overlap. Pure arithmetic; `BandPlanOverlay` in `panadapter_rhi.cpp` only paints it. Covered by `test_bandplanstrip`, including a sweep of every region × band × span.
- `dashgeometry.h` — `DashGeometry::` namespace. Builds the vertices for a dashed vertical overlay line and caps itself at the buffer size `PanadapterConstants::MaxDashFloats` states. Depends on Qt Core only — kept out of `rhi_utils.h` so `test_panadapterdashes` needs no private Qt headers.
- `audioupsampler.{cpp,h}` — `AudioUpsampler`, a streaming windowed-sinc 4:1 interpolator (12 kHz → 48 kHz) for the TCI receive path. Not used by the renderer; lives here as signal processing. Stopband rejection pinned by `test_audioupsampler`.
- `rhi_utils.h` — Shared RHI helpers (color LUT size, texture builders).
- `shaders/` — 4 shader pairs (vert/frag): spectrum, spectrum_fill, waterfall, overlay. Compiled at build time via `qt6_add_shaders()` in `CMakeLists.txt`.

## Amplitude axis

The radio owns it: `#REF` is the bottom of the window in dBm, `#SCL` its height in dB, so the
visible range is `[REF, REF + SCALE]` and a bin's height is `(dBm - REF) / SCALE`. Bin dBm is
`raw_byte - RhiUtils::K4_DBM_OFFSET`.

In manual mode, REF supplies the bottom reference. In AutoRef mode, QK4 tracks the PAN packet's
noise floor using the averaging, debounce and signed offset from `#ARaadd+oom`. AutoRef averaging
accepts 1–30, as advertised by the radio. Disabling AutoRef restores the manual REF value.
`autoreference.h` holds the tracking arithmetic, covered by `test_autoreference`.

Display `#AVG` is separate (1–20): `spectrumaveraging.h` applies a symmetric exponential average
with new-frame weight `1 / AVG` to the main trace, waterfall input and mini-pan. Level 1 follows
each frame directly. Equal rise/fall weights avoid biasing the noise upward; regression tests live
in `test_spectrumaveraging`. The K4's exact filter response still needs live verification.

The PAN bin offset is 160 dB. The previous 146 dB offset raised the trace by 14 dB; simultaneous
K4/QK4 photos supported restoring 160. Absolute accuracy has not been checked with a calibrated
signal source. The trace, waterfall and scale labels share `SpectrumScale::normalizedForDb`.

S-units follow the amateur convention — S9 = -73 dBm, 6 dB per unit, round decades above S9 — and
are drawn at their true dBm positions rather than at even divisions of the widget. Labels outside
the window are omitted, never clamped; clamping is what once printed S1 at two different heights.

With `qk4.pan.diag` enabled, `CALIB` compares the packet's reported floor with the decoded bins'
5th percentile and median. Neither statistic must equal the radio's floor estimate.

Waterfall matching remains incomplete: `#WBS` brightness is not applied and `#WFC` does not change
the renderer palette. The current LUT is black below 10% of the display range. Those controls
need separate implementation; this change does not add a waterfall-only calibration offset.

## Sampling

Both the spectrum trace and the waterfall read the same raw bins through a `Linear` sampler, and
one line of shader math is the entire difference between them:

- `spectrum_fill.frag` → `texU = (binOffset + u * binCount + 0.5) / textureWidth`. Not floored, so
  samples land between texel centres and hardware bilinear blends adjacent bins — a smooth trace.
- `waterfall.frag` → `texU = (binOffset + floor(binIndex) + 0.5) / textureWidth`. The `floor()`
  snaps to an exact texel centre, where bilinear returns that texel unchanged, so the waterfall is
  effectively nearest-neighbour and keeps hard bin edges (`8ad70ea`). That `floor()` also fixes a
  C++ integer vs GLSL float division mismatch that blurred 5/8/11 kHz spans.
- `waterfall.frag` → `texV = (floor(texV * storedRows) + 0.5) / storedRows`, the vertical twin of
  the above. A V between two rows makes bilinear blend **two different moments in time**, which is
  the vertical smear; snapping is also what makes any rows-per-pixel ratio unblended rather than
  only 1:1. `storedRows` of 0 opts out — the mini-pan passes that, because it spreads a fixed
  100-row buffer over its own height and may be downsampling, where the blend is a useful box
  filter. The visible-row fraction must stay `visibleRows / storedRows`; a `- 1` there (which the
  panadapter carried) resolves the top pixel to `writeRow - 2` once snapped, hiding the newest row.

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
