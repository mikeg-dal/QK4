#ifndef RHI_UTILS_H
#define RHI_UTILS_H

#include <QFile>
#include <QVector>
#include <rhi/qshader.h>
#include <QDebug>

namespace RhiUtils {

// Waterfall color LUT constants
constexpr int COLOR_LUT_SIZE = 256;
constexpr int COLOR_LUT_BYTES = COLOR_LUT_SIZE * 4; // RGBA

// Generate 256-entry RGBA waterfall color LUT
// 7-stage progression: Black -> Dark Blue -> Royal Blue -> Cyan -> Green -> Yellow -> Red
inline void generateWaterfallColorLUT(QVector<quint8> &lut) {
    lut.resize(COLOR_LUT_BYTES);
    for (int i = 0; i < COLOR_LUT_SIZE; ++i) {
        float value = i / 255.0f;
        int r, g, b;

        if (value < 0.10f) {
            r = 0;
            g = 0;
            b = 0;
        } else if (value < 0.25f) {
            float t = (value - 0.10f) / 0.15f;
            r = 0;
            g = 0;
            b = static_cast<int>(t * 51);
        } else if (value < 0.40f) {
            float t = (value - 0.25f) / 0.15f;
            r = 0;
            g = 0;
            b = static_cast<int>(51 + t * 102);
        } else if (value < 0.55f) {
            float t = (value - 0.40f) / 0.15f;
            r = 0;
            g = static_cast<int>(t * 128);
            b = static_cast<int>(153 + t * 102);
        } else if (value < 0.70f) {
            float t = (value - 0.55f) / 0.15f;
            r = 0;
            g = static_cast<int>(128 + t * 127);
            b = static_cast<int>(255 * (1.0f - t));
        } else if (value < 0.85f) {
            float t = (value - 0.70f) / 0.15f;
            r = static_cast<int>(t * 255);
            g = 255;
            b = 0;
        } else {
            float t = (value - 0.85f) / 0.15f;
            r = 255;
            g = static_cast<int>(255 * (1.0f - t));
            b = 0;
        }

        lut[i * 4 + 0] = static_cast<quint8>(qBound(0, r, 255));
        lut[i * 4 + 1] = static_cast<quint8>(qBound(0, g, 255));
        lut[i * 4 + 2] = static_cast<quint8>(qBound(0, b, 255));
        lut[i * 4 + 3] = 255;
    }
}

// Load a compiled shader from a .qsb resource file
// Returns invalid QShader on failure (check with isValid())
inline QShader loadShader(const QString &path) {
    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) {
        return QShader::fromSerialized(f.readAll());
    }
    qWarning() << "RhiUtils: Failed to load shader:" << path;
    return QShader();
}

// K4 spectrum calibration constant (shared between panadapter and minipan)
// dBm = raw_byte - K4_DBM_OFFSET
constexpr float K4_DBM_OFFSET = 146.0f;

} // namespace RhiUtils

#endif // RHI_UTILS_H
