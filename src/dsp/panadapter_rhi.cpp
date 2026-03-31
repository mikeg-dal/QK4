#include "panadapter_rhi.h"
#include "panadapter_constants.h"
#include "rhi_utils.h"
#include "ui/k4styles.h"
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QtMath>
#include <cmath>
#include <cstring>

// Transparent overlay widget for dBm/S-unit scale labels
class DbmScaleOverlay : public QWidget {
public:
    DbmScaleOverlay(QWidget *parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_TranslucentBackground);
    }

    void setDbRange(float minDb, float maxDb) {
        m_minDb = minDb;
        m_maxDb = maxDb;
        update();
    }

    void setUseSUnits(bool useSUnits) {
        if (m_useSUnits != useSUnits) {
            m_useSUnits = useSUnits;
            update();
        }
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        QFont scaleFont = K4Styles::Fonts::dataFont(K4Styles::Dimensions::FontSizeNormal, QFont::Normal);
        painter.setFont(scaleFont);
        painter.setPen(Qt::white);

        const int labelCount = 9; // Keep 9 divisions for grid alignment
        const float dbRange = m_maxDb - m_minDb;
        const int leftMargin = 4;
        const int h = height();

        QFontMetrics fm(scaleFont);
        int textHeight = fm.height();

        for (int i = 0; i < labelCount; ++i) {
            // Skip top and bottom labels for breathing room
            if (i == 0 || i == labelCount - 1)
                continue;

            float dbValue = m_maxDb - (static_cast<float>(i) / 8.0f) * dbRange;
            int yPos = h * i / 8;

            QString label;
            if (m_useSUnits) {
                label = dbmToSUnits(dbValue);
            } else {
                label = QString("%1 dBm").arg(static_cast<int>(dbValue));
            }

            // Vertically center on grid line
            int textY = yPos + textHeight / 3;

            painter.drawText(leftMargin, textY, label);
        }
    }

private:
    // Convert dBm to S-unit string
    // S9 = -73 dBm, each S-unit below is 6 dB
    QString dbmToSUnits(float dbm) const {
        const float s9Dbm = -73.0f;
        const float dbPerSUnit = 6.0f;

        if (dbm >= s9Dbm) {
            // Above S9: show as S9+XX
            int dbOver = static_cast<int>(std::round(dbm - s9Dbm));
            if (dbOver == 0) {
                return "S9";
            }
            return QString("S9+%1").arg(dbOver);
        } else {
            // Below S9: calculate S-unit (S1-S9)
            float sUnits = 9.0f + (dbm - s9Dbm) / dbPerSUnit;
            int sValue = static_cast<int>(std::round(sUnits));
            if (sValue < 1)
                sValue = 1;
            if (sValue > 9)
                sValue = 9;
            return QString("S%1").arg(sValue);
        }
    }

    float m_minDb = -138.0f;
    float m_maxDb = -58.0f;
    bool m_useSUnits = false;
};

// Transparent overlay widget for frequency scale labels at spectrum/waterfall boundary
class FrequencyScaleOverlay : public QWidget {
public:
    FrequencyScaleOverlay(QWidget *parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_TranslucentBackground);
    }

    void setFrequencyRange(qint64 centerFreq, int spanHz, int ifShift, const QString &mode, int labelInterval) {
        m_centerFreq = centerFreq;
        m_spanHz = spanHz;
        m_ifShift = ifShift;
        m_mode = mode;
        m_labelInterval = labelInterval;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        if (m_spanHz <= 0)
            return;

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);

        QFont scaleFont = K4Styles::Fonts::dataFont(K4Styles::Dimensions::FontSizeNormal, QFont::Normal);
        painter.setFont(scaleFont);
        painter.setPen(Qt::white);

        QFontMetrics fm(scaleFont);
        const int textHeight = fm.height();
        const int w = width();
        const int h = height();

        // In CW mode, offset the display center by IF shift so the VFO marker appears centered.
        // Labels show dial-equivalent frequencies (round kHz values) positioned via the
        // shifted RF coordinate system so CW operators can read tuning positions directly.
        qint64 effectiveCenter = m_centerFreq;
        qint64 cwOffset = 0;
        if (m_mode == "CW") {
            cwOffset = static_cast<qint64>(m_ifShift) * 10;
            effectiveCenter = m_centerFreq + cwOffset;
        } else if (m_mode == "CW-R") {
            cwOffset = -static_cast<qint64>(m_ifShift) * 10;
            effectiveCenter = m_centerFreq + cwOffset;
        }

        qint64 startFreq = effectiveCenter - m_spanHz / 2;

        // Compute dial-equivalent frequency range for label iteration
        qint64 dialStart = startFreq - cwOffset;
        qint64 dialEnd = dialStart + m_spanHz;

        int interval = m_labelInterval;

        // Find first label at a round dial frequency boundary
        qint64 firstDialLabel = (dialStart / interval) * interval;
        if (firstDialLabel < dialStart)
            firstDialLabel += interval;

        // Measure sample label width for spacing check
        QString sampleLabel = formatFrequency(firstDialLabel);
        int labelWidth = fm.horizontalAdvance(sampleLabel);
        int minSpacing = labelWidth + 12; // Minimum gap between labels

        // Draw labels at each interval (round dial frequencies)
        int lastDrawnX = -1000; // Track last drawn position for overlap prevention
        for (qint64 dialFreq = firstDialLabel; dialFreq <= dialEnd; dialFreq += interval) {
            // Position using RF coordinate system (add CW offset back)
            qint64 rfFreq = dialFreq + cwOffset;
            float normalized = static_cast<float>(rfFreq - startFreq) / static_cast<float>(m_spanHz);
            int x = qRound(normalized * w);

            // Label text shows the dial frequency
            QString label = formatFrequency(dialFreq);
            int textWidth = fm.horizontalAdvance(label);

            // Center text horizontally on the frequency position
            int textX = x - textWidth / 2;

            // Skip if too close to previous label or clipped at edges
            if (textX < 2 || textX + textWidth > w - 2)
                continue;
            if (textX < lastDrawnX + minSpacing)
                continue;

            // Draw text centered vertically in overlay
            int textY = (h + textHeight) / 2 - 2;
            painter.drawText(textX, textY, label);
            lastDrawnX = textX + textWidth;
        }
    }

private:
    // Format frequency as MHz string with adaptive decimal places
    // Narrow spans need more precision to avoid duplicate labels
    QString formatFrequency(qint64 freqHz) const {
        double freqMHz = freqHz / 1000000.0;
        // Use 4 decimals for spans <= 20 kHz, 3 decimals for wider spans
        int decimals = (m_spanHz <= 20000) ? 4 : 3;
        return QString::number(freqMHz, 'f', decimals);
    }

    qint64 m_centerFreq = 0;
    int m_spanHz = 10000;
    int m_ifShift = 50;
    int m_labelInterval = 1000;
    QString m_mode = "USB";
};

PanadapterRhiWidget::PanadapterRhiWidget(QWidget *parent) : QRhiWidget(parent) {
    setMinimumHeight(200);
    setMouseTracking(true);
    m_wheelAccumulator.setFilterMomentum(false);

    // Force Metal API on macOS
#ifdef Q_OS_MACOS
    setApi(QRhiWidget::Api::Metal);
#endif

    // Initialize color LUTs
    initColorLUT();    // Waterfall LUT
    initSpectrumLUT(); // Spectrum fill LUT

    // Note: Waterfall data buffer is allocated in initialize() after devicePixelRatio is known

    // Create dBm scale overlay (child widget)
    m_dbmScaleOverlay = new DbmScaleOverlay(this);
    m_dbmScaleOverlay->setDbRange(m_minDb, m_maxDb);
    m_dbmScaleOverlay->show();

    // Create frequency scale overlay (child widget at spectrum/waterfall boundary)
    m_freqScaleOverlay = new FrequencyScaleOverlay(this);
    m_freqScaleOverlay->setFrequencyRange(m_centerFreq, m_spanHz, m_ifShift, m_mode, calculateGridInterval(m_spanHz));
    m_freqScaleOverlay->show();
}

PanadapterRhiWidget::~PanadapterRhiWidget() {
    // QRhi resources are automatically cleaned up
}

void PanadapterRhiWidget::resizeEvent(QResizeEvent *event) {
    QRhiWidget::resizeEvent(event);
    updateDbmScaleOverlay();
    updateFreqScaleOverlay();
}

void PanadapterRhiWidget::updateDbmScaleOverlay() {
    if (!m_dbmScaleOverlay)
        return;

    // Position overlay to cover spectrum area only (top portion)
    const int h = height();
    const int spectrumHeight = static_cast<int>(h * m_spectrumRatio);

    // Overlay covers left side of spectrum area
    m_dbmScaleOverlay->setGeometry(0, 0, 70, spectrumHeight);
    m_dbmScaleOverlay->setDbRange(m_minDb, m_maxDb);
    m_dbmScaleOverlay->raise(); // Ensure it's on top
}

void PanadapterRhiWidget::updateFreqScaleOverlay() {
    if (!m_freqScaleOverlay)
        return;

    const int h = height();
    const int w = width();
    const int spectrumHeight = static_cast<int>(h * m_spectrumRatio);

    // Position overlay centered on the boundary between spectrum and waterfall
    // Overlay height: 16px, centered on spectrumHeight boundary line
    const int overlayHeight = 16;
    const int overlayY = spectrumHeight - overlayHeight / 2;

    m_freqScaleOverlay->setGeometry(0, overlayY, w, overlayHeight);
    m_freqScaleOverlay->setFrequencyRange(m_centerFreq, m_spanHz, m_ifShift, m_mode, calculateGridInterval(m_spanHz));
    m_freqScaleOverlay->raise(); // Ensure it renders on top
}

