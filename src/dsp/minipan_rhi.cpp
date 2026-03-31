#include "minipan_rhi.h"
#include "panadapter_constants.h"
#include "rhi_utils.h"
#include "ui/k4styles.h"
#include <QMouseEvent>
#include <QResizeEvent>
#include <QtMath>
#include <cmath>

MiniPanRhiWidget::MiniPanRhiWidget(QWidget *parent) : QRhiWidget(parent) {
    setFixedHeight(150);
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
    // QRhi resources are automatically cleaned up
}

void MiniPanRhiWidget::initColorLUT() {
    RhiUtils::generateWaterfallColorLUT(m_colorLUT);
}

void MiniPanRhiWidget::initialize(QRhiCommandBuffer *cb) {
    if (m_rhiInitialized)
        return;

    m_rhi = rhi();
    if (!m_rhi) {
        qWarning() << "MiniPan: QRhi is NULL - Metal backend failed";
        return;
    }

    // Load shaders from compiled .qsb resources (shared with main panadapter)
    m_spectrumVert = RhiUtils::loadShader(":/shaders/src/dsp/shaders/spectrum.vert.qsb");
    m_spectrumFrag = RhiUtils::loadShader(":/shaders/src/dsp/shaders/spectrum.frag.qsb");
    m_waterfallVert = RhiUtils::loadShader(":/shaders/src/dsp/shaders/waterfall.vert.qsb");
    m_waterfallFrag = RhiUtils::loadShader(":/shaders/src/dsp/shaders/waterfall.frag.qsb");
    m_overlayVert = RhiUtils::loadShader(":/shaders/src/dsp/shaders/overlay.vert.qsb");
    m_overlayFrag = RhiUtils::loadShader(":/shaders/src/dsp/shaders/overlay.frag.qsb");

    // Create waterfall texture (single channel for dB values)
    m_waterfallTexture.reset(m_rhi->newTexture(QRhiTexture::R8, QSize(TEXTURE_WIDTH, WATERFALL_HISTORY), 1,
                                               QRhiTexture::UsedAsTransferSource));
    m_waterfallTexture->create();

    // Create color LUT texture (256x1 RGBA)
    m_colorLutTexture.reset(m_rhi->newTexture(QRhiTexture::RGBA8, QSize(256, 1)));
    m_colorLutTexture->create();

    // Upload color LUT data
    QRhiResourceUpdateBatch *rub = m_rhi->nextResourceUpdateBatch();
    QRhiTextureSubresourceUploadDescription lutUpload(m_colorLUT.constData(), m_colorLUT.size());
    rub->uploadTexture(m_colorLutTexture.get(), QRhiTextureUploadEntry(0, 0, lutUpload));

    // Upload initial zeroed waterfall data (prevents uninitialized texture garbage)
    QRhiTextureSubresourceUploadDescription waterfallUpload(m_waterfallData.constData(), m_waterfallData.size());
    rub->uploadTexture(m_waterfallTexture.get(), QRhiTextureUploadEntry(0, 0, waterfallUpload));

    // Create sampler
    m_sampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                      QRhiSampler::ClampToEdge, QRhiSampler::Repeat));
    m_sampler->create();

    // Create vertex buffers (dynamic)
    m_spectrumVbo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, 2048 * 6 * sizeof(float)));
    m_spectrumVbo->create();

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
    m_waterfallVbo.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(waterfallQuad)));
    m_waterfallVbo->create();
    rub->uploadStaticBuffer(m_waterfallVbo.get(), waterfallQuad);

    m_overlayVbo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, 1024 * 2 * sizeof(float)));
    m_overlayVbo->create();

    // Dedicated passband buffers (QRhi requires separate buffers to avoid GPU conflicts)
    m_passbandVbo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, 256 * sizeof(float)));
    m_passbandVbo->create();

    // Dedicated passband edge buffers (12 vertices = 24 floats for 2 rectangles)
    m_passbandEdgeVbo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, 64 * sizeof(float)));
    m_passbandEdgeVbo->create();

    // Dedicated center line buffers
    m_centerLineVbo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, 64 * sizeof(float)));
    m_centerLineVbo->create();

    // Dedicated notch filter buffers
    m_notchVbo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, 64 * sizeof(float)));
    m_notchVbo->create();

    // Dedicated RTTY space tone dashed line buffers
    // 30 dash segments max (300px Retina / 10px stride) × 6 verts × 2 floats = 360; use 512 for headroom
    m_rttySpaceVbo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, 512 * sizeof(float)));
    m_rttySpaceVbo->create();

    // Create uniform buffers
    m_spectrumUniformBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 16));
    m_spectrumUniformBuffer->create();

    m_waterfallUniformBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32));
    m_waterfallUniformBuffer->create();

    m_overlayUniformBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32));
    m_overlayUniformBuffer->create();

    // Dedicated passband uniform buffer
    m_passbandUniformBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32));
    m_passbandUniformBuffer->create();

    // Dedicated passband edge uniform buffer
    m_passbandEdgeUniformBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32));
    m_passbandEdgeUniformBuffer->create();

    // Dedicated center line uniform buffer
    m_centerLineUniformBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32));
    m_centerLineUniformBuffer->create();

    // Dedicated notch filter uniform buffer
    m_notchUniformBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32));
    m_notchUniformBuffer->create();

    // Dedicated RTTY space tone uniform buffer
    m_rttySpaceUniformBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32));
    m_rttySpaceUniformBuffer->create();

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
        m_spectrumSrb.reset(m_rhi->newShaderResourceBindings());
        m_spectrumSrb->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage,
                                                                             m_spectrumUniformBuffer.get())});
        m_spectrumSrb->create();

        m_spectrumPipeline.reset(m_rhi->newGraphicsPipeline());
        m_spectrumPipeline->setShaderStages(
            {{QRhiShaderStage::Vertex, m_spectrumVert}, {QRhiShaderStage::Fragment, m_spectrumFrag}});

        QRhiVertexInputLayout inputLayout;
        inputLayout.setBindings({{6 * sizeof(float)}});                         // position(2) + color(4)
        inputLayout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0}, // position
                                   {0, 1, QRhiVertexInputAttribute::Float4, 2 * sizeof(float)}}); // color
        m_spectrumPipeline->setVertexInputLayout(inputLayout);
        m_spectrumPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        m_spectrumPipeline->setShaderResourceBindings(m_spectrumSrb.get());
        m_spectrumPipeline->setRenderPassDescriptor(m_rpDesc);

        QRhiGraphicsPipeline::TargetBlend blend;
        blend.enable = true;
        blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        m_spectrumPipeline->setTargetBlends({blend});

        m_spectrumPipeline->create();
    }

    // Waterfall pipeline
    {
        m_waterfallSrb.reset(m_rhi->newShaderResourceBindings());
        m_waterfallSrb->setBindings(
            {QRhiShaderResourceBinding::uniformBuffer(
                 0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                 m_waterfallUniformBuffer.get()),
             QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                       m_waterfallTexture.get(), m_sampler.get()),
             QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage,
                                                       m_colorLutTexture.get(), m_sampler.get())});
        m_waterfallSrb->create();

        m_waterfallPipeline.reset(m_rhi->newGraphicsPipeline());
        m_waterfallPipeline->setShaderStages(
            {{QRhiShaderStage::Vertex, m_waterfallVert}, {QRhiShaderStage::Fragment, m_waterfallFrag}});

        QRhiVertexInputLayout inputLayout;
        inputLayout.setBindings({{4 * sizeof(float)}});                         // position(2) + texcoord(2)
        inputLayout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0}, // position
                                   {0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)}}); // texcoord
        m_waterfallPipeline->setVertexInputLayout(inputLayout);
        m_waterfallPipeline->setTopology(QRhiGraphicsPipeline::Triangles);
        m_waterfallPipeline->setShaderResourceBindings(m_waterfallSrb.get());
        m_waterfallPipeline->setRenderPassDescriptor(m_rpDesc);
        m_waterfallPipeline->create();
    }

    // Overlay pipeline (lines)
    {
        m_overlaySrb.reset(m_rhi->newShaderResourceBindings());
        m_overlaySrb->setBindings({QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_overlayUniformBuffer.get())});
        m_overlaySrb->create();

        m_overlayLinePipeline.reset(m_rhi->newGraphicsPipeline());
        m_overlayLinePipeline->setShaderStages(
            {{QRhiShaderStage::Vertex, m_overlayVert}, {QRhiShaderStage::Fragment, m_overlayFrag}});

        QRhiVertexInputLayout inputLayout;
        inputLayout.setBindings({{2 * sizeof(float)}});
        inputLayout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0}});
        m_overlayLinePipeline->setVertexInputLayout(inputLayout);
        m_overlayLinePipeline->setTopology(QRhiGraphicsPipeline::Lines);
        m_overlayLinePipeline->setShaderResourceBindings(m_overlaySrb.get());
        m_overlayLinePipeline->setRenderPassDescriptor(m_rpDesc);

        QRhiGraphicsPipeline::TargetBlend blend;
        blend.enable = true;
        blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        m_overlayLinePipeline->setTargetBlends({blend});

        m_overlayLinePipeline->create();

        // Triangle version for filled shapes
        m_overlayTrianglePipeline.reset(m_rhi->newGraphicsPipeline());
        m_overlayTrianglePipeline->setShaderStages(
            {{QRhiShaderStage::Vertex, m_overlayVert}, {QRhiShaderStage::Fragment, m_overlayFrag}});
        m_overlayTrianglePipeline->setVertexInputLayout(inputLayout);
        m_overlayTrianglePipeline->setTopology(QRhiGraphicsPipeline::Triangles);
        m_overlayTrianglePipeline->setShaderResourceBindings(m_overlaySrb.get());
        m_overlayTrianglePipeline->setRenderPassDescriptor(m_rpDesc);
        m_overlayTrianglePipeline->setTargetBlends({blend});
        m_overlayTrianglePipeline->create();
    }

    // Dedicated passband SRB (to avoid GPU buffer conflicts)
    {
        m_passbandSrb.reset(m_rhi->newShaderResourceBindings());
        m_passbandSrb->setBindings({QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_passbandUniformBuffer.get())});
        m_passbandSrb->create();
    }

    // Dedicated passband edge SRB
    {
        m_passbandEdgeSrb.reset(m_rhi->newShaderResourceBindings());
        m_passbandEdgeSrb->setBindings({QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_passbandEdgeUniformBuffer.get())});
        m_passbandEdgeSrb->create();
    }

    // Dedicated center line SRB
    {
        m_centerLineSrb.reset(m_rhi->newShaderResourceBindings());
        m_centerLineSrb->setBindings({QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_centerLineUniformBuffer.get())});
        m_centerLineSrb->create();
    }

    // Dedicated notch filter SRB
    {
        m_notchSrb.reset(m_rhi->newShaderResourceBindings());
        m_notchSrb->setBindings({QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_notchUniformBuffer.get())});
        m_notchSrb->create();
    }

    // Dedicated RTTY space tone SRB
    {
        m_rttySpaceSrb.reset(m_rhi->newShaderResourceBindings());
        m_rttySpaceSrb->setBindings({QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_rttySpaceUniformBuffer.get())});
        m_rttySpaceSrb->create();
    }

    m_pipelinesCreated = true;
}

