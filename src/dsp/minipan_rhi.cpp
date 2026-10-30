#include "minipan_rhi.h"
#include "dsp/spectrumgeometry.h"
#include "dsp/spectrumscale.h"
#include "dsp/waterfallgeometry.h"
#include "logging/logcategories.h"
#include "panadapter_constants.h"
#include "dashgeometry.h"
#include "rhi_utils.h"
#include "ui/styling/k4styles.h"
#include <QLoggingCategory>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QtMath>
#include <algorithm>
#include <array>
#include <cmath>

MiniPanRhiWidget::MiniPanRhiWidget(QWidget *parent) : QRhiWidget(parent) {
    m_clock.start();
    setFixedHeight(K4Styles::Dimensions::VfoPanelHeight);
    setMinimumWidth(180);
    setMaximumWidth(200);

    // Force Metal API on macOS
#ifdef Q_OS_MACOS
    setApi(QRhiWidget::Api::Metal);
#endif

    // Initialize color LUT
    initColorLUT();

    // Allocate waterfall data buffer
    m_waterfallData.resize(TEXTURE_WIDTH * WATERFALL_HISTORY);
    m_waterfallData.fill(0);

    // Create frequency label overlays
    createFrequencyLabels();
}

MiniPanRhiWidget::~MiniPanRhiWidget() {
    // m_gpu's unique_ptrs release the QRhi resources after this body, while the QRhiWidget base and
    // its QRhi are still alive.
}

QRhi *MiniPanRhiWidget::resourcesRhi() const {
    return m_gpu.waterfallTexture ? m_gpu.waterfallTexture->rhi() : nullptr;
}

void MiniPanRhiWidget::initColorLUT() {
    RhiUtils::generateWaterfallColorLUT(m_colorLUT);
}

void MiniPanRhiWidget::releaseResources() {
    // QRhiWidget calls this before the QRhi these objects came from is destroyed (a GPU switch, a
    // device loss, moving to another window). They must go now, not in the next initialize().
    m_gpu = {};
    m_rpDesc = nullptr;
    m_rhiInitialized = false;
    m_pipelinesCreated = false;
    m_rhi = nullptr;
}

void MiniPanRhiWidget::initialize(QRhiCommandBuffer *cb) {
    initializeResources(rhi(), cb);
}

