#version 440

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(binding = 1) uniform sampler2D waterfallTex;
layout(binding = 2) uniform sampler2D colorLutTex;

// Must stay byte-identical to the block in waterfall.vert — they share one std140 binding.
layout(std140, binding = 0) uniform buf {
    float scrollOffset;     // Oldest visible row, as a fraction of stored rows (vertex stage)
    float binCount;         // Full tier bin count (e.g., 1024)
    float textureWidth;     // Texture width for bin centering (e.g., 8192)
    float tierSpanHz;       // Full tier bandwidth in Hz
    float spanHz;           // Display span in Hz
    float visibleFraction;  // Rows drawn / rows stored (vertex stage)
    float padding[2];
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

    // Nearest-neighbor: truncate to get bin, add 0.5 to sample center of texel
    float texU = (binOffset + floor(binIndex) + 0.5) / textureWidth;

    float dbValue = texture(waterfallTex, vec2(texU, fragTexCoord.y)).r;
    outColor = texture(colorLutTex, vec2(dbValue, 0.5));
}