void PanadapterRhiWidget::initColorLUT() {
    RhiUtils::generateWaterfallColorLUT(m_colorLUT);
}

void PanadapterRhiWidget::initSpectrumLUT() {
    // Create 256-entry RGBA color LUT for spectrum fill
    // 8-stage: Royal Blue -> Cyan -> Green -> Yellow -> Orange -> Red -> White
    // Noise floor starts at royal blue (more visible color earlier)
    m_spectrumLUT.resize(256 * 4);

    for (int i = 0; i < 256; ++i) {
        float value = i / 255.0f;
        int r, g, b;

        if (value < 0.08f) {
            // Royal Blue (visible noise floor) - start brighter
            float t = value / 0.08f;
            r = 0;
            g = 0;
            b = static_cast<int>(80 + t * 100); // 80-180: royal blue range
        } else if (value < 0.20f) {
            // Royal Blue -> Cyan
            float t = (value - 0.08f) / 0.12f;
            r = 0;
            g = static_cast<int>(t * 200);
            b = static_cast<int>(180 + t * 75); // 180-255
        } else if (value < 0.35f) {
            // Cyan -> Green
            float t = (value - 0.20f) / 0.15f;
            r = 0;
            g = static_cast<int>(200 + t * 55); // 200-255
            b = static_cast<int>(255 * (1.0f - t));
        } else if (value < 0.52f) {
            // Green -> Yellow
            float t = (value - 0.35f) / 0.17f;
            r = static_cast<int>(t * 255);
            g = 255;
            b = 0;
        } else if (value < 0.70f) {
            // Yellow -> Orange -> Red
            float t = (value - 0.52f) / 0.18f;
            r = 255;
            g = static_cast<int>(255 * (1.0f - t));
            b = 0;
        } else {
            // Red -> White (strongest signals)
            float t = (value - 0.70f) / 0.30f;
            r = 255;
            g = static_cast<int>(t * 255);
            b = static_cast<int>(t * 255);
        }

        m_spectrumLUT[i * 4 + 0] = static_cast<quint8>(qBound(0, r, 255));
        m_spectrumLUT[i * 4 + 1] = static_cast<quint8>(qBound(0, g, 255));
        m_spectrumLUT[i * 4 + 2] = static_cast<quint8>(qBound(0, b, 255));
        m_spectrumLUT[i * 4 + 3] = 255;
    }
}

void PanadapterRhiWidget::initialize(QRhiCommandBuffer *cb) {
    if (m_rhiInitialized)
        return;

    m_rhi = rhi();
    if (!m_rhi) {
        qWarning() << "QRhi is NULL - GPU backend failed to initialize";
        return;
    }

    // Use fixed texture sizes - GPU bilinear filtering handles scaling to display size
    m_textureWidth = BASE_TEXTURE_WIDTH;
    m_waterfallHistory = BASE_WATERFALL_HISTORY;

    // Allocate waterfall data buffer
    m_waterfallData.resize(m_textureWidth * m_waterfallHistory);
    m_waterfallData.fill(0);

    // Load shaders from compiled .qsb resources
    m_spectrumFillVert = RhiUtils::loadShader(":/shaders/src/dsp/shaders/spectrum_fill.vert.qsb");
    m_spectrumFillFrag = RhiUtils::loadShader(":/shaders/src/dsp/shaders/spectrum_fill.frag.qsb");
    m_waterfallVert = RhiUtils::loadShader(":/shaders/src/dsp/shaders/waterfall.vert.qsb");
    m_waterfallFrag = RhiUtils::loadShader(":/shaders/src/dsp/shaders/waterfall.frag.qsb");
    m_overlayVert = RhiUtils::loadShader(":/shaders/src/dsp/shaders/overlay.vert.qsb");
    m_overlayFrag = RhiUtils::loadShader(":/shaders/src/dsp/shaders/overlay.frag.qsb");

    // Create waterfall texture (single channel for dB values)
    m_waterfallTexture.reset(m_rhi->newTexture(QRhiTexture::R8, QSize(m_textureWidth, m_waterfallHistory), 1,
                                               QRhiTexture::UsedAsTransferSource));
    m_waterfallTexture->create();

    // Create color LUT texture (256x1 RGBA)
    m_colorLutTexture.reset(m_rhi->newTexture(QRhiTexture::RGBA8, QSize(256, 1)));
    m_colorLutTexture->create();

    // Create spectrum data texture (1D texture for fragment shader spectrum)
    m_spectrumDataTexture.reset(m_rhi->newTexture(QRhiTexture::R32F, QSize(m_textureWidth, 1)));
    m_spectrumDataTexture->create();

    // Create spectrum fill color LUT texture (256x1 RGBA)
    m_spectrumFillLutTexture.reset(m_rhi->newTexture(QRhiTexture::RGBA8, QSize(256, 1)));
    m_spectrumFillLutTexture->create();

    // Upload color LUT data (separate LUTs for waterfall and spectrum)
    QRhiResourceUpdateBatch *rub = m_rhi->nextResourceUpdateBatch();
    // Upload waterfall color LUT
    QRhiTextureSubresourceUploadDescription waterfallLutUpload(m_colorLUT.constData(), m_colorLUT.size());
    rub->uploadTexture(m_colorLutTexture.get(), QRhiTextureUploadEntry(0, 0, waterfallLutUpload));
    // Upload spectrum fill color LUT
    QRhiTextureSubresourceUploadDescription spectrumLutUpload(m_spectrumLUT.constData(), m_spectrumLUT.size());
    rub->uploadTexture(m_spectrumFillLutTexture.get(), QRhiTextureUploadEntry(0, 0, spectrumLutUpload));

    // Upload initial zeroed waterfall data (prevents uninitialized texture garbage)
    QRhiTextureSubresourceUploadDescription waterfallUpload(m_waterfallData.constData(), m_waterfallData.size());
    rub->uploadTexture(m_waterfallTexture.get(), QRhiTextureUploadEntry(0, 0, waterfallUpload));

    // Create sampler
    m_sampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                      QRhiSampler::ClampToEdge, QRhiSampler::Repeat));
    m_sampler->create();

    // Waterfall quad (static)
    float tMax = static_cast<float>(m_waterfallHistory - 1) / m_waterfallHistory;
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

    m_overlayVbo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, 4096 * 2 * sizeof(float)));
    m_overlayVbo->create();

    // Create uniform buffers
    m_waterfallUniformBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32));
    m_waterfallUniformBuffer->create();

    m_overlayUniformBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32));
    m_overlayUniformBuffer->create();

    // Fullscreen quad (shared by all fragment-shader spectrum styles)
    // Position (x, y) + texCoord (s, t) - covers normalized -1 to 1 range
    float fullscreenQuad[] = {
        // position (x, y), texcoord (s, t)
        -1.0f, -1.0f, 0.0f, 1.0f, // bottom-left (texCoord y=1 = bottom)
        1.0f,  -1.0f, 1.0f, 1.0f, // bottom-right
        1.0f,  1.0f,  1.0f, 0.0f, // top-right (texCoord y=0 = top)
        -1.0f, -1.0f, 0.0f, 1.0f, // bottom-left
        1.0f,  1.0f,  1.0f, 0.0f, // top-right
        -1.0f, 1.0f,  0.0f, 0.0f  // top-left
    };
    m_fullscreenQuadVbo.reset(
        m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(fullscreenQuad)));
    m_fullscreenQuadVbo->create();
    rub->uploadStaticBuffer(m_fullscreenQuadVbo.get(), fullscreenQuad);

    // Spectrum amplitude style uniform buffer: 80 bytes (std140 layout)
    // fillBaseColor(16) + fillPeakColor(16) + glowColor(16) + params(16) + viewport(16)
    m_spectrumFillUniformBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 80));
    m_spectrumFillUniformBuffer->create();

    // Separate buffers for passband to avoid GPU buffer conflicts
    m_passbandVbo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, 256 * sizeof(float)));
    m_passbandVbo->create();

    m_passbandUniformBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32));
    m_passbandUniformBuffer->create();

    // Separate buffers for marker to avoid conflicts with passband
    m_markerVbo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, 64 * sizeof(float)));
    m_markerVbo->create();

    m_markerUniformBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32));
    m_markerUniformBuffer->create();

    // Separate buffers for notch to avoid conflicts with grid (which shares overlay buffers)
    m_notchVbo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, 1200 * sizeof(float)));
    m_notchVbo->create();

    m_notchUniformBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32));
    m_notchUniformBuffer->create();

    // Secondary VFO passband buffers (for showing other receiver's passband)
    m_secondaryPassbandVbo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, 256 * sizeof(float)));
    m_secondaryPassbandVbo->create();

    m_secondaryPassbandUniformBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32));
    m_secondaryPassbandUniformBuffer->create();

    m_secondaryMarkerVbo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, 64 * sizeof(float)));
    m_secondaryMarkerVbo->create();

    m_secondaryMarkerUniformBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32));
    m_secondaryMarkerUniformBuffer->create();

    // TX marker buffers (shows TX position when RIT/XIT splits TX from RX)
    m_txMarkerVbo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, 64 * sizeof(float)));
    m_txMarkerVbo->create();

    m_txMarkerUniformBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32));
    m_txMarkerUniformBuffer->create();

    // RTTY mark/space tone line buffers (primary VFO)
    m_rttyMarkVbo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, 1200 * sizeof(float)));
    m_rttyMarkVbo->create();
    m_rttyMarkUniformBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32));
    m_rttyMarkUniformBuffer->create();

    m_rttySpaceVbo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, 1200 * sizeof(float)));
    m_rttySpaceVbo->create();
    m_rttySpaceUniformBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32));
    m_rttySpaceUniformBuffer->create();

    // RTTY mark/space tone line buffers (secondary VFO)
    m_secRttyMarkVbo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, 1200 * sizeof(float)));
    m_secRttyMarkVbo->create();
    m_secRttyMarkUniformBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32));
    m_secRttyMarkUniformBuffer->create();

    m_secRttySpaceVbo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, 1200 * sizeof(float)));
    m_secRttySpaceVbo->create();
    m_secRttySpaceUniformBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32));
    m_secRttySpaceUniformBuffer->create();

    cb->resourceUpdate(rub);

    m_rhiInitialized = true;
}

