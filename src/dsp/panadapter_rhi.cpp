#include "panadapter_rhi.h"
#include "logging/logcategories.h"
#include "dsp/bandplanstrip.h"
#include "dsp/spectrumgeometry.h"
#include "dsp/spectrumscale.h"
#include "dsp/waterfallgeometry.h"
#include "models/radiostate/spectrumdisplaystate.h"
#include "panadapter_constants.h"
#include "dashgeometry.h"
#include "rhi_utils.h"
#include "ui/styling/k4styles.h"
#include "utils/panelscale.h"
#include <QFontMetrics>
#include <QLoggingCategory>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QScreen>
#include <QtMath>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <utility>

namespace {

// Stretch a spectrum curve to a different bin count, keeping its shape across the frequency axis.
// Used when the bin count changes under an EMA that must not be interrupted.
QVector<float> resampleLinear(const QVector<float> &src, int dstSize) {
    if (src.isEmpty() || dstSize <= 0)
        return QVector<float>(qMax(0, dstSize), 0.0f);
    if (src.size() == dstSize)
        return src;

    QVector<float> out(dstSize);
    if (src.size() == 1) {
        out.fill(src[0]);
        return out;
    }
    const float step = static_cast<float>(src.size() - 1) / static_cast<float>(qMax(1, dstSize - 1));
    for (int i = 0; i < dstSize; ++i) {
        const float pos = i * step;
        const int lo = qBound(0, static_cast<int>(pos), src.size() - 1);
        const int hi = qMin(lo + 1, src.size() - 1);
        const float frac = pos - lo;
        out[i] = src[lo] * (1.0f - frac) + src[hi] * frac;
    }
    return out;
}

// The amplitude-axis and frequency-scale labels: readout size, Medium weight, tabular figures.
QFont scaleLabelFont() {
    return K4Styles::Fonts::dataFont(K4Styles::Dimensions::FontSizeReadout, QFont::Medium);
}

// Amplitude axis geometry: margin, label, gap, tick. kHaloPx is the 1 px ring drawHaloText adds.
constexpr qreal kAxisLeftMargin = 4.0;
constexpr qreal kAxisTickGap = 3.0;
constexpr qreal kAxisTickLength = 4.0;
constexpr qreal kHaloPx = 1.0;
// Clear space between neighbouring frequency-scale labels.
constexpr qreal kLabelGapPx = 12.0;

} // namespace

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

    // Reserve px at the top: labels that would fall under the band-plan strip are skipped.
    void setTopReserved(int px) {
        if (m_topReserved != px) {
            m_topReserved = px;
            update();
        }
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const QFont scaleFont = scaleLabelFont();
        painter.setFont(scaleFont);

        const int h = height();

        const QFontMetricsF fm(scaleFont, this);
        const qreal textHeight = fm.height();
        // Baseline offset that centres the digits (cap height) on the label's dBm line.
        const qreal baselineOffset = fm.capHeight() / 2.0;

        // WHY labels are placed by value rather than by dividing the widget: S-units sit at fixed
        // dBm (S9 = -73, 6 dB apart), which does not divide the display range. Sampling even
        // divisions and naming whichever S-unit was nearest skipped some and, once clamped at the
        // ends, printed S1 at two different heights. SpectrumScale emits only values that genuinely
        // fall inside the window, already spaced far enough apart to read.
        const auto labels = SpectrumScale::labelsFor(m_minDb, m_maxDb, m_useSUnits, h, fm.lineSpacing());

        const QPen tickHalo(K4Styles::withAlpha(K4Styles::Colors::Black, K4Styles::Colors::Alpha::Halo),
                            1.0 + 2.0 * kHaloPx);
        const QPen tickPen(QColor(K4Styles::Colors::TextPrimary), 1.0);
        for (const auto &label : labels) {
            const float frac = SpectrumScale::normalizedForDb(label.dbm, m_minDb, m_maxDb);
            const qreal yPos = std::round((1.0 - frac) * h);

            // Skip labels that would fall under the band-plan strip.
            if (yPos < m_topReserved)
                continue;
            // Keep the text fully on the widget at both ends.
            if (yPos < textHeight / 2.0 || yPos > h - textHeight / 2.0)
                continue;

            K4Styles::drawHaloText(painter, QPointF(kAxisLeftMargin, yPos + baselineOffset), label.text,
                                   QColor(K4Styles::Colors::TextPrimary));
            // A tick, because labels no longer coincide with the grid: the grid is a fixed-size
            // cell pattern and these sit at physical dBm values, so the two only align by accident.
            // WHY: after the text, not before it — a tick in front of a minus sign read as "--".
            const qreal tickX = kAxisLeftMargin + fm.horizontalAdvance(label.text) + kAxisTickGap;
            const QLineF tick(tickX, yPos + 0.5, tickX + kAxisTickLength, yPos + 0.5);
            painter.setPen(tickHalo);
            painter.drawLine(tick);
            painter.setPen(tickPen);
            painter.drawLine(tick);
        }
    }

private:
    // Always set by the panadapter (setDbRange) before the first paint.
    float m_minDb = static_cast<float>(SpectrumDisplayLimits::RefLevelDefault);
    float m_maxDb = static_cast<float>(SpectrumDisplayLimits::RefLevelDefault + SpectrumDisplayLimits::ScaleDefault);
    bool m_useSUnits = false;
    int m_topReserved = 0;
};