void MiniPanRhiWidget::render(QRhiCommandBuffer *cb) {
    // Always clear to black even if not initialized (prevents white/garbage showing)
    if (!m_rhiInitialized) {
        cb->beginPass(renderTarget(), Qt::black, {1.0f, 0}, nullptr);
        cb->endPass();
        return;
    }

    // Create pipelines on first render (need render pass descriptor)
    if (!m_pipelinesCreated) {
        createPipelines();
        if (!m_pipelinesCreated) {
            cb->beginPass(renderTarget(), Qt::black, {1.0f, 0}, nullptr);
            cb->endPass();
            return;
        }
    }

    const QSize outputSize = renderTarget()->pixelSize();
    const float w = outputSize.width();
    const float h = outputSize.height();
    const float spectrumHeight = h * m_spectrumRatio;
    const float waterfallHeight = h - spectrumHeight;

    QRhiResourceUpdateBatch *rub = m_rhi->nextResourceUpdateBatch();

    // Full waterfall clear (on disconnect)
    if (m_waterfallNeedsFullClear) {
        QRhiTextureSubresourceUploadDescription fullUpload(m_waterfallData.constData(), m_waterfallData.size());
        rub->uploadTexture(m_waterfallTexture.get(), QRhiTextureUploadEntry(0, 0, fullUpload));
        m_waterfallNeedsFullClear = false;
    }

    // Update waterfall texture if needed
    if (m_waterfallNeedsUpdate && !m_smoothedSpectrum.isEmpty()) {
        // Resample spectrum to texture width
        int dataSize = m_smoothedSpectrum.size();
        int row = m_waterfallWriteRow;
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
            float avgDb = sum / count;

            float normalized = normalizeDb(avgDb);
            m_waterfallData[row * TEXTURE_WIDTH + i] =
                static_cast<quint8>(qBound(0, static_cast<int>(normalized * 255), 255));
        }

        QRhiTextureSubresourceUploadDescription rowUpload(
            m_waterfallData.constData() + m_waterfallWriteRow * TEXTURE_WIDTH, TEXTURE_WIDTH);
        rowUpload.setDestinationTopLeft(QPoint(0, m_waterfallWriteRow));
        rowUpload.setSourceSize(QSize(TEXTURE_WIDTH, 1));
        rub->uploadTexture(m_waterfallTexture.get(), QRhiTextureUploadEntry(0, 0, rowUpload));
        m_waterfallWriteRow = (m_waterfallWriteRow + 1) % WATERFALL_HISTORY;
        m_waterfallNeedsUpdate = false;
    }

    // Update spectrum uniform buffer
    struct {
        float viewportWidth;
        float viewportHeight;
        float padding[2];
    } spectrumUniforms = {w, spectrumHeight, {0, 0}};
    rub->updateDynamicBuffer(m_spectrumUniformBuffer.get(), 0, sizeof(spectrumUniforms), &spectrumUniforms);

    // Update waterfall uniform buffer (matches waterfall.frag shader layout)
    float scrollOffset = static_cast<float>(m_waterfallWriteRow) / WATERFALL_HISTORY;
    struct {
        float scrollOffset;
        float binCount;
        float textureWidth;
        float tierSpanHz; // Mini pan: tierSpan == span (no cropping, 1:1 mapping)
        float spanHz;
        float padding[3];
    } waterfallUniforms = {scrollOffset,
                           static_cast<float>(TEXTURE_WIDTH),
                           static_cast<float>(TEXTURE_WIDTH),
                           static_cast<float>(TEXTURE_WIDTH),
                           static_cast<float>(TEXTURE_WIDTH),
                           {0, 0, 0}};
    rub->updateDynamicBuffer(m_waterfallUniformBuffer.get(), 0, sizeof(waterfallUniforms), &waterfallUniforms);

    // Build spectrum vertices with peak-hold downsampling
    if (!m_smoothedSpectrum.isEmpty()) {
        QVector<float> vertices;
        int dataSize = m_smoothedSpectrum.size();
        float scale = static_cast<float>(dataSize) / w;

        // Find smoothed baseline
        float frameMin = 1.0f;
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

            float normalized = normalizeDb(peak);
            if (normalized < frameMin) {
                frameMin = normalized;
            }
        }

        // Smooth the baseline
        const float baselineAlpha = 0.05f;
        if (m_smoothedBaseline < 0.001f) {
            m_smoothedBaseline = frameMin;
        } else {
            m_smoothedBaseline = baselineAlpha * frameMin + (1.0f - baselineAlpha) * m_smoothedBaseline;
        }

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

            float normalized = normalizeDb(peak);
            float adjusted = normalized - m_smoothedBaseline;
            float lineHeight = adjusted * spectrumHeight * 0.95f * m_heightBoost;
            float y = qMax(0.0f, spectrumHeight - lineHeight);

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
            rub->updateDynamicBuffer(m_spectrumVbo.get(), 0, vertices.size() * sizeof(float), vertices.constData());
        }
    }

    cb->resourceUpdate(rub);

    // Begin render pass
    cb->beginPass(renderTarget(), QColor::fromRgbF(0.04f, 0.04f, 0.04f, 1.0f), {1.0f, 0}, nullptr);

    // Draw waterfall (bottom portion)
    if (m_waterfallPipeline) {
        cb->setViewport({0, 0, w, waterfallHeight});
        cb->setGraphicsPipeline(m_waterfallPipeline.get());
        cb->setShaderResources(m_waterfallSrb.get());
        const QRhiCommandBuffer::VertexInput waterfallVbufBinding(m_waterfallVbo.get(), 0);
        cb->setVertexInput(0, 1, &waterfallVbufBinding);
        cb->draw(6);
    }

    // Draw spectrum fill (top portion)
    if (m_spectrumPipeline && !m_smoothedSpectrum.isEmpty()) {
        cb->setViewport({0, waterfallHeight, w, spectrumHeight});
        cb->setGraphicsPipeline(m_spectrumPipeline.get());
        cb->setShaderResources(m_spectrumSrb.get());
        const QRhiCommandBuffer::VertexInput spectrumVbufBinding(m_spectrumVbo.get(), 0);
        cb->setVertexInput(0, 1, &spectrumVbufBinding);
        cb->draw(static_cast<int>(w) * 2);
    }

    // Draw overlays (full viewport)
    cb->setViewport({0, 0, w, h});

    // Draw separator line, passband, center marker, notch, and border using overlay pipeline
    if (m_overlayLinePipeline && m_overlayTrianglePipeline) {
        // Helper lambda to draw filled quad
        auto drawFilledQuad = [&](float x1, float y1, float x2, float y2, const QColor &color) {
            QVector<float> quadVerts = {x1, y1, x2, y1, x2, y2, x1, y1, x2, y2, x1, y2};

            QRhiResourceUpdateBatch *rub2 = m_rhi->nextResourceUpdateBatch();
            rub2->updateDynamicBuffer(m_overlayVbo.get(), 0, quadVerts.size() * sizeof(float), quadVerts.constData());

            struct {
                float viewportWidth;
                float viewportHeight;
                float pad0, pad1; // std140: padding BEFORE color
                float r, g, b, a;
            } overlayUniforms = {w,
                                 h,
                                 0,
                                 0,
                                 static_cast<float>(color.redF()),
                                 static_cast<float>(color.greenF()),
                                 static_cast<float>(color.blueF()),
                                 static_cast<float>(color.alphaF())};
            rub2->updateDynamicBuffer(m_overlayUniformBuffer.get(), 0, sizeof(overlayUniforms), &overlayUniforms);

            cb->resourceUpdate(rub2);
            cb->setGraphicsPipeline(m_overlayTrianglePipeline.get());
            cb->setShaderResources(m_overlaySrb.get());
            const QRhiCommandBuffer::VertexInput overlayVbufBinding(m_overlayVbo.get(), 0);
            cb->setVertexInput(0, 1, &overlayVbufBinding);
            cb->draw(6);
        };

        // Helper lambda to draw lines
        auto drawLines = [&](const QVector<float> &lineVerts, const QColor &color) {
            QRhiResourceUpdateBatch *rub2 = m_rhi->nextResourceUpdateBatch();
            rub2->updateDynamicBuffer(m_overlayVbo.get(), 0, lineVerts.size() * sizeof(float), lineVerts.constData());

            struct {
                float viewportWidth;
                float viewportHeight;
                float pad0, pad1; // std140: padding BEFORE color
                float r, g, b, a;
            } overlayUniforms = {w,
                                 h,
                                 0,
                                 0,
                                 static_cast<float>(color.redF()),
                                 static_cast<float>(color.greenF()),
                                 static_cast<float>(color.blueF()),
                                 static_cast<float>(color.alphaF())};
            rub2->updateDynamicBuffer(m_overlayUniformBuffer.get(), 0, sizeof(overlayUniforms), &overlayUniforms);

            cb->resourceUpdate(rub2);
            cb->setGraphicsPipeline(m_overlayLinePipeline.get());
            cb->setShaderResources(m_overlaySrb.get());
            const QRhiCommandBuffer::VertexInput overlayVbufBinding(m_overlayVbo.get(), 0);
            cb->setVertexInput(0, 1, &overlayVbufBinding);
            cb->draw(lineVerts.size() / 2);
        };

        // Draw filter passband using DEDICATED buffers (full height)
        float centerX = w / 2;
        if (m_filterBw > 0 && m_passbandSrb) {
            float bwPixels = (static_cast<float>(m_filterBw) * w) / m_bandwidthHz;

            // K4 IF shift: value in 10 Hz units (m_ifShift=140 → 1400 Hz → displayed as 1.40 kHz)
            float shiftHz = m_ifShift * 10.0f;
            float shiftPixels = (shiftHz * w) / m_bandwidthHz;

            float passbandX;
            if (m_mode == "CW" || m_mode == "CW-R") {
                // CW: passband position relative to CW pitch
                // When shift == pitch, passband is centered on center line
                // When shift differs from pitch, passband moves accordingly
                float offsetHz = shiftHz - m_cwPitch;
                float offsetPixels = (offsetHz * w) / m_bandwidthHz;

                if (m_mode == "CW") {
                    // CW: positive offset moves passband right (higher freq)
                    passbandX = centerX + offsetPixels - bwPixels / 2;
                } else {
                    // CW-R: positive offset moves passband left (lower freq)
                    passbandX = centerX - offsetPixels - bwPixels / 2;
                }
            } else if ((m_mode == "DATA" || m_mode == "DATA-R") && m_dataSubMode == 3) {
                // PSK-D: passband centered on dial frequency
                passbandX = centerX - bwPixels / 2;
            } else if ((m_mode == "DATA" || m_mode == "DATA-R") && (m_dataSubMode == 1 || m_dataSubMode == 2)) {
                // AFSK-A / FSK-D: LSB — passband centered half-shift below dial
                float offsetPixels = (PanadapterConstants::RttyHalfShiftHz * w) / m_bandwidthHz;
                passbandX = centerX - offsetPixels - bwPixels / 2;
            } else if (m_mode == "LSB") {
                // LSB: passband center is shiftHz below carrier
                passbandX = centerX - shiftPixels - bwPixels / 2;
            } else {
                // USB/AFSK/DATA-A: passband center is shiftHz above carrier
                passbandX = centerX + shiftPixels - bwPixels / 2;
            }

            // Draw passband quad using DEDICATED passband buffers
            QColor fillColor = m_passbandColor;
            fillColor.setAlpha(PanadapterConstants::MiniPanFillAlpha);

            // Build passband quad vertices (two triangles) - full height
            QVector<float> pbQuadVerts = {
                passbandX, 0, passbandX + bwPixels, 0, passbandX + bwPixels, h, passbandX, 0, passbandX + bwPixels, h,
                passbandX, h};

            QRhiResourceUpdateBatch *pbRub = m_rhi->nextResourceUpdateBatch();
            pbRub->updateDynamicBuffer(m_passbandVbo.get(), 0, pbQuadVerts.size() * sizeof(float),
                                       pbQuadVerts.constData());

            struct {
                float viewportWidth;
                float viewportHeight;
                float pad0, pad1; // std140: padding BEFORE color
                float r, g, b, a;
            } pbUniforms = {w,
                            h,
                            0,
                            0,
                            static_cast<float>(fillColor.redF()),
                            static_cast<float>(fillColor.greenF()),
                            static_cast<float>(fillColor.blueF()),
                            static_cast<float>(fillColor.alphaF())};
            pbRub->updateDynamicBuffer(m_passbandUniformBuffer.get(), 0, sizeof(pbUniforms), &pbUniforms);

            cb->resourceUpdate(pbRub);
            cb->setGraphicsPipeline(m_overlayTrianglePipeline.get());
            cb->setShaderResources(m_passbandSrb.get());
            const QRhiCommandBuffer::VertexInput pbVbufBinding(m_passbandVbo.get(), 0);
            cb->setVertexInput(0, 1, &pbVbufBinding);
            cb->draw(6);

            // Draw passband edge rectangles using DEDICATED buffers (2px wide for robust Metal rendering)
            if (m_passbandEdgeSrb) {
                QColor edgeColor = m_passbandColor;
                edgeColor.setAlpha(PanadapterConstants::PassbandEdgeAlpha);
                float edgeWidth = PanadapterConstants::PassbandEdgeWidth;
                // Left edge rectangle + right edge rectangle = 4 triangles = 12 vertices
                QVector<float> passbandEdges = {// Left edge (2 triangles)
                                                passbandX, 0, passbandX + edgeWidth, 0, passbandX + edgeWidth, h,
                                                passbandX, 0, passbandX + edgeWidth, h, passbandX, h,
                                                // Right edge (2 triangles)
                                                passbandX + bwPixels, 0, passbandX + bwPixels + edgeWidth, 0,
                                                passbandX + bwPixels + edgeWidth, h, passbandX + bwPixels, 0,
                                                passbandX + bwPixels + edgeWidth, h, passbandX + bwPixels, h};

                QRhiResourceUpdateBatch *edgeRub = m_rhi->nextResourceUpdateBatch();
                edgeRub->updateDynamicBuffer(m_passbandEdgeVbo.get(), 0, passbandEdges.size() * sizeof(float),
                                             passbandEdges.constData());

                struct {
                    float viewportWidth;
                    float viewportHeight;
                    float pad0, pad1;
                    float r, g, b, a;
                } edgeUniforms = {w,
                                  h,
                                  0,
                                  0,
                                  static_cast<float>(edgeColor.redF()),
                                  static_cast<float>(edgeColor.greenF()),
                                  static_cast<float>(edgeColor.blueF()),
                                  static_cast<float>(edgeColor.alphaF())};
                edgeRub->updateDynamicBuffer(m_passbandEdgeUniformBuffer.get(), 0, sizeof(edgeUniforms), &edgeUniforms);

                cb->resourceUpdate(edgeRub);
                cb->setGraphicsPipeline(m_overlayTrianglePipeline.get());
                cb->setShaderResources(m_passbandEdgeSrb.get());
                const QRhiCommandBuffer::VertexInput edgeVbufBinding(m_passbandEdgeVbo.get(), 0);
                cb->setVertexInput(0, 1, &edgeVbufBinding);
                cb->draw(12);
            }
        }

        // Draw frequency marker (center line) using DEDICATED buffers
        if (m_centerLineSrb) {
            // Draw as filled rectangle instead of line for robust Metal rendering
            // Derive marker color from VFO theme (passband color at full opacity)
            QColor markerColor = m_passbandColor;
            markerColor.setAlpha(255);
            float markerWidth = PanadapterConstants::MarkerLineWidth;
            QVector<float> centerLineVerts = {
                centerX, 0, centerX + markerWidth, 0, centerX + markerWidth, h, centerX, 0, centerX + markerWidth, h,
                centerX, h};

            QRhiResourceUpdateBatch *clRub = m_rhi->nextResourceUpdateBatch();
            clRub->updateDynamicBuffer(m_centerLineVbo.get(), 0, centerLineVerts.size() * sizeof(float),
                                       centerLineVerts.constData());

            struct {
                float viewportWidth;
                float viewportHeight;
                float pad0, pad1; // std140: padding BEFORE color
                float r, g, b, a;
            } clUniforms = {w,
                            h,
                            0,
                            0,
                            static_cast<float>(markerColor.redF()),
                            static_cast<float>(markerColor.greenF()),
                            static_cast<float>(markerColor.blueF()),
                            static_cast<float>(markerColor.alphaF())};
            clRub->updateDynamicBuffer(m_centerLineUniformBuffer.get(), 0, sizeof(clUniforms), &clUniforms);

            cb->resourceUpdate(clRub);
            cb->setGraphicsPipeline(m_overlayTrianglePipeline.get());
            cb->setShaderResources(m_centerLineSrb.get());
            const QRhiCommandBuffer::VertexInput clVbufBinding(m_centerLineVbo.get(), 0);
            cb->setVertexInput(0, 1, &clVbufBinding);
            cb->draw(6);
        }

        // Draw RTTY space tone dashed line for AFSK-A / FSK-D modes
        // Only the space tone is drawn — the mark tone coincides with the dial frequency center line
        if ((m_mode == "DATA" || m_mode == "DATA-R") && (m_dataSubMode == 1 || m_dataSubMode == 2) && m_rttySpaceSrb &&
            m_bandwidthHz > 0) {
            // Space tone is one full RTTY shift below dial (mark) frequency
            float spaceOffsetPixels = (PanadapterConstants::RttyShiftHz * w) / m_bandwidthHz;
            float spaceX = centerX - spaceOffsetPixels;

            if (spaceX >= 0 && spaceX < w) {
                float dashLen = PanadapterConstants::DashLengthPx;
                float gapLen = PanadapterConstants::DashGapPx;
                float stride = dashLen + gapLen;
                float lineWidth = PanadapterConstants::RttyDashLineWidth;

                QVector<float> verts;
                for (float y = 0.0f; y < h; y += stride) {
                    float yEnd = qMin(y + dashLen, h);
                    verts << spaceX << y << spaceX + lineWidth << y << spaceX + lineWidth << yEnd << spaceX << y
                          << spaceX + lineWidth << yEnd << spaceX << yEnd;
                }

                QRhiResourceUpdateBatch *rtRub = m_rhi->nextResourceUpdateBatch();
                rtRub->updateDynamicBuffer(m_rttySpaceVbo.get(), 0, verts.size() * sizeof(float), verts.constData());

                // Yellow-orange tone marker, matches main panadapter
                static const QColor toneColor(255, 200, 0, 200);

                struct {
                    float viewportWidth;
                    float viewportHeight;
                    float pad0, pad1;
                    float r, g, b, a;
                } rtUniforms = {w,
                                h,
                                0,
                                0,
                                static_cast<float>(toneColor.redF()),
                                static_cast<float>(toneColor.greenF()),
                                static_cast<float>(toneColor.blueF()),
                                static_cast<float>(toneColor.alphaF())};
                rtRub->updateDynamicBuffer(m_rttySpaceUniformBuffer.get(), 0, sizeof(rtUniforms), &rtUniforms);

                cb->resourceUpdate(rtRub);
                cb->setGraphicsPipeline(m_overlayTrianglePipeline.get());
                cb->setShaderResources(m_rttySpaceSrb.get());
                const QRhiCommandBuffer::VertexInput rtVbufBinding(m_rttySpaceVbo.get(), 0);
                cb->setVertexInput(0, 1, &rtVbufBinding);
                cb->draw(verts.size() / 2);
            }
        }

        // Draw notch filter marker using DEDICATED buffers
        // Notch position relative to passband center (consistent with main panadapter)
        if (m_notchEnabled && m_notchPitchHz > 0 && m_notchSrb && m_bandwidthHz > 0) {
            // Mini-pan center (centerX) = passband center.
            // In CW: passband center = tunedFreq + cwPitch, notch RF = tunedFreq + NM.
            // So offset from center = NM - cwPitch.
            // In CW-R: passband center = tunedFreq - cwPitch, notch RF = tunedFreq - NM.
            // So offset from center = -(NM - cwPitch).
            int offsetHz;
            if (m_mode == "LSB") {
                offsetHz = -m_notchPitchHz;
            } else if (m_mode == "CW") {
                offsetHz = m_notchPitchHz - m_cwPitch;
            } else if (m_mode == "CW-R") {
                offsetHz = -(m_notchPitchHz - m_cwPitch);
            } else {
                // USB, DATA, DATA-R, AM, FM
                offsetHz = m_notchPitchHz;
            }

            float notchX = centerX + (static_cast<float>(offsetHz) * w) / m_bandwidthHz;
            bool inBounds = (notchX >= 0 && notchX < w);

            if (inBounds) {
                // Draw as filled rectangle (2px wide) instead of line for robust rendering
                QColor notchColor(255, 0, 0); // Red
                float notchWidth = PanadapterConstants::MarkerLineWidth;
                QVector<float> notchVerts = {
                    notchX, 0, notchX + notchWidth, 0, notchX + notchWidth, h, notchX, 0, notchX + notchWidth, h,
                    notchX, h};

                QRhiResourceUpdateBatch *notchRub = m_rhi->nextResourceUpdateBatch();
                notchRub->updateDynamicBuffer(m_notchVbo.get(), 0, notchVerts.size() * sizeof(float),
                                              notchVerts.constData());

                struct {
                    float viewportWidth;
                    float viewportHeight;
                    float pad0, pad1; // std140: padding BEFORE color
                    float r, g, b, a;
                } notchUniforms = {w,
                                   h,
                                   0,
                                   0,
                                   static_cast<float>(notchColor.redF()),
                                   static_cast<float>(notchColor.greenF()),
                                   static_cast<float>(notchColor.blueF()),
                                   static_cast<float>(notchColor.alphaF())};
                notchRub->updateDynamicBuffer(m_notchUniformBuffer.get(), 0, sizeof(notchUniforms), &notchUniforms);

                cb->resourceUpdate(notchRub);
                cb->setGraphicsPipeline(m_overlayTrianglePipeline.get()); // Use triangles not lines
                cb->setShaderResources(m_notchSrb.get());
                const QRhiCommandBuffer::VertexInput notchVbufBinding(m_notchVbo.get(), 0);
                cb->setVertexInput(0, 1, &notchVbufBinding);
                cb->draw(6); // 2 triangles = 6 vertices
            }
        }

        // Draw separator line
        QVector<float> separatorLine = {0, spectrumHeight, w, spectrumHeight};
        drawLines(separatorLine, QColor(51, 51, 51)); // #333333

        // Draw border
        QVector<float> border = {0, 0, w - 1, 0, w - 1, 0, w - 1, h - 1, w - 1, h - 1, 0, h - 1, 0, h - 1, 0, 0};
        drawLines(border, QColor(68, 68, 68)); // #444444
    }

    cb->endPass();
}

