#ifndef DASHGEOMETRY_H
#define DASHGEOMETRY_H

#include <QVector>
#include <QtGlobal>

#include "dsp/panadapter_constants.h"

// Vertex arithmetic for the dashed overlay lines, kept apart from rhi_utils.h so it depends on
// nothing but Qt Core — same split, and for the same reason, as waterfallgeometry.{h,cpp}: the
// drawing cannot be unit-tested but the arithmetic that sizes the GPU buffers can, and a test that
// reached rhi_utils.h would need Qt's PRIVATE rhi/ headers, which are not installed on every
// platform QK4 builds on.
namespace DashGeometry {

// Build a vertical dashed line at `x`, spanning [yTop, yBottom) in DEVICE pixels, as two triangles
// per dash. The last dash is clipped to yBottom.
//
// WHY one definition, and why it caps itself: this loop existed in six places across the two
// widgets — the notch line, the primary and secondary RTTY mark/space lines, and the mini-pan's —
// each uploading verts.size() floats into a fixed-size QRhiBuffer sized by its own constant. Both
// constants were too small. The panadapter's 1200 floats cover 100 dashes = 1000 device px of
// spectrum, which a Retina panel exceeds at any ordinary window height; the mini-pan's 512 covered
// 420 px and its loop spans the whole widget. Past that, updateDynamicBuffer wrote past the end of
// a GPU buffer on every frame.
//
// The cap is the backstop for PanadapterConstants::MaxDisplayHeightPx, not the normal path: on a
// display taller than the ceiling the line stops early and says so, rather than corrupting memory.
inline void appendDashedVerticalLine(QVector<float> &verts, float x, float width, float yTop, float yBottom,
                                     int maxFloats = PanadapterConstants::MaxDashFloats) {
    const float dashLen = PanadapterConstants::DashLengthPx;
    const float stride = PanadapterConstants::DashStridePx;
    for (float y = yTop; y < yBottom; y += stride) {
        if (verts.size() + PanadapterConstants::FloatsPerDash > maxFloats) {
            static bool warned = false;
            if (!warned) {
                warned = true;
                qWarning("DashGeometry: dashed line clipped at %d floats — display taller than "
                         "PanadapterConstants::MaxDisplayHeightPx (%.0f px); raise it",
                         maxFloats, double(PanadapterConstants::MaxDisplayHeightPx));
            }
            return;
        }
        const float yEnd = qMin(y + dashLen, yBottom);
        verts << x << y << x + width << y << x + width << yEnd << x << y << x + width << yEnd << x << yEnd;
    }
}

} // namespace DashGeometry

#endif // DASHGEOMETRY_H