// Transparent overlay widget for frequency scale labels at spectrum/waterfall boundary
class FrequencyScaleOverlay : public QWidget {
public:
    FrequencyScaleOverlay(QWidget *parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_TranslucentBackground);
    }

    // displayCenter is the frequency at the horizontal centre (SpectrumGeometry::displayCenterHz);
    // it differs from centerFreq only in CW, where the display is offset by the IF shift.
    void setFrequencyRange(qint64 centerFreq, qint64 displayCenter, int spanHz, int labelInterval) {
        m_centerFreq = centerFreq;
        m_displayCenter = displayCenter;
        m_spanHz = spanHz;
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

        const QFont scaleFont = scaleLabelFont();
        painter.setFont(scaleFont);

        const QFontMetricsF fm(scaleFont, this);
        const qreal w = width();
        // The digits' cap height is centred on the spectrum/waterfall boundary, which runs through
        // the middle of this overlay.
        const qreal baselineY = height() / 2.0 + fm.capHeight() / 2.0;

        // In CW the display centre is offset by the IF shift so the VFO marker appears centred.
        // Labels show dial-equivalent frequencies (round kHz values) positioned via the
        // shifted RF coordinate system so CW operators can read tuning positions directly.
        const qint64 cwOffset = m_displayCenter - m_centerFreq;
        qint64 startFreq = m_displayCenter - m_spanHz / 2;

        // Compute dial-equivalent frequency range for label iteration
        qint64 dialStart = startFreq - cwOffset;
        qint64 dialEnd = dialStart + m_spanHz;

        // First round dial frequency on a multiple of `step` at or after the left edge.
        const auto firstOnStep = [dialStart](qint64 step) {
            qint64 first = (dialStart / step) * step;
            return first < dialStart ? first + step : first;
        };

        // One step for the whole scale, wide enough for the widest label in view, so labels stay on
        // grid lines and evenly spaced.
        qreal widestLabel = 0.0;
        for (qint64 f = firstOnStep(m_labelInterval); f <= dialEnd; f += m_labelInterval)
            widestLabel = std::max(widestLabel, fm.horizontalAdvance(formatFrequency(f)));
        const qint64 interval =
            SpectrumGeometry::frequencyLabelStepHz(m_labelInterval, m_spanHz, w, widestLabel, kLabelGapPx);
        const qint64 firstDialLabel = firstOnStep(interval);

        for (qint64 dialFreq = firstDialLabel; dialFreq <= dialEnd; dialFreq += interval) {
            // Position using RF coordinate system (add CW offset back)
            qint64 rfFreq = dialFreq + cwOffset;
            const qreal normalized = static_cast<qreal>(rfFreq - startFreq) / static_cast<qreal>(m_spanHz);
            const qreal x = std::round(normalized * w);

            // Label text shows the dial frequency
            const QString label = formatFrequency(dialFreq);
            const qreal textWidth = fm.horizontalAdvance(label);

            // Center text horizontally on the frequency position, on a whole pixel
            const qreal textX = std::round(x - textWidth / 2.0);

            // Skip a label the widget edge would cut (the halo needs 1 px too)
            if (textX < 2 + kHaloPx || textX + textWidth > w - 2 - kHaloPx)
                continue;

            K4Styles::drawHaloText(painter, QPointF(textX, baselineY), label, QColor(K4Styles::Colors::TextPrimary));
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
    qint64 m_displayCenter = 0;
    int m_spanHz = 10000;
    int m_labelInterval = 1000;
};

// Band-plan strip across the top of the panadapter: one row of mode segments with the band name
// pinned as a tag, hatched where the screen runs past the band edge, and calling-frequency marker
// ticks. Placement is decided by BandPlanStrip::layout; this only paints it.
class BandPlanOverlay : public QWidget {
public:
    static constexpr int kStripH = 18;

    BandPlanOverlay(QWidget *parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_TranslucentBackground);
    }

    // viewStartHz is the frequency at the left edge, from the same display centre as the
    // frequency-scale overlay so segments line up with the labels below.
    void setData(qint64 viewStartHz, int spanHz, const QString &bandName,
                 const QVector<BandPlan::BandSegment> &segments, const QVector<BandPlan::BandMarker> &markers) {
        m_viewStartHz = viewStartHz;
        m_spanHz = spanHz;
        m_bandName = bandName;
        m_segments = segments;
        m_markers = markers;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        if (m_spanHz <= 0 || m_segments.isEmpty())
            return;

        const int w = width();
        const int h = height();
        QFont labelFont = K4Styles::Fonts::dataFont(K4Styles::Dimensions::FontSizeCaption, QFont::Bold);
        QFont markerFont = K4Styles::Fonts::dataFont(K4Styles::Dimensions::FontSizeCaption, QFont::Bold);
        const QFontMetrics fm(labelFont);
        const QFontMetrics mfm(markerFont);

        const BandPlanStrip::Layout strip = BandPlanStrip::layout(
            m_segments, m_markers, m_bandName, m_viewStartHz, m_spanHz, w,
            [&fm](const QString &text) { return fm.horizontalAdvance(text); },
            [&mfm](const QString &text) { return mfm.horizontalAdvance(text); });

        QPainter painter(this);
        painter.setRenderHint(QPainter::TextAntialiasing);

        for (int i = 0; i < strip.blocks.size(); ++i) {
            const BandPlanStrip::Block &b = strip.blocks[i];
            const QRect rect(b.x1, 0, b.x2 - b.x1, h);
            if (b.kind == BandPlanStrip::BlockKind::Segment) {
                QColor fill = BandPlan::modeColor(b.mode);
                fill.setAlpha(K4Styles::Colors::Alpha::BandPlanSegment); // translucent so the spectrum shows through
                painter.fillRect(rect, fill);
            } else {
                QColor fill(K4Styles::Colors::BandPlanOutOfBand);
                fill.setAlpha(K4Styles::Colors::Alpha::BandPlanOutOfBand);
                painter.fillRect(rect, fill);
                painter.fillRect(
                    rect, QBrush(K4Styles::withAlpha(K4Styles::Colors::White, K4Styles::Colors::Alpha::BandPlanHatch),
                                 Qt::BDiagPattern));
            }
            // WHY a divider as well as a colour change: the labels and these lines carry the
            // boundaries, so the strip still reads for a red-green colour-blind operator.
            if (i > 0) {
                painter.setPen(K4Styles::withAlpha(K4Styles::Colors::White, K4Styles::Colors::Alpha::BandPlanDivider));
                painter.drawLine(b.x1, 0, b.x1, h);
            }
        }

        painter.setFont(labelFont);
        if (strip.tag.visible) {
            const QRect tagRect(strip.tag.x, 0, strip.tag.width, h);
            painter.fillRect(tagRect, QColor(K4Styles::Colors::BandPlanTag));
            painter.setPen(QColor(K4Styles::Colors::TextPrimary));
            painter.drawText(tagRect, Qt::AlignCenter, m_bandName);
        }

        for (const BandPlanStrip::Label &label : strip.labels) {
            const bool outOfBand = strip.blocks[label.block].kind == BandPlanStrip::BlockKind::OutOfBand;
            painter.setPen(outOfBand ? QColor(K4Styles::Colors::TextSecondary) : QColor(K4Styles::Colors::TextPrimary));
            painter.drawText(QRect(label.x, 0, label.width + 2, h), Qt::AlignVCenter | Qt::AlignLeft, label.text);
        }

        // Calling-frequency markers: a hairline and a small downward caret mark the exact spot, so
        // the label reads as a point rather than a section.
        painter.setFont(markerFont);
        for (const BandPlanStrip::Marker &m : strip.markers) {
            painter.setPen(
                QPen(K4Styles::withAlpha(K4Styles::Colors::White, K4Styles::Colors::Alpha::BandPlanMarker), 1));
            painter.drawLine(m.x, 0, m.x, h);

            QPainterPath caret;
            caret.moveTo(m.x - 3.0, 0.5);
            caret.lineTo(m.x + 3.0, 0.5);
            caret.lineTo(m.x, 4.5);
            caret.closeSubpath();
            painter.fillPath(caret,
                             K4Styles::withAlpha(K4Styles::Colors::White, K4Styles::Colors::Alpha::BandPlanCaret));

            if (m.labelVisible) {
                painter.setPen(QColor(K4Styles::Colors::TextPrimary));
                painter.drawText(QRect(m.labelX, 0, m.labelWidth + 2, h), Qt::AlignVCenter | Qt::AlignLeft, m.name);
            }
        }
    }

private:
    qint64 m_viewStartHz = 0;
    int m_spanHz = 10000;
    QString m_bandName;
    QVector<BandPlan::BandSegment> m_segments;
    QVector<BandPlan::BandMarker> m_markers;
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
    m_freqScaleOverlay->setFrequencyRange(m_centerFreq, displayCenterHz(), m_spanHz, calculateGridInterval(m_spanHz));
    m_freqScaleOverlay->show();

    // Create band-plan overlay strip (child widget at the top). Hidden until segments are set.
    m_bandPlanOverlay = new BandPlanOverlay(this);
    m_bandPlanOverlay->hide();
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
    m_dbmScaleOverlay->setGeometry(0, 0, amplitudeAxisWidthPx(), spectrumHeight);
    m_dbmScaleOverlay->setDbRange(m_minDb, m_maxDb);
    m_dbmScaleOverlay->raise(); // Ensure it's on top
}

int PanadapterRhiWidget::amplitudeAxisWidthPx() const {
    // WHY "-188 dBm": the widest label the axis can print. Figures are tabular (equal width) on
    // Qt 6.7+; with proportional figures (Qt < 6.7) 8 is among the widest digits.
    const QFontMetricsF fm(scaleLabelFont(), this);
    return qCeil(kAxisLeftMargin + fm.horizontalAdvance(QStringLiteral("-188 dBm")) + kAxisTickGap + kAxisTickLength +
                 kHaloPx);
}

