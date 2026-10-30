#version 440

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(binding = 1) uniform sampler2D spectrumData;
layout(binding = 2) uniform sampler2D colorLut;  // Spectrum color LUT

// Uniform buffer: 80 bytes, std140 layout
layout(std140, binding = 0) uniform buf {
    vec4 fillBaseColor;     // offset 0:  (unused)
    vec4 fillPeakColor;     // offset 16: (unused)
    vec4 glowColor;         // offset 32: cyan glow color
    float glowIntensity;    // offset 48: glow strength (0.8)
    float glowWidth;        // offset 52: glow falloff width (0.04)
    float spectrumHeight;   // offset 56: spectrum area height in pixels
    float binCount;         // offset 60: actual spectrum bin count
    vec2 viewportSize;      // offset 64: viewport dimensions
    float textureWidth;     // offset 72: texture width for bin centering
    float spanRatio;        // offset 76: display span / tier span (the waterfall's window)
};  // Total: 80 bytes

// The waterfall's mapping (waterfall.frag), on the same full-tier bins, so the trace sits exactly
// over the waterfall (SPEC-011): the visible span is centred in the tier, bin i's centre is at
// i + 0.5, the Linear sampler blends the two nearest bins, and the edges clamp to the outer bin
// centres so they never blend with the empty texels beside the bins.
float sampleBilinear(float u) {
    float r = clamp(spanRatio, 0.0, 1.0);
    float tierU = (1.0 - r) / 2.0 + u * r;
    // Bins are centered in texture - use floor() to match C++ integer division
    float binOffset = floor((textureWidth - binCount) / 2.0);
    float binPos = clamp(tierU * binCount, 0.5, binCount - 0.5);
    float texU = (binOffset + binPos) / textureWidth;
    return texture(spectrumData, vec2(texU, 0.5)).r;
}

void main() {
    // Sample spectrum value using GPU bilinear interpolation
    float spectrumValue = sampleBilinear(fragTexCoord.x);

    // spectrumValue is 0.0 (no signal) to ~1.0 (max signal)
    // Convert to Y threshold: peakY where 0.0 = top, 1.0 = bottom (in texCoord space)
    float peakY = 1.0 - spectrumValue;
    float currentY = fragTexCoord.y;

    // Calculate distance from peak
    float distFromPeak = currentY - peakY;

    // Discard pixels above the peak (allow region for glow)
    float glowRegion = glowWidth * 2.0;
    if (distFromPeak < -glowRegion) {
        discard;
    }

    // === dBm-to-Color LUT Mapping ===
    // currentY: 0.0 = top (strong signal), 1.0 = bottom (weak signal)
    // LUT index: 0 = weak, 1 = strong (invert Y)
    float lutIndex = 1.0 - currentY;
    lutIndex = clamp(lutIndex, 0.0, 1.0);

    // Sample color from LUT
    vec3 mappedColor = texture(colorLut, vec2(lutIndex, 0.5)).rgb;

    // Fill mask: only show below the peak
    float fillMask = smoothstep(-0.002, 0.002, distFromPeak);

    // === Glow effect - exponential falloff from peak ===
    float glowDist = abs(distFromPeak);
    float glow = exp(-glowDist / glowWidth) * glowIntensity;

    // Extra peak glow - tighter, brighter at the edge
    float peakGlow = exp(-glowDist / (glowWidth * 0.3)) * glowIntensity * 0.5;
    glow = max(glow, peakGlow);

    // === FINAL COMPOSITE ===
    vec3 finalColor = mappedColor * fillMask;
    // Add glow (additive blending for bright halo effect)
    finalColor += glowColor.rgb * glow;

    // Alpha: fill alpha where filled, glow alpha where glowing. Mirrored by
    // SpectrumGeometry::fillAlphaFor.
    //
    // WHY no fade towards the bottom: a fade over the lowest 12% (down to 10% alpha) used to sit
    // exactly where AutoRef puts the band noise, so the noise was nearly invisible. The K4 draws
    // that noise at full strength.
    float finalAlpha = max(0.9 * fillMask, glow * glowColor.a);

    // Clamp to prevent over-saturation
    finalColor = clamp(finalColor, 0.0, 1.0);

    outColor = vec4(finalColor, finalAlpha);
}
