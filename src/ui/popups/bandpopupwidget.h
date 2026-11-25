#ifndef BANDPOPUPWIDGET_H
#define BANDPOPUPWIDGET_H

#include "ui/popups/k4popupbase.h"
#include <QList>
#include <QMap>
#include <QPushButton>
#include <QVBoxLayout>

class MemoryModel;
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
 * MEM and GEN modes (R40, after tapping MEM or GEN): twelve frequency-memory cells and the
 * mode key with HF below it, the way the radio replaces its band labels. MEM holds the first
 * twelve memories inside a ham band and GEN the first twelve outside one, searched from #001
 * upward, each cell showing its label (or "#001" when it has none) over its frequency. The
 * cells fill column-major, as the radio's do. Tapping one recalls that channel.
 *
 * Bands emitted as `bandSelected(name)`:
 *   - "1.8".."50" (HF) → BN00..BN10
 *   - "XVTR1".."XVTR12" → BN11..BN22
 *   - "GEN" / "MEM" / "HF" → no BN command; they flip the grid
 * Memory cells emit `memoryRecallRequested(channel)` instead.
 */
class BandPopupWidget : public K4PopupBase {
    Q_OBJECT

public:
    // RadioState is used to read per-band XVTR config for the XVTR sub-grid
    // labels. May be nullptr — in that case all XVTR cells render as "--".
    explicit BandPopupWidget(RadioState *radioState = nullptr, QWidget *parent = nullptr);

    // The memories behind the MEM and GEN pages. May be left unset, in which case both pages show
    // empty cells.
    void setMemoryModel(MemoryModel *model);

    // Set the currently selected band by name
    void setSelectedBand(const QString &bandName);

    // Band number methods for K4 BN command
    void setSelectedBandByNumber(int bandNum);
    int getBandNumber(const QString &bandName) const;
    QString getBandName(int bandNum) const;

signals:
    void bandSelected(const QString &bandName);
    void memoryRecallRequested(int channel);

protected:
    QSize contentSize() const override;

private:
    enum class Mode { Hf, Xvtr, Mem, Gen };

    void setupUi();
    void buildHfGrid();
    void buildXvtrGrid();
    void buildMemoryGrid(Mode mode); // Mem or Gen
    void clearGrid();
    void setMode(Mode mode);

    QPushButton *createBandButton(const QString &text);
    QPushButton *createXvtrCell(int bandIndex); // bandIndex is 1..12
    QPushButton *createMemoryCell(int channel); // channel 1..200, or <= 0 for an empty cell
    void updateButtonStyles();
    void onBandButtonClicked();
    void refreshXvtrCellLabels();

    RadioState *m_radioState;
    MemoryModel *m_memoryModel = nullptr;
    Mode m_mode = Mode::Hf;
    QVBoxLayout *m_rootLayout = nullptr;
    QMap<QString, QPushButton *> m_buttonMap;
    QString m_selectedBand;
};

#endif // BANDPOPUPWIDGET_H
