#version 440

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(binding = 1) uniform sampler2D waterfallTex;
layout(binding = 2) uniform sampler2D colorLutTex;

// Must stay byte-identical to the block in waterfall.vert — they share one std140 binding.
layout(std140, binding = 0) uniform buf {
    float scrollOffset;     // Oldest visible row, as a fraction of stored rows (vertex stage)
    float binCount;         // Full tier bin count (e.g., 1024)
    float textureWidth;     // Texture width for bin centering (e.g., 4096)
    float tierSpanHz;       // Full tier bandwidth in Hz
    float spanHz;           // Display span in Hz
    float visibleFraction;  // Rows drawn / rows stored (vertex stage)
    float storedRows;       // Rows in the texture; 0.0 disables the V snap
    float colorLow;         // Palette position = clamp((texel - colorLow) * colorGain, 0, 1)
    float colorGain;
    float rowShift;         // Rows added to V so row edges land on physical pixels (vertex stage)
    float scrollPhase;      // Sub-row scroll residual added to V, <= 0 (vertex stage)
    float pad1;
};

void main() {
    // Map display X [0,1] to the visible portion of the full tier data.
    // The tier data covers tierSpanHz bandwidth; we only display spanHz centered within it.
    //
    // WHY one tier for every row is safe: the waterfall is cleared whenever the radio changes
    // tier, so no row on screen was ever captured at a different bandwidth than this uniform
    // describes. Without that clear, this would silently rescale all history on a tier change.
    float spanRatio = clamp(spanHz / tierSpanHz, 0.0, 1.0);
    float tierStart = (1.0 - spanRatio) / 2.0;       // center the visible window
    float tierU = tierStart + fragTexCoord.x * spanRatio;

    // Map to texture coordinate (bins centered in texture)
    float binIndex = tierU * binCount;
    float binOffset = floor((textureWidth - binCount) / 2.0);

    // Blend the two nearest bins (the sampler is Linear). Bin i's centre stays at i + 0.5, where the
    // old nearest-bin snap drew it, so nothing moves sideways. WHY: at narrow spans a bin is several
    // pixels wide, and the snap drew each one as a hard-edged block (~6 px at 14 MHz, bench
    // 2026-10-30) where the K4 front panel shows a smooth waterfall even at #AVG 1. The blend is of
    // levels, before the palette, so it never invents a colour between two unrelated hues.
    // Clamped to the outer bin centres so the edges never blend with the empty texels beside the
    // bins (they read as "no data").
    float binPos = clamp(binIndex, 0.5, binCount - 0.5);
    float texU = (binOffset + binPos) / textureWidth;

    // Vertical reconstruction. Time is not blended by the sampler, unlike frequency above: a V
    // landing between two rows makes Linear filtering return a mix of two different moments, which
    // is the vertical smear.
    //
    // WHY a cubic B-spline and not the texel-centre snap this replaced. Snapping killed the smear
    // but its frequency response depends on the phase: as the sample point sweeps across a row the
    // kernel alternates between one sharp row and a half-and-half blend, so a noisy waterfall
    // pulses — and once rows are several pixels tall, or the scroll moves by a fraction of a row,
    // that sweep happens constantly. A cubic B-spline over the four nearest rows has the same
    // response at every phase, so there is no pulse, and it is what allows the sub-row scroll
    // residual to move the picture smoothly between arriving frames.
    //
    // Taps are clamped to the rows the window may show, so the kernel can never reach the write
    // head (which holds a row still being built) nor wrap the newest row in under the oldest. The
    // two rows of slack below the oldest visible row are what rowShift and scrollPhase reach into;
    // the renderer reserves them by holding visibleRows at storedRows - 3.
    //
    // storedRows 0.0 opts out — the mini-pan shares these shaders but may be downsampling its
    // 100-row buffer, where the sampler's own box filter is worth keeping.
    //
    // The texel is the bin's palette position, fixed when its row was captured
    // (WaterfallGeometry::capturedByteFor); both widgets pass it through with colorLow 0 and
    // colorGain 1. WaterfallGeometry::shaderFractionFor mirrors the colour line below.
    float level;
    if (storedRows > 0.0) {
        float firstRow = scrollOffset * storedRows;
        float lastRow = firstRow + visibleFraction * storedRows - 1.0;
        float oldestTap = firstRow - 2.0;

        float logicalTexel = fragTexCoord.y * storedRows - 0.5;
        float base = floor(logicalTexel);
        float f = logicalTexel - base;
        float f2 = f * f;
        float f3 = f2 * f;
        float w0 = (1.0 - 3.0 * f + 3.0 * f2 - f3) / 6.0;
        float w1 = (4.0 - 6.0 * f2 + 3.0 * f3) / 6.0;
        float w2 = (1.0 + 3.0 * f + 3.0 * f2 - 3.0 * f3) / 6.0;
        float w3 = f3 / 6.0;

        // Repeat addressing wraps the ring, so a row index outside 0..storedRows needs no modulo.
        float r0 = (clamp(base - 1.0, oldestTap, lastRow) + 0.5) / storedRows;
        float r1 = (clamp(base, oldestTap, lastRow) + 0.5) / storedRows;
        float r2 = (clamp(base + 1.0, oldestTap, lastRow) + 0.5) / storedRows;
        float r3 = (clamp(base + 2.0, oldestTap, lastRow) + 0.5) / storedRows;

        level = w0 * texture(waterfallTex, vec2(texU, r0)).r + w1 * texture(waterfallTex, vec2(texU, r1)).r +
                w2 * texture(waterfallTex, vec2(texU, r2)).r + w3 * texture(waterfallTex, vec2(texU, r3)).r;
    } else {
        level = texture(waterfallTex, vec2(texU, fragTexCoord.y)).r;
    }
    // Byte 0 is "no data" (rows not yet written, a cleared history): palette position 0 under any
    // uniforms.
    float palettePos = level < 0.5 / 255.0 ? 0.0 : clamp((level - colorLow) * colorGain, 0.0, 1.0);
    outColor = texture(colorLutTex, vec2(palettePos, 0.5));
}
