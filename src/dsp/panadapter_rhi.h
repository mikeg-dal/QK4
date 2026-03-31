#ifndef PANADAPTER_RHI_H
#define PANADAPTER_RHI_H

#include <QRhiWidget>
#include <rhi/qrhi.h>
#include <QColor>
#include <QElapsedTimer>
#include <QTimer>
#include <QVector>
#include <memory>
#include "../ui/wheelaccumulator.h"

// Forward declarations for overlay widgets
class DbmScaleOverlay;
class FrequencyScaleOverlay;

// Modern GPU-accelerated panadapter using Qt RHI
// Supports Metal (macOS), DirectX (Windows), Vulkan (Linux)
class PanadapterRhiWidget : public QRhiWidget {
    Q_OBJECT

public:
    explicit PanadapterRhiWidget(QWidget *parent = nullptr);
    ~PanadapterRhiWidget();

    // Update spectrum data from K4 PAN packet (payload + offset avoids deep copy)
    void updateSpectrum(const QByteArray &payload, int binsOffset, int binCount, qint64 centerFreq, qint32 sampleRate,
                        float noiseFloor);

    // Configuration
    void setDbRange(float minDb, float maxDb);
    void setSpectrumRatio(float ratio);
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
    void setSpan(int spanHz);
    void setWaterfallHeight(int percent); // 0-100: percentage of display for waterfall
    int span() const { return m_spanHz; }
    void setNotchFilter(bool enabled, int pitchHz);
    void setCursorVisible(bool visible);
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
    // Update dB range based on current ref level and scale
    void updateDbRangeFromRefAndScale();
    // Initialization
    void initColorLUT();
    void initSpectrumLUT();
    void createPipelines();

    // Data processing
    void decompressBins(const QByteArray &bins, QVector<float> &out);
    void updateWaterfallData();

    // Coordinate helpers
    float normalizeDb(float db);
    float freqToNormalized(qint64 freq);
    qint64 xToFreq(int x, int w);
    int calculateGridInterval(int spanHz) const;
    // RHI resources
    QRhi *m_rhi = nullptr;
    std::unique_ptr<QRhiBuffer> m_waterfallVbo;
    std::unique_ptr<QRhiBuffer> m_waterfallUniformBuffer;
    std::unique_ptr<QRhiBuffer> m_overlayVbo;
    std::unique_ptr<QRhiBuffer> m_overlayUniformBuffer;
    std::unique_ptr<QRhiBuffer> m_passbandVbo;
    std::unique_ptr<QRhiBuffer> m_passbandUniformBuffer;
    std::unique_ptr<QRhiBuffer> m_markerVbo;
    std::unique_ptr<QRhiBuffer> m_markerUniformBuffer;
    std::unique_ptr<QRhiBuffer> m_notchVbo;
    std::unique_ptr<QRhiBuffer> m_notchUniformBuffer;
    std::unique_ptr<QRhiTexture> m_waterfallTexture;
    std::unique_ptr<QRhiTexture> m_colorLutTexture;
    std::unique_ptr<QRhiTexture> m_spectrumDataTexture; // 1D texture for spectrum values
    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiGraphicsPipeline> m_waterfallPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_overlayLinePipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_overlayTrianglePipeline;
    std::unique_ptr<QRhiBuffer> m_fullscreenQuadVbo; // Shared fullscreen quad for fragment-shader styles
    // Spectrum fill resources (LUT-based colors)
    std::unique_ptr<QRhiGraphicsPipeline> m_spectrumFillPipeline;
    std::unique_ptr<QRhiShaderResourceBindings> m_spectrumFillSrb;
    std::unique_ptr<QRhiBuffer> m_spectrumFillUniformBuffer;
    std::unique_ptr<QRhiTexture> m_spectrumFillLutTexture; // 256-entry color LUT for spectrum fill
    std::unique_ptr<QRhiShaderResourceBindings> m_waterfallSrb;
    std::unique_ptr<QRhiShaderResourceBindings> m_overlaySrb;
    std::unique_ptr<QRhiShaderResourceBindings> m_passbandSrb;
    std::unique_ptr<QRhiShaderResourceBindings> m_markerSrb;
    std::unique_ptr<QRhiShaderResourceBindings> m_notchSrb;
    // Secondary passband buffers (for other VFO's overlay)
    std::unique_ptr<QRhiBuffer> m_secondaryPassbandVbo;
    std::unique_ptr<QRhiBuffer> m_secondaryPassbandUniformBuffer;
    std::unique_ptr<QRhiShaderResourceBindings> m_secondaryPassbandSrb;
    std::unique_ptr<QRhiBuffer> m_secondaryMarkerVbo;
    std::unique_ptr<QRhiBuffer> m_secondaryMarkerUniformBuffer;
    std::unique_ptr<QRhiShaderResourceBindings> m_secondaryMarkerSrb;
    // TX marker buffers (shows TX position when RIT/XIT active)
    std::unique_ptr<QRhiBuffer> m_txMarkerVbo;
    std::unique_ptr<QRhiBuffer> m_txMarkerUniformBuffer;
    std::unique_ptr<QRhiShaderResourceBindings> m_txMarkerSrb;
    // RTTY mark/space tone line buffers (primary VFO)
    std::unique_ptr<QRhiBuffer> m_rttyMarkVbo;
    std::unique_ptr<QRhiBuffer> m_rttyMarkUniformBuffer;
    std::unique_ptr<QRhiShaderResourceBindings> m_rttyMarkSrb;
    std::unique_ptr<QRhiBuffer> m_rttySpaceVbo;
    std::unique_ptr<QRhiBuffer> m_rttySpaceUniformBuffer;
    std::unique_ptr<QRhiShaderResourceBindings> m_rttySpaceSrb;
    // RTTY mark/space tone line buffers (secondary VFO)
    std::unique_ptr<QRhiBuffer> m_secRttyMarkVbo;
    std::unique_ptr<QRhiBuffer> m_secRttyMarkUniformBuffer;
    std::unique_ptr<QRhiShaderResourceBindings> m_secRttyMarkSrb;
    std::unique_ptr<QRhiBuffer> m_secRttySpaceVbo;
    std::unique_ptr<QRhiBuffer> m_secRttySpaceUniformBuffer;
    std::unique_ptr<QRhiShaderResourceBindings> m_secRttySpaceSrb;
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

