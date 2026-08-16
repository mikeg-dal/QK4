#version 440

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(binding = 1) uniform sampler2D waterfallTex;
layout(binding = 2) uniform sampler2D colorLutTex;
// One texel per row: the tier bandwidth that row's bins were captured at.
layout(binding = 3) uniform sampler2D rowTierTex;

layout(std140, binding = 0) uniform buf {
    float scrollOffset;   // Row scroll offset (used by vertex shader)
    float binCount;       // Full tier bin count (e.g., 1024)
    float textureWidth;   // Texture width for bin centering (e.g., 4096)
    float tierSpanHz;     // Newest row's tier bandwidth, fallback for rows never written
    float spanHz;         // Display span in Hz
    float padding[3];
};

void main() {
    // Each row is windowed by the tier IT was captured at, not by the newest packet's tier.
    // Using one tier for the whole texture meant crossing a tier boundary re-scaled every row of
    // history in a single frame — the visible "blip" when zooming across a boundary.
    float rowTierSpanHz = texture(rowTierTex, vec2(0.5, fragTexCoord.y)).r;
    if (rowTierSpanHz <= 0.0)
        rowTierSpanHz = tierSpanHz;

    // Deliberately unclamped. A row captured on a narrower tier genuinely holds no data for the
    // parts of a wider display span that fall outside it, so spanRatio above 1.0 is meaningful:
    // it pushes those texels off the stored bins. The texture is zero-padded either side of the
    // centered bin region and clamps to a zero texel beyond that, so absent data reads as empty
    // rather than being stretched to fill — the row shows only the bandwidth it actually covers.
    float spanRatio = (rowTierSpanHz > 0.0) ? spanHz / rowTierSpanHz : 1.0;

    // Center the visible window on the row's own bins.
    float tierU = 0.5 + (fragTexCoord.x - 0.5) * spanRatio;

    // Map to texture coordinate (bins centered in texture)
    float binIndex = tierU * binCount;
    float binOffset = floor((textureWidth - binCount) / 2.0);

    // Nearest-neighbor: truncate to get bin, add 0.5 to sample center of texel
    float texU = (binOffset + floor(binIndex) + 0.5) / textureWidth;

    float dbValue = texture(waterfallTex, vec2(texU, fragTexCoord.y)).r;
    outColor = texture(colorLutTex, vec2(dbValue, 0.5));
}
