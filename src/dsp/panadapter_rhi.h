#ifndef PANADAPTER_RHI_H
#define PANADAPTER_RHI_H

#include "dsp/poweraverager.h"
#include <QRhiWidget>
#include <rhi/qrhi.h>
#include <QColor>
#include <QElapsedTimer>
#include <QRect>
#include <QSize>
#include <QTimer>
#include <QVector>
#include <array>
#include <memory>
#include "dsp/spectrumgeometry.h"
#include "models/radiostate/spectrumdisplaystate.h"
#include "utils/bandplan.h"
#include "utils/wheelaccumulator.h"

// Forward declarations for overlay widgets
class DbmScaleOverlay;
class FrequencyScaleOverlay;
class BandPlanOverlay;
class QScreen;

// Modern GPU-accelerated panadapter using Qt RHI
// Supports Metal (macOS), DirectX (Windows), Vulkan (Linux)
class PanadapterRhiWidget : public QRhiWidget {
    Q_OBJECT

public:
    explicit PanadapterRhiWidget(QWidget *parent = nullptr);
    ~PanadapterRhiWidget();

    // Width of the dBm / S-unit label column on the left of the spectrum, measured from the label
    // font. Overlays that draw text over the spectrum (DX spots) keep out of it.
    int amplitudeAxisWidthPx() const;

    // Update spectrum data from K4 PAN packet (payload + offset avoids deep copy)
    void updateSpectrum(const QByteArray &payload, int binsOffset, int binCount, qint64 centerFreq, qint32 sampleRate,
                        float noiseFloor);

    // Configuration
    qint64 centerFreq() const { return m_centerFreq; }
    int ifShift() const { return m_ifShift; }
    float spectrumRatio() const { return m_spectrumRatio; }

    void setTunedFrequency(qint64 freq);
    void setFilterBandwidth(int bwHz);
    void setMode(const QString &mode);
    void setDataSubMode(int subMode);
    void setIfShift(int shift);
    void setCwPitch(int pitchHz);
    void setFskMarkTone(int toneHz);
    void clear();

    // Display settings
    void setGridEnabled(bool enabled);
    void setRefLevel(int level);
    void setScale(int scale); // 10-150, affects display gain/range
    // #WBS, 5-30 dB: the waterfall's brightness (WaterfallGeometry::colorWindowFor). -1 = unknown.
    void setWaterfallColorRange(int db);
    void setSpan(int spanHz);
    void setWaterfallHeight(int percent); // 0-100: percentage of display for waterfall
    int span() const { return m_spanHz; }
    void setNotchFilter(bool enabled, int pitchHz);
    void setCursorVisible(bool visible);

    // Band-plan overlay strip (top of panadapter): one row of mode segments with the band name as a
    // tag, hatched past the band edge, plus digital calling-frequency markers. Segments/markers are
    // absolute-frequency; the strip re-maps to the freq axis on pan/zoom.
    void setBandPlan(const QString &bandName, const QVector<BandPlan::BandSegment> &segments,
                     const QVector<BandPlan::BandMarker> &markers);
    void setBandPlanVisible(bool visible);
    void setAmplitudeUnits(bool useSUnits); // false=dBm, true=S-units
    void setAveraging(int level);           // 1-20: K4 #AVG display averaging

    // Secondary VFO (other receiver's passband)
    void setSecondaryVfo(qint64 freq, int bwHz, const QString &mode, int ifShift, int dataSubMode = 0);
    void setSecondaryVisible(bool visible);
    void setSecondaryPassbandColor(const QColor &color);
    void setSecondaryMarkerColor(const QColor &color);

    // Color configuration
    void setPassbandColor(const QColor &color);
    void setFrequencyMarkerColor(const QColor &color);