    // Spectrum data (cropped to display span for live trace)
    QVector<float> m_currentSpectrum;
    QVector<float> m_rawSpectrum;
    QVector<float> m_normalizedSpectrum; // Reused per-frame to avoid allocation in render()
    // Full-tier spectrum data (all 1024 bins for waterfall storage)
    QVector<float> m_tierSpectrum;
    QVector<float> m_tierRawSpectrum;
    qint32 m_lastTierSampleRate = 0; // Detect tier transitions to reset EMA
    int m_waterfallTierBinCount = 0; // Bin count written to current waterfall row
    float m_waterfallTierSpanHz = 0; // Tier span for current waterfall data
    // Waterfall data - sized for 4K/HiDPI displays
    // Memory: 4096 × 1024 × 1 byte = 4 MB (trivial for modern GPUs)
    static constexpr int BASE_WATERFALL_HISTORY = 1024;
    static constexpr int BASE_TEXTURE_WIDTH = 4096;
    int m_textureWidth = BASE_TEXTURE_WIDTH;
    int m_waterfallHistory = BASE_WATERFALL_HISTORY;
    int m_waterfallWriteRow = 0;
    QVector<quint8> m_waterfallData;
    bool m_waterfallNeedsUpdate = false;
    bool m_waterfallNeedsFullClear = false;

    // Color LUT (256 RGBA entries) - for waterfall
    QVector<quint8> m_colorLUT;
    // Spectrum color LUT (256 RGBA entries) for amplitude-based fill
    QVector<quint8> m_spectrumLUT;

    // Frequency info
    qint64 m_centerFreq = 0;
    qint32 m_sampleRate = 192000;
    float m_noiseFloor = -130.0f;
    qint64 m_tunedFreq = 0;
    int m_filterBw = 2400;
    QString m_mode = "USB";
    int m_dataSubMode = 0;
    int m_ifShift = 50;
    int m_cwPitch = 500;
    int m_fskMarkTone = 915; // FSK Mark-Tone (user-configurable from K4 front panel)
    int m_rttyShift = 170;   // Fixed 170 Hz shift between Mark and Space

    // Display settings
    float m_minDb = -138.0f;
    float m_maxDb = -58.0f;
    float m_spectrumRatio = 0.30f;
    float m_smoothedBaseline = 0.0f;
    bool m_gridEnabled = true;
    int m_refLevel = -110;
    int m_scale = 75; // 10-150, default 75 (neutral)
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
    QString m_secondaryMode = "";
    int m_secondaryDataSubMode = 0;
    int m_secondaryIfShift = 50;
    bool m_secondaryVisible = false;
    QColor m_secondaryPassbandColor{0, 255, 0, 64}; // Green 25% alpha
    QColor m_secondaryMarkerColor{0, 255, 0, 255};  // Green 100% alpha

    // Colors
    QColor m_gridColor{160, 160, 160, 77};      // Light gray with 30% alpha
    QColor m_passbandColor{0, 191, 255, 64};    // Cyan with 25% alpha (VFO A default)
    QColor m_frequencyMarkerColor{0, 140, 200}; // Darker cyan (VFO A default)
    QColor m_notchColor{255, 0, 0};             // Red

    // RTTY tone overlay colors
    QColor m_rttyToneColor{255, 200, 0, 200};        // Yellow-orange for primary VFO
    QColor m_secondaryRttyToneColor{0, 255, 0, 160}; // Green for secondary VFO

    // TX marker state
    qint64 m_txFreq = 0;
    bool m_txMarkerVisible = false;
    QColor m_txMarkerColor{255, 60, 60, 160}; // Translucent red

    // Display averaging (K4 #AVG control, 1-20)
    int m_averagingLevel = 1;
    float m_attackAlpha = 0.52f;
    float m_decayAlpha = 0.34f;

    WheelAccumulator m_wheelAccumulator;

    // dBm scale overlay (child widget for text rendering)
    DbmScaleOverlay *m_dbmScaleOverlay = nullptr;
    // Frequency scale overlay (child widget for frequency labels at boundary)
    FrequencyScaleOverlay *m_freqScaleOverlay = nullptr;
};

#endif // PANADAPTER_RHI_H
