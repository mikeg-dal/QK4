#ifndef SSTVMODEREGISTRY_H
#define SSTVMODEREGISTRY_H

#include <QString>
#include <QVector>

enum class SstvModeId {
    // Values 0-5 were persisted by the first SSTV builds. Keep them stable.
    ScottieS1 = 0,
    ScottieS2 = 1,
    MartinM1 = 2,
    MartinM2 = 3,
    Robot36 = 4,
    Pd120 = 5,
    MartinM3 = 6,
    MartinM4 = 7,
    ScottieDx = 8,
    ScottieS3 = 9,
    ScottieS4 = 10,
    Pd50 = 11,
    Pd90 = 12,
    Pd160 = 13,
    Pd180 = 14,
    Pd240 = 15,
    Pd290 = 16,
    WraaseSc2120 = 17,
    WraaseSc2180 = 18,
    PasokonP3 = 19,
    PasokonP5 = 20,
    PasokonP7 = 21
};

enum class SstvColorFamily { RgbSequential, GbrSequential, RobotYuv, PdYuv };

enum class SstvLineLayout { Martin, Scottie, Robot36, Pd, Wraase, Pasokon };

struct SstvModeSpec {
    SstvModeId id;
    QString displayName;
    int width;
    int height;
    int visCode;
    int durationMs;
    SstvColorFamily colorFamily;
    SstvLineLayout lineLayout;
    // All values describe the on-air waveform. componentMs is one RGB or PD
    // channel scan; lineTimeMs is the complete sync-to-sync period.
    double lineSyncMs = 0.0;
    double porchMs = 0.0;
    double componentMs = 0.0;
    double lineTimeMs = 0.0;
    bool encoderImplemented = false;
    bool decoderImplemented = false;
};

class SstvModeRegistry {
public:
    static const QVector<SstvModeSpec> &all();
    static const SstvModeSpec *find(SstvModeId id);
    static const SstvModeSpec *findByVis(int visCode);
    static const SstvModeSpec &defaultTransmitMode();
};

#endif // SSTVMODEREGISTRY_H
