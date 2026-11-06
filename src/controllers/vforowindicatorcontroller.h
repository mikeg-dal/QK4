#ifndef VFOROWINDICATORCONTROLLER_H
#define VFOROWINDICATORCONTROLLER_H

#include <QObject>

class RadioState;
class SpectrumController;
class VfoRowWidget;
class QLabel;

// Observes RadioState and updates the VFO-row indicator labels —
// SPLIT, B-SET, VOX, QSK, TEST, ATU, and the message-bank label. Also
// drives the TX-triangle arrows on split changes and calls
// SpectrumController::updateTxMarkers when the split-state flip moves
// the TX VFO.
//
// SPLIT and B-SET labels are mutually exclusive: when B-SET is engaged,
// SPLIT hides and B-SET shows; when B-SET releases, SPLIT visibility
// reverts to the split-state value. Handled together here because they
// share the same screen position.
//
// Scoped intentionally to the "simple" state-label handlers; RIT/XIT
// handling lives elsewhere (RitXitController) because it cross-calls
// frequency display logic. Sub-RX and Div-mode indicator styling lives
// on SubDivIndicatorController. Side-panel BW/SHFT active-receiver
// styling lives on SideControlDisplayController.
class VfoRowIndicatorController : public QObject {
    Q_OBJECT

public:
    struct Labels {
        QLabel *splitLabel;
        QLabel *bSetLabel;
        QLabel *txTriangle;
        QLabel *txTriangleB;
        QLabel *voxLabel;
        QLabel *qskLabel;
        QLabel *atuLabel;
        QLabel *msgBankLabel;
    };

    explicit VfoRowIndicatorController(RadioState *radioState, SpectrumController *spectrum, VfoRowWidget *vfoRow,
                                       const Labels &labels, QObject *parent = nullptr);

    // Resets split/bSet/vox/qsk/atu/msgBank indicators to their disconnected
    // defaults. (TX triangles are TxStateController territory.)
    void reset();

private slots:
    void onSplitChanged(bool enabled);
    void onBSetChanged(bool enabled);
    void onVoxChanged();
    void onQskEnabledChanged(bool enabled);
    void onTestModeChanged(bool enabled);
    void onAtuModeChanged(int mode);
    void onMessageBankChanged(int bank);

private:
    RadioState *m_radioState;       // injected, not owned
    SpectrumController *m_spectrum; // injected, not owned — TX-marker refresh on split change
    VfoRowWidget *m_vfoRow;         // injected, not owned — drives test-visible
    Labels m_labels;
};

#endif // VFOROWINDICATORCONTROLLER_H