void PanadapterRhiWidget::createPipelines() {
    if (m_pipelinesCreated)
        return;

    if (!m_spectrumFillVert.isValid() || !m_spectrumFillFrag.isValid())
        return;

    m_rpDesc = renderTarget()->renderPassDescriptor();

    // Spectrum fill pipeline (LUT-based colors with amplitude brightness)
    {
        m_spectrumFillSrb.reset(m_rhi->newShaderResourceBindings());
        m_spectrumFillSrb->setBindings(
            {QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::FragmentStage,
                                                      m_spectrumFillUniformBuffer.get()),
             QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                       m_spectrumDataTexture.get(), m_sampler.get()),
             QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage,
                                                       m_spectrumFillLutTexture.get(), m_sampler.get())});
        m_spectrumFillSrb->create();

        m_spectrumFillPipeline.reset(m_rhi->newGraphicsPipeline());
        m_spectrumFillPipeline->setShaderStages(
            {{QRhiShaderStage::Vertex, m_spectrumFillVert}, {QRhiShaderStage::Fragment, m_spectrumFillFrag}});

        QRhiVertexInputLayout inputLayout;
        inputLayout.setBindings({{4 * sizeof(float)}});                         // position(2) + texcoord(2)
        inputLayout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0}, // position
                                   {0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)}}); // texcoord
        m_spectrumFillPipeline->setVertexInputLayout(inputLayout);
        m_spectrumFillPipeline->setTopology(QRhiGraphicsPipeline::Triangles);
        m_spectrumFillPipeline->setShaderResourceBindings(m_spectrumFillSrb.get());
        m_spectrumFillPipeline->setRenderPassDescriptor(m_rpDesc);

        QRhiGraphicsPipeline::TargetBlend blend;
        blend.enable = true;
        blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        m_spectrumFillPipeline->setTargetBlends({blend});

        m_spectrumFillPipeline->create();
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

        // Separate SRB for passband to avoid buffer conflicts
        m_passbandSrb.reset(m_rhi->newShaderResourceBindings());
        m_passbandSrb->setBindings({QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_passbandUniformBuffer.get())});
        m_passbandSrb->create();

        // Separate SRB for marker to avoid buffer conflicts with passband
        m_markerSrb.reset(m_rhi->newShaderResourceBindings());
        m_markerSrb->setBindings({QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_markerUniformBuffer.get())});
        m_markerSrb->create();

        m_notchSrb.reset(m_rhi->newShaderResourceBindings());
        m_notchSrb->setBindings({QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_notchUniformBuffer.get())});
        m_notchSrb->create();

        // Secondary VFO passband SRBs
        m_secondaryPassbandSrb.reset(m_rhi->newShaderResourceBindings());
        m_secondaryPassbandSrb->setBindings({QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_secondaryPassbandUniformBuffer.get())});
        m_secondaryPassbandSrb->create();

        m_secondaryMarkerSrb.reset(m_rhi->newShaderResourceBindings());
        m_secondaryMarkerSrb->setBindings({QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_secondaryMarkerUniformBuffer.get())});
        m_secondaryMarkerSrb->create();

        m_txMarkerSrb.reset(m_rhi->newShaderResourceBindings());
        m_txMarkerSrb->setBindings({QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_txMarkerUniformBuffer.get())});
        m_txMarkerSrb->create();

        // RTTY mark/space tone SRBs (primary VFO)
        m_rttyMarkSrb.reset(m_rhi->newShaderResourceBindings());
        m_rttyMarkSrb->setBindings({QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_rttyMarkUniformBuffer.get())});
        m_rttyMarkSrb->create();

        m_rttySpaceSrb.reset(m_rhi->newShaderResourceBindings());
        m_rttySpaceSrb->setBindings({QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_rttySpaceUniformBuffer.get())});
        m_rttySpaceSrb->create();

        // RTTY mark/space tone SRBs (secondary VFO)
        m_secRttyMarkSrb.reset(m_rhi->newShaderResourceBindings());
        m_secRttyMarkSrb->setBindings({QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_secRttyMarkUniformBuffer.get())});
        m_secRttyMarkSrb->create();

        m_secRttySpaceSrb.reset(m_rhi->newShaderResourceBindings());
        m_secRttySpaceSrb->setBindings({QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_secRttySpaceUniformBuffer.get())});
        m_secRttySpaceSrb->create();

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

    m_pipelinesCreated = true;
}