void MiniPanRhiWidget::updateSpectrum(const QByteArray &bins) {
    if (bins.isEmpty())
        return;

    // Decompress MiniPAN data: byte / 10 (different from main panadapter)
    m_spectrum.resize(bins.size());
    for (int i = 0; i < bins.size(); ++i) {
        m_spectrum[i] = static_cast<quint8>(bins[i]) / 10.0f;
    }

    // Skip bins at edges to remove blank areas
    const int SKIP_START = 75;
    const int SKIP_END = 75;

    if (m_spectrum.size() > SKIP_START + SKIP_END + 10) {
        int validSize = m_spectrum.size() - SKIP_START - SKIP_END;
        QVector<float> trimmed(validSize);
        for (int i = 0; i < validSize; ++i) {
            trimmed[i] = m_spectrum[SKIP_START + i];
        }
        m_spectrum = trimmed;
    }

    // Apply asymmetric EMA smoothing (attack fast, decay slow)
    if (m_smoothedSpectrum.size() != m_spectrum.size()) {
        m_smoothedSpectrum = m_spectrum;
    } else {
        for (int i = 0; i < m_spectrum.size(); ++i) {
            float alpha = (m_spectrum[i] > m_smoothedSpectrum[i]) ? m_attackAlpha : m_decayAlpha;
            m_smoothedSpectrum[i] = alpha * m_spectrum[i] + (1.0f - alpha) * m_smoothedSpectrum[i];
        }
    }

    m_waterfallNeedsUpdate = true;
    update();
}