void MiniPanRhiWidget::initializeResources(QRhi *current, QRhiCommandBuffer *cb) {
    // WHY compare, not just test a flag: see PanadapterRhiWidget::initialize — a resize keeps the
    // QRhi, a GPU switch or device loss replaces it.
    if (m_rhi != current) {
        if (m_rhi)
            releaseResources();
        m_rhi = current;
    }
    if (!m_rhi) {
        qCWarning(lcSpectrumMiniPan) << "MiniPan: QRhi is NULL - GPU backend failed to initialize";
        return;
    }
    if (m_rhiInitialized)
        return;

    // A new texture starts empty (uploaded below), so the history ring starts over with it.
    m_waterfallData.fill(0);
    m_waterfallWriteRow = 0;
    m_waterfallNeedsFullClear = false;

    // Load shaders from compiled .qsb resources (shared with main panadapter)
    m_spectrumVert = RhiUtils::loadShader(":/shaders/src/dsp/shaders/spectrum.vert.qsb");
    m_spectrumFrag = RhiUtils::loadShader(":/shaders/src/dsp/shaders/spectrum.frag.qsb");
    m_waterfallVert = RhiUtils::loadShader(":/shaders/src/dsp/shaders/waterfall.vert.qsb");
    m_waterfallFrag = RhiUtils::loadShader(":/shaders/src/dsp/shaders/waterfall.frag.qsb");
    m_overlayVert = RhiUtils::loadShader(":/shaders/src/dsp/shaders/overlay.vert.qsb");
    m_overlayFrag = RhiUtils::loadShader(":/shaders/src/dsp/shaders/overlay.frag.qsb");

    // Create waterfall texture (single channel for dB values)
    m_gpu.waterfallTexture.reset(m_rhi->newTexture(QRhiTexture::R8, QSize(TEXTURE_WIDTH, WATERFALL_HISTORY), 1,
                                                   QRhiTexture::UsedAsTransferSource));
    m_gpu.waterfallTexture->create();

    // Create color LUT texture (256x1 RGBA)
    m_gpu.colorLutTexture.reset(m_rhi->newTexture(QRhiTexture::RGBA8, QSize(256, 1)));
    m_gpu.colorLutTexture->create();

    // Upload color LUT data
    QRhiResourceUpdateBatch *rub = m_rhi->nextResourceUpdateBatch();
    QRhiTextureSubresourceUploadDescription lutUpload(m_colorLUT.constData(), m_colorLUT.size());
    rub->uploadTexture(m_gpu.colorLutTexture.get(), QRhiTextureUploadEntry(0, 0, lutUpload));

    // Upload initial zeroed waterfall data (prevents uninitialized texture garbage)
    QRhiTextureSubresourceUploadDescription waterfallUpload(m_waterfallData.constData(), m_waterfallData.size());
    rub->uploadTexture(m_gpu.waterfallTexture.get(), QRhiTextureUploadEntry(0, 0, waterfallUpload));

    // Create sampler
    m_gpu.sampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                          QRhiSampler::ClampToEdge, QRhiSampler::Repeat));
    m_gpu.sampler->create();

    // Create vertex buffers (dynamic).
    // Spectrum VBO holds up to 2048 triangles × 6 vertices per triangle (two per column, filled
    // top + outline top). Sized for the widest panadapter layout and never resized thereafter.
    constexpr int kSpectrumMaxTriangles = 2048;
    constexpr int kVertsPerTriangle = 6;
    m_gpu.spectrumVbo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer,
                                             kSpectrumMaxTriangles * kVertsPerTriangle * sizeof(float)));
    m_gpu.spectrumVbo->create();

    // Waterfall quad (static)
    float tMax = static_cast<float>(WATERFALL_HISTORY - 1) / WATERFALL_HISTORY;
    float waterfallQuad[] = {
        // position (x, y), texcoord (s, t)
        -1.0f, -1.0f, 0.0f, 0.0f, // bottom-left
        1.0f,  -1.0f, 1.0f, 0.0f, // bottom-right
        1.0f,  1.0f,  1.0f, tMax, // top-right
        -1.0f, -1.0f, 0.0f, 0.0f, // bottom-left
        1.0f,  1.0f,  1.0f, tMax, // top-right
        -1.0f, 1.0f,  0.0f, tMax  // top-left
    };
    m_gpu.waterfallVbo.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(waterfallQuad)));
    m_gpu.waterfallVbo->create();
    rub->uploadStaticBuffer(m_gpu.waterfallVbo.get(), waterfallQuad);

    // Overlay VBO: up to 1024 line segments × 2 (x,y) = 2048 floats. Holds tuning markers,
    // noise-floor line, and related thin overlays that draw above the spectrum trace.
    constexpr int kOverlayMaxSegments = 1024;
    constexpr int kFloatsPerVertex = 2;
    m_gpu.overlayVbo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer,
                                            kOverlayMaxSegments * kFloatsPerVertex * sizeof(float)));
    m_gpu.overlayVbo->create();

    // Dedicated passband buffers (QRhi requires separate buffers to avoid GPU conflicts).
    // 256 floats = 128 vertices = 32 rectangles; ample for the pair of passband fills we draw.
    constexpr int kPassbandFloatCount = 256;
    m_gpu.passbandVbo.reset(
        m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, kPassbandFloatCount * sizeof(float)));
    m_gpu.passbandVbo->create();

    // Dedicated passband edge buffers (12 vertices = 24 floats for 2 rectangles)
    m_gpu.passbandEdgeVbo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, 64 * sizeof(float)));
    m_gpu.passbandEdgeVbo->create();

    // Dedicated center line buffers
    m_gpu.centerLineVbo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, 64 * sizeof(float)));
    m_gpu.centerLineVbo->create();

    // Dedicated notch filter buffers
    m_gpu.notchVbo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, 64 * sizeof(float)));
    m_gpu.notchVbo->create();

    // Dedicated RTTY space-tone dashed-line buffer. Sized from the shared ceiling, NOT from this
    // widget's expected height: the loop below spans the full widget in DEVICE pixels, so the old
    // 512-float size (42 dashes = 420 px, reasoned from a 300 px Retina window) was overrun by any
    // mini-pan taller than that. One constant for both widgets — see PanadapterConstants.
    m_gpu.rttySpaceVbo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer,
                                              PanadapterConstants::MaxDashFloats * sizeof(float)));
    m_gpu.rttySpaceVbo->create();

    // Create uniform buffers
    m_gpu.spectrumUniformBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 16));
    m_gpu.spectrumUniformBuffer->create();

    m_gpu.waterfallUniformBuffer.reset(
        m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(RhiUtils::WaterfallUniforms)));
    m_gpu.waterfallUniformBuffer->create();

    m_gpu.overlayUniformBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32));
    m_gpu.overlayUniformBuffer->create();

    // Dedicated passband uniform buffer
    m_gpu.passbandUniformBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32));
    m_gpu.passbandUniformBuffer->create();

    // Dedicated passband edge uniform buffer
    m_gpu.passbandEdgeUniformBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32));
    m_gpu.passbandEdgeUniformBuffer->create();

    // Dedicated center line uniform buffer
    m_gpu.centerLineUniformBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32));
    m_gpu.centerLineUniformBuffer->create();

    // Dedicated notch filter uniform buffer
    m_gpu.notchUniformBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32));
    m_gpu.notchUniformBuffer->create();

    // Dedicated RTTY space tone uniform buffer
    m_gpu.rttySpaceUniformBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32));
    m_gpu.rttySpaceUniformBuffer->create();

    m_gpu.borderVbo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, 64 * sizeof(float)));
    m_gpu.borderVbo->create();
    m_gpu.borderUniformBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32));
    m_gpu.borderUniformBuffer->create();

    cb->resourceUpdate(rub);

    m_rhiInitialized = true;
}