void PanadapterRhiWidget::render(QRhiCommandBuffer *cb) {
    // Always clear to black even if not initialized (prevents red/garbage showing)
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
    if (m_waterfallNeedsUpdate && !m_currentSpectrum.isEmpty()) {
        updateWaterfallData();
        QRhiTextureSubresourceUploadDescription rowUpload(
            m_waterfallData.constData() + m_waterfallWriteRow * m_textureWidth, m_textureWidth);
        rowUpload.setDestinationTopLeft(QPoint(0, m_waterfallWriteRow));
        rowUpload.setSourceSize(QSize(m_textureWidth, 1));
        rub->uploadTexture(m_waterfallTexture.get(), QRhiTextureUploadEntry(0, 0, rowUpload));
        m_waterfallWriteRow = (m_waterfallWriteRow + 1) % m_waterfallHistory;
        m_waterfallNeedsUpdate = false;
    }

    // Update waterfall uniform buffer with bin parameters
    float scrollOffset = static_cast<float>(m_waterfallWriteRow) / m_waterfallHistory;
    // Use full tier bin count for waterfall (matches what updateWaterfallData writes)
    float binCount = m_waterfallTierBinCount > 0   ? static_cast<float>(m_waterfallTierBinCount)
                     : m_currentSpectrum.isEmpty() ? static_cast<float>(m_textureWidth)
                                                   : static_cast<float>(m_currentSpectrum.size());
    float tierSpanHz = m_waterfallTierSpanHz > 0 ? m_waterfallTierSpanHz : static_cast<float>(m_spanHz);
    float spanHz = static_cast<float>(m_spanHz);
    struct {
        float scrollOffset;
        float binCount;
        float textureWidth;
        float tierSpanHz;
        float spanHz;
        float padding[3];
    } waterfallUniforms = {scrollOffset, binCount, static_cast<float>(m_textureWidth), tierSpanHz, spanHz, {0, 0, 0}};
    rub->updateDynamicBuffer(m_waterfallUniformBuffer.get(), 0, sizeof(waterfallUniforms), &waterfallUniforms);

    // Calculate smoothed baseline for spectrum normalization
    if (!m_currentSpectrum.isEmpty()) {
        float frameMinNormalized = 1.0f;
        for (int i = 0; i < m_currentSpectrum.size(); ++i) {
            float normalized = normalizeDb(m_currentSpectrum[i]);
            if (normalized < frameMinNormalized)
                frameMinNormalized = normalized;
        }
        const float baselineAlpha = 0.05f;
        if (m_smoothedBaseline < 0.001f)
            m_smoothedBaseline = frameMinNormalized;
        else
            m_smoothedBaseline = baselineAlpha * frameMinNormalized + (1.0f - baselineAlpha) * m_smoothedBaseline;

        // Upload raw spectrum bins to 1D texture - GPU shader does bilinear interpolation
        int specSize = m_currentSpectrum.size();
        int offset = (m_textureWidth - specSize) / 2;

        // Reuse member vector to avoid per-frame heap allocation
        m_normalizedSpectrum.resize(m_textureWidth);
        std::fill(m_normalizedSpectrum.begin(), m_normalizedSpectrum.end(), 0.0f);
        for (int i = 0; i < specSize; ++i) {
            float normalized = normalizeDb(m_currentSpectrum[i]);
            float adjusted = qMax(0.0f, normalized - m_smoothedBaseline);
            m_normalizedSpectrum[offset + i] = adjusted * 0.95f;
        }

        QRhiTextureSubresourceUploadDescription specDataUpload(m_normalizedSpectrum.constData(),
                                                               m_normalizedSpectrum.size() * sizeof(float));
        specDataUpload.setSourceSize(QSize(m_textureWidth, 1));
        rub->uploadTexture(m_spectrumDataTexture.get(), QRhiTextureUploadEntry(0, 0, specDataUpload));

        // Update spectrum fill uniform buffer (80 bytes, std140 layout)
        float specBinCount =
            static_cast<float>(m_currentSpectrum.isEmpty() ? m_textureWidth : m_currentSpectrum.size());
        struct {
            float fillBaseColor[4]; // offset 0: dark navy
            float fillPeakColor[4]; // offset 16: electric blue
            float glowColor[4];     // offset 32: cyan glow
            float glowIntensity;    // offset 48
            float glowWidth;        // offset 52
            float spectrumHeightPx; // offset 56
            float binCount;         // offset 60: actual bin count
            float viewportSize[2];  // offset 64
            float textureWidth;     // offset 72: for bin centering
            float padding;          // offset 76
        } specFillUniforms = {
            {0.0f, 0.08f, 0.16f, 0.85f},        // fillBaseColor: dark navy
            {0.0f, 0.63f, 1.0f, 0.85f},         // fillPeakColor: electric blue
            {0.0f, 0.83f, 1.0f, 1.0f},          // glowColor: cyan
            0.8f,                               // glowIntensity
            0.04f,                              // glowWidth
            spectrumHeight,                     // spectrumHeight in pixels
            specBinCount,                       // binCount for shader
            {w, h},                             // viewportSize
            static_cast<float>(m_textureWidth), // textureWidth for bin centering
            0.0f                                // padding
        };
        rub->updateDynamicBuffer(m_spectrumFillUniformBuffer.get(), 0, sizeof(specFillUniforms), &specFillUniforms);
    }

    cb->resourceUpdate(rub);

    // Begin render pass
    cb->beginPass(renderTarget(), QColor::fromRgbF(0.08f, 0.08f, 0.08f, 1.0f), {1.0f, 0}, nullptr);

    // Draw waterfall (bottom portion)
    if (m_waterfallPipeline) {
        cb->setViewport({0, 0, w, waterfallHeight});
        cb->setGraphicsPipeline(m_waterfallPipeline.get());
        cb->setShaderResources(m_waterfallSrb.get());
        const QRhiCommandBuffer::VertexInput waterfallVbufBinding(m_waterfallVbo.get(), 0);
        cb->setVertexInput(0, 1, &waterfallVbufBinding);
        cb->draw(6);
    }

    // Draw grid BEHIND spectrum (in spectrum area)
    if (m_gridEnabled && m_overlayLinePipeline) {
        cb->setViewport({0, waterfallHeight, w, spectrumHeight});

        QVector<float> gridVerts;

        // Horizontal lines (dB scale) - 8 divisions in spectrum area
        for (int i = 1; i < 8; ++i) {
            float y = spectrumHeight * i / 8.0f;
            gridVerts << 0.0f << y << w << y;
        }

        // Vertical lines at frequency-aligned positions (matching label intervals)
        // Grid lines are placed at round dial-frequency boundaries, same as labels.
        {
            qint64 effectiveCenter = m_centerFreq;
            qint64 cwOffset = 0;
            if (m_mode == "CW") {
                cwOffset = static_cast<qint64>(m_ifShift) * 10;
                effectiveCenter = m_centerFreq + cwOffset;
            } else if (m_mode == "CW-R") {
                cwOffset = -static_cast<qint64>(m_ifShift) * 10;
                effectiveCenter = m_centerFreq + cwOffset;
            }
            qint64 startFreq = effectiveCenter - m_spanHz / 2;
            qint64 dialStart = startFreq - cwOffset;
            qint64 dialEnd = dialStart + m_spanHz;
            int interval = calculateGridInterval(m_spanHz);
            qint64 firstDialLine = (dialStart / interval) * interval;
            if (firstDialLine < dialStart)
                firstDialLine += interval;
            for (qint64 dialFreq = firstDialLine; dialFreq < dialEnd; dialFreq += interval) {
                qint64 rfFreq = dialFreq + cwOffset;
                float x = static_cast<float>(rfFreq - startFreq) / static_cast<float>(m_spanHz) * w;
                gridVerts << x << 0.0f << x << spectrumHeight;
            }
        }

        QRhiResourceUpdateBatch *gridRub = m_rhi->nextResourceUpdateBatch();
        gridRub->updateDynamicBuffer(m_overlayVbo.get(), 0, gridVerts.size() * sizeof(float), gridVerts.constData());

        struct {
            float viewportWidth;
            float viewportHeight;
            float pad0, pad1;
            float r, g, b, a;
        } gridUniforms = {w,
                          spectrumHeight,
                          0,
                          0,
                          static_cast<float>(m_gridColor.redF()),
                          static_cast<float>(m_gridColor.greenF()),
                          static_cast<float>(m_gridColor.blueF()),
                          static_cast<float>(m_gridColor.alphaF())};
        gridRub->updateDynamicBuffer(m_overlayUniformBuffer.get(), 0, sizeof(gridUniforms), &gridUniforms);

        cb->resourceUpdate(gridRub);
        cb->setGraphicsPipeline(m_overlayLinePipeline.get());
        cb->setShaderResources(m_overlaySrb.get());
        const QRhiCommandBuffer::VertexInput gridVbufBinding(m_overlayVbo.get(), 0);
        cb->setVertexInput(0, 1, &gridVbufBinding);
        cb->draw(gridVerts.size() / 2);
    }

    // Draw spectrum fill ON TOP of grid (shader-based fullscreen quad)
    if (!m_currentSpectrum.isEmpty() && m_spectrumFillPipeline) {
        cb->setViewport({0, waterfallHeight, w, spectrumHeight});
        cb->setGraphicsPipeline(m_spectrumFillPipeline.get());
        cb->setShaderResources(m_spectrumFillSrb.get());

        const QRhiCommandBuffer::VertexInput quadVbufBinding(m_fullscreenQuadVbo.get(), 0);
        cb->setVertexInput(0, 1, &quadVbufBinding);
        cb->draw(6); // Fullscreen quad (2 triangles)
    }

    // Draw overlays (full viewport for grid, markers, passband)
    cb->setViewport({0, 0, w, h});

    if (m_overlayLinePipeline && m_overlayTrianglePipeline) {

        // Draw secondary VFO passband first (so it renders behind primary when overlapping)
        if (m_secondaryVisible && m_secondaryFilterBw > 0 && m_secondaryTunedFreq > 0) {
            qint64 secLowFreq, secHighFreq;
            int secShiftOffsetHz = m_secondaryIfShift * 10;

            bool secIsData = (m_secondaryMode == "DATA" || m_secondaryMode == "DATA-R");
            bool secIsAfskA = secIsData && m_secondaryDataSubMode == 1;
            bool secIsFskD = secIsData && m_secondaryDataSubMode == 2;
            bool secIsPskD = secIsData && m_secondaryDataSubMode == 3;

            if (secIsPskD) {
                secLowFreq = m_secondaryTunedFreq - m_secondaryFilterBw / 2;
                secHighFreq = m_secondaryTunedFreq + m_secondaryFilterBw / 2;
            } else if (secIsFskD) {
                // FSK-D: dial IS the mark frequency
                qint64 secCenter = m_secondaryTunedFreq - m_rttyShift / 2;
                secLowFreq = secCenter - m_secondaryFilterBw / 2;
                secHighFreq = secCenter + m_secondaryFilterBw / 2;
            } else if (secIsAfskA) {
                // AFSK-A: LSB mode — tones below dial, same geometry as FSK-D
                qint64 secCenter = m_secondaryTunedFreq - m_rttyShift / 2;
                secLowFreq = secCenter - m_secondaryFilterBw / 2;
                secHighFreq = secCenter + m_secondaryFilterBw / 2;
            } else if (m_secondaryMode == "LSB") {
                qint64 center = m_secondaryTunedFreq - secShiftOffsetHz;
                secLowFreq = center - m_secondaryFilterBw / 2;
                secHighFreq = center + m_secondaryFilterBw / 2;
            } else if (m_secondaryMode == "USB" || secIsData) {
                qint64 center = m_secondaryTunedFreq + secShiftOffsetHz;
                secLowFreq = center - m_secondaryFilterBw / 2;
                secHighFreq = center + m_secondaryFilterBw / 2;
            } else if (m_secondaryMode == "CW" || m_secondaryMode == "CW-R") {
                qint64 center = (m_secondaryMode == "CW") ? m_secondaryTunedFreq + secShiftOffsetHz
                                                          : m_secondaryTunedFreq - secShiftOffsetHz;
                secLowFreq = center - m_secondaryFilterBw / 2;
                secHighFreq = center + m_secondaryFilterBw / 2;
            } else {
                // AM/FM - symmetric around carrier (both sidebands, no IF shift)
                secLowFreq = m_secondaryTunedFreq - m_secondaryFilterBw / 2;
                secHighFreq = m_secondaryTunedFreq + m_secondaryFilterBw / 2;
            }

            float secX1 = freqToNormalized(secLowFreq) * w;
            float secX2 = freqToNormalized(secHighFreq) * w;
            secX1 = qBound(0.0f, secX1, w);
            secX2 = qBound(0.0f, secX2, w);

            if (secX2 > secX1) {
                QVector<float> secQuadVerts = {
                    secX1, 0, secX2, 0, secX2, spectrumHeight, secX1, 0, secX2, spectrumHeight, secX1, spectrumHeight};

                QRhiResourceUpdateBatch *secPbRub = m_rhi->nextResourceUpdateBatch();
                secPbRub->updateDynamicBuffer(m_secondaryPassbandVbo.get(), 0, secQuadVerts.size() * sizeof(float),
                                              secQuadVerts.constData());

                struct {
                    float viewportWidth;
                    float viewportHeight;
                    float pad0, pad1;
                    float r, g, b, a;
                } secPbUniforms = {w,
                                   h,
                                   0,
                                   0,
                                   static_cast<float>(m_secondaryPassbandColor.redF()),
                                   static_cast<float>(m_secondaryPassbandColor.greenF()),
                                   static_cast<float>(m_secondaryPassbandColor.blueF()),
                                   static_cast<float>(m_secondaryPassbandColor.alphaF())};
                secPbRub->updateDynamicBuffer(m_secondaryPassbandUniformBuffer.get(), 0, sizeof(secPbUniforms),
                                              &secPbUniforms);

                cb->resourceUpdate(secPbRub);
                cb->setGraphicsPipeline(m_overlayTrianglePipeline.get());
                cb->setShaderResources(m_secondaryPassbandSrb.get());
                const QRhiCommandBuffer::VertexInput secPbVbufBinding(m_secondaryPassbandVbo.get(), 0);
                cb->setVertexInput(0, 1, &secPbVbufBinding);
                cb->draw(6);
            }

            // Secondary VFO marker
            qint64 secMarkerFreq = m_secondaryTunedFreq;
            if (m_secondaryMode == "CW") {
                secMarkerFreq = m_secondaryTunedFreq + secShiftOffsetHz;
            } else if (m_secondaryMode == "CW-R") {
                secMarkerFreq = m_secondaryTunedFreq - secShiftOffsetHz;
            }
            float secMarkerX = freqToNormalized(secMarkerFreq) * w;
            if (secMarkerX >= 0 && secMarkerX <= w) {
                float markerWidth = PanadapterConstants::MarkerLineWidth;
                QVector<float> secMarkerVerts = {secMarkerX,
                                                 0.0f,
                                                 secMarkerX + markerWidth,
                                                 0.0f,
                                                 secMarkerX + markerWidth,
                                                 spectrumHeight,
                                                 secMarkerX,
                                                 0.0f,
                                                 secMarkerX + markerWidth,
                                                 spectrumHeight,
                                                 secMarkerX,
                                                 spectrumHeight};

                QRhiResourceUpdateBatch *secMkRub = m_rhi->nextResourceUpdateBatch();
                secMkRub->updateDynamicBuffer(m_secondaryMarkerVbo.get(), 0, secMarkerVerts.size() * sizeof(float),
                                              secMarkerVerts.constData());

                struct {
                    float viewportWidth;
                    float viewportHeight;
                    float pad0, pad1;
                    float r, g, b, a;
                } secMkUniforms = {w,
                                   h,
                                   0,
                                   0,
                                   static_cast<float>(m_secondaryMarkerColor.redF()),
                                   static_cast<float>(m_secondaryMarkerColor.greenF()),
                                   static_cast<float>(m_secondaryMarkerColor.blueF()),
                                   static_cast<float>(m_secondaryMarkerColor.alphaF())};
                secMkRub->updateDynamicBuffer(m_secondaryMarkerUniformBuffer.get(), 0, sizeof(secMkUniforms),
                                              &secMkUniforms);

                cb->resourceUpdate(secMkRub);
                cb->setGraphicsPipeline(m_overlayTrianglePipeline.get());
                cb->setShaderResources(m_secondaryMarkerSrb.get());
                const QRhiCommandBuffer::VertexInput secMkVbufBinding(m_secondaryMarkerVbo.get(), 0);
                cb->setVertexInput(0, 1, &secMkVbufBinding);
                cb->draw(6);
            }

            // Secondary VFO RTTY mark/space dashed lines
            if ((secIsAfskA || secIsFskD) && m_fskMarkTone > 0) {
                float dashLen = PanadapterConstants::DashLengthPx;
                float gapLen = PanadapterConstants::DashGapPx;
                float stride = dashLen + gapLen;
                float lineWidth = PanadapterConstants::RttyDashLineWidth;

                auto drawSecRttyLine = [&](qint64 toneFreq, QRhiBuffer *vbo, QRhiBuffer *ubo,
                                           QRhiShaderResourceBindings *srb) {
                    float toneX = freqToNormalized(toneFreq) * w;
                    if (toneX < 0 || toneX > w)
                        return;

                    QVector<float> verts;
                    for (float y = 0.0f; y < spectrumHeight; y += stride) {
                        float yEnd = qMin(y + dashLen, spectrumHeight);
                        verts << toneX << y << toneX + lineWidth << y << toneX + lineWidth << yEnd << toneX << y
                              << toneX + lineWidth << yEnd << toneX << yEnd;
                    }

                    QRhiResourceUpdateBatch *rub = m_rhi->nextResourceUpdateBatch();
                    rub->updateDynamicBuffer(vbo, 0, verts.size() * sizeof(float), verts.constData());

                    struct {
                        float viewportWidth, viewportHeight, pad0, pad1;
                        float r, g, b, a;
                    } uniforms = {w,
                                  h,
                                  0,
                                  0,
                                  static_cast<float>(m_secondaryRttyToneColor.redF()),
                                  static_cast<float>(m_secondaryRttyToneColor.greenF()),
                                  static_cast<float>(m_secondaryRttyToneColor.blueF()),
                                  static_cast<float>(m_secondaryRttyToneColor.alphaF())};
                    rub->updateDynamicBuffer(ubo, 0, sizeof(uniforms), &uniforms);

                    cb->resourceUpdate(rub);
                    cb->setGraphicsPipeline(m_overlayTrianglePipeline.get());
                    cb->setShaderResources(srb);
                    const QRhiCommandBuffer::VertexInput vbufBinding(vbo, 0);
                    cb->setVertexInput(0, 1, &vbufBinding);
                    cb->draw(verts.size() / 2);
                };

                // Mark line coincides with the solid secondary dial marker — only draw space
                qint64 secSpaceFreq = m_secondaryTunedFreq - m_rttyShift;
                drawSecRttyLine(secSpaceFreq, m_secRttySpaceVbo.get(), m_secRttySpaceUniformBuffer.get(),
                                m_secRttySpaceSrb.get());
            }
        }

        // Draw passband overlay (uses separate buffers to avoid GPU conflicts)
        if (m_cursorVisible && m_filterBw > 0 && m_tunedFreq > 0) {
            // Calculate passband edges based on mode
            qint64 lowFreq, highFreq;

            // K4 IF shift is reported in decahertz (10 Hz units)
            // This is the passband center offset from the dial frequency
            // USB with shift=150 means passband centered 1500 Hz above dial
            // CW with shift=50 means passband centered at 500 Hz pitch
            int shiftOffsetHz = m_ifShift * 10;

            // DATA submode-specific passband rendering
            bool isDataMode = (m_mode == "DATA" || m_mode == "DATA-R");
            bool isAfskA = isDataMode && m_dataSubMode == 1;
            bool isFskD = isDataMode && m_dataSubMode == 2;
            bool isPskD = isDataMode && m_dataSubMode == 3;

            if (isPskD) {
                // PSK-D: passband centered on dial frequency
                lowFreq = m_tunedFreq - m_filterBw / 2;
                highFreq = m_tunedFreq + m_filterBw / 2;
            } else if (isFskD) {
                // FSK-D: dial = mark; space is 170 Hz below; box straddles left of dial
                qint64 center = m_tunedFreq - m_rttyShift / 2;
                lowFreq = center - m_filterBw / 2;
                highFreq = center + m_filterBw / 2;
            } else if (isAfskA) {
                // AFSK-A: LSB mode — tones below dial, same geometry as FSK-D
                qint64 center = m_tunedFreq - m_rttyShift / 2;
                lowFreq = center - m_filterBw / 2;
                highFreq = center + m_filterBw / 2;
            } else if (m_mode == "LSB") {
                // LSB: passband below dial, offset by IS
                qint64 center = m_tunedFreq - shiftOffsetHz;
                lowFreq = center - m_filterBw / 2;
                highFreq = center + m_filterBw / 2;
            } else if (m_mode == "USB" || isDataMode) {
                // USB/AFSK/DATA-A: passband above dial, offset by IS
                qint64 center = m_tunedFreq + shiftOffsetHz;
                lowFreq = center - m_filterBw / 2;
                highFreq = center + m_filterBw / 2;
            } else if (m_mode == "CW" || m_mode == "CW-R") {
                // CW uses IS (IF shift) for passband center
                // CW: passband above dial; CW-R: passband below dial
                qint64 center = (m_mode == "CW") ? m_tunedFreq + shiftOffsetHz : m_tunedFreq - shiftOffsetHz;
                lowFreq = center - m_filterBw / 2;
                highFreq = center + m_filterBw / 2;
            } else {
                // AM/FM - symmetric around carrier (both sidebands, no IF shift)
                lowFreq = m_tunedFreq - m_filterBw / 2;
                highFreq = m_tunedFreq + m_filterBw / 2;
            }

            // Convert to pixel coordinates
            float x1 = freqToNormalized(lowFreq) * w;
            float x2 = freqToNormalized(highFreq) * w;

            // Clamp to visible area
            x1 = qBound(0.0f, x1, w);
            x2 = qBound(0.0f, x2, w);

            if (x2 > x1) {
                // Draw passband quad - use dedicated VBO, uniform buffer, and SRB
                // Use spectrumHeight not h - passband should only appear in spectrum area, not waterfall
                QVector<float> quadVerts = {
                    x1, 0, x2, 0, x2, spectrumHeight, x1, 0, x2, spectrumHeight, x1, spectrumHeight};

                QRhiResourceUpdateBatch *pbRub = m_rhi->nextResourceUpdateBatch();
                pbRub->updateDynamicBuffer(m_passbandVbo.get(), 0, quadVerts.size() * sizeof(float),
                                           quadVerts.constData());

                struct {
                    float viewportWidth;
                    float viewportHeight;
                    float pad0, pad1;
                    float r, g, b, a;
                } pbUniforms = {w,
                                h,
                                0,
                                0,
                                static_cast<float>(m_passbandColor.redF()),
                                static_cast<float>(m_passbandColor.greenF()),
                                static_cast<float>(m_passbandColor.blueF()),
                                static_cast<float>(m_passbandColor.alphaF())};
                pbRub->updateDynamicBuffer(m_passbandUniformBuffer.get(), 0, sizeof(pbUniforms), &pbUniforms);

                cb->resourceUpdate(pbRub);
                cb->setGraphicsPipeline(m_overlayTrianglePipeline.get());
                cb->setShaderResources(m_passbandSrb.get());
                const QRhiCommandBuffer::VertexInput pbVbufBinding(m_passbandVbo.get(), 0);
                cb->setVertexInput(0, 1, &pbVbufBinding);
                cb->draw(6);
            }

            // Draw primary VFO RTTY mark/space dashed lines
            if ((isAfskA || isFskD) && m_fskMarkTone > 0) {
                float dashLen = PanadapterConstants::DashLengthPx;
                float gapLen = PanadapterConstants::DashGapPx;
                float stride = dashLen + gapLen;
                float lineWidth = PanadapterConstants::RttyDashLineWidth;

                auto drawRttyLine = [&](qint64 toneFreq, QRhiBuffer *vbo, QRhiBuffer *ubo,
                                        QRhiShaderResourceBindings *srb) {
                    float toneX = freqToNormalized(toneFreq) * w;
                    if (toneX < 0 || toneX > w)
                        return;

                    QVector<float> verts;
                    for (float y = 0.0f; y < spectrumHeight; y += stride) {
                        float yEnd = qMin(y + dashLen, spectrumHeight);
                        verts << toneX << y << toneX + lineWidth << y << toneX + lineWidth << yEnd << toneX << y
                              << toneX + lineWidth << yEnd << toneX << yEnd;
                    }

                    QRhiResourceUpdateBatch *rub = m_rhi->nextResourceUpdateBatch();
                    rub->updateDynamicBuffer(vbo, 0, verts.size() * sizeof(float), verts.constData());

                    struct {
                        float viewportWidth, viewportHeight, pad0, pad1;
                        float r, g, b, a;
                    } uniforms = {w,
                                  h,
                                  0,
                                  0,
                                  static_cast<float>(m_rttyToneColor.redF()),
                                  static_cast<float>(m_rttyToneColor.greenF()),
                                  static_cast<float>(m_rttyToneColor.blueF()),
                                  static_cast<float>(m_rttyToneColor.alphaF())};
                    rub->updateDynamicBuffer(ubo, 0, sizeof(uniforms), &uniforms);

                    cb->resourceUpdate(rub);
                    cb->setGraphicsPipeline(m_overlayTrianglePipeline.get());
                    cb->setShaderResources(srb);
                    const QRhiCommandBuffer::VertexInput vbufBinding(vbo, 0);
                    cb->setVertexInput(0, 1, &vbufBinding);
                    cb->draw(verts.size() / 2);
                };

                // FSK-D and AFSK-A: dial IS mark, space is mark - shift (both LSB)
                // Mark line coincides with the solid dial frequency marker — only draw space
                qint64 spaceFreq = m_tunedFreq - m_rttyShift;
                drawRttyLine(spaceFreq, m_rttySpaceVbo.get(), m_rttySpaceUniformBuffer.get(), m_rttySpaceSrb.get());
            }

            // Draw frequency marker - use dedicated VBO, uniform buffer, and SRB
            // Use spectrumHeight not h - marker should only appear in spectrum area, not waterfall
            // For CW modes: marker at passband center (dial + IS offset)
            // For SSB/DATA: marker at dial frequency (passband shifts around it)
            qint64 markerFreq = m_tunedFreq;
            if (m_mode == "CW") {
                // CW = upper sideband: marker above dial by IS offset (pitch)
                markerFreq = m_tunedFreq + shiftOffsetHz;
            } else if (m_mode == "CW-R") {
                // CW-R = lower sideband: marker below dial by IS offset (pitch)
                markerFreq = m_tunedFreq - shiftOffsetHz;
            }
            float markerX = freqToNormalized(markerFreq) * w;
            if (markerX >= 0 && markerX <= w) {
                // Draw as filled rectangle instead of line for robust Metal rendering
                float markerWidth = PanadapterConstants::MarkerLineWidth;
                QVector<float> markerVerts = {markerX,
                                              0.0f,
                                              markerX + markerWidth,
                                              0.0f,
                                              markerX + markerWidth,
                                              spectrumHeight,
                                              markerX,
                                              0.0f,
                                              markerX + markerWidth,
                                              spectrumHeight,
                                              markerX,
                                              spectrumHeight};

                QRhiResourceUpdateBatch *mkRub = m_rhi->nextResourceUpdateBatch();
                mkRub->updateDynamicBuffer(m_markerVbo.get(), 0, markerVerts.size() * sizeof(float),
                                           markerVerts.constData());

                struct {
                    float viewportWidth;
                    float viewportHeight;
                    float pad0, pad1;
                    float r, g, b, a;
                } mkUniforms = {w,
                                h,
                                0,
                                0,
                                static_cast<float>(m_frequencyMarkerColor.redF()),
                                static_cast<float>(m_frequencyMarkerColor.greenF()),
                                static_cast<float>(m_frequencyMarkerColor.blueF()),
                                static_cast<float>(m_frequencyMarkerColor.alphaF())};
                mkRub->updateDynamicBuffer(m_markerUniformBuffer.get(), 0, sizeof(mkUniforms), &mkUniforms);

                cb->resourceUpdate(mkRub);
                cb->setGraphicsPipeline(m_overlayTrianglePipeline.get());
                cb->setShaderResources(m_markerSrb.get());
                const QRhiCommandBuffer::VertexInput mkVbufBinding(m_markerVbo.get(), 0);
                cb->setVertexInput(0, 1, &mkVbufBinding);
                cb->draw(6);
            }

            // Draw TX marker line when RIT/XIT causes TX freq to differ from RX freq
            // m_txFreq is the dial TX frequency — apply same CW pitch offset as the RX marker
            if (m_txMarkerVisible && m_txFreq > 0 && m_spanHz > 0) {
                qint64 txDisplayFreq = m_txFreq;
                if (m_mode == "CW") {
                    // CW = upper sideband: TX tone above dial by pitch
                    txDisplayFreq += m_cwPitch;
                } else if (m_mode == "CW-R") {
                    // CW-R = lower sideband: TX tone below dial by pitch
                    txDisplayFreq -= m_cwPitch;
                }
                float txX = freqToNormalized(txDisplayFreq) * w;
                if (txX >= 0 && txX <= w) {
                    float txWidth = PanadapterConstants::MarkerLineWidth;
                    QVector<float> txVerts = {txX, 0.0f, txX + txWidth, 0.0f,           txX + txWidth, spectrumHeight,
                                              txX, 0.0f, txX + txWidth, spectrumHeight, txX,           spectrumHeight};

                    QRhiResourceUpdateBatch *txRub = m_rhi->nextResourceUpdateBatch();
                    txRub->updateDynamicBuffer(m_txMarkerVbo.get(), 0, txVerts.size() * sizeof(float),
                                               txVerts.constData());

                    struct {
                        float viewportWidth;
                        float viewportHeight;
                        float pad0, pad1;
                        float r, g, b, a;
                    } txUniforms = {w,
                                    h,
                                    0,
                                    0,
                                    static_cast<float>(m_txMarkerColor.redF()),
                                    static_cast<float>(m_txMarkerColor.greenF()),
                                    static_cast<float>(m_txMarkerColor.blueF()),
                                    static_cast<float>(m_txMarkerColor.alphaF())};
                    txRub->updateDynamicBuffer(m_txMarkerUniformBuffer.get(), 0, sizeof(txUniforms), &txUniforms);

                    cb->resourceUpdate(txRub);
                    cb->setGraphicsPipeline(m_overlayTrianglePipeline.get());
                    cb->setShaderResources(m_txMarkerSrb.get());
                    const QRhiCommandBuffer::VertexInput txVbufBinding(m_txMarkerVbo.get(), 0);
                    cb->setVertexInput(0, 1, &txVbufBinding);
                    cb->draw(6);
                }
            }

            // Draw notch filter marker (dotted line) - uses dedicated notch buffers
            // Calculate notch offset from tunedFreq (consistent with mini-pan)
            if (m_notchEnabled && m_notchPitchHz > 0 && m_spanHz > 0) {
                // NM value is audio frequency offset from dial frequency (tunedFreq).
                // CW/CW-R use the same mapping as USB/LSB respectively:
                // CW:   notchRF = tunedFreq + NM  (USB-like sideband)
                // CW-R: notchRF = tunedFreq - NM  (LSB-like sideband)
                int offsetHz;
                if (m_mode == "LSB" || m_mode == "CW-R") {
                    offsetHz = -m_notchPitchHz;
                } else {
                    // USB, CW, DATA, DATA-R, AM, FM
                    offsetHz = m_notchPitchHz;
                }

                float tunedX = freqToNormalized(m_tunedFreq) * w;
                float notchX = tunedX + (static_cast<float>(offsetHz) * w) / m_spanHz;
                bool inBounds = (notchX >= 0 && notchX <= w);

                if (inBounds) {
                    // Draw as dotted line (dashed segments with gaps)
                    float notchWidth = PanadapterConstants::MarkerLineWidth;
                    float dashLen = PanadapterConstants::DashLengthPx;
                    float gapLen = PanadapterConstants::DashGapPx;
                    float stride = dashLen + gapLen;
                    QVector<float> notchVerts;
                    for (float y = 0.0f; y < spectrumHeight; y += stride) {
                        float yEnd = qMin(y + dashLen, spectrumHeight);
                        // Two triangles per dash segment
                        notchVerts << notchX << y << notchX + notchWidth << y << notchX + notchWidth << yEnd << notchX
                                   << y << notchX + notchWidth << yEnd << notchX << yEnd;
                    }

                    QRhiResourceUpdateBatch *notchRub = m_rhi->nextResourceUpdateBatch();
                    notchRub->updateDynamicBuffer(m_notchVbo.get(), 0, notchVerts.size() * sizeof(float),
                                                  notchVerts.constData());

                    struct {
                        float viewportWidth;
                        float viewportHeight;
                        float pad0, pad1;
                        float r, g, b, a;
                    } notchUniforms = {w,
                                       h,
                                       0,
                                       0,
                                       static_cast<float>(m_notchColor.redF()),
                                       static_cast<float>(m_notchColor.greenF()),
                                       static_cast<float>(m_notchColor.blueF()),
                                       static_cast<float>(m_notchColor.alphaF())};
                    notchRub->updateDynamicBuffer(m_notchUniformBuffer.get(), 0, sizeof(notchUniforms), &notchUniforms);

                    cb->resourceUpdate(notchRub);
                    cb->setGraphicsPipeline(m_overlayTrianglePipeline.get());
                    cb->setShaderResources(m_notchSrb.get());
                    const QRhiCommandBuffer::VertexInput notchVbufBinding(m_notchVbo.get(), 0);
                    cb->setVertexInput(0, 1, &notchVbufBinding);
                    cb->draw(notchVerts.size() / 2); // 2 floats per vertex
                }
            }
        }
    }

    cb->endPass();
}