void PanadapterRhiWidget::updateBandPlanOverlay() {
    if (!m_bandPlanOverlay)
        return;
    const bool active = m_bandPlanVisible && !m_bandPlanSegments.isEmpty();
    if (m_dbmScaleOverlay)
        m_dbmScaleOverlay->setTopReserved(active ? BandPlanOverlay::kStripH : 0);
    if (!active) {
        m_bandPlanOverlay->hide();
        return;
    }
    m_bandPlanOverlay->setGeometry(0, 0, width(), BandPlanOverlay::kStripH);
    m_bandPlanOverlay->setData(displayCenterHz() - m_spanHz / 2, m_spanHz, m_bandPlanName, m_bandPlanSegments,
                               m_bandPlanMarkers);
    m_bandPlanOverlay->show();
    m_bandPlanOverlay->raise();
}

void PanadapterRhiWidget::setBandPlan(const QString &bandName, const QVector<BandPlan::BandSegment> &segments,
                                      const QVector<BandPlan::BandMarker> &markers) {
    m_bandPlanName = bandName;
    m_bandPlanSegments = segments;
    m_bandPlanMarkers = markers;
    updateBandPlanOverlay();
}

void PanadapterRhiWidget::setBandPlanVisible(bool visible) {
    if (m_bandPlanVisible != visible) {
        m_bandPlanVisible = visible;
        updateBandPlanOverlay();
    }
}