void MiniPanRhiWidget::createPipelines() {
    if (m_pipelinesCreated)
        return;

    if (!m_spectrumVert.isValid() || !m_spectrumFrag.isValid())
        return;

    m_rpDesc = renderTarget()->renderPassDescriptor();

    // Spectrum pipeline (triangle strip with per-vertex color)
    {
        m_gpu.spectrumSrb.reset(m_rhi->newShaderResourceBindings());
        m_gpu.spectrumSrb->setBindings({QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage, m_gpu.spectrumUniformBuffer.get())});
        m_gpu.spectrumSrb->create();

        m_gpu.spectrumPipeline.reset(m_rhi->newGraphicsPipeline());
        m_gpu.spectrumPipeline->setShaderStages(
            {{QRhiShaderStage::Vertex, m_spectrumVert}, {QRhiShaderStage::Fragment, m_spectrumFrag}});

        QRhiVertexInputLayout inputLayout;
        inputLayout.setBindings({{6 * sizeof(float)}});                         // position(2) + color(4)
        inputLayout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0}, // position
                                   {0, 1, QRhiVertexInputAttribute::Float4, 2 * sizeof(float)}}); // color
        m_gpu.spectrumPipeline->setVertexInputLayout(inputLayout);
        m_gpu.spectrumPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        m_gpu.spectrumPipeline->setShaderResourceBindings(m_gpu.spectrumSrb.get());
        m_gpu.spectrumPipeline->setRenderPassDescriptor(m_rpDesc);

        QRhiGraphicsPipeline::TargetBlend blend;
        blend.enable = true;
        blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        m_gpu.spectrumPipeline->setTargetBlends({blend});

        m_gpu.spectrumPipeline->create();
    }

    // Waterfall pipeline
    {
        m_gpu.waterfallSrb.reset(m_rhi->newShaderResourceBindings());
        m_gpu.waterfallSrb->setBindings(
            {QRhiShaderResourceBinding::uniformBuffer(
                 0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                 m_gpu.waterfallUniformBuffer.get()),
             QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                       m_gpu.waterfallTexture.get(), m_gpu.sampler.get()),
             QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage,
                                                       m_gpu.colorLutTexture.get(), m_gpu.sampler.get())});
        m_gpu.waterfallSrb->create();

        m_gpu.waterfallPipeline.reset(m_rhi->newGraphicsPipeline());
        m_gpu.waterfallPipeline->setShaderStages(
            {{QRhiShaderStage::Vertex, m_waterfallVert}, {QRhiShaderStage::Fragment, m_waterfallFrag}});

        QRhiVertexInputLayout inputLayout;
        inputLayout.setBindings({{4 * sizeof(float)}});                         // position(2) + texcoord(2)
        inputLayout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0}, // position
                                   {0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)}}); // texcoord
        m_gpu.waterfallPipeline->setVertexInputLayout(inputLayout);
        m_gpu.waterfallPipeline->setTopology(QRhiGraphicsPipeline::Triangles);
        m_gpu.waterfallPipeline->setShaderResourceBindings(m_gpu.waterfallSrb.get());
        m_gpu.waterfallPipeline->setRenderPassDescriptor(m_rpDesc);
        m_gpu.waterfallPipeline->create();
    }

    // Overlay pipeline (lines)
    {
        m_gpu.overlaySrb.reset(m_rhi->newShaderResourceBindings());
        m_gpu.overlaySrb->setBindings({QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_gpu.overlayUniformBuffer.get())});
        m_gpu.overlaySrb->create();

        m_gpu.overlayLinePipeline.reset(m_rhi->newGraphicsPipeline());
        m_gpu.overlayLinePipeline->setShaderStages(
            {{QRhiShaderStage::Vertex, m_overlayVert}, {QRhiShaderStage::Fragment, m_overlayFrag}});

        QRhiVertexInputLayout inputLayout;
        inputLayout.setBindings({{2 * sizeof(float)}});
        inputLayout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0}});
        m_gpu.overlayLinePipeline->setVertexInputLayout(inputLayout);
        m_gpu.overlayLinePipeline->setTopology(QRhiGraphicsPipeline::Lines);
        m_gpu.overlayLinePipeline->setShaderResourceBindings(m_gpu.overlaySrb.get());
        m_gpu.overlayLinePipeline->setRenderPassDescriptor(m_rpDesc);

        QRhiGraphicsPipeline::TargetBlend blend;
        blend.enable = true;
        blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        m_gpu.overlayLinePipeline->setTargetBlends({blend});

        m_gpu.overlayLinePipeline->create();

        // Triangle version for filled shapes
        m_gpu.overlayTrianglePipeline.reset(m_rhi->newGraphicsPipeline());
        m_gpu.overlayTrianglePipeline->setShaderStages(
            {{QRhiShaderStage::Vertex, m_overlayVert}, {QRhiShaderStage::Fragment, m_overlayFrag}});
        m_gpu.overlayTrianglePipeline->setVertexInputLayout(inputLayout);
        m_gpu.overlayTrianglePipeline->setTopology(QRhiGraphicsPipeline::Triangles);
        m_gpu.overlayTrianglePipeline->setShaderResourceBindings(m_gpu.overlaySrb.get());
        m_gpu.overlayTrianglePipeline->setRenderPassDescriptor(m_rpDesc);
        m_gpu.overlayTrianglePipeline->setTargetBlends({blend});
        m_gpu.overlayTrianglePipeline->create();
    }

    // Dedicated passband SRB (to avoid GPU buffer conflicts)
    {
        m_gpu.passbandSrb.reset(m_rhi->newShaderResourceBindings());
        m_gpu.passbandSrb->setBindings({QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_gpu.passbandUniformBuffer.get())});
        m_gpu.passbandSrb->create();
    }

    // Dedicated passband edge SRB
    {
        m_gpu.passbandEdgeSrb.reset(m_rhi->newShaderResourceBindings());
        m_gpu.passbandEdgeSrb->setBindings({QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_gpu.passbandEdgeUniformBuffer.get())});
        m_gpu.passbandEdgeSrb->create();
    }

    // Dedicated center line SRB
    {
        m_gpu.centerLineSrb.reset(m_rhi->newShaderResourceBindings());
        m_gpu.centerLineSrb->setBindings({QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_gpu.centerLineUniformBuffer.get())});
        m_gpu.centerLineSrb->create();
    }

    // Dedicated notch filter SRB
    {
        m_gpu.notchSrb.reset(m_rhi->newShaderResourceBindings());
        m_gpu.notchSrb->setBindings({QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_gpu.notchUniformBuffer.get())});
        m_gpu.notchSrb->create();
    }

    // Dedicated RTTY space tone SRB
    {
        m_gpu.rttySpaceSrb.reset(m_rhi->newShaderResourceBindings());
        m_gpu.rttySpaceSrb->setBindings({QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_gpu.rttySpaceUniformBuffer.get())});
        m_gpu.rttySpaceSrb->create();
    }

    // Dedicated frame-border SRB
    {
        m_gpu.borderSrb.reset(m_rhi->newShaderResourceBindings());
        m_gpu.borderSrb->setBindings({QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_gpu.borderUniformBuffer.get())});
        m_gpu.borderSrb->create();
    }

    m_pipelinesCreated = true;
}