void PanadapterRhiWidget::updateSpectrum(const QByteArray &payload, int binsOffset, int binCount, qint64 centerFreq,
                                         qint32 sampleRate, float noiseFloor) {
    m_centerFreq = centerFreq;
    m_sampleRate = sampleRate;
    m_noiseFloor = noiseFloor;

    // Zero-copy view into payload's bin data
    QByteArray bins = QByteArray::fromRawData(payload.constData() + binsOffset, binCount);

    // K4 tier span = sampleRate * 1000 Hz
    qint32 tierSpanHz = sampleRate * 1000;
    int totalBins = binCount;

    const float attackAlpha = m_attackAlpha;
    const float decayAlpha = m_decayAlpha;

    // === Full-tier path (all bins → waterfall storage) ===
    decompressBins(bins, m_tierRawSpectrum);

    // Reset tier EMA on tier transition to avoid cross-tier blending
    if (sampleRate != m_lastTierSampleRate) {
        m_tierSpectrum = m_tierRawSpectrum;
        m_lastTierSampleRate = sampleRate;
    } else if (m_tierSpectrum.size() != m_tierRawSpectrum.size()) {
        m_tierSpectrum = m_tierRawSpectrum;
    } else {
        for (int i = 0; i < m_tierRawSpectrum.size(); ++i) {
            float alpha = (m_tierRawSpectrum[i] > m_tierSpectrum[i]) ? attackAlpha : decayAlpha;
            m_tierSpectrum[i] = alpha * m_tierRawSpectrum[i] + (1.0f - alpha) * m_tierSpectrum[i];
        }
    }
    m_waterfallTierBinCount = totalBins;
    m_waterfallTierSpanHz = static_cast<float>(tierSpanHz);

    // === Cropped path (center bins → live spectrum trace) ===
    QByteArray binsToUse;
    if (tierSpanHz > m_spanHz && totalBins > 100 && m_spanHz > 0) {
        int requestedBins = (static_cast<qint64>(m_spanHz) * totalBins) / tierSpanHz;
        requestedBins = qBound(50, requestedBins, totalBins);
        int centerStart = (totalBins - requestedBins) / 2;
        binsToUse = bins.mid(centerStart, requestedBins);
    } else {
        binsToUse = bins;
    }

    decompressBins(binsToUse, m_rawSpectrum);

    if (m_currentSpectrum.size() != m_rawSpectrum.size()) {
        m_currentSpectrum = m_rawSpectrum;
    } else {
        for (int i = 0; i < m_rawSpectrum.size(); ++i) {
            float alpha = (m_rawSpectrum[i] > m_currentSpectrum[i]) ? attackAlpha : decayAlpha;
            m_currentSpectrum[i] = alpha * m_rawSpectrum[i] + (1.0f - alpha) * m_currentSpectrum[i];
        }
    }

    m_waterfallNeedsUpdate = true;
    updateFreqScaleOverlay(); // Update frequency labels when center freq changes
    update();
}