    // TX frequency marker (shows TX position when RIT/XIT splits TX from RX)
    void setTxMarker(qint64 freq, bool visible);

signals:
    void frequencyClicked(qint64 freq);
    void frequencyDragged(qint64 freq);
    void frequencyScrolled(int steps);
    // Right-click signals (for tuning opposite VFO)
    void frequencyRightClicked(qint64 freq);
    void frequencyRightDragged(qint64 freq);
    // Scale/RefLevel scroll signals (Shift+Wheel, Ctrl+Wheel)
    void scaleScrolled(int steps);
    void refLevelScrolled(int steps);

protected:
    // QRhiWidget overrides
    void initialize(QRhiCommandBuffer *cb) override;
    // Drops every QRhi resource; QRhiWidget calls it before the QRhi they belong to goes away.
    void releaseResources() override;
    // The QRhi the GPU resources were created from; null while there are none, or once that QRhi
    // is gone. Equals rhi() whenever render() draws.
    QRhi *resourcesRhi() const;
    // initialize() for a given QRhi: drops the resources when it is not the one they came from, then
    // creates whatever is missing. initialize() passes rhi(); the lifecycle test passes a Null QRhi
    // of its own, because the offscreen platform has no QRhiWidget support to swap one for real.
    void initializeResources(QRhi *current, QRhiCommandBuffer *cb);
    void render(QRhiCommandBuffer *cb) override;
    void resizeEvent(QResizeEvent *event) override;

    // Input events
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    // Update dBm scale overlay position and values
    void updateDbmScaleOverlay();
    // Update frequency scale overlay position and values
    void updateFreqScaleOverlay();
    // Update band-plan overlay position and segment data
    void updateBandPlanOverlay();
    // Update dB range based on current ref level and scale
    void updateDbRangeFromRefAndScale();
    // Initialization
    void initColorLUT();
    void initSpectrumLUT();
    void createPipelines();

    // Data processing
    static void decompressBins(const QByteArray &bins, QVector<float> &out);
    void updateWaterfallData();

    // Diagnostics — no rendering effect. Gated on the qk4.spectrum.diag category being debug-enabled.
    void logTierCrossing(qint32 oldSampleRate, qint32 newSampleRate, qint32 newTierSpanHz);
    void logRenderGeometry(const QSize &outputSize);
    // Row commit vs. render timing, logged every CadenceReportRows rows: how many display refreshes
    // each row stayed on screen and how many PAN frames arrived too close together to get a row.
    void noteRenderCadence(bool rowCommitted);

    // Re-derives m_devicePxPerRow when the widget's screen, its geometry or its scale changes.
    void updateDevicePxPerRow();

