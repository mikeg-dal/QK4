#include "sstvmoderegistry.h"

const QVector<SstvModeSpec> &SstvModeRegistry::all() {
    // Values are expressed in the on-air SSTV timing domain.  Keep this table
    // as the only mode authority: UI labels, image sizing, VIS detection and
    // the encoder/decoder all consume this definition.
    // Timing and VIS values were cross-checked against Open-SSTV and the
    // PySSTV classes it uses. QK4 keeps the conventional 320-pixel sampling
    // grid for M2/S2; the analog scan timing is identical to a 160-pixel grid.
    static const QVector<SstvModeSpec> modes = {
        {SstvModeId::Robot36, QStringLiteral("Robot 36"), 320, 240, 0x08, 36000, SstvColorFamily::RobotYuv,
         SstvLineLayout::Robot36, 9.0, 3.0, 44.0, 150.0, true, true},

        {SstvModeId::MartinM1, QStringLiteral("Martin M1"), 320, 256, 0x2c, 114000, SstvColorFamily::GbrSequential,
         SstvLineLayout::Martin, 4.862, 0.572, 146.432, 446.446, true, true},
        {SstvModeId::MartinM2, QStringLiteral("Martin M2"), 320, 256, 0x28, 58000, SstvColorFamily::GbrSequential,
         SstvLineLayout::Martin, 4.862, 0.572, 73.216, 226.798, true, true},
        {SstvModeId::MartinM3, QStringLiteral("Martin M3"), 320, 128, 0x24, 57000, SstvColorFamily::GbrSequential,
         SstvLineLayout::Martin, 4.862, 0.572, 146.432, 446.446, true, true},
        {SstvModeId::MartinM4, QStringLiteral("Martin M4"), 160, 128, 0x20, 29000, SstvColorFamily::GbrSequential,
         SstvLineLayout::Martin, 4.862, 0.572, 73.216, 226.798, true, true},

        {SstvModeId::ScottieS1, QStringLiteral("Scottie S1"), 320, 256, 0x3c, 110000, SstvColorFamily::GbrSequential,
         SstvLineLayout::Scottie, 9.0, 1.5, 136.74, 428.22, true, true},
        {SstvModeId::ScottieS2, QStringLiteral("Scottie S2"), 320, 256, 0x38, 71000, SstvColorFamily::GbrSequential,
         SstvLineLayout::Scottie, 9.0, 1.5, 86.564, 277.692, true, true},
        {SstvModeId::ScottieDx, QStringLiteral("Scottie DX"), 320, 256, 0x4c, 269000, SstvColorFamily::GbrSequential,
         SstvLineLayout::Scottie, 9.0, 1.5, 344.1, 1050.3, true, true},
        {SstvModeId::ScottieS3, QStringLiteral("Scottie S3"), 320, 128, 0x34, 55000, SstvColorFamily::GbrSequential,
         SstvLineLayout::Scottie, 9.0, 1.5, 136.74, 428.22, true, true},
        {SstvModeId::ScottieS4, QStringLiteral("Scottie S4"), 160, 128, 0x30, 36000, SstvColorFamily::GbrSequential,
         SstvLineLayout::Scottie, 9.0, 1.5, 86.564, 277.692, true, true},

        {SstvModeId::Pd50, QStringLiteral("PD-50"), 320, 256, 0x5d, 50000, SstvColorFamily::PdYuv, SstvLineLayout::Pd,
         20.0, 2.08, 91.52, 388.16, true, true},
        {SstvModeId::Pd90, QStringLiteral("PD-90"), 320, 256, 0x63, 90000, SstvColorFamily::PdYuv, SstvLineLayout::Pd,
         20.0, 2.08, 170.24, 703.04, true, true},
        {SstvModeId::Pd120, QStringLiteral("PD-120"), 640, 496, 0x5f, 126000, SstvColorFamily::PdYuv,
         SstvLineLayout::Pd, 20.0, 2.08, 121.6, 508.48, true, true},
        {SstvModeId::Pd160, QStringLiteral("PD-160"), 512, 400, 0x62, 161000, SstvColorFamily::PdYuv,
         SstvLineLayout::Pd, 20.0, 2.08, 195.584, 804.416, true, true},
        {SstvModeId::Pd180, QStringLiteral("PD-180"), 640, 496, 0x60, 187000, SstvColorFamily::PdYuv,
         SstvLineLayout::Pd, 20.0, 2.08, 183.04, 754.24, true, true},
        {SstvModeId::Pd240, QStringLiteral("PD-240"), 640, 496, 0x61, 248000, SstvColorFamily::PdYuv,
         SstvLineLayout::Pd, 20.0, 2.08, 244.48, 1000.0, true, true},
        {SstvModeId::Pd290, QStringLiteral("PD-290"), 800, 616, 0x5e, 289000, SstvColorFamily::PdYuv,
         SstvLineLayout::Pd, 20.0, 2.08, 228.8, 937.28, true, true},

        {SstvModeId::WraaseSc2120, QStringLiteral("Wraase SC2-120"), 320, 256, 0x3f, 121000,
         SstvColorFamily::RgbSequential, SstvLineLayout::Wraase, 5.5225, 0.5, 156.0, 474.0225, true, true},
        {SstvModeId::WraaseSc2180, QStringLiteral("Wraase SC2-180"), 320, 256, 0x37, 182000,
         SstvColorFamily::RgbSequential, SstvLineLayout::Wraase, 5.5225, 0.5, 235.0, 711.0225, true, true},

        {SstvModeId::PasokonP3, QStringLiteral("Pasokon P3"), 640, 496, 0x71, 203000, SstvColorFamily::RgbSequential,
         SstvLineLayout::Pasokon, 5.208333333, 1.041666667, 133.333333333, 409.375, true, true},
        {SstvModeId::PasokonP5, QStringLiteral("Pasokon P5"), 640, 496, 0x72, 305000, SstvColorFamily::RgbSequential,
         SstvLineLayout::Pasokon, 7.8125, 1.5625, 200.0, 614.0625, true, true},
        {SstvModeId::PasokonP7, QStringLiteral("Pasokon P7"), 640, 496, 0x73, 406000, SstvColorFamily::RgbSequential,
         SstvLineLayout::Pasokon, 10.416666667, 2.083333333, 266.666666667, 818.75, true, true},
    };
    return modes;
}

const SstvModeSpec *SstvModeRegistry::find(SstvModeId id) {
    const auto &modes = all();
    for (const SstvModeSpec &mode : modes) {
        if (mode.id == id)
            return &mode;
    }
    return nullptr;
}

const SstvModeSpec *SstvModeRegistry::findByVis(int visCode) {
    const auto &modes = all();
    for (const SstvModeSpec &mode : modes) {
        if (mode.visCode == visCode)
            return &mode;
    }
    return nullptr;
}

const SstvModeSpec &SstvModeRegistry::defaultTransmitMode() {
    return *find(SstvModeId::ScottieS1);
}
