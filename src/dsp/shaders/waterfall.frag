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
    float pad1;
    float pad2;
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

    // Snap V to a texel centre (time is not blended, unlike frequency above). A V landing between
    // two rows makes Linear filtering return a blend of two different moments in time, which is
    // the vertical smear. Snapping also makes any rows-per-pixel ratio unblended, not just 1:1.
    // Composes with the ring wrap: Repeat addressing handles a snapped coordinate past 1.0 the
    // same as an unsnapped one. storedRows 0.0 opts out — the mini-pan shares these shaders but
    // may be downsampling its 100-row buffer, where the blend is a box filter worth keeping.
    float texV = fragTexCoord.y;
    if (storedRows > 0.0)
        texV = (floor(texV * storedRows) + 0.5) / storedRows;

    // The texel is the bin's level (dBm + K4_DBM_OFFSET, as a normalised byte), not a palette
    // position, so the colour window applies here and moves the whole history when it changes.
    // WaterfallGeometry::colorUniformsFor sets it and shaderFractionFor mirrors this line.
    float level = texture(waterfallTex, vec2(texU, texV)).r;
    // Byte 0 is "no data" (rows not yet written, a cleared history): palette position 0 under any
    // window, even one whose bottom edge sits below the byte's own -162 dBm.
    float palettePos = level < 0.5 / 255.0 ? 0.0 : clamp((level - colorLow) * colorGain, 0.0, 1.0);
    outColor = texture(colorLutTex, vec2(palettePos, 0.5));
}
