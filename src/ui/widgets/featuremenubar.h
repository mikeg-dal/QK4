#ifndef FEATUREMENUBAR_H
#define FEATUREMENUBAR_H

#include "ui/popups/k4popupbase.h"
#include "utils/wheelaccumulator.h"

class QPushButton;

/**
 * @brief Popup control bar for the ATTN / NB / NR / MANUAL NOTCH features. Shows a toggle, +/-
 *        buttons, value readout, and an extra feature-specific button (e.g., NB filter cycle).
 *        Works in BSET mode by routing CAT with `$` suffix for Sub RX.
 *
 * A K4PopupBase like every other popup: placement, click-away, Escape and the reopen guard come
 * from the base. It was a hand-made Qt::Popup until 2026-10, which Qt closes on an outside press and
 * then replays that press to the widget under it (Qt::WA_NoMouseReplay) — so a second press on the
 * ATTN key closed the bar and the replayed press opened it again.
 */
class FeatureMenuBar : public K4PopupBase {
    Q_OBJECT

public:
    enum Feature { Attenuator, NbLevel, NrAdjust, ManualNotch };
    enum NrEngine { Lms, Ssnr };

    explicit FeatureMenuBar(QWidget *parent = nullptr);

    // Switch to @p feature, size for it, and show centred directly above @p anchor.
    void showForFeature(Feature feature, QWidget *anchor);
    Feature currentFeature() const { return m_currentFeature; }

    // State updates
    void setFeatureEnabled(bool enabled);
    void setValue(int value);
    void setNbFilter(int filter); // 0=NONE, 1=NARROW, 2=WIDE

    // NR engine selection (only meaningful when currentFeature() == NrAdjust)
    NrEngine currentNrEngine() const { return m_nrEngine; }
    void setNrEngine(NrEngine engine);

signals:
    void toggleRequested();
    void incrementRequested();
    void decrementRequested();
    void extraButtonClicked();
    void nrEngineToggleRequested(); // LMS/NR+ engine button clicked (NR ADJUST only)

protected:
    QSize contentSize() const override;
    void paintContent(QPainter &painter, const QRect &contentRect) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void setupUi();
    void updateForFeature();
    static QString nbFilterText(int filter);

    QPushButton *m_titleLabel;
    QPushButton *m_toggleBtn;
    QPushButton *m_nrEngineBtn; // Shown only for NrAdjust — toggles LMS / SSNR
    QPushButton *m_extraBtn;
    QPushButton *m_valueLabel;
    QPushButton *m_decrementBtn;
    QPushButton *m_incrementBtn;

    Feature m_currentFeature = Attenuator;
    NrEngine m_nrEngine = Lms;
    int m_value = 0;
    QString m_valueUnit;
    int m_nbFilter = 0; // 0=NONE, 1=NARROW, 2=WIDE
    WheelAccumulator m_wheelAccumulator;
};

#endif // FEATUREMENUBAR_H