    // Coordinate helpers
    qint64 displayCenterHz() const;
    float freqToNormalized(qint64 freq) const;
    qint64 xToFreq(int x, int w) const;
    int calculateGridInterval(int spanHz) const;
    void rebuildGridIfNeeded(float w, float spectrumHeight, float dpr);
    // RHI resources. Every object created from m_rhi lives in m_gpu, so a QRhi change drops all of
    // them at once (releaseResources()) and a new resource cannot be missed there.
    QRhi *m_rhi = nullptr;
    struct GpuResources {
        std::unique_ptr<QRhiBuffer> waterfallVbo;
        std::unique_ptr<QRhiBuffer> waterfallUniformBuffer;
        std::unique_ptr<QRhiBuffer> overlayVbo;
        std::unique_ptr<QRhiBuffer> overlayUniformBuffer;
        std::unique_ptr<QRhiBuffer> passbandVbo;
        std::unique_ptr<QRhiBuffer> passbandUniformBuffer;
        std::unique_ptr<QRhiBuffer> markerVbo;
        std::unique_ptr<QRhiBuffer> markerUniformBuffer;
        std::unique_ptr<QRhiBuffer> notchVbo;
        std::unique_ptr<QRhiBuffer> notchUniformBuffer;
        std::unique_ptr<QRhiTexture> waterfallTexture;
        std::unique_ptr<QRhiTexture> colorLutTexture;
        std::unique_ptr<QRhiTexture> spectrumDataTexture; // 1D texture for spectrum values
        std::unique_ptr<QRhiSampler> sampler;
        std::unique_ptr<QRhiGraphicsPipeline> waterfallPipeline;
        std::unique_ptr<QRhiGraphicsPipeline> overlayLinePipeline;
        std::unique_ptr<QRhiGraphicsPipeline> overlayTrianglePipeline;
        std::unique_ptr<QRhiBuffer> fullscreenQuadVbo; // Shared fullscreen quad for fragment-shader styles
        // Spectrum fill resources (LUT-based colors)
        std::unique_ptr<QRhiGraphicsPipeline> spectrumFillPipeline;
        std::unique_ptr<QRhiShaderResourceBindings> spectrumFillSrb;
        std::unique_ptr<QRhiBuffer> spectrumFillUniformBuffer;
        std::unique_ptr<QRhiTexture> spectrumFillLutTexture; // 256-entry color LUT for spectrum fill
        std::unique_ptr<QRhiShaderResourceBindings> waterfallSrb;
        std::unique_ptr<QRhiShaderResourceBindings> overlaySrb;
        std::unique_ptr<QRhiShaderResourceBindings> passbandSrb;
        std::unique_ptr<QRhiShaderResourceBindings> markerSrb;
        std::unique_ptr<QRhiShaderResourceBindings> notchSrb;
        // Secondary passband buffers (for other VFO's overlay)
        std::unique_ptr<QRhiBuffer> secondaryPassbandVbo;
        std::unique_ptr<QRhiBuffer> secondaryPassbandUniformBuffer;
        std::unique_ptr<QRhiShaderResourceBindings> secondaryPassbandSrb;
        std::unique_ptr<QRhiBuffer> secondaryMarkerVbo;
        std::unique_ptr<QRhiBuffer> secondaryMarkerUniformBuffer;
        std::unique_ptr<QRhiShaderResourceBindings> secondaryMarkerSrb;
        // TX marker buffers (shows TX position when RIT/XIT active)
        std::unique_ptr<QRhiBuffer> txMarkerVbo;
        std::unique_ptr<QRhiBuffer> txMarkerUniformBuffer;
        std::unique_ptr<QRhiShaderResourceBindings> txMarkerSrb;
        // RTTY space tone line buffers (the mark tone coincides with the VFO marker)
        std::unique_ptr<QRhiBuffer> rttySpaceVbo;
        std::unique_ptr<QRhiBuffer> rttySpaceUniformBuffer;
        std::unique_ptr<QRhiShaderResourceBindings> rttySpaceSrb;
        std::unique_ptr<QRhiBuffer> secRttySpaceVbo;
        std::unique_ptr<QRhiBuffer> secRttySpaceUniformBuffer;
        std::unique_ptr<QRhiShaderResourceBindings> secRttySpaceSrb;
    };
    GpuResources m_gpu;
    QRhiRenderPassDescriptor *m_rpDesc = nullptr;

    bool m_rhiInitialized = false;
    bool m_pipelinesCreated = false;
    // Shader stages (loaded from .qsb files)
    QShader m_spectrumFillVert;
    QShader m_spectrumFillFrag;
    QShader m_waterfallVert;
    QShader m_waterfallFrag;
    QShader m_overlayVert;
    QShader m_overlayFrag;

