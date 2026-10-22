#ifndef MINIPAN_RHI_H
#define MINIPAN_RHI_H

#include <QRhiWidget>
#include <rhi/qrhi.h>
#include <QColor>
#include <QLabel>
#include <QTimer>
#include <QVector>
#include <memory>
#include "ui/styling/k4constants.h"

// Compact Mini-Pan widget for VFO area using Qt RHI
// GPU-accelerated via Metal (macOS), DirectX (Windows), Vulkan (Linux)
// Shows simplified spectrum + waterfall in a small form factor
// Fixed mode-dependent span around VFO frequency
class MiniPanRhiWidget : public QRhiWidget {
    Q_OBJECT

public:
    explicit MiniPanRhiWidget(QWidget *parent = nullptr);
    ~MiniPanRhiWidget();

    // Update spectrum from MiniPAN packet (TYPE=3)
    void updateSpectrum(const QByteArray &bins);
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
    void setFilterBandwidth(int bwHz);
    void setIfShift(int shift);
    void setCwPitch(int pitchHz);
    void setAveraging(int level); // 1-20: K4 #AVG display averaging

signals:
    void clicked(); // Emitted when user clicks to toggle back to normal view

protected:
    // QRhiWidget overrides
    void initialize(QRhiCommandBuffer *cb) override;
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

    // Data processing
    float normalizeDb(float db);
    int bandwidthForMode(const QString &mode) const;

    // RHI resources
    QRhi *m_rhi = nullptr;
    std::unique_ptr<QRhiBuffer> m_spectrumVbo;
    std::unique_ptr<QRhiBuffer> m_spectrumUniformBuffer;
    std::unique_ptr<QRhiBuffer> m_waterfallVbo;
    std::unique_ptr<QRhiBuffer> m_waterfallUniformBuffer;
    std::unique_ptr<QRhiBuffer> m_overlayVbo;
    std::unique_ptr<QRhiBuffer> m_overlayUniformBuffer;
    std::unique_ptr<QRhiTexture> m_waterfallTexture;
    std::unique_ptr<QRhiTexture> m_colorLutTexture;
    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiGraphicsPipeline> m_spectrumPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_waterfallPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_overlayLinePipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_overlayTrianglePipeline;
    std::unique_ptr<QRhiShaderResourceBindings> m_spectrumSrb;
    std::unique_ptr<QRhiShaderResourceBindings> m_waterfallSrb;
    std::unique_ptr<QRhiShaderResourceBindings> m_overlaySrb;

    // Dedicated passband buffers (QRhi requires separate buffers to avoid conflicts)
    std::unique_ptr<QRhiBuffer> m_passbandVbo;
    std::unique_ptr<QRhiBuffer> m_passbandUniformBuffer;
    std::unique_ptr<QRhiShaderResourceBindings> m_passbandSrb;

    // Dedicated passband edge buffers (separate from fill to avoid conflicts)
    std::unique_ptr<QRhiBuffer> m_passbandEdgeVbo;
    std::unique_ptr<QRhiBuffer> m_passbandEdgeUniformBuffer;
    std::unique_ptr<QRhiShaderResourceBindings> m_passbandEdgeSrb;

    // Dedicated center line buffers
    std::unique_ptr<QRhiBuffer> m_centerLineVbo;
    std::unique_ptr<QRhiBuffer> m_centerLineUniformBuffer;
    std::unique_ptr<QRhiShaderResourceBindings> m_centerLineSrb;

    // Dedicated notch filter buffers (QRhi requires separate buffers to avoid conflicts)
    std::unique_ptr<QRhiBuffer> m_notchVbo;
    std::unique_ptr<QRhiBuffer> m_notchUniformBuffer;
    std::unique_ptr<QRhiShaderResourceBindings> m_notchSrb;

    // Dedicated RTTY space tone dashed line buffers
    std::unique_ptr<QRhiBuffer> m_rttySpaceVbo;
    std::unique_ptr<QRhiBuffer> m_rttySpaceUniformBuffer;
    std::unique_ptr<QRhiShaderResourceBindings> m_rttySpaceSrb;

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

    // Waterfall data
    static constexpr int WATERFALL_HISTORY = 100;
    static constexpr int TEXTURE_WIDTH = 512;
    int m_waterfallWriteRow = 0;
    QVector<quint8> m_waterfallData;
    bool m_waterfallNeedsUpdate = false;
    bool m_waterfallNeedsFullClear = false;

    // Color LUT (256 RGBA entries)
    QVector<quint8> m_colorLUT;

    // Display settings
    float m_minDb = -1.0f;
    float m_maxDb = 4.0f;
    float m_smoothedBaseline = 0.0f;
    float m_heightBoost = 1.0f;
    float m_spectrumRatio = 0.40f; // 40% spectrum, 60% waterfall

    // Spectrum line color — default to the VFO A cyan theme; caller may override.
    QColor m_spectrumColor{QColor(K4Styles::Colors::VfoACyan)};

    // Passband color
    QColor m_passbandColor{0, 128, 255, 64}; // Blue with 25% alpha

    // Notch filter visualization
    bool m_notchEnabled = false;
    int m_notchPitchHz = 0;
    QString m_mode = "USB";
    int m_dataSubMode = 0;
    int m_bandwidthHz = 10000; // Mode-dependent span: CW=3kHz, Voice/Data=10kHz

    // Display averaging (K4 #AVG control, 1-20)
    int m_averagingLevel = 1;

    // Filter passband visualization
    int m_filterBw = 2400;
    int m_ifShift = 50;
    int m_cwPitch = 600;

    // Frequency label overlays
    QLabel *m_leftFreqLabel = nullptr;
    QLabel *m_rightFreqLabel = nullptr;
};

#endif // MINIPAN_RHI_H