void PanadapterRhiWidget::decompressBins(const QByteArray &bins, QVector<float> &out) {
    // K4 spectrum bins: dBm = raw_byte - RhiUtils::K4_DBM_OFFSET
    out.resize(bins.size());
    for (int i = 0; i < bins.size(); ++i) {
        out[i] = static_cast<quint8>(bins[i]) - RhiUtils::K4_DBM_OFFSET;
    }
}

void PanadapterRhiWidget::updateWaterfallData() {
    // Use full-tier data for waterfall (eliminates black bars on span change)
    const QVector<float> &source = m_tierSpectrum.isEmpty() ? m_currentSpectrum : m_tierSpectrum;
    if (source.isEmpty())
        return;

    // Upload raw bins centered in texture for shader sampling
    int row = m_waterfallWriteRow;
    int specSize = source.size();
    int offset = (m_textureWidth - specSize) / 2;

    // Clear row (zeros outside bin region = no signal)
    std::memset(&m_waterfallData[row * m_textureWidth], 0, m_textureWidth);

    // Copy raw bins (no interpolation - GPU handles it)
    for (int i = 0; i < specSize; ++i) {
        float normalized = normalizeDb(source[i]);
        m_waterfallData[row * m_textureWidth + offset + i] =
            static_cast<quint8>(qBound(0, static_cast<int>(normalized * 255), 255));
    }
}

