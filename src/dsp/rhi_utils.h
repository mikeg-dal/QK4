#ifndef RHI_UTILS_H
#define RHI_UTILS_H

#include <QColor>
#include "logging/logcategories.h"
#include "dsp/spectrumscale.h"
#include "dsp/waterfallcolorlut.h"
#include <QFile>
#include <QVector>
#include <rhi/qrhi.h>
#include <rhi/qshader.h>
#include <QDebug>
#include <array>

namespace RhiUtils {

// Load a compiled shader from a .qsb resource file
// Returns invalid QShader on failure (check with isValid())
inline QShader loadShader(const QString &path) {
    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) {
        return QShader::fromSerialized(f.readAll());
    }
    qCWarning(lcSpectrumRender) << "RhiUtils: Failed to load shader:" << path;
    return QShader();
}

// K4 spectrum calibration constant (shared between panadapter and minipan): dBm = raw_byte -
// K4_DBM_OFFSET. Owned by SpectrumScale, where it is measured and tested.
constexpr float K4_DBM_OFFSET = SpectrumScale::K4_DBM_OFFSET;

// Uniform block for waterfall.vert / waterfall.frag.
//
// WHY this lives here rather than in each widget: PanadapterRhiWidget and MiniPanRhiWidget draw
// with the same two shaders, and each used to declare its own copy of this struct. Adding a field
// to the shaders then required remembering to add it to both — and when it was missed, the mini-pan
// kept sending the shorter block, so the shader read a later field out of the caller's padding.
// visibleFraction arriving as 0 makes every screen row sample the same texture row, which renders
// as vertical streaks rather than a scrolling waterfall. That has now happened twice (9b7bfeb, and
// again when visibleFraction was introduced), so there is one definition and both widgets use it.
//
// Must match the std140 block in BOTH shader files.
struct WaterfallUniforms {
    float scrollOffset;    // oldest visible row, as a fraction of stored rows
    float binCount;        // bins written per row
    float textureWidth;    // texture width, for centring the bins
    float tierSpanHz;      // bandwidth those bins cover
    float spanHz;          // bandwidth on display
    float visibleFraction; // rows drawn / rows stored; 1.0 to show the whole buffer
    float storedRows;      // rows in the texture, for the V snap; 0.0 disables snapping
    float colorLow;        // palette position = clamp((texel - colorLow) * colorGain, 0, 1);
    float colorGain;       //   WaterfallGeometry::colorUniformsFor. 0 and 1 pass the texel through.
    float rowShift;        // rows added to V so row edges land on physical pixels; 0.0 for none
    float pad1, pad2;      // std140: the block is a whole number of 16-byte rows
};
static_assert(sizeof(WaterfallUniforms) == 48, "must match the std140 block in waterfall.{vert,frag}");

// Uniform block for overlay.vert / overlay.frag. Must match the std140 block in both shader files.
struct OverlayUniforms {
    float viewportWidth;
    float viewportHeight;
    float pad0, pad1; // std140: vec2 padding before the vec4 colour
    float r, g, b, a;
};
static_assert(sizeof(OverlayUniforms) == 32, "must match the std140 block in overlay.{vert,frag}");

// Two triangles covering the rectangle (x1, y1)-(x2, y2), as (x, y) pairs for the overlay pipeline.
inline std::array<float, 12> quadVerts(float x1, float y1, float x2, float y2) {
    return {x1, y1, x2, y1, x2, y2, x1, y1, x2, y2, x1, y2};
}

// Record one overlay draw's vertices and colour into the frame's update batch.
//
// WHY every overlay draw has its own vertex and uniform buffer, all written before beginPass():
// QRhiCommandBuffer::resourceUpdate() cannot be called inside a pass, and a QRhiBuffer::Dynamic
// buffer is double-buffered per frame, not per draw. Two draws in one frame that write the same
// buffer both see whichever write came last, so a shared overlay buffer drew every user of it
// with the final user's vertices and colour.
//
// Returns the number of vertices to draw: 0 when the vertices would not fit the buffer.
inline int stageOverlay(QRhiResourceUpdateBatch *rub, QRhiBuffer *vbo, QRhiBuffer *ubo, const float *verts,
                        int floatCount, float viewportWidth, float viewportHeight, const QColor &color) {
    const quint32 bytes = static_cast<quint32>(floatCount) * sizeof(float);
    Q_ASSERT(bytes <= vbo->size());
    if (floatCount <= 0 || bytes > vbo->size())
        return 0;
    rub->updateDynamicBuffer(vbo, 0, bytes, verts);
    const OverlayUniforms uniforms = {viewportWidth,
                                      viewportHeight,
                                      0.0f,
                                      0.0f,
                                      static_cast<float>(color.redF()),
                                      static_cast<float>(color.greenF()),
                                      static_cast<float>(color.blueF()),
                                      static_cast<float>(color.alphaF())};
    rub->updateDynamicBuffer(ubo, 0, sizeof(uniforms), &uniforms);
    return floatCount / 2;
}

} // namespace RhiUtils

#endif // RHI_UTILS_H
