#ifndef MINIPAN_RHI_H
#define MINIPAN_RHI_H

#include "dsp/poweraverager.h"
#include <QRhiWidget>
#include <rhi/qrhi.h>
#include <QColor>
#include <QElapsedTimer>
#include <QLabel>
#include <QTimer>
#include <QVector>
#include <memory>
#include "dsp/spectrumgeometry.h"
#include "models/radiostate/spectrumdisplaystate.h"
#include "ui/styling/k4constants.h"

// Compact Mini-Pan widget for VFO area using Qt RHI
// GPU-accelerated via Metal (macOS), DirectX (Windows), Vulkan (Linux)
// Shows simplified spectrum + waterfall in a small form factor.
// The frequency axis comes from each MiniPAN frame: its centre and span (3 kHz in CW, 12 kHz in
// DATA-A, chosen by the radio). Overlays are placed by absolute frequency on that axis.
class MiniPanRhiWidget : public QRhiWidget {
    Q_OBJECT

public:
    explicit MiniPanRhiWidget(QWidget *parent = nullptr);
    ~MiniPanRhiWidget();

    // Update spectrum from a MiniPAN packet (TYPE=3): its bins, centre (Hz) and span (Hz).
    void updateSpectrum(const QByteArray &bins, qint64 centerHz, int spanHz);
    void clear();

    // Passband color (default blue)
    void setPassbandColor(const QColor &color);

    // Waterfall height (percent waterfall, matching WTRFALL control)
    void setWaterfallHeight(int percent);

    // Notch filter visualization
    void setNotchFilter(bool enabled, int pitchHz);
    void setMode(const QString &mode);
    void setDataSubMode(int subMode);

    // Filter passband visualization
    void setTunedFrequency(qint64 freqHz); // the receiver's dial, RIT included
    void setFilterBandwidth(int bwHz);
    void setIfShift(int shift);
    void setAveraging(int level); // 1-20: K4 #AVG display averaging

    // Amplitude: the receiver's #REF (dBm, bottom of the trace) and the shared #SCL (dB, its height),
    // and #WBS for the waterfall's colours — the panadapter's window and rules.
    void setRefLevel(int level);
    void setScale(int scale);
    void setWaterfallColorRange(int db);

signals:
    void clicked(); // Emitted when user clicks to toggle back to normal view

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

private:
    // Initialization
    void initColorLUT();
    void createPipelines();
    void createFrequencyLabels();
    void updateFrequencyLabels();
    void positionFrequencyLabels();

    void updateDbRange();

    // RHI resources. Every object created from m_rhi lives in m_gpu, so a QRhi change drops all of
    // them at once (releaseResources()) and a new resource cannot be missed there.
    QRhi *m_rhi = nullptr;
    struct GpuResources {
        std::unique_ptr<QRhiBuffer> spectrumVbo;
        std::unique_ptr<QRhiBuffer> spectrumUniformBuffer;
        std::unique_ptr<QRhiBuffer> waterfallVbo;
        std::unique_ptr<QRhiBuffer> waterfallUniformBuffer;
        std::unique_ptr<QRhiBuffer> overlayVbo;
        std::unique_ptr<QRhiBuffer> overlayUniformBuffer;
        std::unique_ptr<QRhiTexture> waterfallTexture;
        std::unique_ptr<QRhiTexture> colorLutTexture;
        std::unique_ptr<QRhiSampler> sampler;
        std::unique_ptr<QRhiGraphicsPipeline> spectrumPipeline;
        std::unique_ptr<QRhiGraphicsPipeline> waterfallPipeline;
        std::unique_ptr<QRhiGraphicsPipeline> overlayLinePipeline;
        std::unique_ptr<QRhiGraphicsPipeline> overlayTrianglePipeline;
        std::unique_ptr<QRhiShaderResourceBindings> spectrumSrb;
        std::unique_ptr<QRhiShaderResourceBindings> waterfallSrb;
        std::unique_ptr<QRhiShaderResourceBindings> overlaySrb;

        // Dedicated passband buffers (QRhi requires separate buffers to avoid conflicts)
        std::unique_ptr<QRhiBuffer> passbandVbo;
        std::unique_ptr<QRhiBuffer> passbandUniformBuffer;
        std::unique_ptr<QRhiShaderResourceBindings> passbandSrb;

        // Dedicated passband edge buffers (separate from fill to avoid conflicts)
        std::unique_ptr<QRhiBuffer> passbandEdgeVbo;
        std::unique_ptr<QRhiBuffer> passbandEdgeUniformBuffer;
        std::unique_ptr<QRhiShaderResourceBindings> passbandEdgeSrb;