float PanadapterRhiWidget::normalizeDb(float db) {
    return qBound(0.0f, (db - m_minDb) / (m_maxDb - m_minDb), 1.0f);
}

float PanadapterRhiWidget::freqToNormalized(qint64 freq) {
    // Map frequency to normalized range [0.0, 1.0] for drawing markers and passbands.
    //
    // In CW mode, offset the display center by IF shift to match the K4's display convention:
    // the VFO marker (at dial + IS*10) appears centered, and labels show dial-equivalent
    // frequencies so CW operators can read tuning positions directly from the waterfall.
    // The K4 auto-adjusts IS to track CW pitch, keeping the display in sync.
    qint64 effectiveCenter = m_centerFreq;
    if (m_mode == "CW") {
        effectiveCenter = m_centerFreq + m_ifShift * 10;
    } else if (m_mode == "CW-R") {
        effectiveCenter = m_centerFreq - m_ifShift * 10;
    }
    qint64 startFreq = effectiveCenter - m_spanHz / 2;
    return static_cast<float>(freq - startFreq) / static_cast<float>(m_spanHz);
}

qint64 PanadapterRhiWidget::xToFreq(int x, int w) {
    // Map pixel position to frequency for click-to-tune.
    // Must use the same shifted coordinate system (effectiveCenter) as the display
    // so the click lands where the user sees it on the frequency labels.
    if (w <= 0)
        return m_centerFreq;
    qint64 effectiveCenter = m_centerFreq;
    if (m_mode == "CW") {
        effectiveCenter = m_centerFreq + m_ifShift * 10;
    } else if (m_mode == "CW-R") {
        effectiveCenter = m_centerFreq - m_ifShift * 10;
    }
    qint64 startFreq = effectiveCenter - m_spanHz / 2;
    // Clamp to [0, 1] to prevent runaway acceleration when dragging past edges
    double normalized = qBound(0.0, static_cast<double>(x) / static_cast<double>(w), 1.0);
    return startFreq + static_cast<qint64>(normalized * m_spanHz);
}

int PanadapterRhiWidget::calculateGridInterval(int spanHz) const {
    int targetLines = 10;
    static const int niceIntervals[] = {
        100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000,
    };
    for (int nice : niceIntervals) {
        if (spanHz / nice <= targetLines + 2)
            return nice;
    }
    return 100000;
}

// Configuration setters
void PanadapterRhiWidget::setDbRange(float minDb, float maxDb) {
    m_minDb = minDb;
    m_maxDb = maxDb;
    updateDbmScaleOverlay(); // Update overlay labels when range changes
    update();
}

void PanadapterRhiWidget::setSpectrumRatio(float ratio) {
    m_spectrumRatio = qBound(0.1f, ratio, 0.9f);
    updateDbmScaleOverlay();  // Resize dBm scale to match new spectrum area
    updateFreqScaleOverlay(); // Reposition frequency labels at boundary
    update();
}

void PanadapterRhiWidget::setWaterfallHeight(int percent) {
    // Waterfall height percentage: 50% means 50% waterfall, 50% spectrum
    // Spectrum ratio = (100 - waterfallHeight) / 100
    float ratio = (100.0f - qBound(10, percent, 90)) / 100.0f;
    m_spectrumRatio = qBound(0.1f, ratio, 0.9f);
    updateDbmScaleOverlay();  // Resize dBm scale to match new spectrum area
    updateFreqScaleOverlay(); // Reposition frequency labels at boundary
    update();
}