void MiniPanRhiWidget::render(QRhiCommandBuffer *cb) {
    // Always clear to black even if not initialized (prevents white/garbage showing)
    if (!m_rhiInitialized) {
        cb->beginPass(renderTarget(), QColor(K4Styles::Colors::Black), {1.0f, 0}, nullptr);
        cb->endPass();
        return;
    }

    // Create pipelines on first render (need render pass descriptor)
    if (!m_pipelinesCreated) {
        createPipelines();
        if (!m_pipelinesCreated) {
            cb->beginPass(renderTarget(), QColor(K4Styles::Colors::Black), {1.0f, 0}, nullptr);
            cb->endPass();
            return;
        }
    }
    Q_ASSERT(resourcesRhi() == m_rhi); // never draw with resources from a QRhi that has gone

    const QSize outputSize = renderTarget()->pixelSize();
    const float w = outputSize.width();
    const float h = outputSize.height();
    const float spectrumHeight = h * m_spectrumRatio;
    const float waterfallHeight = h - spectrumHeight;

    QRhiResourceUpdateBatch *rub = m_rhi->nextResourceUpdateBatch();

    // Full waterfall clear (on disconnect)
    if (m_waterfallNeedsFullClear) {
        QRhiTextureSubresourceUploadDescription fullUpload(m_waterfallData.constData(), m_waterfallData.size());
        rub->uploadTexture(m_gpu.waterfallTexture.get(), QRhiTextureUploadEntry(0, 0, fullUpload));
        m_waterfallNeedsFullClear = false;
    }

    // Update waterfall texture if needed
    if (m_waterfallNeedsUpdate && !m_smoothedSpectrum.isEmpty()) {
        // Resample spectrum to texture width
        int dataSize = m_smoothedSpectrum.size();
        int row = m_waterfallWriteRow;
        // Palette positions fixed at capture under the colour window in effect now, by the
        // panadapter's rule (REF and #WBS), so a row keeps its colour as it scrolls.
        const WaterfallGeometry::ColorWindow window =
            WaterfallGeometry::colorWindowFor(static_cast<float>(m_refLevel), m_scale, m_waterfallColorRange);
        for (int i = 0; i < TEXTURE_WIDTH; ++i) {
            // Average downsampling for waterfall
            int startBin = static_cast<int>(static_cast<float>(i) / TEXTURE_WIDTH * dataSize);
            int endBin = static_cast<int>(static_cast<float>(i + 1) / TEXTURE_WIDTH * dataSize);
            startBin = qBound(0, startBin, dataSize - 1);
            endBin = qBound(startBin + 1, endBin, dataSize);

            float sum = 0.0f;
            int count = endBin - startBin;
            for (int j = startBin; j < endBin; ++j) {
                sum += m_smoothedSpectrum[j];
            }
            const float avgDbm = sum / count;
            m_waterfallData[row * TEXTURE_WIDTH + i] = WaterfallGeometry::capturedByteFor(avgDbm, window);
        }

        QRhiTextureSubresourceUploadDescription rowUpload(
            m_waterfallData.constData() + m_waterfallWriteRow * TEXTURE_WIDTH, TEXTURE_WIDTH);
        rowUpload.setDestinationTopLeft(QPoint(0, m_waterfallWriteRow));
        rowUpload.setSourceSize(QSize(TEXTURE_WIDTH, 1));
        rub->uploadTexture(m_gpu.waterfallTexture.get(), QRhiTextureUploadEntry(0, 0, rowUpload));
        m_waterfallWriteRow = (m_waterfallWriteRow + 1) % WATERFALL_HISTORY;
        m_waterfallNeedsUpdate = false;
    }

    // Update spectrum uniform buffer
    struct {
        float viewportWidth;
        float viewportHeight;
        float padding[2];
    } spectrumUniforms = {w, spectrumHeight, {0, 0}};
    rub->updateDynamicBuffer(m_gpu.spectrumUniformBuffer.get(), 0, sizeof(spectrumUniforms), &spectrumUniforms);

    // Layout comes from RhiUtils so it cannot drift from the shaders or from the main panadapter.
    // Mini pan: tierSpan == span (no cropping, 1:1 mapping), and visibleFraction is 1.0 because it
    // shows its whole buffer — the quad's own texcoords already stop one row short of the row being
    // written.
    //
    // WHY storedRows is 0.0 (the seventh field): that opts out of waterfall.frag's V snap.
    // The panadapter draws one stored row per device pixel, where snapping removes a blend of two
    // moments in time. The mini-pan instead spreads a fixed WATERFALL_HISTORY across whatever
    // height it happens to have, so when that height is under WATERFALL_HISTORY px it is
    // downsampling — and there the linear blend is a cheap box filter, while snapping would drop
    // rows outright. Revisit once the diagnostic reports this widget's px-per-row.
    //
    // colorLow 0 and colorGain 1 pass the texel through: rows store palette positions fixed at
    // capture (WaterfallGeometry::capturedByteFor), as the panadapter's do.
    float scrollOffset = static_cast<float>(m_waterfallWriteRow) / WATERFALL_HISTORY;
    RhiUtils::WaterfallUniforms waterfallUniforms = {scrollOffset,
                                                     static_cast<float>(TEXTURE_WIDTH),
                                                     static_cast<float>(TEXTURE_WIDTH),
                                                     static_cast<float>(TEXTURE_WIDTH),
                                                     static_cast<float>(TEXTURE_WIDTH),
                                                     1.0f,
                                                     0.0f,
                                                     0.0f,
                                                     1.0f,
                                                     0.0f,
                                                     0.0f,
                                                     0.0f};
    rub->updateDynamicBuffer(m_gpu.waterfallUniformBuffer.get(), 0, sizeof(waterfallUniforms), &waterfallUniforms);

    // Build spectrum vertices with peak-hold downsampling
    if (!m_smoothedSpectrum.isEmpty()) {
        QVector<float> vertices;
        int dataSize = m_smoothedSpectrum.size();
        float scale = static_cast<float>(dataSize) / w;

        // Build vertices (triangle strip)
        for (int x = 0; x < static_cast<int>(w); ++x) {
            int startBin = static_cast<int>(x * scale);
            int endBin = static_cast<int>((x + 1) * scale);
            startBin = qBound(0, startBin, dataSize - 1);
            endBin = qBound(startBin + 1, endBin, dataSize);

            float peak = m_smoothedSpectrum[startBin];
            for (int i = startBin + 1; i < endBin; ++i) {
                if (m_smoothedSpectrum[i] > peak) {
                    peak = m_smoothedSpectrum[i];
                }
            }

            // Height is a pure function of dBm over [REF, REF + SCALE], as on the panadapter: nothing
            // is subtracted (no noise-floor tracker) and nothing is scaled down.
            const float lineHeight = SpectrumScale::normalizedForDb(peak, m_minDb, m_maxDb) * spectrumHeight;
            const float y = (std::max)(0.0f, spectrumHeight - lineHeight);

            // Bottom vertex (baseline) - spectrum color with transparency
            vertices.append(static_cast<float>(x));
            vertices.append(spectrumHeight);
            vertices.append(m_spectrumColor.redF() * 0.3f);
            vertices.append(m_spectrumColor.greenF() * 0.3f);
            vertices.append(m_spectrumColor.blueF() * 0.3f);
            vertices.append(0.5f);

            // Top vertex (signal) - full spectrum color
            vertices.append(static_cast<float>(x));
            vertices.append(y);
            vertices.append(m_spectrumColor.redF());
            vertices.append(m_spectrumColor.greenF());
            vertices.append(m_spectrumColor.blueF());
            vertices.append(1.0f);
        }

        if (!vertices.isEmpty()) {
            rub->updateDynamicBuffer(m_gpu.spectrumVbo.get(), 0, vertices.size() * sizeof(float), vertices.constData());
        }
    }

    // Overlays, staged in draw order. Every write goes into this frame's batch before the pass
    // begins, and each draw has its own buffers (see RhiUtils::stageOverlay).
    using SpectrumGeometry::Mode;
    struct OverlayDraw {
        QRhiGraphicsPipeline *pipeline;
        QRhiBuffer *vbo;
        QRhiShaderResourceBindings *srb;
        int vertexCount;
    };
    std::array<OverlayDraw, 7> overlayDraws;
    int overlayDrawCount = 0;
    auto stage = [&](QRhiGraphicsPipeline *pipeline, QRhiBuffer *vbo, QRhiBuffer *ubo, QRhiShaderResourceBindings *srb,
                     const float *verts, int floatCount, const QColor &color) {
        const int vertexCount = RhiUtils::stageOverlay(rub, vbo, ubo, verts, floatCount, w, h, color);
        if (vertexCount > 0 && overlayDrawCount < static_cast<int>(overlayDraws.size()))
            overlayDraws[overlayDrawCount++] = {pipeline, vbo, srb, vertexCount};
    };

    if (m_gpu.overlayLinePipeline && m_gpu.overlayTrianglePipeline) {
        QRhiGraphicsPipeline *triangles = m_gpu.overlayTrianglePipeline.get();
        // Every overlay is placed by absolute frequency with the same rules as the panadapter
        // (SpectrumGeometry), on an axis centred at the dial plus the frame's learned offset, so the
        // overlays stay put while tuning and the data moves beneath them (DialLockedAxis). Nothing is
        // drawn until both the axis (first frame) and the dial are known.
        const bool axisKnown = m_spanHz > 0 && m_tunedFreq > 0;
        const qint64 overlayCenterHz = m_overlayAxis.centerFor(m_tunedFreq, m_centerHz);
        auto xFor = [&](qint64 freqHz) {
            return SpectrumGeometry::normalizedXFor(freqHz, overlayCenterHz, m_spanHz) * w;
        };

        // Filter passband (full height)
        if (axisKnown && m_filterBw > 0 && m_gpu.passbandSrb) {
            const SpectrumGeometry::Passband pb =
                SpectrumGeometry::passbandFor(m_mode, m_dataSubMode, m_tunedFreq, m_filterBw, m_ifShift,
                                              static_cast<int>(PanadapterConstants::RttyShiftHz));
            const float passbandX = std::clamp(xFor(pb.lowHz), 0.0f, w);
            const float rightX = std::clamp(xFor(pb.highHz), 0.0f, w);

            if (rightX > passbandX) {
                QColor fillColor = m_passbandColor;
                fillColor.setAlpha(PanadapterConstants::MiniPanFillAlpha);
                const auto fill = RhiUtils::quadVerts(passbandX, 0, rightX, h);
                stage(triangles, m_gpu.passbandVbo.get(), m_gpu.passbandUniformBuffer.get(), m_gpu.passbandSrb.get(),
                      fill.data(), static_cast<int>(fill.size()), fillColor);

                // Passband edges as 2px rectangles for robust Metal rendering
                if (m_gpu.passbandEdgeSrb) {
                    QColor edgeColor = m_passbandColor;
                    edgeColor.setAlpha(PanadapterConstants::PassbandEdgeAlpha);
                    const float edgeWidth = PanadapterConstants::PassbandEdgeWidth;
                    const auto left = RhiUtils::quadVerts(passbandX, 0, passbandX + edgeWidth, h);
                    const auto right = RhiUtils::quadVerts(rightX, 0, rightX + edgeWidth, h);
                    std::array<float, 24> edges;
                    std::copy(left.begin(), left.end(), edges.begin());
                    std::copy(right.begin(), right.end(), edges.begin() + left.size());
                    stage(triangles, m_gpu.passbandEdgeVbo.get(), m_gpu.passbandEdgeUniformBuffer.get(),
                          m_gpu.passbandEdgeSrb.get(), edges.data(), static_cast<int>(edges.size()), edgeColor);
                }
            }
        }

        // Frequency marker as a filled rectangle, where the panadapter puts it: the passband centre
        // in CW (dial + IS), the dial otherwise. Colour is the VFO theme at full opacity.
        if (axisKnown && m_gpu.centerLineSrb) {
            const float markerX = xFor(SpectrumGeometry::markerFreqHz(m_tunedFreq, m_ifShift, m_mode));
            if (markerX >= 0 && markerX < w) {
                QColor markerColor = m_passbandColor;
                markerColor.setAlpha(255);
                const auto verts = RhiUtils::quadVerts(markerX, 0, markerX + PanadapterConstants::MarkerLineWidth, h);
                stage(triangles, m_gpu.centerLineVbo.get(), m_gpu.centerLineUniformBuffer.get(),
                      m_gpu.centerLineSrb.get(), verts.data(), static_cast<int>(verts.size()), markerColor);
            }
        }

        // RTTY space tone for AFSK-A / FSK-D, one full shift below the dial (mark). The mark tone
        // coincides with the marker.
        if (axisKnown && SpectrumGeometry::isRtty(m_mode, m_dataSubMode) && m_gpu.rttySpaceSrb) {
            const float spaceX = xFor(m_tunedFreq - static_cast<qint64>(PanadapterConstants::RttyShiftHz));
            if (spaceX >= 0 && spaceX < w) {
                m_rttyDashVerts.clear();
                DashGeometry::appendDashedVerticalLine(m_rttyDashVerts, spaceX, PanadapterConstants::RttyDashLineWidth,
                                                       0.0f, h);
                // Yellow-orange tone marker, matches main panadapter
                static const QColor toneColor(255, 200, 0, 200);
                stage(triangles, m_gpu.rttySpaceVbo.get(), m_gpu.rttySpaceUniformBuffer.get(), m_gpu.rttySpaceSrb.get(),
                      m_rttyDashVerts.constData(), m_rttyDashVerts.size(), toneColor);
            }
        }

        // Manual notch, by the panadapter's rule (SpectrumGeometry::notchFreqHz).
        if (axisKnown && m_notchEnabled && m_notchPitchHz > 0 && m_gpu.notchSrb) {
            const float notchX = xFor(SpectrumGeometry::notchFreqHz(m_mode, m_tunedFreq, m_notchPitchHz));
            if (notchX >= 0 && notchX < w) {
                const auto verts = RhiUtils::quadVerts(notchX, 0, notchX + PanadapterConstants::MarkerLineWidth, h);
                stage(triangles, m_gpu.notchVbo.get(), m_gpu.notchUniformBuffer.get(), m_gpu.notchSrb.get(),
                      verts.data(), static_cast<int>(verts.size()), QColor(K4Styles::Colors::TxRed));
            }
        }

        // Separator line — DialogBorder (#333333) keeps minor dividers consistent across UI.
        const std::array<float, 4> separator = {0, spectrumHeight, w, spectrumHeight};
        stage(m_gpu.overlayLinePipeline.get(), m_gpu.overlayVbo.get(), m_gpu.overlayUniformBuffer.get(),
              m_gpu.overlaySrb.get(), separator.data(), static_cast<int>(separator.size()),
              QColor(K4Styles::Colors::DialogBorder));

        // Frame border — matches VFO panels.
        const std::array<float, 16> border = {0,     0,     w - 1, 0,     w - 1, 0,     w - 1, h - 1,
                                              w - 1, h - 1, 0,     h - 1, 0,     h - 1, 0,     0};
        stage(m_gpu.overlayLinePipeline.get(), m_gpu.borderVbo.get(), m_gpu.borderUniformBuffer.get(),
              m_gpu.borderSrb.get(), border.data(), static_cast<int>(border.size()),
              QColor(K4Styles::Colors::PanelBorder));
    }

    cb->resourceUpdate(rub);

    // Begin render pass
    cb->beginPass(renderTarget(), QColor(K4Styles::Colors::DarkBackground), {1.0f, 0}, nullptr);

    // Draw waterfall (bottom portion)
    if (m_gpu.waterfallPipeline) {
        cb->setViewport({0, 0, w, waterfallHeight});
        cb->setGraphicsPipeline(m_gpu.waterfallPipeline.get());
        cb->setShaderResources(m_gpu.waterfallSrb.get());
        const QRhiCommandBuffer::VertexInput waterfallVbufBinding(m_gpu.waterfallVbo.get(), 0);
        cb->setVertexInput(0, 1, &waterfallVbufBinding);
        cb->draw(6);
    }

    // Draw spectrum fill (top portion)
    if (m_gpu.spectrumPipeline && !m_smoothedSpectrum.isEmpty()) {
        cb->setViewport({0, waterfallHeight, w, spectrumHeight});
        cb->setGraphicsPipeline(m_gpu.spectrumPipeline.get());
        cb->setShaderResources(m_gpu.spectrumSrb.get());
        const QRhiCommandBuffer::VertexInput spectrumVbufBinding(m_gpu.spectrumVbo.get(), 0);
        cb->setVertexInput(0, 1, &spectrumVbufBinding);
        cb->draw(static_cast<int>(w) * 2);
    }

    // Draw overlays (full viewport)
    cb->setViewport({0, 0, w, h});
    for (int i = 0; i < overlayDrawCount; ++i) {
        const OverlayDraw &draw = overlayDraws[i];
        cb->setGraphicsPipeline(draw.pipeline);
        cb->setShaderResources(draw.srb);
        const QRhiCommandBuffer::VertexInput vbufBinding(draw.vbo, 0);
        cb->setVertexInput(0, 1, &vbufBinding);
        cb->draw(draw.vertexCount);
    }

    cb->endPass();
}