void MiniPanRhiWidget::clear() {
    m_spectrum.clear();
    m_smoothedSpectrum.clear();
    m_waterfallWriteRow = 0;
    m_smoothedBaseline = 0.0f;
    m_waterfallData.fill(0);
    m_waterfallNeedsFullClear = true;

    // Reset persistent display state for clean reconnect
    m_notchEnabled = false;
    m_notchPitchHz = 0;
    m_mode = "USB";
    m_bandwidthHz = 10000;
    m_filterBw = 2400;
    m_ifShift = 50;
    m_cwPitch = 600;

    // Clear frequency labels
    if (m_leftFreqLabel)
        m_leftFreqLabel->setText("");
    if (m_rightFreqLabel)
        m_rightFreqLabel->setText("");

    update();
}

float MiniPanRhiWidget::normalizeDb(float db) {
    return qBound(0.0f, (db - m_minDb) / (m_maxDb - m_minDb), 1.0f);
}

int MiniPanRhiWidget::bandwidthForMode(const QString &mode) const {
    if (mode == "CW" || mode == "CW-R") {
        return PanadapterConstants::SpanNarrowHz;
    }
    if ((mode == "DATA" || mode == "DATA-R") && (m_dataSubMode == 1 || m_dataSubMode == 2 || m_dataSubMode == 3)) {
        return PanadapterConstants::SpanNarrowHz; // FSK-D / AFSK-A: narrow span like CW
    }
    return PanadapterConstants::SpanWideHz;
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
    if (m_mode != mode) {
        m_mode = mode;
        m_bandwidthHz = bandwidthForMode(mode);
        updateFrequencyLabels();
        update();
    }
}