void PanadapterRhiWidget::updateFreqScaleOverlay() {
    updateBandPlanOverlay(); // band-plan strip refreshes on the same center/span/mode/resize triggers
    if (!m_freqScaleOverlay)
        return;

    const int h = height();
    const int w = width();
    const int spectrumHeight = static_cast<int>(h * m_spectrumRatio);

    // Centred on the boundary between spectrum and waterfall; tall enough for the label font and
    // its halo, measured rather than fixed so it follows the font size.
    const QFontMetricsF fm(scaleLabelFont(), this);
    const int overlayHeight = qCeil(fm.height() + 2.0 * kHaloPx);
    const int overlayY = spectrumHeight - overlayHeight / 2;

    m_freqScaleOverlay->setGeometry(0, overlayY, w, overlayHeight);
    m_freqScaleOverlay->setFrequencyRange(m_centerFreq, displayCenterHz(), m_spanHz, calculateGridInterval(m_spanHz));
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
        qCWarning(lcSpectrumRender) << "QRhi is NULL - GPU backend failed to initialize";
        return;
    }

    // Use fixed texture sizes - GPU bilinear filtering handles scaling to display size
    m_textureWidth = BASE_TEXTURE_WIDTH;
    m_waterfallHistory = MAX_WATERFALL_HISTORY;

    // Allocate waterfall data buffer
    m_waterfallData.resize(m_textureWidth * m_waterfallHistory);
    m_waterfallData.fill(0);
    m_rowTierSpanHz.resize(m_waterfallHistory);
    m_rowTierSpanHz.fill(0.0f);

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

    // Waterfall quad (static). Texcoord t spans the full 0..1 and the vertex shader scales it by
    // visibleFraction, so changing how many rows are on screen costs a uniform rather than a
    // rebuilt vertex buffer on the render path.
    float waterfallQuad[] = {
        // position (x, y), texcoord (s, t)
        -1.0f, -1.0f, 0.0f, 0.0f, // bottom-left
        1.0f,  -1.0f, 1.0f, 0.0f, // bottom-right
        1.0f,  1.0f,  1.0f, 1.0f, // top-right
        -1.0f, -1.0f, 0.0f, 0.0f, // bottom-left
        1.0f,  1.0f,  1.0f, 1.0f, // top-right
        -1.0f, 1.0f,  0.0f, 1.0f  // top-left
    };
    m_waterfallVbo.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(waterfallQuad)));
    m_waterfallVbo->create();
    rub->uploadStaticBuffer(m_waterfallVbo.get(), waterfallQuad);

    // Overlay VBO: up to 4096 segments × 2 (x,y) floats. Covers the main panadapter overlays
    // (passband edges, markers, mouse QSY, span cursor). Wider than MiniPan because the full
    // panadapter shows more simultaneous overlays.
    constexpr int kOverlayMaxSegments = 4096;
    constexpr int kFloatsPerVertex = 2;
    m_overlayVbo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer,
                                        kOverlayMaxSegments * kFloatsPerVertex * sizeof(float)));
    m_overlayVbo->create();

    // Create uniform buffers
    m_waterfallUniformBuffer.reset(
        m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(RhiUtils::WaterfallUniforms)));
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
    m_notchVbo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer,
                                      PanadapterConstants::MaxDashFloats * sizeof(float)));
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

    // RTTY space tone line buffers (primary VFO)
    m_rttySpaceVbo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer,
                                          PanadapterConstants::MaxDashFloats * sizeof(float)));
    m_rttySpaceVbo->create();
    m_rttySpaceUniformBuffer.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32));
    m_rttySpaceUniformBuffer->create();

    // RTTY space tone line buffers (secondary VFO)
    m_secRttySpaceVbo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer,
                                             PanadapterConstants::MaxDashFloats * sizeof(float)));
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

        // RTTY space tone SRBs (primary VFO)
        m_rttySpaceSrb.reset(m_rhi->newShaderResourceBindings());
        m_rttySpaceSrb->setBindings({QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_rttySpaceUniformBuffer.get())});
        m_rttySpaceSrb->create();

        // RTTY space tone SRBs (secondary VFO)
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
    // Always clear to the spectrum background even if not initialized (prevents red/garbage)
    if (!m_rhiInitialized) {
        cb->beginPass(renderTarget(), QColor(K4Styles::Colors::SpectrumBackground), {1.0f, 0}, nullptr);
        cb->endPass();
        return;
    }

    // Create pipelines on first render (need render pass descriptor)
    if (!m_pipelinesCreated) {
        createPipelines();
        if (!m_pipelinesCreated) {
            cb->beginPass(renderTarget(), QColor(K4Styles::Colors::SpectrumBackground), {1.0f, 0}, nullptr);
            cb->endPass();
            return;
        }
    }

    const QSize outputSize = renderTarget()->pixelSize();
    const float w = outputSize.width();
    const float h = outputSize.height();
    const float spectrumHeight = h * m_spectrumRatio;
    const float waterfallHeight = h - spectrumHeight;

    // WHY: when the band-plan strip is shown it occupies the top kStripH logical px of the
    // spectrum. Stop the per-VFO overlays (passband fills, center/TX markers, RTTY dashes) at
    // its lower edge so they read as bounded by the banner instead of bleeding under the
    // translucent strip and looking clipped. kStripH is logical; scale to the physical render
    // surface. Banner off -> overlayTop is 0 and the overlays span the full spectrum as before.
    const bool bandPlanActive = m_bandPlanVisible && !m_bandPlanSegments.isEmpty();
    const float overlayTop =
        bandPlanActive ? static_cast<float>(BandPlanOverlay::kStripH) * static_cast<float>(devicePixelRatioF()) : 0.0f;

    QRhiResourceUpdateBatch *rub = m_rhi->nextResourceUpdateBatch();

    // Full waterfall clear (on disconnect)
    if (m_waterfallNeedsFullClear) {
        QRhiTextureSubresourceUploadDescription fullUpload(m_waterfallData.constData(), m_waterfallData.size());
        rub->uploadTexture(m_waterfallTexture.get(), QRhiTextureUploadEntry(0, 0, fullUpload));
        m_waterfallNeedsFullClear = false;
    }

    // Update waterfall texture if needed
    const bool rowCommitted = m_waterfallNeedsUpdate && !m_currentSpectrum.isEmpty();
    if (rowCommitted) {
        updateWaterfallData();
        QRhiTextureSubresourceUploadDescription rowUpload(
            m_waterfallData.constData() + m_waterfallWriteRow * m_textureWidth, m_textureWidth);
        rowUpload.setDestinationTopLeft(QPoint(0, m_waterfallWriteRow));
        rowUpload.setSourceSize(QSize(m_textureWidth, 1));
        rub->uploadTexture(m_waterfallTexture.get(), QRhiTextureUploadEntry(0, 0, rowUpload));
        m_waterfallWriteRow = (m_waterfallWriteRow + 1) % m_waterfallHistory;
        m_waterfallNeedsUpdate = false;
    }

    // One stored row per physical pixel of waterfall height, so a row is never stretched across
    // several pixels and blended with its neighbours, and each scroll step moves the image by one
    // whole physical pixel. That is one device pixel unless the OS downscales the framebuffer onto
    // the panel (updateDevicePxPerRow). Clamped to what is actually stored: past that the old
    // stretching returns, which the qk4.spectrum.diag geometry line reports as pxPerRow climbing
    // above devicePxPerRow.
    updateDevicePxPerRow();
    m_visibleRows =
        WaterfallGeometry::visibleRowsFor(waterfallHeight, m_waterfallHistory, MIN_VISIBLE_ROWS, m_devicePxPerRow);
    // Row edges onto the panel's pixel grid. The waterfall's bottom edge is this widget's bottom
    // edge (QRhi viewports are bottom-left), placed in the window's framebuffer: the window's own
    // origin is a whole number of points, so it never shifts the grid; the widget's position inside
    // it does whenever DPR is not a multiple of the row height (DPR 3 with QK4 at 150%).
    const qint64 waterfallBottomRow = std::llround(static_cast<double>(mapTo(window(), QPoint(0, height())).y()) *
                                                   static_cast<double>(devicePixelRatioF()));
    const float rowShift = WaterfallGeometry::rowShiftFor(waterfallBottomRow, m_devicePxPerRow);
    m_waterfallRowShift = rowShift;
    // WHY: a shifted window shows a partial row older than the oldest; keep that row off the write head.
    if (rowShift != 0.0f)
        m_visibleRows = (std::min)(m_visibleRows, m_waterfallHistory - 2);

    // Logged here rather than earlier in the frame: the geometry line reports pxPerRow, which is
    // only meaningful once the visible row count for this frame has been chosen.
    if (lcSpectrumDiag().isDebugEnabled()) {
        logRenderGeometry(outputSize);
        noteRenderCadence(rowCommitted);
    }

    // Show the newest m_visibleRows rows, ending one short of the row being written.
    //
    // WHY visibleFraction has no -1: the in-flight guard lives entirely in oldestVisible, which
    // starts the window m_visibleRows back from the write head so the top pixel lands on
    // m_waterfallWriteRow - 1. Carrying a second -1 in the fraction compressed the rows into one
    // slot fewer than there are pixels, which the shader's V snap turns from a half-row drift into
    // a real defect: the top pixel resolves to writeRow - 2, the newest complete row is never
    // drawn, and one row is skipped outright.
    const int oldestVisible =
        WaterfallGeometry::oldestVisibleRow(m_waterfallWriteRow, m_visibleRows, m_waterfallHistory);
    const float scrollOffset = WaterfallGeometry::scrollOffsetFor(oldestVisible, m_waterfallHistory);
    const float visibleFraction = WaterfallGeometry::visibleFractionFor(m_visibleRows, m_waterfallHistory);

    // Use full tier bin count for waterfall (matches what updateWaterfallData writes)
    float binCount = m_waterfallTierBinCount > 0   ? static_cast<float>(m_waterfallTierBinCount)
                     : m_currentSpectrum.isEmpty() ? static_cast<float>(m_textureWidth)
                                                   : static_cast<float>(m_currentSpectrum.size());
    float tierSpanHz = m_waterfallTierSpanHz > 0 ? m_waterfallTierSpanHz : static_cast<float>(m_spanHz);
    float spanHz = static_cast<float>(m_spanHz);
    // Layout comes from RhiUtils so it cannot drift from the shaders or from the mini-pan.
    // Rows store palette positions fixed at capture (updateWaterfallData), so the shader passes them
    // through: a REF or #WBS change recolours only rows drawn after it.
    const WaterfallGeometry::ColorUniforms color{};
    RhiUtils::WaterfallUniforms waterfallUniforms = {scrollOffset,
                                                     binCount,
                                                     static_cast<float>(m_textureWidth),
                                                     tierSpanHz,
                                                     spanHz,
                                                     visibleFraction,
                                                     static_cast<float>(m_waterfallHistory),
                                                     color.colorLow,
                                                     color.colorGain,
                                                     rowShift,
                                                     0.0f,
                                                     0.0f};
    rub->updateDynamicBuffer(m_waterfallUniformBuffer.get(), 0, sizeof(waterfallUniforms), &waterfallUniforms);

    if (!m_currentSpectrum.isEmpty()) {
        // Upload raw spectrum bins to 1D texture - GPU shader does bilinear interpolation
        int specSize = m_currentSpectrum.size();
        int offset = (m_textureWidth - specSize) / 2;

        // Reuse member vector to avoid per-frame heap allocation
        m_normalizedSpectrum.resize(m_textureWidth);
        std::fill(m_normalizedSpectrum.begin(), m_normalizedSpectrum.end(), 0.0f);
        // WHY nothing is subtracted here: this used to remove a smoothed per-frame minimum, an
        // automatic noise-floor tracker, before scaling by 0.95. It moved the trace by ~20% of the
        // chart height depending on band noise, so a signal's height depended on conditions rather
        // than on its strength, and no dBm or S-unit label could be correct. REF and SCALE define
        // the window; the radio owns them, including in auto-reference mode where it adjusts REF
        // itself and reports it. Height is now a pure function of dBm.
        for (int i = 0; i < specSize; ++i)
            m_normalizedSpectrum[offset + i] = SpectrumScale::normalizedForDb(m_currentSpectrum[i], m_minDb, m_maxDb);

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

    // Grid lines behind the spectrum, in the spectrum area's own coordinates.
    const float dpr = static_cast<float>(devicePixelRatioF());
    int gridVertexCount = 0;
    if (m_gridEnabled && m_overlayLinePipeline) {
        rebuildGridIfNeeded(w, spectrumHeight, dpr);
        gridVertexCount =
            RhiUtils::stageOverlay(rub, m_overlayVbo.get(), m_overlayUniformBuffer.get(), m_gridVerts.constData(),
                                   m_gridVerts.size(), w, spectrumHeight, m_gridColor);
    }

    // Per-VFO overlays, staged in draw order. Each has its own buffers (see RhiUtils::stageOverlay),
    // so every write lands in this frame's batch before the pass begins.
    struct OverlayDraw {
        QRhiBuffer *vbo;
        QRhiShaderResourceBindings *srb;
        int vertexCount;
    };
    std::array<OverlayDraw, 8> overlayDraws;
    int overlayDrawCount = 0;
    auto stage = [&](QRhiBuffer *vbo, QRhiBuffer *ubo, QRhiShaderResourceBindings *srb, const float *verts,
                     int floatCount, const QColor &color) {
        const int vertexCount = RhiUtils::stageOverlay(rub, vbo, ubo, verts, floatCount, w, h, color);
        if (vertexCount > 0 && overlayDrawCount < static_cast<int>(overlayDraws.size()))
            overlayDraws[overlayDrawCount++] = {vbo, srb, vertexCount};
    };
    auto stageBar = [&](float x1, float x2, QRhiBuffer *vbo, QRhiBuffer *ubo, QRhiShaderResourceBindings *srb,
                        const QColor &color) {
        const auto verts = RhiUtils::quadVerts(x1, overlayTop, x2, spectrumHeight);
        stage(vbo, ubo, srb, verts.data(), static_cast<int>(verts.size()), color);
    };
    auto stagePassband = [&](const SpectrumGeometry::Passband &pb, QRhiBuffer *vbo, QRhiBuffer *ubo,
                             QRhiShaderResourceBindings *srb, const QColor &color) {
        const float x1 = qBound(0.0f, freqToNormalized(pb.lowHz) * w, w);
        const float x2 = qBound(0.0f, freqToNormalized(pb.highHz) * w, w);
        if (x2 > x1)
            stageBar(x1, x2, vbo, ubo, srb, color);
    };
    auto stageMarker = [&](qint64 freq, QRhiBuffer *vbo, QRhiBuffer *ubo, QRhiShaderResourceBindings *srb,
                           const QColor &color) {
        const float x = freqToNormalized(freq) * w;
        if (x >= 0 && x <= w)
            stageBar(x, x + PanadapterConstants::MarkerLineWidth, vbo, ubo, srb, color);
    };
    auto stageDashed = [&](float x, float lineWidth, QVector<float> &verts, QRhiBuffer *vbo, QRhiBuffer *ubo,
                           QRhiShaderResourceBindings *srb, const QColor &color) {
        if (x < 0 || x > w)
            return;
        verts.clear();
        DashGeometry::appendDashedVerticalLine(verts, x, lineWidth, overlayTop, spectrumHeight);
        stage(vbo, ubo, srb, verts.constData(), verts.size(), color);
    };

    if (m_overlayLinePipeline && m_overlayTrianglePipeline) {
        // Secondary VFO first, so it renders behind the primary where they overlap.
        if (m_secondaryVisible && m_secondaryFilterBw > 0 && m_secondaryTunedFreq > 0) {
            stagePassband(SpectrumGeometry::passbandFor(m_secondaryMode, m_secondaryDataSubMode, m_secondaryTunedFreq,
                                                        m_secondaryFilterBw, m_secondaryIfShift, m_rttyShift),
                          m_secondaryPassbandVbo.get(), m_secondaryPassbandUniformBuffer.get(),
                          m_secondaryPassbandSrb.get(), m_secondaryPassbandColor);
            stageMarker(SpectrumGeometry::markerFreqHz(m_secondaryTunedFreq, m_secondaryIfShift, m_secondaryMode),
                        m_secondaryMarkerVbo.get(), m_secondaryMarkerUniformBuffer.get(), m_secondaryMarkerSrb.get(),
                        m_secondaryMarkerColor);
            // The mark tone coincides with the solid marker, so only the space tone is drawn.
            if (SpectrumGeometry::isRtty(m_secondaryMode, m_secondaryDataSubMode) && m_fskMarkTone > 0)
                stageDashed(freqToNormalized(m_secondaryTunedFreq - m_rttyShift) * w,
                            PanadapterConstants::RttyDashLineWidth, m_secRttyDashVerts, m_secRttySpaceVbo.get(),
                            m_secRttySpaceUniformBuffer.get(), m_secRttySpaceSrb.get(), m_secondaryRttyToneColor);
        }

        if (m_cursorVisible && m_filterBw > 0 && m_tunedFreq > 0) {
            // K4 IF shift is in decahertz: the passband centre's offset from the dial.
            stagePassband(
                SpectrumGeometry::passbandFor(m_mode, m_dataSubMode, m_tunedFreq, m_filterBw, m_ifShift, m_rttyShift),
                m_passbandVbo.get(), m_passbandUniformBuffer.get(), m_passbandSrb.get(), m_passbandColor);

            // FSK-D and AFSK-A: the dial is the mark, which the marker already shows; space is one
            // shift below.
            if (SpectrumGeometry::isRtty(m_mode, m_dataSubMode) && m_fskMarkTone > 0)
                stageDashed(freqToNormalized(m_tunedFreq - m_rttyShift) * w, PanadapterConstants::RttyDashLineWidth,
                            m_rttyDashVerts, m_rttySpaceVbo.get(), m_rttySpaceUniformBuffer.get(), m_rttySpaceSrb.get(),
                            m_rttyToneColor);

            // CW: marker at the passband centre (dial + IS); other modes: at the dial.
            stageMarker(SpectrumGeometry::markerFreqHz(m_tunedFreq, m_ifShift, m_mode), m_markerVbo.get(),
                        m_markerUniformBuffer.get(), m_markerSrb.get(), m_frequencyMarkerColor);

            // TX marker when RIT/XIT moves TX away from RX. m_txFreq is the TX dial, so CW gets the
            // same pitch offset as the RX marker.
            if (m_txMarkerVisible && m_txFreq > 0 && m_spanHz > 0) {
                qint64 txDisplayFreq = m_txFreq;
                if (m_mode == SpectrumGeometry::Mode::Cw)
                    txDisplayFreq += m_cwPitch;
                else if (m_mode == SpectrumGeometry::Mode::CwR)
                    txDisplayFreq -= m_cwPitch;
                stageMarker(txDisplayFreq, m_txMarkerVbo.get(), m_txMarkerUniformBuffer.get(), m_txMarkerSrb.get(),
                            m_txMarkerColor);
            }

            // Notch: NM is an audio offset from the dial (SpectrumGeometry::notchFreqHz, shared with
            // the mini-pan).
            if (m_notchEnabled && m_notchPitchHz > 0 && m_spanHz > 0) {
                const float notchX =
                    freqToNormalized(SpectrumGeometry::notchFreqHz(m_mode, m_tunedFreq, m_notchPitchHz)) * w;
                stageDashed(notchX, PanadapterConstants::MarkerLineWidth, m_notchDashVerts, m_notchVbo.get(),
                            m_notchUniformBuffer.get(), m_notchSrb.get(), m_notchColor);
            }
        }
    }

    cb->resourceUpdate(rub);

    // Begin render pass (clear color = the spectrum background; see K4Styles::Colors)
    cb->beginPass(renderTarget(), QColor(K4Styles::Colors::SpectrumBackground), {1.0f, 0}, nullptr);

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
    if (gridVertexCount > 0) {
        cb->setViewport({0, waterfallHeight, w, spectrumHeight});
        cb->setGraphicsPipeline(m_overlayLinePipeline.get());
        cb->setShaderResources(m_overlaySrb.get());
        const QRhiCommandBuffer::VertexInput gridVbufBinding(m_overlayVbo.get(), 0);
        cb->setVertexInput(0, 1, &gridVbufBinding);
        cb->draw(gridVertexCount);
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

    // Draw overlays (full viewport for markers and passbands)
    cb->setViewport({0, 0, w, h});
    for (int i = 0; i < overlayDrawCount; ++i) {
        const OverlayDraw &draw = overlayDraws[i];
        cb->setGraphicsPipeline(m_overlayTrianglePipeline.get());
        cb->setShaderResources(draw.srb);
        const QRhiCommandBuffer::VertexInput vbufBinding(draw.vbo, 0);
        cb->setVertexInput(0, 1, &vbufBinding);
        cb->draw(draw.vertexCount);
    }

    cb->endPass();
}

void PanadapterRhiWidget::rebuildGridIfNeeded(float w, float spectrumHeight, float dpr) {
    if (w == m_gridWidth && spectrumHeight == m_gridHeight && dpr == m_gridDpr)
        return;
    m_gridWidth = w;
    m_gridHeight = spectrumHeight;
    m_gridDpr = dpr;
    m_gridVerts.clear();

    // WHY: Fixed-pixel grid cells. The grid is a visual reference (graph-paper), decoupled from
    // both the frequency labels (which snap to round dial values) and the dB/S-meter scale.
    // Cells keep a constant VISIBLE size: spacing is a logical px constant scaled by the device
    // pixel ratio, so the number of cells changes with window size / spectrum-waterfall ratio
    // while each cell stays the same size. The trailing partial cell at the top/right is normal.
    const float cellW = PanadapterConstants::GridCellWidthPx * dpr;
    const float cellH = PanadapterConstants::GridCellHeightPx * dpr;

    // Horizontal lines every cellH up the spectrum area
    if (cellH > 0.0f)
        for (float y = cellH; y < spectrumHeight; y += cellH)
            m_gridVerts << 0.0f << y << w << y;

    // Vertical lines every cellW across the width
    if (cellW > 0.0f)
        for (float x = cellW; x < w; x += cellW)
            m_gridVerts << x << 0.0f << x << spectrumHeight;
}

void PanadapterRhiWidget::updateSpectrum(const QByteArray &payload, int binsOffset, int binCount, qint64 centerFreq,
                                         qint32 sampleRate, float noiseFloor) {
    // WHY validated here: binCount is read off the wire, and every bin is written into a texture row
    // centred at (m_textureWidth - binCount) / 2. A count wider than the row made that offset
    // negative and wrote outside m_waterfallData and m_normalizedSpectrum.
    if (SpectrumGeometry::usableBinCount(payload.size(), binsOffset, binCount, m_textureWidth) == 0) {
        // Logged on the 1st, 2nd, 4th, 8th... drop so a stream of bad frames cannot flood the log.
        static quint64 dropped = 0;
        ++dropped;
        if ((dropped & (dropped - 1)) == 0)
            qCWarning(lcSpectrumRender) << "Dropped PAN frame:" << binCount << "bins at offset" << binsOffset << "in a"
                                        << payload.size() << "byte payload, texture width" << m_textureWidth << "("
                                        << dropped << "dropped so far)";
        return;
    }

    m_centerFreq = centerFreq;
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
        if (lcSpectrumDiag().isDebugEnabled())
            logTierCrossing(m_lastTierSampleRate, sampleRate, tierSpanHz);

        // WHY the waterfall is discarded here: stored rows hold raw bins whose width is the tier
        // they were captured at, but the shader windows every row with a single tierSpanHz. Rows
        // from the previous tier would be redrawn at the new tier's scale, silently moving every
        // signal in the history sideways. Clearing costs the visible history (~40 s at 12 fps) and
        // is only paid when the radio actually changes tier, which is rarer than changing span.
        // The alternative, keeping the rows and windowing each by its own tier, is truthful but
        // leaves the older rows covering less than the display width — rejected as a look-and-feel
        // call, not a correctness one.
        if (m_lastTierSampleRate > 0) {
            m_waterfallData.fill(0);
            m_rowTierSpanHz.fill(0.0f);
            m_waterfallNeedsFullClear = true;
        }

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

    // WHY resample instead of reassigning: the cropped bin count is spanHz * totalBins / tierSpanHz,
    // so it changes on every span change and every tier crossing. Dropping the smoothed curve there
    // put one raw, unsmoothed frame on screen, which reads as a visible flash against an EMA running
    // at attack 0.52 / decay 0.34. The frequency axis is unchanged across that boundary — old and
    // new both cover spanHz about the same centre, only the resolution differs — so the smoothed
    // curve carries over correctly once stretched to the new bin count, and smoothing continues
    // uninterrupted.
    if (m_currentSpectrum.isEmpty()) {
        m_currentSpectrum = m_rawSpectrum;
    } else {
        if (m_currentSpectrum.size() != m_rawSpectrum.size())
            m_currentSpectrum = resampleLinear(m_currentSpectrum, m_rawSpectrum.size());
        for (int i = 0; i < m_rawSpectrum.size(); ++i) {
            float alpha = (m_rawSpectrum[i] > m_currentSpectrum[i]) ? attackAlpha : decayAlpha;
            m_currentSpectrum[i] = alpha * m_rawSpectrum[i] + (1.0f - alpha) * m_currentSpectrum[i];
        }
    }

    // The radio reports the noise floor it measured, in the same dBm the bins are meant to be in
    // once RhiUtils::K4_DBM_OFFSET is applied. Comparing the two is a free check that the offset is
    // right — a systematic gap means every dBm and S-unit on the scale is off by that much. Logged
    // only when the disagreement changes, so it stays quiet while the calibration holds.
    if (lcSpectrumDiag().isDebugEnabled() && !m_rawSpectrum.isEmpty()) {
        // Raw bins, not the smoothed curve: the EMA's fast-attack/slow-decay asymmetry biases the
        // result upward and would be read as a calibration error. The median is the meaningful
        // comparison — with signals sparse across the span, most bins are noise, whereas the single
        // lowest bin is the deepest downward excursion of that noise and sits well below any floor
        // statistic the radio would report.
        QVector<float> sorted = m_rawSpectrum;
        std::sort(sorted.begin(), sorted.end());
        const float medianDbm = sorted[sorted.size() / 2];
        // 5th percentile, not the outright minimum: the lowest single bin is the deepest downward
        // excursion of the noise and moves several dB frame to frame, which made this line fire
        // over a thousand times in one session. A low percentile tracks the same floor without the
        // jitter.
        const float minDbm = sorted[sorted.size() / 20];
        // A sanity check, not the calibration (that came from a carrier against SMH; see
        // SpectrumScale::K4_DBM_OFFSET). With the offset right, noise statistics put this percentile
        // some dB below the radio's floor and the median ~3-4 dB above it; a shift of ~16 dB is
        // what the old 146 looked like. Logged on settling and on a sustained change.
        // Smoothed before comparing: calibration error is a standing offset, so only a sustained
        // move is worth a line. Per-frame variation is band noise, not the constant drifting.
        const float delta = minDbm - m_noiseFloor;
        m_calibDelta = (m_calibSamples == 0) ? delta : 0.02f * delta + 0.98f * m_calibDelta;
        ++m_calibSamples;

        // One line once the average has settled, then only on a sustained change. Reporting the
        // settled value even when it is small is the point: a silent check cannot be told apart
        // from a broken one, and "offset +0.6 dB" is the evidence that the constant is right.
        const bool settled = m_calibSamples == 120;
        const bool drifted = m_calibLogged && std::abs(m_calibDelta - m_lastLoggedCalibDelta) >= 2.0f;
        if (settled || drifted) {
            m_calibLogged = true;
            m_lastLoggedCalibDelta = m_calibDelta;
            qCDebug(
                lcSpectrumDiag, "%s",
                qPrintable(QString::asprintf("CALIB       radio floor %.1f dBm  raw min %.1f  raw median %.1f  "
                                             "smoothed offset %+.1f dB  (K4_DBM_OFFSET %.0f)",
                                             m_noiseFloor, minDbm, medianDbm, m_calibDelta, RhiUtils::K4_DBM_OFFSET)));
        }
    }

    // A row is written per render, not per frame: a frame arriving before the previous one was
    // drawn replaces it (the EMA has already folded it in), and that history slice is never stored.
    if (m_waterfallNeedsUpdate && lcSpectrumDiag().isDebugEnabled())
        ++m_cadenceCoalesced;
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

    // Record which tier this row holds. The renderer applies one tierSpanHz to every row, so this
    // is what makes it measurable how many on-screen rows are being windowed at the wrong scale.
    if (row >= 0 && row < m_rowTierSpanHz.size())
        m_rowTierSpanHz[row] = m_waterfallTierSpanHz;

    // Palette positions fixed at capture under the current colour window (REF, #WBS): the row keeps
    // its colour as the history scrolls, as on the K4. A live window recoloured the whole waterfall
    // on every ~1 dB AutoRef step, which read as a flicker over the history.
    const WaterfallGeometry::ColorWindow window =
        WaterfallGeometry::colorWindowFor(static_cast<float>(m_refLevel), m_waterfallColorRange);
    for (int i = 0; i < specSize; ++i)
        m_waterfallData[row * m_textureWidth + offset + i] = WaterfallGeometry::capturedByteFor(source[i], window);
}

void PanadapterRhiWidget::logTierCrossing(qint32 oldSampleRate, qint32 newSampleRate, qint32 newTierSpanHz) {
    // The first packet after construction or reset moves the tier from 0, which is initialization
    // rather than a crossing: there is no history to rescale and no meaningful ratio. Logging it
    // would put two misleading "rescaleFactor 0x" lines in front of every real measurement.
    if (oldSampleRate <= 0)
        return;

    const float oldTierSpanHz = static_cast<float>(oldSampleRate) * 1000.0f;

    // How many rows already on screen were captured at a tier other than the one about to become
    // current. Those rows keep their old bins but get windowed with the new tier span, so this is
    // the count of rows the next frame will draw at the wrong frequency scale.
    int staleRows = 0;
    for (float rowSpan : std::as_const(m_rowTierSpanHz)) {
        if (rowSpan > 0.0f && !qFuzzyCompare(rowSpan, static_cast<float>(newTierSpanHz)))
            ++staleRows;
    }

    const float spanRatioBefore =
        oldTierSpanHz > 0.0f ? qMin(1.0f, static_cast<float>(m_spanHz) / oldTierSpanHz) : 0.0f;
    const float spanRatioAfter = newTierSpanHz > 0 ? qMin(1.0f, static_cast<float>(m_spanHz) / newTierSpanHz) : 0.0f;
    const float rescaleFactor = oldTierSpanHz > 0.0f ? static_cast<float>(newTierSpanHz) / oldTierSpanHz : 0.0f;

    qCDebug(lcSpectrumDiag).nospace() << "TIER CROSS  sampleRate " << oldSampleRate << "->" << newSampleRate
                                      << " kHz  tierSpan " << static_cast<qint32>(oldTierSpanHz) << "->"
                                      << newTierSpanHz << " Hz  rescaleFactor " << rescaleFactor << "x  span "
                                      << m_spanHz << " Hz  spanRatio " << spanRatioBefore << "->" << spanRatioAfter
                                      << "  writeRow " << m_waterfallWriteRow << "  staleRows " << staleRows << "/"
                                      << m_waterfallHistory
                                      << "  (EMA reset this frame: trace may flash independently)";
}

void PanadapterRhiWidget::logRenderGeometry(const QSize &outputSize) {
    if (outputSize.isEmpty())
        return;

    // The first size is settled before any PAN packet arrives, so a size-change-only trigger would
    // report the horizontal figures as zero and never revisit them. Allow exactly one repeat once
    // bin data exists, so pxPerBin is answered without logging every frame.
    // The split ratio is draggable (0.1..0.9), and it scales the waterfall's share of the height,
    // so it moves pxPerRow further than resizing the window does. Treat a ratio change as a
    // geometry change or the most direct control over the artifact would go unrecorded.
    const bool haveBins = m_waterfallTierSpanHz > 0.0f && m_waterfallTierBinCount > 0;
    const bool sameGeometry = outputSize == m_lastLoggedOutputSize &&
                              qFuzzyCompare(m_spectrumRatio, m_lastLoggedSpectrumRatio) &&
                              m_waterfallRowShift == m_lastLoggedRowShift;
    if (sameGeometry && (m_geometryLoggedWithBins || !haveBins))
        return;
    m_lastLoggedOutputSize = outputSize;
    m_lastLoggedSpectrumRatio = m_spectrumRatio;
    m_lastLoggedRowShift = m_waterfallRowShift;
    m_geometryLoggedWithBins = haveBins;

    const float waterfallHeightPx = static_cast<float>(outputSize.height()) * (1.0f - m_spectrumRatio);
    const float pxPerRow = m_visibleRows > 0 ? waterfallHeightPx / m_visibleRows : 0.0f;

    // Rows follow physical pixel height, so pxPerRow is ~devicePxPerRow; anything above means a
    // clamp bit: the window is either taller than the stored rows or shorter than MIN_VISIBLE_ROWS.
    const float perPhysical = pxPerRow / static_cast<float>(m_devicePxPerRow);
    const char *verdict = perPhysical <= 1.05f ? "1:1 physical"
                          : perPhysical < 2.0f ? "above 1:1, clamped by stored rows"
                                               : "well above 1:1, blur returning";

    const float visibleBins = m_waterfallTierSpanHz > 0.0f
                                  ? static_cast<float>(m_waterfallTierBinCount) *
                                        qMin(1.0f, static_cast<float>(m_spanHz) / m_waterfallTierSpanHz)
                                  : 0.0f;
    const float pxPerBin = visibleBins > 0.0f ? static_cast<float>(outputSize.width()) / visibleBins : 0.0f;

    // WHY a formatted string and not a chain of <<: a long operator<< chain sitting near the
    // column limit is wrapped differently by different clang-format 18 builds, so CI's apt binary
    // rejected what the Homebrew one accepted. One asprintf has no wrapping decision to disagree
    // about, and reads better besides.
    qCDebug(lcSpectrumDiag, "%s",
            qPrintable(QString::asprintf("GEOMETRY    output %dx%d px  dpr %g  framebuffer/panel %.3f  "
                                         "devicePxPerRow %d  rowShift %g  spectrumRatio %g  "
                                         "waterfall %.1f px  rows %d/%d visible/stored  "
                                         "pxPerRow %.4f (%s)  visibleBins %.0f  pxPerBin %.2f",
                                         outputSize.width(), outputSize.height(), devicePixelRatioF(),
                                         m_framebufferPerPanel, m_devicePxPerRow, m_waterfallRowShift, m_spectrumRatio,
                                         waterfallHeightPx, m_visibleRows, m_waterfallHistory, pxPerRow, verdict,
                                         visibleBins, pxPerBin)));
}

void PanadapterRhiWidget::updateDevicePxPerRow() {
    const QScreen *current = screen();
    const QRect geometry = current ? current->geometry() : QRect();
    const qreal dpr = devicePixelRatioF();
    if (current == m_panelScaleScreen && geometry == m_panelScaleGeometry && qFuzzyCompare(dpr, m_panelScaleDpr))
        return;
    m_panelScaleScreen = current;
    m_panelScaleGeometry = geometry;
    m_panelScaleDpr = dpr;
    m_framebufferPerPanel = PanelScale::framebufferPixelsPerPanelPixel(current);
    m_devicePxPerRow = WaterfallGeometry::devicePxPerRowFor(m_framebufferPerPanel);
}

void PanadapterRhiWidget::noteRenderCadence(bool rowCommitted) {
    if (!m_cadenceClock.isValid())
        m_cadenceClock.start();
    ++m_cadenceRenders;
    if (!rowCommitted)
        return;

    const qint64 now = m_cadenceClock.nsecsElapsed();
    if (m_cadenceLastRowNs >= 0) {
        const qint64 interval = now - m_cadenceLastRowNs;
        const qreal refreshHz = screen() ? screen()->refreshRate() : 60.0;
        const qint64 refreshNs = static_cast<qint64>(1e9 / (refreshHz > 0.0 ? refreshHz : 60.0));
        const int refreshes =
            static_cast<int>(std::clamp((interval + refreshNs / 2) / refreshNs, qint64{0}, qint64{7}));
        ++m_cadenceRefreshesPerRow[static_cast<size_t>(refreshes)];
        m_cadenceMinRowNs = m_cadenceRows == 0 ? interval : (std::min)(m_cadenceMinRowNs, interval);
        m_cadenceMaxRowNs = (std::max)(m_cadenceMaxRowNs, interval);
        m_cadenceSumRowNs += interval;
        ++m_cadenceRows;
    }
    m_cadenceLastRowNs = now;

    if (m_cadenceRows < CadenceReportRows)
        return;
    const auto &h = m_cadenceRefreshesPerRow;
    qCDebug(
        lcSpectrumDiag, "%s",
        qPrintable(QString::asprintf("CADENCE     rows %d  row interval ms mean %.1f min %.1f max %.1f  "
                                     "refreshes/row 1:%d 2:%d 3:%d 4:%d 5:%d 6:%d 7+:%d (0:%d)  "
                                     "renders/row %.2f  frames coalesced %d",
                                     m_cadenceRows, m_cadenceSumRowNs / 1e6 / m_cadenceRows, m_cadenceMinRowNs / 1e6,
                                     m_cadenceMaxRowNs / 1e6, h[1], h[2], h[3], h[4], h[5], h[6], h[7], h[0],
                                     static_cast<double>(m_cadenceRenders) / m_cadenceRows, m_cadenceCoalesced)));
    m_cadenceRows = 0;
    m_cadenceRenders = 0;
    m_cadenceCoalesced = 0;
    m_cadenceSumRowNs = 0;
    m_cadenceMaxRowNs = 0;
    m_cadenceRefreshesPerRow.fill(0);
}

qint64 PanadapterRhiWidget::displayCenterHz() const {
    // In CW the display centre is offset by the IF shift to match the K4's display convention: the
    // VFO marker (at dial + IS*10) appears centred, and labels show dial-equivalent frequencies so
    // CW operators can read tuning positions directly from the waterfall. The K4 auto-adjusts IS
    // to track CW pitch, keeping the display in sync.
    return SpectrumGeometry::displayCenterHz(m_centerFreq, m_ifShift, m_mode);
}

float PanadapterRhiWidget::freqToNormalized(qint64 freq) const {
    // Map frequency to normalized range [0.0, 1.0] for drawing markers and passbands.
    return SpectrumGeometry::normalizedXFor(freq, displayCenterHz(), m_spanHz);
}

qint64 PanadapterRhiWidget::xToFreq(int x, int w) const {
    // Map pixel position to frequency for click-to-tune, in the same shifted coordinate system as
    // the display so the click lands where the user sees it on the frequency labels.
    if (w <= 0)
        return m_centerFreq;
    // Clamp to [0, 1] to prevent runaway acceleration when dragging past edges
    const double normalized = std::clamp(static_cast<double>(x) / static_cast<double>(w), 0.0, 1.0);
    return SpectrumGeometry::freqForNormalizedX(normalized, displayCenterHz(), m_spanHz);
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
    m_mode = SpectrumGeometry::modeFromString(mode);
    updateFreqScaleOverlay();
    update();
}

void PanadapterRhiWidget::setDataSubMode(int subMode) {
    const SpectrumGeometry::DataSubMode parsed = SpectrumGeometry::dataSubModeFromInt(subMode);
    if (m_dataSubMode != parsed) {
        m_dataSubMode = parsed;
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
    // Clear runtime data buffers, including the full-tier EMA so the first frame after a reconnect
    // is not blended with the last one before it.
    m_currentSpectrum.clear();
    m_rawSpectrum.clear();
    m_averagingLevel = -1; // the next session's #AVG always applies
    m_tierSpectrum.clear();
    m_tierRawSpectrum.clear();
    m_lastTierSampleRate = 0;
    m_waterfallTierBinCount = 0;
    m_waterfallTierSpanHz = 0;
    m_waterfallWriteRow = 0;
    m_waterfallData.fill(0);
    m_rowTierSpanHz.fill(0.0f);
    m_waterfallNeedsFullClear = true;

    // Reset all radio state to header defaults.
    // Most values match their member initializers; only m_cursorVisible differs
    // (header inits to true, clear() hides it on disconnect).
    m_centerFreq = 0;
    m_tunedFreq = 0;
    m_spanHz = 10000;
    m_mode = SpectrumGeometry::Mode::Usb;
    m_dataSubMode = SpectrumGeometry::DataSubMode::DataA;
    m_noiseFloor = -130.0f;
    m_cwPitch = SpectrumGeometry::DefaultCwPitchHz;
    m_ifShift = 50;
    m_filterBw = 2400;
    m_notchEnabled = false;
    m_notchPitchHz = 0;
    m_cursorVisible = false; // Intentionally differs from header default (true)
    m_secondaryTunedFreq = 0;
    m_secondaryFilterBw = 0;
    m_secondaryMode = SpectrumGeometry::Mode::Unknown;
    m_secondaryDataSubMode = SpectrumGeometry::DataSubMode::DataA;
    m_secondaryIfShift = 50;
    m_txFreq = 0;
    m_txMarkerVisible = false;
    m_fskMarkTone = 915;
    m_rttyShift = 170;

    // The next session's #REF / #SCL set the window again; until then show the default, not the
    // last radio's. The split (m_spectrumRatio) is kept: it is layout, and #WFH replaces it.
    m_refLevel = SpectrumDisplayLimits::RefLevelDefault;
    m_scale = SpectrumDisplayLimits::ScaleDefault;
    m_waterfallColorRange = -1;
    updateDbRangeFromRefAndScale();

    // Diagnostics restart with the next session's data.
    m_calibDelta = 0.0f;
    m_calibSamples = 0;
    m_calibLogged = false;
    m_lastLoggedCalibDelta = 0.0f;
    m_geometryLoggedWithBins = false;

    // The band plan belongs to the frequency the radio reports; SpectrumController pushes it again.
    m_bandPlanName.clear();
    m_bandPlanSegments.clear();
    m_bandPlanMarkers.clear();
    updateBandPlanOverlay();

    // Hide frequency labels (paintEvent returns early when spanHz <= 0)
    if (m_freqScaleOverlay)
        m_freqScaleOverlay->setFrequencyRange(0, 0, 0, 0);

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

void PanadapterRhiWidget::setWaterfallColorRange(int db) {
    // Out-of-range values (the -1 sentinel) fall back inside WaterfallGeometry::colorWindowFor.
    if (m_waterfallColorRange != db) {
        m_waterfallColorRange = db;
        update();
    }
}

void PanadapterRhiWidget::setScale(int scale) {
    // Scale range: 10-150 (per K4 documentation)
    // Higher values = more compressed display (signals appear weaker, wider dB range)
    // Lower values = more expanded display (signals appear stronger, narrower dB range)
    if (m_scale != scale && scale >= SpectrumDisplayLimits::ScaleMin && scale <= SpectrumDisplayLimits::ScaleMax) {
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
    level = std::clamp(level, 1, 20);
    if (m_averagingLevel == level)
        return;
    m_averagingLevel = level;
    const SpectrumGeometry::EmaAlphas alphas = SpectrumGeometry::emaAlphasFor(level);
    m_attackAlpha = alphas.attack;
    m_decayAlpha = alphas.decay;
}

// Secondary VFO setters
void PanadapterRhiWidget::setSecondaryVfo(qint64 freq, int bwHz, const QString &mode, int ifShift, int dataSubMode) {
    m_secondaryTunedFreq = freq;
    m_secondaryFilterBw = bwHz;
    m_secondaryMode = SpectrumGeometry::modeFromString(mode);
    m_secondaryDataSubMode = SpectrumGeometry::dataSubModeFromInt(dataSubMode);
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