void PanadapterRhiWidget::setTunedFrequency(qint64 freq) {
    if (m_tunedFreq != freq) {
        m_tunedFreq = freq;
        update();
    }
}

void PanadapterRhiWidget::setFilterBandwidth(int bwHz) {
    m_filterBw = bwHz;
    update();
}

void PanadapterRhiWidget::setMode(const QString &mode) {
    m_mode = mode;
    updateFreqScaleOverlay();
    update();
}

void PanadapterRhiWidget::setDataSubMode(int subMode) {
    if (m_dataSubMode != subMode) {
        m_dataSubMode = subMode;
        update();
    }
}

void PanadapterRhiWidget::setIfShift(int shift) {
    if (m_ifShift != shift) {
        m_ifShift = shift;
        updateFreqScaleOverlay();
        update();
    }
}

void PanadapterRhiWidget::setCwPitch(int pitchHz) {
    if (m_cwPitch != pitchHz) {
        m_cwPitch = pitchHz;
        update();
    }
}

void PanadapterRhiWidget::clear() {
    // Clear runtime data buffers
    m_currentSpectrum.clear();
    m_rawSpectrum.clear();
    m_waterfallWriteRow = 0;
    m_waterfallData.fill(0);
    m_waterfallNeedsFullClear = true;

    // Reset all radio state to header defaults.
    // Most values match their member initializers; only m_cursorVisible differs
    // (header inits to true, clear() hides it on disconnect).
    m_centerFreq = 0;
    m_tunedFreq = 0;
    m_spanHz = 10000;
    m_mode = "USB";
    m_cwPitch = 500;
    m_ifShift = 50;
    m_filterBw = 2400;
    m_notchEnabled = false;
    m_notchPitchHz = 0;
    m_cursorVisible = false; // Intentionally differs from header default (true)
    m_secondaryTunedFreq = 0;
    m_secondaryFilterBw = 0;
    m_txFreq = 0;
    m_txMarkerVisible = false;
    m_fskMarkTone = 915;
    m_rttyShift = 170;

    // Hide frequency labels (paintEvent returns early when spanHz <= 0)
    if (m_freqScaleOverlay)
        m_freqScaleOverlay->setFrequencyRange(0, 0, 0, "", 0);

    update();
}

void PanadapterRhiWidget::setGridEnabled(bool enabled) {
    m_gridEnabled = enabled;
    update();
}

void PanadapterRhiWidget::setRefLevel(int level) {
    if (m_refLevel != level) {
        m_refLevel = level;
        updateDbRangeFromRefAndScale();
        update();
    }
}

void PanadapterRhiWidget::setScale(int scale) {
    // Scale range: 10-150 (per K4 documentation)
    // Higher values = more compressed display (signals appear weaker, wider dB range)
    // Lower values = more expanded display (signals appear stronger, narrower dB range)
    if (m_scale != scale && scale >= 10 && scale <= 150) {
        m_scale = scale;
        updateDbRangeFromRefAndScale();
        update();
    }
}

void PanadapterRhiWidget::updateDbRangeFromRefAndScale() {
    // RefLevel is the bottom reference, scale is the dB range upward
    // Display shows from refLevel to (refLevel + scale)
    m_minDb = static_cast<float>(m_refLevel);
    m_maxDb = static_cast<float>(m_refLevel) + static_cast<float>(m_scale);

    updateDbmScaleOverlay();
}

void PanadapterRhiWidget::setSpan(int spanHz) {
    if (m_spanHz != spanHz && spanHz > 0) {
        m_spanHz = spanHz;
        updateFreqScaleOverlay();
        update();
    }
}

void PanadapterRhiWidget::setNotchFilter(bool enabled, int pitchHz) {
    if (m_notchEnabled != enabled || m_notchPitchHz != pitchHz) {
        m_notchEnabled = enabled;
        m_notchPitchHz = pitchHz;
        update();
    }
}

void PanadapterRhiWidget::setCursorVisible(bool visible) {
    if (m_cursorVisible != visible) {
        m_cursorVisible = visible;
        update();
    }
}

void PanadapterRhiWidget::setAmplitudeUnits(bool useSUnits) {
    if (m_dbmScaleOverlay) {
        m_dbmScaleOverlay->setUseSUnits(useSUnits);
    }
}

void PanadapterRhiWidget::setAveraging(int level) {
    level = qBound(1, level, 20);
    if (m_averagingLevel == level)
        return;
    m_averagingLevel = level;
    float t = (level - 1) / 19.0f;
    m_attackAlpha = 0.52f - t * 0.22f; // 0.52 → 0.30
    m_decayAlpha = 0.34f - t * 0.24f;  // 0.34 → 0.10
}

// Secondary VFO setters
void PanadapterRhiWidget::setSecondaryVfo(qint64 freq, int bwHz, const QString &mode, int ifShift, int dataSubMode) {
    m_secondaryTunedFreq = freq;
    m_secondaryFilterBw = bwHz;
    m_secondaryMode = mode;
    m_secondaryDataSubMode = dataSubMode;
    m_secondaryIfShift = ifShift;
    update();
}

void PanadapterRhiWidget::setSecondaryVisible(bool visible) {
    if (m_secondaryVisible != visible) {
        m_secondaryVisible = visible;
        update();
    }
}

void PanadapterRhiWidget::setSecondaryPassbandColor(const QColor &color) {
    m_secondaryPassbandColor = color;
    update();
}

void PanadapterRhiWidget::setSecondaryMarkerColor(const QColor &color) {
    m_secondaryMarkerColor = color;
    update();
}

void PanadapterRhiWidget::setPassbandColor(const QColor &color) {
    m_passbandColor = color;
    update();
}

void PanadapterRhiWidget::setFrequencyMarkerColor(const QColor &color) {
    m_frequencyMarkerColor = color;
    update();
}

void PanadapterRhiWidget::setTxMarker(qint64 freq, bool visible) {
    if (m_txFreq != freq || m_txMarkerVisible != visible) {
        m_txFreq = freq;
        m_txMarkerVisible = visible;
        update();
    }
}

void PanadapterRhiWidget::setFskMarkTone(int toneHz) {
    if (m_fskMarkTone != toneHz) {
        m_fskMarkTone = toneHz;
        update();
    }
}

// Mouse events
void PanadapterRhiWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_isDragging = true;
        m_isRightDragging = false;
        qint64 freq = xToFreq(event->pos().x(), width());
        emit frequencyClicked(freq);
        event->accept();
    } else if (event->button() == Qt::RightButton) {
        m_isRightDragging = true;
        m_isDragging = false;
        qint64 freq = xToFreq(event->pos().x(), width());
        emit frequencyRightClicked(freq);
        event->accept();
    }
}

void PanadapterRhiWidget::mouseMoveEvent(QMouseEvent *event) {
    if (m_isDragging && (event->buttons() & Qt::LeftButton)) {
        int x = event->pos().x();
        int w = width();

        if (x < 0) {
            // Dragging past left edge → emit scroll signal (like wheel down)
            // Rate limit to prevent flooding: at most one scroll per 100ms
            if (!m_edgeScrollTimer.isValid() || m_edgeScrollTimer.elapsed() >= 100) {
                m_edgeScrollTimer.restart();
                emit frequencyScrolled(-1);
            }
        } else if (x > w) {
            // Dragging past right edge → emit scroll signal (like wheel up)
            if (!m_edgeScrollTimer.isValid() || m_edgeScrollTimer.elapsed() >= 100) {
                m_edgeScrollTimer.restart();
                emit frequencyScrolled(1);
            }
        } else {
            // Normal drag within display bounds
            qint64 freq = xToFreq(x, w);
            emit frequencyDragged(freq);
        }
        event->accept();
    } else if (m_isRightDragging && (event->buttons() & Qt::RightButton)) {
        int x = event->pos().x();
        int w = width();

        if (x < 0) {
            // Dragging past left edge → emit scroll signal for opposite VFO
            if (!m_edgeScrollTimer.isValid() || m_edgeScrollTimer.elapsed() >= 100) {
                m_edgeScrollTimer.restart();
                emit frequencyScrolled(-1);
            }
        } else if (x > w) {
            // Dragging past right edge → emit scroll signal for opposite VFO
            if (!m_edgeScrollTimer.isValid() || m_edgeScrollTimer.elapsed() >= 100) {
                m_edgeScrollTimer.restart();
                emit frequencyScrolled(1);
            }
        } else {
            // Normal drag within display bounds
            qint64 freq = xToFreq(x, w);
            emit frequencyRightDragged(freq);
        }
        event->accept();
    }
}

void PanadapterRhiWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_isDragging = false;
        event->accept();
    } else if (event->button() == Qt::RightButton) {
        m_isRightDragging = false;
        event->accept();
    }
}

void PanadapterRhiWidget::wheelEvent(QWheelEvent *event) {
    int key = 0; // frequency (no modifier)
    if (event->modifiers() & Qt::ShiftModifier)
        key = 1; // scale
    else if (event->modifiers() & Qt::ControlModifier)
        key = 2; // ref level

    int steps = m_wheelAccumulator.accumulate(event, key);
    if (steps != 0) {
        switch (key) {
        case 1:
            emit scaleScrolled(steps);
            break;
        case 2:
            emit refLevelScrolled(steps);
            break;
        default:
            emit frequencyScrolled(steps);
            break;
        }
    }
    event->accept();
}