void MiniPanRhiWidget::setDataSubMode(int subMode) {
    if (m_dataSubMode != subMode) {
        m_dataSubMode = subMode;
        int newBw = bandwidthForMode(m_mode);
        if (newBw != m_bandwidthHz) {
            m_bandwidthHz = newBw;
            updateFrequencyLabels();
        }
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

void MiniPanRhiWidget::setCwPitch(int pitchHz) {
    if (m_cwPitch != pitchHz) {
        m_cwPitch = pitchHz;
        update();
    }
}

void MiniPanRhiWidget::setAveraging(int level) {
    level = qBound(1, level, 20);
    if (m_averagingLevel == level)
        return;
    m_averagingLevel = level;
    float t = (level - 1) / 19.0f;
    m_attackAlpha = 0.52f - t * 0.22f; // 0.52 → 0.30
    m_decayAlpha = 0.34f - t * 0.24f;  // 0.34 → 0.10
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
    // Style for the frequency labels - small, semi-transparent background
    QString labelStyle = QString("QLabel {"
                                 "  color: #CCCCCC;"
                                 "  background-color: rgba(0, 0, 0, 160);"
                                 "  padding: 1px 3px;"
                                 "  font-size: %1px;"
                                 "  font-weight: bold;"
                                 "  border-radius: 2px;"
                                 "}")
                             .arg(K4Styles::Dimensions::FontSizeNormal);

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

    // Update text based on bandwidth (mode-dependent)
    if (m_bandwidthHz == 2000) {
        // CW mode: ±1.0 kHz
        m_leftFreqLabel->setText("-1.0 kHz");
        m_rightFreqLabel->setText("+1.0 kHz");
    } else {
        // Voice/Data mode: ±5 kHz
        m_leftFreqLabel->setText("-5 kHz");
        m_rightFreqLabel->setText("+5 kHz");
    }

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