void MiniPanRhiWidget::updateSpectrum(const QByteArray &bins, qint64 centerHz, int spanHz) {
    if (bins.isEmpty() || spanHz <= 0)
        return;

    // A new span (CW <-> DATA, chosen by the radio) puts different frequencies under every bin and
    // every waterfall column, so the history and the smoothed trace start over, as the panadapter
    // does on a tier crossing.
    if (spanHz != m_spanHz) {
        m_spanHz = spanHz;
        m_smoothedSpectrum.clear();
        m_waterfallData.fill(0);
        m_waterfallWriteRow = 0;
        m_waterfallNeedsFullClear = true;
        updateFrequencyLabels();
    }
    m_centerHz = centerHz;
    m_overlayAxis.frameArrived(centerHz, m_tunedFreq, m_clock.elapsed());

    // MiniPAN bins: 1 byte = 1 dB, dBm = byte - K4_MINIPAN_DBM_OFFSET (measured; see spectrumscale.h).
    m_spectrum.resize(bins.size());
    for (int i = 0; i < bins.size(); ++i)
        m_spectrum[i] = static_cast<quint8>(bins[i]) - SpectrumScale::K4_MINIPAN_DBM_OFFSET;

    // #AVG over the last n frames, in power, like the main pan (PowerAverager)
    m_averager.addEased(m_spectrum, m_smoothedSpectrum);

    m_waterfallNeedsUpdate = true;
    update();
}

