# dsp/

GPU-accelerated spectrum + waterfall rendering via Qt RHI, through `QRhiWidget`.

Graphics API: `QRhiWidget` defaults to Metal on macOS, Direct3D 11 on Windows and OpenGL elsewhere
(Qt docs, `QRhiWidget::setApi()`). Both widgets also call `setApi(Metal)` on macOS. Nothing selects
Vulkan, so Linux, the Raspberry Pi and the Flatpak render with OpenGL. When no `QRhi` can be
created, `QRhiWidget::renderFailed` fires; `SpectrumController` logs it.

Qt version: `QRhiWidget` needs Qt 6.7 or later. The QRhi classes (`QRhi`, `QShader`,
`QShaderDescription`) have no source or binary compatibility guarantee across Qt minor releases
(Qt docs, QRhiWidget). They are included as `<rhi/qrhi.h>` through `Qt6::GuiPrivate`. Re-test the
panadapter on every Qt minor bump; every release package is built with the one Qt named in
`.github/qt-version` (Flatpak takes the same minor from the KDE runtime).

Reparenting: neither widget implements `QRhiWidget::releaseResources()`. Qt expects a robust
subclass to implement it if the widget can move to a different top-level window. These widgets are
never moved to another top-level window; implement it before doing that.

## Files

- `panadapter_rhi.{cpp,h}` — Main panadapter (spectrum + waterfall). Click-tune, passband overlays, DX spot overlays, TX markers. Naturally large (RHI pipeline + buffer management).
- `minipan_rhi.{cpp,h}` — `MiniPanRhiWidget`, the per-VFO mini-pan. Its frequency axis is each MiniPAN frame's own centre and span (3 kHz in CW, 12 kHz in DATA-A, chosen by the radio; `docs/k4-protocol-quirks.md` section 15), and its passband, marker, notch and RTTY space line are placed on that axis by the panadapter's rules in `SpectrumGeometry`, from the dial the controller reports.
- `panadapter_constants.h` — Shared rendering parameters: RTTY shift, grid cell size, line widths, dash pattern. Texture and history dimensions are **not** here; they live in `panadapter_rhi.h` (`BASE_TEXTURE_WIDTH`, `MAX_WATERFALL_HISTORY`).
- `waterfallgeometry.{cpp,h}` — `WaterfallGeometry::` namespace. Which stored waterfall row each screen pixel shows (visible-row count, rows drawn one per physical pixel — two device pixels when macOS halves a HiDPI framebuffer onto the panel, with row edges shifted onto the panel's pixel grid wherever the widget sits — ring offsets, the uniforms `waterfall.vert` consumes), and which palette colour a level gets (the colour window from REF and `#WBS`, the stored byte, the shader's `colorLow`/`colorGain`). Mirrors the shaders' math so both can be tested without a GPU; covered by `test_waterfallgeometry`.
- `spectrumgeometry.{cpp,h}` — `SpectrumGeometry::` namespace. The frequency axis: operating mode and DATA sub-mode as enums (parsed once in the setters, not compared as strings per frame), the CW display centre shared by the panadapter, its frequency-scale and band-plan overlays and click-tune, passband edges and marker position for primary and secondary VFO, frequency ↔ horizontal position on a display of a given centre and span (both widgets), the manual notch's frequency, the default CW pitch, and the bounds check on a PAN frame's wire-supplied bin count. Pure arithmetic on Qt Core, covered by `test_spectrumgeometry`.
- `spectrumscale.{cpp,h}` — `SpectrumScale::` namespace. The amplitude axis: dBm → chart fraction, S-unit dBm values, and which values earn a label at a given size. Pure arithmetic, no widget dependency, covered by `test_spectrumscale`.
- `bandplanstrip.{cpp,h}` — `BandPlanStrip::` namespace. Layout of the band-plan strip at the top of the panadapter: mode-segment blocks, out-of-band blocks past the band edge (so the strip always spans the full width), the pinned band tag, sticky mode labels and marker labels that never overlap. Pure arithmetic; `BandPlanOverlay` in `panadapter_rhi.cpp` only paints it. Covered by `test_bandplanstrip`, including a sweep of every region × band × span.
- `dashgeometry.h` — `DashGeometry::` namespace. Builds the vertices for a dashed vertical overlay line and caps itself at the buffer size `PanadapterConstants::MaxDashFloats` states. Depends on Qt Core only — kept out of `rhi_utils.h` so `test_panadapterdashes` needs no private Qt headers.
- `audioupsampler.{cpp,h}` — `AudioUpsampler`, a streaming windowed-sinc 4:1 interpolator (12 kHz → 48 kHz) for the TCI receive path. Not used by the renderer; lives here as signal processing. Stopband rejection pinned by `test_audioupsampler`.
- `rhi_utils.h` — Shared RHI helpers: shader loading, the uniform-block structs shared with the shaders, overlay staging.
- `waterfallcolorlut.h` — The waterfall's 256-entry colour table (`RhiUtils::generateWaterfallColorLUT`). Qt Core only, so `test_waterfallgeometry` can check colours against the real table.
- `shaders/` — Vulkan-style GLSL (`#version 440`), compiled at build time to `.qsb` by `qt6_add_shaders()` in `CMakeLists.txt` and loaded from `:/shaders/src/dsp/shaders/*.qsb`.
  - `spectrum_fill.{vert,frag}` — the panadapter's filled spectrum trace. Opaque (0.9) all the way down: the band noise is drawn at the bottom, so nothing fades there. `SpectrumGeometry::fillAlphaFor` mirrors its alpha for `test_spectrumgeometry`.
  - `spectrum.{vert,frag}` — the mini-pan's spectrum trace.
  - `waterfall.{vert,frag}` — the waterfall, in both widgets.
  - `overlay.{vert,frag}` — flat-colour overlay geometry (passband, markers, grid lines), in both widgets.

