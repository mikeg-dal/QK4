#ifndef BANDPOPUPWIDGET_H
#define BANDPOPUPWIDGET_H

#include "ui/popups/k4popupbase.h"
#include <QList>
#include <QMap>
#include <QPushButton>
#include <QVBoxLayout>

class RadioState;

/**
 * @brief Band selection popup with two display modes.
 *
 * HF mode (default):
 *   Row 1: 1.8, 3.5, 7, 14, 21, 28, MEM
 *   Row 2: GEN, 5, 10, 18, 24, 50, XVTR    ← tapping XVTR flips to XVTR mode
 *
 * XVTR mode (after tapping XVTR):
 *   Row 1: XVTR1, XVTR3, XVTR5, XVTR7, XVTR9, XVTR11, MEM
 *   Row 2: XVTR2, XVTR4, XVTR6, XVTR8, XVTR10, XVTR12, HF  ← HF flips back
 *   Each XVTR cell shows its RF dial frequency or "--" if unconfigured.
 *
 * Bands emitted as `bandSelected(name)`:
 *   - "1.8".."50" (HF) → BN00..BN10
 *   - "XVTR1".."XVTR12" → BN11..BN22
 *   - "GEN" / "MEM" → no BN command (special pseudo-bands)
 */
class BandPopupWidget : public K4PopupBase {
    Q_OBJECT

public:
    // RadioState is used to read per-band XVTR config for the XVTR sub-grid
    // labels. May be nullptr — in that case all XVTR cells render as "--".
    explicit BandPopupWidget(RadioState *radioState = nullptr, QWidget *parent = nullptr);

    // Set the currently selected band by name
    void setSelectedBand(const QString &bandName);

    // Band number methods for K4 BN command
    void setSelectedBandByNumber(int bandNum);
    int getBandNumber(const QString &bandName) const;
    QString getBandName(int bandNum) const;

signals:
    void bandSelected(const QString &bandName);

protected:
    QSize contentSize() const override;

private:
    enum class Mode { Hf, Xvtr };

    void setupUi();
    void buildHfGrid();
    void buildXvtrGrid();
    void clearGrid();
    void setMode(Mode mode);

    QPushButton *createBandButton(const QString &text);
    QPushButton *createXvtrCell(int bandIndex); // bandIndex is 1..12
    void updateButtonStyles();
    void onBandButtonClicked();
    void refreshXvtrCellLabels();

    RadioState *m_radioState;
    Mode m_mode = Mode::Hf;
    QVBoxLayout *m_rootLayout = nullptr;
    QMap<QString, QPushButton *> m_buttonMap;
    QString m_selectedBand;
};

#endif // BANDPOPUPWIDGET_H