    // The drawn trace: the full tier's bins, averaged and eased; spectrum_fill.frag windows them to the span
    QVector<float> m_currentSpectrum;
    QVector<float> m_normalizedSpectrum; // Reused per-frame to avoid allocation in render()
    // Per-frame scratch for the dashed overlay lines; cleared, not reallocated, each frame.
    QVector<float> m_notchDashVerts;
    QVector<float> m_rttyDashVerts;
    QVector<float> m_secRttyDashVerts;
    // Grid lines depend only on the spectrum area's size and the device pixel ratio, so they are
    // rebuilt when one of those changes rather than every frame.
    QVector<float> m_gridVerts;
    float m_gridWidth = -1.0f;
    float m_gridHeight = -1.0f;
    float m_gridDpr = -1.0f;
    // Full-tier spectrum data (all 1024 bins for waterfall storage)
    QVector<float> m_tierSpectrum;
    QVector<float> m_tierRawSpectrum;
    qint32 m_lastTierSampleRate = 0; // Detect tier transitions to reset EMA
    int m_waterfallTierBinCount = 0; // Bin count written to current waterfall row
    float m_waterfallTierSpanHz = 0; // Tier span for current waterfall data
    // Waterfall storage. Rows stored and rows shown are deliberately different numbers: the visible
    // count tracks the waterfall's height in device pixels so a row is always one pixel tall, which
    // is what keeps it sharp at any window size. A fixed count stretched to fit, and every row was
    // bilinear-blended across as many screen pixels as the window was tall — measured at 3.05 px
    // per row full screen, against the ~1.25 the old constant was hand-tuned for.
    //
    // Storage is allocated once at the maximum and never resized, so growing the window costs two
    // uniforms rather than a texture rebuild, and reveals history captured while it was small
    // instead of stretching what is already on screen.
    //
    // Memory: 4096 × 2048 × 1 byte = ~8.4 MB. Only the centred 1024 texels of each row are ever
    // sampled — the radio always sends 1024 bins, they are written 1:1 into the middle, and the
    // shader samples at exact texel centres — so width past that is alignment headroom, not
    // resolution. 8192 was measured and produced bit-identical output for twice the memory.
    static constexpr int MAX_WATERFALL_HISTORY = 2048;
    static constexpr int MIN_VISIBLE_ROWS = 64;
    static constexpr int BASE_TEXTURE_WIDTH = 4096;
    int m_textureWidth = BASE_TEXTURE_WIDTH;
    int m_waterfallHistory = MAX_WATERFALL_HISTORY; // rows stored (ring size)
    int m_visibleRows = MAX_WATERFALL_HISTORY;      // rows drawn; follows waterfall pixel height
    int m_waterfallWriteRow = 0;
    // Device pixels per row: one row per physical pixel of the panel, which is two device pixels
    // when macOS halves a HiDPI framebuffer onto a panel of half its size. See
    // WaterfallGeometry::devicePxPerRowFor for why a half-pixel scroll step twinkles.
    int m_devicePxPerRow = 1;
    qreal m_framebufferPerPanel = 1.0;
    // V shift that puts row edges on the panel's pixel grid (WaterfallGeometry::rowShiftFor).
    float m_waterfallRowShift = 0.0f;
    float m_lastLoggedRowShift = 0.0f;
    // What m_devicePxPerRow was derived from. Querying the OS enumerates display modes, so it runs
    // only when one of these changes, not per frame. The pointer is compared, never dereferenced.
    const QScreen *m_panelScaleScreen = nullptr;
    QRect m_panelScaleGeometry;
    qreal m_panelScaleDpr = 0.0;
    QVector<quint8> m_waterfallData;
    bool m_waterfallNeedsUpdate = false;
    bool m_waterfallNeedsFullClear = false;
    // Tier span each stored row was captured at. Diagnostic only: the waterfall is cleared on a
    // tier change, so every stored row shares the current tier and the renderer needs no per-row
    // value. Kept because it reports how much history a crossing is about to discard.
    QVector<float> m_rowTierSpanHz;
    // Last render-target size reported by the geometry diagnostic, so it logs on change not per frame.
    QSize m_lastLoggedOutputSize;
    float m_lastLoggedSpectrumRatio = -1.0f;
    bool m_geometryLoggedWithBins = false;
    // Last logged gap between the radio's reported noise floor and our own minimum bin, so the
    // calibration check reports on change rather than every frame.
    // Render cadence diagnostic (noteRenderCadence); only touched while qk4.spectrum.diag is on.
    static constexpr int CadenceReportRows = 150;
    QElapsedTimer m_cadenceClock;
    qint64 m_cadenceLastRowNs = -1;
    qint64 m_cadenceMinRowNs = 0;
    qint64 m_cadenceMaxRowNs = 0;
    qint64 m_cadenceSumRowNs = 0;
    int m_cadenceRows = 0;
    int m_cadenceRenders = 0;
    int m_cadenceCoalesced = 0;
    std::array<int, 8> m_cadenceRefreshesPerRow{}; // index = refreshes the row was shown for; 7 = 7+
    float m_lastLoggedCalibDelta = 0.0f;
    float m_calibDelta = 0.0f;
    int m_calibSamples = 0;
    bool m_calibLogged = false;

    // Color LUT (256 RGBA entries) - for waterfall
    QVector<quint8> m_colorLUT;
    // Spectrum color LUT (256 RGBA entries) for amplitude-based fill
    QVector<quint8> m_spectrumLUT;

    // Frequency info
    qint64 m_centerFreq = 0;
    float m_noiseFloor = -130.0f;
    qint64 m_tunedFreq = 0;
    int m_filterBw = 2400;
    SpectrumGeometry::Mode m_mode = SpectrumGeometry::Mode::Usb;
    SpectrumGeometry::DataSubMode m_dataSubMode = SpectrumGeometry::DataSubMode::DataA;
    int m_ifShift = 50;
    int m_cwPitch = SpectrumGeometry::DefaultCwPitchHz;
    int m_fskMarkTone = 915; // FSK Mark-Tone (user-configurable from K4 front panel)
    int m_rttyShift = 170;   // Fixed 170 Hz shift between Mark and Space