        // Dedicated center line buffers
        std::unique_ptr<QRhiBuffer> centerLineVbo;
        std::unique_ptr<QRhiBuffer> centerLineUniformBuffer;
        std::unique_ptr<QRhiShaderResourceBindings> centerLineSrb;

        // Dedicated notch filter buffers (QRhi requires separate buffers to avoid conflicts)
        std::unique_ptr<QRhiBuffer> notchVbo;
        std::unique_ptr<QRhiBuffer> notchUniformBuffer;
        std::unique_ptr<QRhiShaderResourceBindings> notchSrb;

        // Dedicated RTTY space tone dashed line buffers
        std::unique_ptr<QRhiBuffer> rttySpaceVbo;
        std::unique_ptr<QRhiBuffer> rttySpaceUniformBuffer;
        std::unique_ptr<QRhiShaderResourceBindings> rttySpaceSrb;

        // Dedicated frame-border buffers: the spectrum/waterfall separator uses the shared overlay
        // buffers, and two draws in one frame cannot share a dynamic buffer.
        std::unique_ptr<QRhiBuffer> borderVbo;
        std::unique_ptr<QRhiBuffer> borderUniformBuffer;
        std::unique_ptr<QRhiShaderResourceBindings> borderSrb;
    };
    GpuResources m_gpu;
    QRhiRenderPassDescriptor *m_rpDesc = nullptr;

    bool m_rhiInitialized = false;
    bool m_pipelinesCreated = false;

    // Shader stages (loaded from .qsb files - shared with panadapter)
    QShader m_spectrumVert;
    QShader m_spectrumFrag;
    QShader m_waterfallVert;
    QShader m_waterfallFrag;
    QShader m_overlayVert;
    QShader m_overlayFrag;

    // Spectrum data
    QVector<float> m_spectrum;
    QVector<float> m_smoothedSpectrum;
    QVector<float> m_rttyDashVerts; // per-frame scratch, cleared rather than reallocated

    // Waterfall data
    static constexpr int WATERFALL_HISTORY = 100;
    static constexpr int TEXTURE_WIDTH = 512;
    int m_waterfallWriteRow = 0;
    QVector<quint8> m_waterfallData;
    bool m_waterfallNeedsUpdate = false;
    bool m_waterfallNeedsFullClear = false;

    // Color LUT (256 RGBA entries)
    QVector<quint8> m_colorLUT;

    // Amplitude window: the receiver's own [REF, REF + SCALE] (#REF or #REF$, and #SCL), as on the
    // panadapter. Defaults only until the radio reports them.
    int m_refLevel = SpectrumDisplayLimits::RefLevelDefault;
    int m_scale = SpectrumDisplayLimits::ScaleDefault;
    int m_waterfallColorRange = -1; // #WBS dB; -1 until the radio reports it
    float m_minDb = static_cast<float>(SpectrumDisplayLimits::RefLevelDefault);
    float m_maxDb = static_cast<float>(SpectrumDisplayLimits::RefLevelDefault + SpectrumDisplayLimits::ScaleDefault);
    float m_spectrumRatio = 0.40f; // 40% spectrum, 60% waterfall

    // Spectrum line color — default to the VFO A cyan theme; caller may override.
    QColor m_spectrumColor{QColor(K4Styles::Colors::VfoACyan)};

    // Passband color
    QColor m_passbandColor{0, 128, 255, 64}; // Blue with 25% alpha

    // Notch filter visualization
    bool m_notchEnabled = false;
    int m_notchPitchHz = 0;
    SpectrumGeometry::Mode m_mode = SpectrumGeometry::Mode::Usb;
    SpectrumGeometry::DataSubMode m_dataSubMode = SpectrumGeometry::DataSubMode::DataA;
    // Frequency axis of the latest MiniPAN frame; 0 until the first one arrives.
    qint64 m_centerHz = 0;
    int m_spanHz = 0;
    // Overlays are centred on the dial plus the frame's learned offset, so they stay fixed while
    // tuning and only the data moves (SpectrumGeometry::DialLockedAxis). m_clock times dial changes.
    SpectrumGeometry::DialLockedAxis m_overlayAxis;
    QElapsedTimer m_clock;

    // Display averaging (K4 #AVG control, 1-20)
    int m_averagingLevel = -1; // until the radio reports #AVG; see PanadapterRhiWidget
    PowerAverager m_averager;  // #AVG, see PanadapterRhiWidget

    // Filter passband visualization
    qint64 m_tunedFreq = 0; // 0 until the controller reports the dial
    int m_filterBw = 2400;
    int m_ifShift = 50;

    // Frequency label overlays
    QLabel *m_leftFreqLabel = nullptr;
    QLabel *m_rightFreqLabel = nullptr;
};

#endif // MINIPAN_RHI_H
