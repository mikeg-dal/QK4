#ifndef SPECTRUMGEOMETRY_H
#define SPECTRUMGEOMETRY_H

#include <QString>
#include <QtGlobal>

// Frequency-axis arithmetic shared by the panadapter, its overlays and the mini-pan: the
// operating mode as the renderers see it, where the display is centred, where a passband and its
// marker sit, and whether a PAN frame's bins fit the buffers they are written into. Pure
// functions on Qt Core only, so they are covered by test_spectrumgeometry without a GPU.
namespace SpectrumGeometry {

enum class Mode { Unknown, Lsb, Usb, Cw, CwR, Data, DataR, Am, Fm };

// CW pitch assumed until the radio reports CW: the K4's own default, 500 Hz. One value for every
// renderer (the mini-pan once assumed 600).
constexpr int DefaultCwPitchHz = 500;

// K4 DATA sub-mode (DT command): DATA-A, AFSK-A, FSK-D, PSK-D.
enum class DataSubMode { DataA = 0, AfskA = 1, FskD = 2, PskD = 3 };

// Parses RadioState::modeToString() spelling ("LSB", "CW-R", "DATA-R", ...). Anything else,
// including the empty string RadioState reports before the radio has said, is Unknown.
Mode modeFromString(const QString &mode);
DataSubMode dataSubModeFromInt(int subMode);

bool isCw(Mode mode);
bool isData(Mode mode);
// AFSK-A and FSK-D: the RTTY sub-modes whose dial frequency is the mark tone.
bool isRtty(Mode mode, DataSubMode subMode);

// Frequency at the horizontal centre of the panadapter. In CW the K4 centres the passband rather
// than the dial, so the display is shifted by the IF shift (decahertz) — up for CW, down for CW-R.
qint64 displayCenterHz(qint64 centerFreqHz, int ifShiftDecaHz, Mode mode);

// Frequency where the VFO marker is drawn: the passband centre in CW, the dial otherwise.
qint64 markerFreqHz(qint64 tunedFreqHz, int ifShiftDecaHz, Mode mode);

// Horizontal position of a frequency on a display spanning spanHz around centerHz: 0 at the left
// edge, 1 at the right, unclamped so callers can tell off-screen from on-screen. 0 for a span <= 0.
// The panadapter passes its display centre (displayCenterHz), the mini-pan its MiniPAN frame's centre.
float normalizedXFor(qint64 freqHz, qint64 centerHz, int spanHz);

// The inverse: the frequency at normalised position x (not clamped). centerHz for a span <= 0.
qint64 freqForNormalizedX(double x, qint64 centerHz, int spanHz);

// Where the manual notch sits. NM is an audio offset from the dial: above it in USB, CW and the DATA
// and AM/FM modes, below it in LSB and CW-R.
qint64 notchFreqHz(Mode mode, qint64 tunedFreqHz, int notchPitchHz);

struct Passband {
    qint64 lowHz;
    qint64 highHz;
};

// Filter passband edges for a VFO, by mode and DATA sub-mode.
Passband passbandFor(Mode mode, DataSubMode subMode, qint64 tunedFreqHz, int bandwidthHz, int ifShiftDecaHz,
                     int rttyShiftHz);

// Number of bins of a PAN frame that may be used, or 0 if the frame must be dropped: the bins must
// lie inside the payload and must fit the waterfall texture row they are centred in. The count is
// read straight off the wire, so it is validated here rather than assumed to be 1024.
int usableBinCount(qsizetype payloadSize, int binsOffset, int binCount, int maxBins);

// Step between frequency-scale labels: the smallest of gridIntervalHz x {1, 2, 5, 10, 20, 50, ...}
// whose on-screen spacing leaves room for the widest label plus gapPx. One step for the whole scale
// keeps labels on grid lines and evenly spaced; dropping labels one by one where they collided left
// uneven gaps. Returns gridIntervalHz for degenerate input.
int frequencyLabelStepHz(int gridIntervalHz, int spanHz, double widthPx, double widestLabelPx, double gapPx);

// Opacity spectrum_fill.frag gives a pixel — a mirror of its alpha lines, kept in sync by hand
// (shaders cannot be unit tested). y and peakY are in the fill quad's texture space, 0 at the top
// and 1 at the bottom; the trace is at peakY. Below the trace the fill is 0.9 opaque at every
// height: there is no fade towards the bottom, which is where the band noise is drawn.
float fillAlphaFor(float y, float peakY, float glowWidth, float glowIntensity, float glowAlpha);

} // namespace SpectrumGeometry

#endif // SPECTRUMGEOMETRY_H