void MiniPanRhiWidget::clear() {
    m_averagingLevel = -1; // the next session's #AVG always applies
    m_averager.reset();
    m_spectrum.clear();
    m_smoothedSpectrum.clear();
    m_waterfallWriteRow = 0;
    m_refLevel = SpectrumDisplayLimits::RefLevelDefault;
    m_scale = SpectrumDisplayLimits::ScaleDefault;
    m_waterfallColorRange = -1;
    updateDbRange();
    m_waterfallData.fill(0);
    m_waterfallNeedsFullClear = true;

    // Reset persistent display state for clean reconnect
    m_notchEnabled = false;
    m_notchPitchHz = 0;
    m_mode = SpectrumGeometry::Mode::Usb;
    m_dataSubMode = SpectrumGeometry::DataSubMode::DataA;
    m_centerHz = 0;
    m_spanHz = 0;
    m_tunedFreq = 0;
    m_overlayAxis.reset();
    m_filterBw = 2400;
    m_ifShift = 50;

    // The labels describe the frame's span, which is now unknown: hidden until the next frame.
    updateFrequencyLabels();

    update();
}

void MiniPanRhiWidget::setRefLevel(int level) {
    if (m_refLevel != level) {
        m_refLevel = level;
        updateDbRange();
        update();
    }
}

