#version 440

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;

layout(location = 0) out vec2 fragTexCoord;

layout(std140, binding = 0) uniform buf {
    float scrollOffset;     // Oldest visible row, as a fraction of stored rows
    float binCount;         // Full tier bin count (e.g., 1024)
    float textureWidth;     // Texture width (e.g., 4096)
    float tierSpanHz;       // Full tier bandwidth in Hz
    float spanHz;           // Display span in Hz
    float visibleFraction;  // Rows drawn / rows stored
    float storedRows;       // Rows in the texture; 0.0 disables the V snap
    float colorLow;         // Palette position = clamp((texel - colorLow) * colorGain, 0, 1)
    float colorGain;
    float pad0;
    float pad1;
    float pad2;
};

void main() {
    gl_Position = vec4(position, 0.0, 1.0);

    // The texture stores more history than is shown. Scale the quad's 0..1 t across only the
    // visible slice, so the rows on screen are the newest ones and each maps to one device pixel;
    // V wraps, which is what makes the ring buffer scroll.
    fragTexCoord = vec2(texCoord.x, texCoord.y * visibleFraction + scrollOffset);
}