## Amplitude axis

The radio owns it: `#REF` is the bottom of the window in dBm, `#SCL` its height in dB, so the
visible range is `[REF, REF + SCALE]` and a bin's height is `(dBm - REF) / SCALE`. Bin dBm is
`raw_byte - RhiUtils::K4_DBM_OFFSET`.

Nothing is subtracted from that. The renderer used to remove a smoothed per-frame minimum — an
automatic noise-floor tracker — which moved the trace by roughly 20% of the chart height depending
on band conditions, so signal height tracked the noise rather than the signal, and no dBm or S-unit
label could be correct. Auto-reference is the radio's job: in `AR=Auto` the K4 adjusts REF itself
and reports it, which arrives as `#REF` like any other change.

**Do not reintroduce a display-side baseline.** The trace and the scale labels both route through
`SpectrumScale::normalizedForDb` specifically so they cannot disagree about where a given dBm
belongs.

## Waterfall brightness

The waterfall is not drawn on the REF..REF+SCALE window. Each stored row holds the bins' levels as
the K4 sent them (`byte = dBm + K4_DBM_OFFSET`, 0 reserved for "no data"), and `waterfall.frag`
places a level in the palette with two uniforms, `colorLow` and `colorGain`. The palette itself
(`waterfallcolorlut.h`) is unchanged.

The window is `WaterfallGeometry::colorWindowFor(REF, #WBS)`: bottom of the palette at
`REF - #WBS`, top `PaletteSpanDb` (40 dB) above it. That is a **hypothesis**, documented and pinned
by tests in that one function so it can be corrected from K4 screenshots without touching anything
else. What it rests on: the K4 manual says a higher WTR CLRS (`#WBS`, 5-30) "will use brighter colors
for a given amount of waterfall signal intensity", and AutoRef parks REF on the average noise. With the
K4's 14 dB, noise lands at about 0.4 of the palette (mid blue, dark troughs, light-blue peaks), as on
the K4's own screen; the old REF..REF+SCALE mapping put it at about 0.05, which the palette draws
black. The 40 dB span and the default of 14 dB before the radio reports `#WBS` are guesses.

