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
    ~SpectrumController();

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
    void onMiniSpectrumData(int receiver, const QByteArray &payload, int binsOffset, int binCount);
    void updatePanadapterPassbands();
    void updateTxMarkers();
    void checkAndHideMiniPanB();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    qint64 adjustClickFreqForMode(qint64 freq, bool vfoB);
    // Push the band-plan overlay segments for the current VFO A/B band + IARU region.
    // Cached band+region (sentinel -2) so per-Hz tuning doesn't re-push within a band.
    void updateBandPlanA();
    void updateBandPlanB();

    ConnectionController *m_connectionController = nullptr;
    RadioState *m_radioState = nullptr;
    VFOWidget *m_vfoA = nullptr;
    VFOWidget *m_vfoB = nullptr;
    PanadapterRhiWidget *m_panadapterA = nullptr;
    PanadapterRhiWidget *m_panadapterB = nullptr;
    QWidget *m_spectrumContainer = nullptr;
    QFrame *m_spectrumSeparator = nullptr;
    QPushButton *m_zoomInBtn = nullptr;
    QPushButton *m_zoomOutBtn = nullptr;
    QPushButton *m_centerBtn = nullptr;
    QPushButton *m_zoomInBtnB = nullptr;
    QPushButton *m_zoomOutBtnB = nullptr;
    QPushButton *m_centerBtnB = nullptr;
    QLabel *m_vfoIndicatorA = nullptr;
    QLabel *m_vfoIndicatorB = nullptr;
    MouseVfoIndicator *m_mouseVfoIndicatorA = nullptr;
    MouseVfoIndicator *m_mouseVfoIndicatorB = nullptr;
    bool m_scrollVfoB = false;
    int m_mouseQsyMode = 0;
    int m_lastBandA = -2;
    int m_lastRegionA = -2;
    int m_lastBandB = -2;
    int m_lastRegionB = -2;

    DxClusterController *m_dxClusterController = nullptr;
    DxSpotOverlay *m_spotOverlayA = nullptr;
    DxSpotOverlay *m_spotOverlayB = nullptr;
    void updateSpotOverlays();
};

#endif // SPECTRUMCONTROLLER_H
