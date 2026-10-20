#ifndef FEATUREMENUBAR_H
#define FEATUREMENUBAR_H

#include <QWidget>
#include <QPushButton>
#include "utils/wheelaccumulator.h"

/**
 * @brief Popup control bar for the ATTN / NB / NR / MANUAL NOTCH features. Shows a toggle, +/-
 *        buttons, value readout, and an extra feature-specific button (e.g., NB filter cycle).
 *        Works in BSET mode by routing CAT with `$` suffix for Sub RX.
 */
class FeatureMenuBar : public QWidget {
    Q_OBJECT

public:
    enum Feature { Attenuator, NbLevel, NrAdjust, ManualNotch };
    enum NrEngine { Lms, Ssnr };

    explicit FeatureMenuBar(QWidget *parent = nullptr);

    void showForFeature(Feature feature);
    void showAboveWidget(QWidget *referenceWidget); // Position popup above a reference widget
    void hideMenu();
    Feature currentFeature() const { return m_currentFeature; }
    bool isMenuVisible() const { return isVisible(); }

    // State updates
    void setFeatureEnabled(bool enabled);
    void setValue(int value);
    void setValueUnit(const QString &unit);
    void setNbFilter(int filter); // 0=NONE, 1=NARROW, 2=WIDE

    // NR engine selection (only meaningful when currentFeature() == NrAdjust)
    NrEngine currentNrEngine() const { return m_nrEngine; }
    void setNrEngine(NrEngine engine);

signals:
    void toggleRequested();
    void incrementRequested();
    void decrementRequested();
    void extraButtonClicked();
    void nrEngineToggleRequested(); // Title label clicked while showing NR ADJUST
    void nrResetRequested();        // RESET clicked while showing NR ADJUST with the SSNR engine

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void setupUi();
    void updateForFeature();

    QPushButton *m_titleLabel;
    QPushButton *m_toggleBtn;
    QPushButton *m_nrEngineBtn; // Shown only for NrAdjust — toggles LMS / SSNR
    QPushButton *m_nrResetBtn;  // Shown only for NrAdjust + SSNR — resets the noise estimate
    QPushButton *m_extraBtn;
    QPushButton *m_valueLabel;
    QPushButton *m_decrementBtn;
    QPushButton *m_incrementBtn;

    Feature m_currentFeature = Attenuator;
    NrEngine m_nrEngine = Lms;
    bool m_featureEnabled = false;
    int m_value = 0;
    QString m_valueUnit;
    int m_nbFilter = 0;                   // 0=NONE, 1=NARROW, 2=WIDE
    QWidget *m_referenceWidget = nullptr; // Widget to position relative to
    WheelAccumulator m_wheelAccumulator;
};

#endif // FEATUREMENUBAR_H