Rows store the palette position under the window in effect when they were captured
(`WaterfallGeometry::capturedByteFor`) and the shader passes them through (`colorLow` 0,
`colorGain` 1), so an AutoRef step or a `#WBS` change recolours only new rows. The mini-pan does the
same with its receiver's REF and the shared `#WBS`.

S-units follow the amateur convention — S9 = -73 dBm, 6 dB per unit, round decades above S9 — and
are drawn at their true dBm positions rather than at even divisions of the widget. Labels outside
the window are omitted, never clamped; clamping is what once printed S1 at two different heights.

Label density follows the space available: neighbouring labels stay at least
`SpectrumScale::kLabelPitchLines` (2.5) lines of the label font apart, measured with
`QFontMetricsF::lineSpacing()` against the spectrum area's height after the waterfall split. SCALE,
REF, the waterfall share, the window size and the font size all change the step (5, 10, 20 … dB)
with no extra wiring. The tick sits after each label, and DX spot labels keep out of the column
(`PanadapterRhiWidget::amplitudeAxisWidthPx()`, measured from the label font).

## Mini-pan amplitude

The mini-pan uses the panadapter's rules with its own byte zero: MiniPAN bins are 1 dB per byte,
`dBm = byte - SpectrumScale::K4_MINIPAN_DBM_OFFSET` (118), measured against `SMH` on the WWV carrier
(the pairs and their 13 dB scatter are in `spectrumscale.h` and pinned by `test_spectrumscale`).
Byte 0 (-118 dBm) is the floor; much of the noise arrives as 0.

Its window is its receiver's `[REF, REF + SCALE]` — `#REF` for Main, `#REF$` for Sub, the shared
`#SCL` — through `SpectrumScale::normalizedForDb`, and its waterfall colours come from
`WaterfallGeometry::colorWindowFor` with the same REF and `#WBS`. The K4 has no mini-pan REF or SCALE
of its own (the Programmer's Reference lists only `#MP` / `#MP$`), and with the measured offset the
mini-pan's noise sits at that REF (median -112 dBm against REF -110 on the K4D, 2026-10-30), so it
lands on the bottom edge as on the panadapter. As there, nothing is subtracted: the old mini-pan read
`byte / 10` over a fixed window and removed a smoothed per-frame minimum, the tracker this README
forbids. Smoothing is `SpectrumGeometry::emaAlphasFor`, the panadapter's.

`K4_DBM_OFFSET` (162, in `spectrumscale.h`) was measured against the radio's own dBm meter (`SMH`)
on a steady carrier, at two span tiers; the comment there has the numbers. The bins are power per
bin, so noise rises 3 dB each time the span tier doubles while a carrier stays put.

With `qk4.spectrum.diag` enabled a `CALIB` line compares the radio's reported noise floor against
the 5th-percentile bin. That is a sanity check, not the calibration: noise statistics put the
percentile some dB below the floor (median about 3-4 dB above it) even with the offset right. Recalibrate
from a carrier against `SMH`, not from this line.

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
resampling on resize cannot recover detail that is not there. The mini-pan averages its 1024 bins
into a 512-wide texture (`minipan_rhi.cpp`); that loses nothing, because every MiniPAN frame captured
(1192 of them, 2026-10-30) carries 512 values each sent twice.

## Ownership

Owned by `SpectrumController` (`controllers/spectrumcontroller.cpp`).

## Data flow

K4 spectrum packets → `Protocol` (I/O thread) → `ConnectionController` (re-emits, queued to the main thread) → `SpectrumController` → `PanadapterRhiWidget` / `MiniPanRhiWidget`.

Spectrum display state includes #AR (auto-reference), #SCL (scale), #SPN (span), #REF (reference level), #WFC (waterfall color), #WBS (waterfall colour range), #WFH (waterfall height) — all stored on `SpectrumDisplayState` in `models/radiostate/`.

## See also

- `docs/k4-protocol-quirks.md` → "PAN spectrum tiers" — tier boundaries, sample rates, bin counts,
  and why the waterfall is cleared when the radio changes tier.
- `docs/k4-protocol-quirks.md` → "Span dial skips 6 kHz going up".
- `controllers/spectrumcontroller.cpp` — all wiring + click-tune logic.
