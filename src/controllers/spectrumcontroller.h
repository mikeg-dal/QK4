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

class SpectrumController : public QObject {
    Q_OBJECT

public:
    explicit SpectrumController(ConnectionController *conn, RadioState *radioState, QObject *parent = nullptr);
    ~SpectrumController();

    void setupSpectrumUI(QWidget *parentWidget, VFOWidget *vfoA, VFOWidget *vfoB);

    QWidget *spectrumContainer() const;
    PanadapterRhiWidget *panadapterA() const;
    PanadapterRhiWidget *panadapterB() const;

    enum class PanadapterMode { MainOnly, Dual, SubOnly, DualAlt };
    void setPanadapterMode(PanadapterMode mode);

    void setMouseQsyMode(int mode);
    void setDxClusterController(DxClusterController *controller);

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

    ConnectionController *m_connectionController = nullptr;
    RadioState *m_radioState = nullptr;
    VFOWidget *m_vfoA = nullptr;
    VFOWidget *m_vfoB = nullptr;
    PanadapterRhiWidget *m_panadapterA = nullptr;
    PanadapterRhiWidget *m_panadapterB = nullptr;
    QWidget *m_spectrumContainer = nullptr;
    QFrame *m_spectrumSeparator = nullptr;
    QPushButton *m_spanUpBtn = nullptr;
    QPushButton *m_spanDownBtn = nullptr;
    QPushButton *m_centerBtn = nullptr;
    QPushButton *m_spanUpBtnB = nullptr;
    QPushButton *m_spanDownBtnB = nullptr;
    QPushButton *m_centerBtnB = nullptr;
    QLabel *m_vfoIndicatorA = nullptr;
    QLabel *m_vfoIndicatorB = nullptr;
    MouseVfoIndicator *m_mouseVfoIndicatorA = nullptr;
    MouseVfoIndicator *m_mouseVfoIndicatorB = nullptr;
    bool m_scrollVfoB = false;
    int m_mouseQsyMode = 0;
    bool m_dualAltActive = false; // Track A+B Alt mode (QK4-only, locally significant)

    DxClusterController *m_dxClusterController = nullptr;
    DxSpotOverlay *m_spotOverlayA = nullptr;
    DxSpotOverlay *m_spotOverlayB = nullptr;
    void updateSpotOverlays();
};

#endif // SPECTRUMCONTROLLER_H