void MiniPanRhiWidget::setScale(int scale) {
    if (m_scale != scale && scale >= SpectrumDisplayLimits::ScaleMin && scale <= SpectrumDisplayLimits::ScaleMax) {
        m_scale = scale;
        updateDbRange();
        update();
    }
}

void MiniPanRhiWidget::setWaterfallColorRange(int db) {
    // Out-of-range values (the -1 sentinel) fall back inside WaterfallGeometry::colorWindowFor.
    m_waterfallColorRange = db;
}

void MiniPanRhiWidget::updateDbRange() {
    m_minDb = static_cast<float>(m_refLevel);
    m_maxDb = static_cast<float>(m_refLevel + m_scale);
}

void MiniPanRhiWidget::setPassbandColor(const QColor &color) {
    if (m_passbandColor != color) {
        m_passbandColor = color;
        update();
    }
}

void MiniPanRhiWidget::setWaterfallHeight(int percent) {
    float ratio = (100.0f - qBound(10, percent, 90)) / 100.0f;
    m_spectrumRatio = qBound(0.1f, ratio, 0.9f);
    update();
}

void MiniPanRhiWidget::setNotchFilter(bool enabled, int pitchHz) {
    if (m_notchEnabled != enabled || m_notchPitchHz != pitchHz) {
        m_notchEnabled = enabled;
        m_notchPitchHz = pitchHz;
        update();
    }
}