    // Display settings. The dB window is always [m_refLevel, m_refLevel + m_scale]
    // (updateDbRangeFromRefAndScale); these are the values before the radio's first #REF / #SCL,
    // and clear() returns to them so a reconnect never inherits the last session's window.
    int m_refLevel = SpectrumDisplayLimits::RefLevelDefault;
    int m_scale = SpectrumDisplayLimits::ScaleDefault; // 10-150
    float m_minDb = static_cast<float>(SpectrumDisplayLimits::RefLevelDefault);
    float m_maxDb = static_cast<float>(SpectrumDisplayLimits::RefLevelDefault + SpectrumDisplayLimits::ScaleDefault);
    int m_waterfallColorRange = -1; // #WBS dB; -1 until the radio reports it
    // WHY a fixed value: layout only, until the radio's #WFH arrives (setWaterfallHeight); the
    // radio's height replaces it on every connect, as the RadioState sentinel guarantees an emit.
    float m_spectrumRatio = 0.30f;
    bool m_gridEnabled = true;
    int m_spanHz = 10000;
    bool m_notchEnabled = false;
    int m_notchPitchHz = 0;
    bool m_cursorVisible = true;

    // Mouse drag state
    bool m_isDragging = false;
    bool m_isRightDragging = false;
    QElapsedTimer m_edgeScrollTimer; // Rate limiting for edge drag scrolling

    // Secondary VFO (other receiver's passband)
    qint64 m_secondaryTunedFreq = 0;
    int m_secondaryFilterBw = 0;
    SpectrumGeometry::Mode m_secondaryMode = SpectrumGeometry::Mode::Unknown;
    SpectrumGeometry::DataSubMode m_secondaryDataSubMode = SpectrumGeometry::DataSubMode::DataA;
    int m_secondaryIfShift = 50;
    bool m_secondaryVisible = false;
    QColor m_secondaryPassbandColor{0, 255, 0, 64}; // Green 25% alpha
    QColor m_secondaryMarkerColor{0, 255, 0, 255};  // Green 100% alpha

    // Colors
    QColor m_gridColor{160, 160, 160, 77};      // Light gray with 30% alpha
    QColor m_passbandColor{0, 191, 255, 64};    // Cyan with 25% alpha (VFO A default)
    QColor m_frequencyMarkerColor{0, 191, 255}; // VfoACyan, as SpectrumController sets it
    QColor m_notchColor{255, 0, 0};             // Red

    // RTTY tone overlay colors
    QColor m_rttyToneColor{255, 200, 0, 200};        // Yellow-orange for primary VFO
    QColor m_secondaryRttyToneColor{0, 255, 0, 160}; // Green for secondary VFO

    // TX marker state
    qint64 m_txFreq = 0;
    bool m_txMarkerVisible = false;
    QColor m_txMarkerColor{255, 60, 60, 160}; // Translucent red

    // Display averaging (K4 #AVG control, 1-20)
    // -1 until the radio reports #AVG, so its first report always applies (a legal default here
    // swallowed #AVG1 and left the old smoothing on). Unsmoothed until then.
    int m_averagingLevel = -1;
    PowerAverager m_tierAverager;  // full-tier bins -> waterfall
    PowerAverager m_traceAverager; // cropped bins -> trace

    WheelAccumulator m_wheelAccumulator;

    // dBm scale overlay (child widget for text rendering)
    DbmScaleOverlay *m_dbmScaleOverlay = nullptr;
    // Frequency scale overlay (child widget for frequency labels at boundary)
    FrequencyScaleOverlay *m_freqScaleOverlay = nullptr;
    // Band-plan overlay strip (child widget at top of panadapter)
    BandPlanOverlay *m_bandPlanOverlay = nullptr;
    QString m_bandPlanName;
    QVector<BandPlan::BandSegment> m_bandPlanSegments;
    QVector<BandPlan::BandMarker> m_bandPlanMarkers;
    bool m_bandPlanVisible = true;
};

#endif // PANADAPTER_RHI_H
