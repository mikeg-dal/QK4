#ifndef SPECTRUMCONTROLLER_H
#define SPECTRUMCONTROLLER_H

#include <QObject>
#include <QWidget>

class PanadapterRhiWidget;
class ConnectionController;
class DxClusterController;
class RadioState;
class DxSpotOverlay;
class MouseVfoIndicator;
class VFOWidget;
class QFrame;
class QPushButton;
class QLabel;
class QSize;

/**
 * @brief Orchestrates the panadapter/waterfall UI: owns PanadapterRhiWidget A/B, the span +/-
 *        buttons, center buttons, VFO indicator labels, and the DxSpotOverlays. Routes inbound
 *        spectrum/mini-spectrum payloads to the right receiver, handles click-to-tune with
 *        mode-aware offset correction, and installs a custom mouse-QSY overlay.
 */
class SpectrumController : public QObject {
    Q_OBJECT

public:
    explicit SpectrumController(ConnectionController *conn, RadioState *radioState, QObject *parent = nullptr);

    void setupSpectrumUI(QWidget *parentWidget, VFOWidget *vfoA, VFOWidget *vfoB);

    QWidget *spectrumContainer() const;
    PanadapterRhiWidget *panadapterA() const;
    PanadapterRhiWidget *panadapterB() const;

    enum class PanadapterMode { MainOnly, Dual, SubOnly };
    void setPanadapterMode(PanadapterMode mode);

    void setMouseQsyMode(int mode);
    void setDxClusterController(DxClusterController *controller);

    // Task-level setters that route to both panadapters. Prefer these over
    // reaching through panadapterA()/panadapterB() — CONVENTIONS Rule 2.
    void setAmplitudeUnits(bool useSUnits);
    void setFskMarkTone(int toneHz);
    void setWaterfallHeight(int percent);

    // Clear both panadapter displays — used when the K4 disconnects to
    // prevent stale spectrum data from sitting on-screen.
    void clearDisplays();

public slots:
    void onSpectrumData(int receiver, const QByteArray &payload, int binsOffset, int binCount, qint64 centerFreq,
                        qint32 sampleRate, float noiseFloor);
    void onMiniSpectrumData(int receiver, const QByteArray &payload, int binsOffset, int binCount, qint64 centerFreq,
                            int spanHz);
    void updatePanadapterPassbands();
    void updateTxMarkers();
    void checkAndHideMiniPanB();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // Widgets that sit on one panadapter, plus that panadapter's band-plan cache.
    struct PanUi {
        PanadapterRhiWidget *pad = nullptr;
        QPushButton *zoomOutBtn = nullptr;
        QPushButton *zoomInBtn = nullptr;
        QPushButton *centerBtn = nullptr;
        QLabel *vfoIndicator = nullptr;
        MouseVfoIndicator *mouseVfoIndicator = nullptr;
        DxSpotOverlay *spotOverlay = nullptr;
        // Band-plan cache: the plan band (BandPlan::planBandIndex) + region last pushed.
        // Sentinel -2 so the first update always pushes.
        int lastPlanBand = -2;
        int lastRegion = -2;
    };

    PanUi &pan(bool vfoB) { return vfoB ? m_panB : m_panA; }
    void createPanOverlays(PanUi &ui, const QString &buttonStyle, const QString &indicatorStyle);
    void wirePan(bool vfoB);
    void layoutPanOverlays(PanUi &ui, const QSize &size);

    // Mouse and wheel actions, shared by both panadapters.
    qint64 adjustClickFreqForMode(qint64 freq, bool vfoB);
    void tuneFromPan(bool vfoB, qint64 freq, bool dragging);
    void tuneToSpot(bool vfoB, qint64 freq);
    void sendVfoFrequency(bool vfoB, qint64 freq, bool dragging);
    void setScrollVfo(bool vfoB);
    void scrollTune(int steps);
    void zoomSpan(bool vfoB, bool zoomIn);
    void adjustScaleFromWheel(int steps);
    void adjustRefLevelFromWheel(bool vfoB, int steps);

    // Push the band-plan overlay segments for the VFO's current band + IARU region. Skipped while
    // the plan band and region are unchanged, so per-Hz tuning doesn't re-push within a band.
    void updateBandPlan(bool vfoB);
    void updateSpotOverlay(bool vfoB);

    ConnectionController *m_connectionController = nullptr;
    RadioState *m_radioState = nullptr;
    VFOWidget *m_vfoA = nullptr;
    VFOWidget *m_vfoB = nullptr;
    PanUi m_panA;
    PanUi m_panB;
    QWidget *m_spectrumContainer = nullptr;
    QFrame *m_spectrumSeparator = nullptr;
    bool m_scrollVfoB = false;
    int m_mouseQsyMode = 0;

    DxClusterController *m_dxClusterController = nullptr;
    void updateSpotOverlays();
};

#endif // SPECTRUMCONTROLLER_H