void MiniPanRhiWidget::setMode(const QString &mode) {
    const SpectrumGeometry::Mode parsed = SpectrumGeometry::modeFromString(mode);
    if (m_mode != parsed) {
        m_mode = parsed;
        update();
    }
}

void MiniPanRhiWidget::setDataSubMode(int subMode) {
    const SpectrumGeometry::DataSubMode parsed = SpectrumGeometry::dataSubModeFromInt(subMode);
    if (m_dataSubMode != parsed) {
        m_dataSubMode = parsed;
        update();
    }
}

void MiniPanRhiWidget::setTunedFrequency(qint64 freqHz) {
    if (m_tunedFreq != freqHz) {
        m_tunedFreq = freqHz;
        m_overlayAxis.dialChanged(m_clock.elapsed());
        update();
    }
}

void MiniPanRhiWidget::setFilterBandwidth(int bwHz) {
    if (m_filterBw != bwHz) {
        m_filterBw = bwHz;
        update();
    }
}

void MiniPanRhiWidget::setIfShift(int shift) {
    if (m_ifShift != shift) {
        m_ifShift = shift;
        update();
    }
}

void MiniPanRhiWidget::setAveraging(int level) {
    level = std::clamp(level, 1, 20);
    if (m_averagingLevel == level)
        return;
    m_averagingLevel = level;
    // One rule for both pans (Rule 1): no smoothing at #AVG 1, as on the K4.
    m_averager.setLength(level);
}

void MiniPanRhiWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        emit clicked();
        event->accept();
    } else {
        QRhiWidget::mousePressEvent(event);
    }
}

void MiniPanRhiWidget::resizeEvent(QResizeEvent *event) {
    QRhiWidget::resizeEvent(event);
    positionFrequencyLabels();
}

void MiniPanRhiWidget::createFrequencyLabels() {
    // Frequency annotations over the mini-pan waterfall, on a dark backing so they read over any
    // waterfall colour.
    QString labelStyle = QString("QLabel {"
                                 "  color: %1;"
                                 "  background-color: %3;"
                                 "  padding: 1px 3px;"
                                 "  font-size: %2px;"
                                 "  font-weight: bold;"
                                 "  border-radius: 2px;"
                                 "}")
                             .arg(K4Styles::Colors::TextSecondary)
                             .arg(K4Styles::Dimensions::FontSizeBody)
                             .arg(K4Styles::cssRgba(K4Styles::Colors::Black, K4Styles::Colors::Alpha::LabelScrim));

    // Left label (negative frequency offset)
    m_leftFreqLabel = new QLabel(this);
    m_leftFreqLabel->setStyleSheet(labelStyle);
    m_leftFreqLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    // Right label (positive frequency offset)
    m_rightFreqLabel = new QLabel(this);
    m_rightFreqLabel->setStyleSheet(labelStyle);
    m_rightFreqLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    // Set initial text and position
    updateFrequencyLabels();
    positionFrequencyLabels();
}

void MiniPanRhiWidget::updateFrequencyLabels() {
    if (!m_leftFreqLabel || !m_rightFreqLabel)
        return;

    // The edges are half the frame's span either side of its centre: +-1.5 kHz in CW, +-6 kHz in
    // DATA-A on the K4D measured 2026-10-30. Hidden until a frame has said what the span is.
    const bool known = m_spanHz > 0;
    m_leftFreqLabel->setVisible(known);
    m_rightFreqLabel->setVisible(known);
    if (!known)
        return;
    const QString halfSpan = QString::number(m_spanHz / 2000.0, 'g', 3);
    m_leftFreqLabel->setText(QStringLiteral("-%1 kHz").arg(halfSpan));
    m_rightFreqLabel->setText(QStringLiteral("+%1 kHz").arg(halfSpan));

    m_leftFreqLabel->adjustSize();
    m_rightFreqLabel->adjustSize();
    positionFrequencyLabels();
}

void MiniPanRhiWidget::positionFrequencyLabels() {
    if (!m_leftFreqLabel || !m_rightFreqLabel)
        return;

    const int margin = 2;

    // Position left label in top-left corner
    m_leftFreqLabel->move(margin, margin);

    // Position right label in top-right corner
    m_rightFreqLabel->move(width() - m_rightFreqLabel->width() - margin, margin);
}
